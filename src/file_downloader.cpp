/*

Copyright (c) 2012-2014, 2017-2018, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "webui.hpp"
#include "file_downloader.hpp"
#include "auth_interface.hpp"
#include "parse_http_auth.hpp"
#include "hex.hpp"
#include "alert_handler.hpp"
#include "utils.hpp"
#include "mime_type.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/peer_id.hpp" // for lt::sha1_hash
#include "libtorrent/alert_types.hpp"
#include "libtorrent/file_storage.hpp"

#include <boost/shared_array.hpp>
#include <boost/beast/http/write.hpp>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <charconv>
#include <filesystem>
#include <iostream>

#include "percent_encode.hpp"

namespace ltweb {

namespace fs = std::filesystem;

struct file_request_conn : std::enable_shared_from_this<file_request_conn> {
	file_request_conn(
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done,
		lt::torrent_handle th,
		lt::piece_index_t next_piece,
		lt::piece_index_t end_piece,
		int offset,
		int piece_size,
		std::int64_t left_to_send
	)
		: m_next_piece(next_piece)
		, m_next_priority_piece(next_piece)
		, m_end_piece(end_piece)
		, m_left_to_send(left_to_send)
		, m_socket(socket)
		, m_done(std::move(done))
		, m_torrent(std::move(th))
		, m_piece_size(piece_size)
		, m_offset(offset)
	{
	}

	bool stopped() const
	{
		std::lock_guard<std::mutex> l(m_mutex);
		return m_stopped;
	}

	void cancel()
	{
		std::lock_guard<std::mutex> l(m_mutex);
		abort();
	}

	void set_piece_deadlines()
	{
		std::lock_guard<std::mutex> l(m_mutex);
		set_piece_deadlines_impl();
	}

	void on_piece_alert(lt::read_piece_alert const& a)
	{
		std::lock_guard<std::mutex> l(m_mutex);

		if (m_stopped) return;

		if (a.piece == m_next_piece && !m_writing) {

			if (a.error) return abort();

			int const size = std::min(std::int64_t(m_piece_size - m_offset), m_left_to_send);
			async_write(a.buffer, m_offset, size);
			++m_next_piece;
			TORRENT_ASSERT(m_left_to_send >= size);
			m_left_to_send -= size;
			m_offset = 0;

			set_piece_deadlines_impl();
		} else if (a.piece >= m_next_piece && a.piece < m_end_piece) {
			if (a.error) return abort();
			m_out_of_order[a.piece] = a.buffer;
		}
	}

	void on_write(beast::error_code const& ec, std::size_t)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(m_writing);
		m_writing = false;
		m_currently_sending = nullptr;
		if (ec) return abort();
		if (m_stopped) return;

		if (m_next_piece == m_end_piece) {
			TORRENT_ASSERT(m_left_to_send == 0);
			m_stopped = true;
			l.unlock();
			m_done(false);
			return;
		}
		auto const it = m_out_of_order.find(m_next_piece);
		if (it == m_out_of_order.end()) return;

		int const size = std::min(std::int64_t(m_piece_size - m_offset), m_left_to_send);
		std::cout << "async_write: " << m_next_piece << '\n';
		async_write(std::move(it->second), m_offset, size);
		m_out_of_order.erase(it);
		++m_next_piece;
		TORRENT_ASSERT(m_left_to_send >= size);
		m_left_to_send -= size;
		m_offset = 0;

		set_piece_deadlines_impl();
	}

private:
	void set_piece_deadlines_impl()
	{
		if (m_stopped) return;

		// this 4 MiB should be picked in a more sophisticated way
		lt::piece_index_t::diff_type const prefetch(std::max(1, 4 * 1024 * 1024 / m_piece_size));
		int deadline = 1;
		while (m_next_priority_piece - m_next_piece < prefetch
			   && m_next_priority_piece < m_end_piece) {
			std::cout << "set piece deadline: " << m_next_priority_piece << '\n';
			m_torrent.set_piece_deadline(
				m_next_priority_piece, deadline, lt::torrent_handle::alert_when_available
			);
			++deadline;
			++m_next_priority_piece;
		}
	}

	void abort()
	{
		if (m_stopped) return;
		m_stopped = true;
		m_out_of_order.clear();
		for (lt::piece_index_t i = m_next_piece; i < m_next_priority_piece; ++i) {
			std::cout << "reset piece deadline: " << i << '\n';
			m_torrent.reset_piece_deadline(i);
		}
		//TODO: It would be nice to have reference counting, so we won't remove the
		// deadline in case there are other requests in flight

		// we can't call m_done here, since we have m_mutex locked, and the done
		// callback will inspect our stopped() state, which also requires
		// locking the mutex
		post(m_socket.get_executor(), std::bind(m_done, true));
	}

	void async_write(boost::shared_array<char> buf, int offset, int size)
	{
		using boost::asio::buffer;
		TORRENT_ASSERT(!m_currently_sending);
		TORRENT_ASSERT(!m_writing);
		boost::asio::async_write(
			m_socket,
			buffer(buf.get() + offset, size),
			beast::bind_front_handler(&file_request_conn::on_write, shared_from_this())
		);
		m_writing = true;
		m_currently_sending = std::move(buf);
	}

	// since we only have a single async operation outstanding (async_write())
	// all completion handlers will be called serially and don't need
	// synchronization. Howerver, the on_alert handler is called from a
	// different thread and so access to all members must be protected by a
	// mutec
	mutable std::mutex m_mutex;

	// pieces we may receive out of order. store them here until it's time
	// to send them
	std::map<lt::piece_index_t, boost::shared_array<char>> m_out_of_order;

	// we use this to keep a reference to the buffer currently in the
	// async_write() call, to keep it alive.
	boost::shared_array<char> m_currently_sending;

	lt::piece_index_t m_next_piece;
	lt::piece_index_t m_next_priority_piece;
	lt::piece_index_t m_end_piece;

	// the number of bytes left to send
	std::int64_t m_left_to_send;

	// the socket to write the response to
	beast::ssl_stream<beast::tcp_stream>& m_socket;

	// called when the full response has been sent
	std::function<void(bool)> m_done;

	// we need this in order to keep updating file priorities and deadlines
	// as we receive pieces
	lt::torrent_handle m_torrent;

	int m_piece_size;

	// offset into the next piece (should be zero except for the first
	// piece)
	int m_offset;

	// when this is true, we have an outstanding write operation to the
	// socket and we cannot issue another one until it completes.
	// we always start in writing mode, because we're writing the header
	bool m_writing = true;

	// when the request has been fully sent, or aborted, this is set to true, to
	// prevent anything else from being sent on the socket
	bool m_stopped = false;

	// TODO: there should be a timeout too
};

namespace {
struct write_header_op {
	write_header_op(http::status s, unsigned v)
		: res(s, v)
	{
	}
	http::response<http::empty_body> res;
	http::response_serializer<http::empty_body> sr{res};
};
} // namespace

std::tuple<std::int64_t, std::int64_t, bool>
parse_range(http::request<http::string_body> const& req, std::int64_t file_size)
{
	auto range_header = req.find(http::field::range);
	if (range_header == req.end()) return {0, file_size - 1, false};

	auto range_str = trim(range_header->value());
	if (!starts_with(range_str, "bytes=")) return {0, file_size - 1, false};

	// skip bytes=
	auto const [first, last] = split(range_str.substr(6), '-');

	std::int64_t first_byte = 0;
	std::from_chars_result ret =
		std::from_chars(first.data(), first.data() + first.size(), first_byte);

	if (ret.ec != std::errc{} || ret.ptr != first.data() + first.size())
		return {0, file_size - 1, false};

	std::int64_t last_byte = file_size - 1;
	if (!last.empty()) {
		ret = std::from_chars(last.data(), last.data() + last.size(), last_byte);
		if (ret.ec != std::errc{} || ret.ptr != last.data() + last.size())
			return {0, file_size - 1, false};
	}

	return {first_byte, last_byte, true};
}

file_downloader::file_downloader(lt::session& s, alert_handler* alert, auth_interface const& auth)
	: m_ses(s)
	, m_auth(auth)
	, m_attachment(true)
	, m_alert(alert)
{
	m_alert->subscribe(this, 0, lt::read_piece_alert::alert_type, 0);
}

file_downloader::~file_downloader() { m_alert->unsubscribe(this); }

void file_downloader::shutdown()
{
	std::vector<std::shared_ptr<file_request_conn>> conns;
	{
		std::lock_guard<std::mutex> l(m_mutex);
		for (auto& [_, conn] : m_outstanding_requests)
			conns.push_back(conn);
	}
	for (auto& conn : conns)
		conn->cancel();
}

std::string file_downloader::path_prefix() const { return "/download/"; }

void file_downloader::handle_alert(lt::alert const* a)
{
	lt::read_piece_alert const* rp = lt::alert_cast<lt::read_piece_alert>(a);
	if (!rp) return;

	lt::torrent_handle h = rp->handle;
	std::lock_guard<std::mutex> l(m_mutex);
	auto requests = m_outstanding_requests.equal_range(h);
	for (auto i = requests.first; i != requests.second; ++i) {
		i->second->on_piece_alert(*rp);
	}
}

void file_downloader::handle_http(
	http::request<http::string_body> request,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	permissions_interface const* perms = parse_http_auth(request, m_auth);
	if (!perms || !perms->allow_get_data())
		return send_http(socket, std::move(done), http_error(request, http::status::unauthorized));

	auto const [info_hash_str, file_str] = split(request.target().substr(10), '/');

	if (info_hash_str.size() != 40)
		return send_http(socket, std::move(done), http_error(request, http::status::bad_request));

	lt::sha1_hash info_hash;
	if (!from_hex(info_hash_str, info_hash.data()))
		return send_http(socket, std::move(done), http_error(request, http::status::bad_request));

	lt::file_index_t const file{atoi(std::string(file_str).c_str())};

	// TODO: find_torrent() is synchronous, we should use async functions only
	lt::torrent_handle h = m_ses.find_torrent(info_hash);

	if (!h.is_valid())
		return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	std::shared_ptr<lt::torrent_info const> ti = h.torrent_file();
	if (!ti || !ti->is_valid())
		return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	if (file < lt::file_index_t{} || file >= ti->layout().end_file())
		return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	std::int64_t const file_size = ti->layout().file_size(file);

	auto const [range_first_byte, range_last_byte, range_request] = parse_range(request, file_size);

	if (range_request
		&& (range_first_byte > range_last_byte || range_last_byte >= file_size
			|| range_first_byte < 0)) {
		std::stringstream content_range;
		content_range << "*/" << file_size;
		http::response<http::empty_body> response(
			http::status::range_not_satisfiable, request.version()
		);
		response.keep_alive(request.keep_alive());
		response.set(http::field::content_range, content_range.str());
		return send_http(socket, std::move(done), std::move(response));
	}

	std::cout << info_hash << " / " << file << '\n';
	if (range_request) {
		std::cout << "GET range: " << range_first_byte << " - " << range_last_byte << '\n';
	}

	lt::peer_request const req = ti->map_file(file, range_first_byte, 0);
	lt::piece_index_t const first_piece = req.piece;
	lt::piece_index_t const end_piece = next(ti->map_file(file, range_last_byte, 0).piece);
	int offset = req.start;

	// wrap the done callback to also remove the file_downloader_conn from the
	// map
	auto wrap_done = [this, h, d = std::move(done)](bool close) {
		{
			std::lock_guard<std::mutex> l(m_mutex);
			auto conns = m_outstanding_requests.equal_range(h);
			for (auto it = conns.first; it != conns.second;) {
				if (it->second->stopped())
					it = m_outstanding_requests.erase(it);
				else
					++it;
			}
		}
		d(close);
	};

	auto freq = std::make_shared<file_request_conn>(
		socket,
		std::move(wrap_done),
		h,
		first_piece,
		end_piece,
		offset,
		ti->layout().piece_length(),
		range_last_byte - range_first_byte + 1
	);

	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_outstanding_requests.emplace(h, freq);
	}

	freq->set_piece_deadlines();

	http::status const status = range_request ? http::status::partial_content : http::status::ok;

	// TODO: this could use make_unique
	auto op = std::make_shared<write_header_op>(status, request.version());
	op->res.content_length(range_last_byte - range_first_byte + 1);
	op->res.keep_alive(request.keep_alive());
	op->res.set(http::field::accept_ranges, "bytes");
	// TODO: get_renamed_files() is synchronous, we should use async functions only
	lt::renamed_files const renames = h.get_renamed_files();
	lt::string_view const fname = renames.file_name(ti->layout(), file);
	op->res.set(http::field::content_type, mime_type(extension(fname)));
	if (m_attachment) {
		op->res.set(
			http::field::content_disposition, str("attachment; filename=", percent_encode(fname))
		);
	}
	if (range_request) {
		std::stringstream range;
		range << "bytes " << range_first_byte << '-' << range_last_byte << '/' << file_size;
		op->res.set(http::field::content_range, range.str());
	}

	async_write_header(
		socket,
		op->sr,
		[send_op = op, freq = std::move(freq)](beast::error_code const& ec, std::size_t size) {
			freq->on_write(ec, size);
		}
	);
}

} // namespace ltweb
