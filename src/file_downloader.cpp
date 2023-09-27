/*

Copyright (c) 2012-2014, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "webui.hpp"
#include "file_downloader.hpp"
#include "no_auth.hpp"
#include "auth.hpp"
#include "hex.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/peer_id.hpp" // for sha1_hash
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for escape_string

#include <boost/shared_array.hpp>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cinttypes>

extern "C" {
#include "local_mongoose.h"
}

namespace {
	lt::string_view::size_type find(lt::string_view haystack, lt::string_view needle, lt::string_view::size_type pos)
	{
		auto const p = haystack.substr(pos).find(needle);
		if (p == lt::string_view::npos) return p;
		return pos + p;
	}

	lt::string_view url_has_argument(
		lt::string_view url, std::string argument, std::string::size_type* out_pos = nullptr)
	{
		auto i = url.find('?');
		if (i == std::string::npos) return {};
		++i;

		argument += '=';

		if (url.substr(i, argument.size()) == argument)
		{
			auto const pos = i + argument.size();
			if (out_pos) *out_pos = pos;
			return url.substr(pos, url.substr(pos).find('&'));
		}
		argument.insert(0, "&");
		i = find(url, argument, i);
		if (i == std::string::npos) return {};
		auto const pos = i + argument.size();
		if (out_pos) *out_pos = pos;
		return url.substr(pos, find(url, "&", pos) - pos);
	}
}

namespace libtorrent
{
	struct piece_entry
	{
		boost::shared_array<char> buffer;
		int size;
		piece_index_t piece;
		// we want ascending order!
		bool operator<(piece_entry const& rhs) const { return piece > rhs.piece; }
	};

	struct torrent_piece_queue
	{
		// this is the range of pieces we're interested in
		piece_index_t begin;
		piece_index_t end;
		// end may not progress past this. This is end of file
		// or end of request
		piece_index_t finish;
		std::priority_queue<piece_entry> queue;
		std::condition_variable cond;
		std::mutex queue_mutex;
	};

	struct request_t
	{
		request_t(std::string filename, std::set<request_t*>& list, std::mutex& m)
			: start_time(clock_type::now())
			, file(filename)
			, request_size(0)
			, file_size(0)
			, start_offset(0)
			, bytes_sent(0)
			, piece(-1)
			, state(0)
			, m_requests(list)
			, m_mutex(m)
		{
			std::unique_lock<std::mutex> l(m_mutex);
			m_requests.insert(this);
		}

		~request_t()
		{
			std::unique_lock<std::mutex> l(m_mutex);
			debug_print(clock_type::now());
			m_requests.erase(this);
		}

		void debug_print(time_point now) const
		{
			const int progress_width = 150;
			char prefix[progress_width+1];
			char suffix[progress_width+1];
			char progress[progress_width+1];
			char invprogress[progress_width+1];

			memset(prefix, ' ', sizeof(prefix));
			memset(suffix, ' ', sizeof(suffix));
			memset(progress, '#', sizeof(progress));
			memset(invprogress, '.', sizeof(invprogress));

			int start = (std::min)(start_offset * progress_width / file_size, std::uint64_t(progress_width) - 1);
			int progress_range = (std::max)(std::uint64_t(1), request_size * progress_width / file_size);
			int pos = request_size == 0 ? 0 : bytes_sent * progress_range / request_size;
			int pos_end = progress_range - pos;
			prefix[start] = 0;
			progress[pos] = 0;
			invprogress[pos_end] = 0;
			suffix[progress_width-start-pos-pos_end] = 0;

			printf("%4.1f [%s%s%s%s] [p: %4d] [s: %d] %s\n"
				, total_milliseconds(now - start_time) / 1000.f
				, prefix, progress, invprogress, suffix, static_cast<int>(piece)
				, state, file.c_str());
		}

		enum state_t
		{
			received, writing_to_socket, waiting_for_libtorrent
		};

		time_point const start_time;
		std::string const file;
		std::uint64_t request_size;
		std::uint64_t file_size;
		std::uint64_t start_offset;
		std::uint64_t bytes_sent;
		piece_index_t piece;
		int state;

	private:
		std::set<request_t*>& m_requests;
		std::mutex& m_mutex;
	};

	// TODO: replace this with file_requests class
	struct piece_alert_dispatch : plugin
	{
		libtorrent::feature_flags_t implemented_features() override { return plugin::alert_feature; }

		void on_alert(alert const* a) override
		{
			read_piece_alert const* p = alert_cast<read_piece_alert>(a);
			if (p == nullptr) return;

//			fprintf(stderr, "piece: %d\n", p->piece);

			std::unique_lock<std::mutex> l(m_mutex);
			using iter = std::multimap<sha1_hash, torrent_piece_queue*>::iterator;
			std::shared_ptr<aux::torrent> t = p->handle.native_handle();

			std::pair<iter, iter> range = m_torrents.equal_range(t->info_hash().get_best());
			if (range.first == m_torrents.end()) return;

			for (iter i = range.first; i != range.second; ++i)
			{
				std::unique_lock<std::mutex> l2(i->second->queue_mutex);
				if (p->piece < i->second->begin || p->piece >= i->second->end)
					continue;
				piece_entry pe;
				pe.buffer = p->buffer;
				pe.piece = p->piece;
				pe.size = p->size;

				i->second->queue.push(pe);
				if (pe.piece == i->second->begin)
					i->second->cond.notify_all();
			}
		}

		void subscribe(sha1_hash const& ih, torrent_piece_queue* pq)
		{
			std::unique_lock<std::mutex> l(m_mutex);
			m_torrents.insert(std::make_pair(ih, pq));
		}

		// if the pieces pointer is specified, it's filled in with pieces
		// that were part of the 'pq' request, and are also still parts of
		// other requests, that are still outstanding
		void unsubscribe(sha1_hash const& ih, torrent_piece_queue* pq
			, std::set<piece_index_t>* pieces = nullptr)
		{
			std::unique_lock<std::mutex> l(m_mutex);
			using iter = std::multimap<sha1_hash, torrent_piece_queue*>::iterator;

			std::pair<iter, iter> range = m_torrents.equal_range(ih);
			if (range.first == m_torrents.end()) return;

			iter to_delete = m_torrents.end();

			for (iter i = range.first; i != range.second; ++i)
			{
				if (i->second != pq)
				{
					if (pieces)
					{
						for (piece_index_t k = (std::max)(pq->begin, i->second->begin)
							, end((std::min)(pq->end, i->second->end)); k < end; ++k)
						{
							pieces->insert(k);
						}
					}
					continue;
				}

				to_delete = i;

				// if we want to know the pieces that are still in use, we need to
				// continue and iterate over all other torrents
				if (!pieces) break;
			}

			if (to_delete != m_torrents.end())
				m_torrents.erase(to_delete);
		}

	private:

		std::mutex m_mutex;
		std::multimap<sha1_hash, torrent_piece_queue*> m_torrents;

	};

	file_downloader::file_downloader(session& s, auth_interface const* auth)
		: m_ses(s)
		, m_auth(auth)
		, m_dispatch(new piece_alert_dispatch())
// TODO: this number needs to be proportional to the rate at which a file
// is downloaded
		, m_queue_size(20 * 1024 * 1024)
		, m_attachment(true)
	{
		if (m_auth == nullptr)
		{
			const static no_auth n;
			m_auth = &n;
		}

		m_ses.add_extension(std::static_pointer_cast<libtorrent::plugin>(m_dispatch));
	}

	bool file_downloader::handle_http(mg_connection* conn,
		mg_request_info const* request_info)
	{
		if (!aux::string_begins_no_case(request_info->uri, "/download")
			&& !aux::string_begins_no_case(request_info->uri, "/proxy"))
			return false;

		permissions_interface const* perms = parse_http_auth(conn, m_auth);
		if (!perms || !perms->allow_get_data())
		{
			mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\n"
				"WWW-Authenticate: Basic realm=\"BitTorrent\"\r\n"
				"Content-Length: 0\r\n\r\n");
			return true;
		}

		string_view info_hash_str;
		string_view file_str;
		std::string query_string = "?";
		if (request_info->query_string)
		{
			query_string += request_info->query_string;
			info_hash_str = url_has_argument(query_string, "ih");
			file_str = url_has_argument(query_string, "file");
			if (info_hash_str.empty())
				info_hash_str = url_has_argument(query_string, "sid");
		}

		if (file_str.empty() || info_hash_str.empty() || info_hash_str.size() != 40)
		{
			mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\n");
			return true;
		}

		file_index_t const file{atoi(std::string(file_str).c_str())};

		sha1_hash info_hash;
		from_hex(info_hash_str, info_hash.data());

		torrent_handle h = m_ses.find_torrent(info_hash);

		// TODO: it would be nice to wait for the metadata to complete
		if (!h.is_valid())
		{
			mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\n");
			return true;
		}

		std::shared_ptr<torrent_info const> ti = h.torrent_file();
		if (!ti->is_valid())
		{
			mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\n");
			return true;
		}

		if (file < file_index_t{} || file >= ti->files().end_file())
		{
			mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\n");
			return true;
		}

		std::int64_t const file_size = ti->files().file_size(file);
		std::int64_t range_first_byte = 0;
		std::int64_t range_last_byte = file_size - 1;
		bool range_request = false;

		char const* range = mg_get_header(conn, "range");
		if (range)
		{
			range = strstr(range, "bytes=");
			if (range)
			{
				range += 6; // skip bytes=
				char const* divider = strchr(range, '-');
				if (divider)
				{
					range_first_byte = strtoll(range, nullptr, 10);

					// if the end of a range is not specified, the end of file
					// is implied
					if (divider[1] != '\0')
						range_last_byte = strtoll(divider+1, nullptr, 10);
					else
						range_last_byte = file_size - 1;

					range_request = true;
				}
			}
		}

		peer_request req = ti->map_file(file, range_first_byte, 0);
		piece_index_t const first_piece = req.piece;
		piece_index_t const end_piece = next(ti->map_file(file, range_last_byte, 0).piece);
		std::uint64_t offset = req.start;

		if (range_request && (range_first_byte > range_last_byte
			|| range_last_byte >= file_size
			|| range_first_byte < 0))
		{
			mg_printf(conn, "HTTP/1.1 416 Requested Range Not Satisfiable\r\n"
				"Content-Length: %" PRId64 "\r\n\r\n"
				, file_size);
			return true;
		}

		printf("GET range: %" PRId64 " - %" PRId64 "\n", range_first_byte, range_last_byte);

		torrent_piece_queue pq;
		pq.begin = first_piece;
		pq.finish = end_piece;
		auto const num_pieces = static_cast<piece_index_t::diff_type>(std::max(m_queue_size / ti->piece_length(), 1));
		pq.end = (std::min)(first_piece + num_pieces, pq.finish);

		m_dispatch->subscribe(info_hash, &pq);

		piece_index_t priority_cursor = pq.begin;

		request_t r(ti->files().file_path(file), m_requests, m_mutex);
		r.request_size = range_last_byte - range_first_byte + 1;
		r.file_size = ti->files().file_size(file);
		r.start_offset = range_first_byte;

		string_view const fname = ti->files().file_name(file);
		r.state = request_t::writing_to_socket;
		mg_printf(conn, "HTTP/1.1 %s\r\n"
			"Content-Length: %" PRId64 "\r\n"
			"Content-Type: %s\r\n"
			"%s%s%s"
			"Accept-Ranges: bytes\r\n"
			, range_request ? "206 Partial Content" : "200 OK"
			, range_last_byte - range_first_byte + 1
			, mg_get_builtin_mime_type(std::string(ti->files().file_name(file)).c_str())
			, m_attachment ? "Content-Disposition: attachment; filename=" : ""
			, m_attachment ? escape_string(fname).c_str() : ""
			, m_attachment ? "\r\n" : "");

		if (range_request)
		{
			mg_printf(conn, "Content-Range: bytes %" PRId64 "-%" PRId64 "/%" PRId64 "\r\n\r\n"
				, range_first_byte, range_last_byte, file_size);
		}
		else
		{
			mg_printf(conn, "\r\n");
		}
		r.state = request_t::waiting_for_libtorrent;

		std::int64_t left_to_send = range_last_byte - range_first_byte + 1;
//		printf("left_to_send: %" PRId64 " bytes\n", left_to_send);

		// increase the priority of this range to 5
		std::vector<std::pair<piece_index_t, download_priority_t>> pieces_in_req;
		pieces_in_req.resize(static_cast<int>(pq.finish - pq.begin));
		piece_index_t p = pq.begin;
		for (auto& e : pieces_in_req)
		{
			e = {p, lt::download_priority_t{5}};
			++p;
		}
		h.prioritize_pieces(pieces_in_req);

		while (priority_cursor < pq.end)
		{
//			printf("set_piece_deadline: %d\n", priority_cursor);
			h.set_piece_deadline(priority_cursor
				, 100 * static_cast<int>(priority_cursor - pq.begin)
				, torrent_handle::alert_when_available);
			++priority_cursor;
		}

		for (piece_index_t i = pq.begin; i < pq.finish; ++i)
		{
			std::unique_lock<std::mutex> l(pq.queue_mutex);

			// TODO: come up with some way to abort
			while (pq.queue.empty() || pq.queue.top().piece > i)
			{
				pq.cond.wait(l);
				// TODO: we may have woken up because of a SIGPIPE and this
				// connection may have been broken. Test to see if our connection
				// to the client is still open, and if it isn't, abort
			}

			piece_entry pe = pq.queue.top();
			pq.queue.pop();

			if (pe.piece < i)
			{
				--i; // we don't want to increment i in this case. Just ignore
				// the piece we got in from the queue
				continue;
			}

			pq.end = std::min(next(pq.end), pq.finish);
			pq.begin = std::min(next(pq.begin), pq.end);

			l.unlock();

			while (priority_cursor < pq.end)
			{
//				printf("set_piece_deadline: %d\n", priority_cursor);
				h.set_piece_deadline(priority_cursor
					, 100 * static_cast<int>(priority_cursor - i)
					, torrent_handle::alert_when_available);
				++priority_cursor;
			}

			r.piece = pe.piece;

			if (pe.size == 0)
			{
				printf("interrupted (zero bytes read)\n");
				break;
			}

			int ret = -1;
			int amount_to_send = (std::min)(std::int64_t(pe.size - offset), left_to_send);
//			fprintf(stderr, "[%p] amount_to_send: 0x%x bytes [p: %d] [l: %" PRId64 "]\n"
//				, &r, amount_to_send, pq.finish - i, left_to_send);

			while (amount_to_send > 0)
			{
				r.state = request_t::writing_to_socket;
				TORRENT_ASSERT(offset >= 0);
				TORRENT_ASSERT(offset + amount_to_send <= pe.size);
				ret = mg_write(conn, &pe.buffer[offset], amount_to_send);
				if (ret <= 0)
				{
					fprintf(stderr, "interrupted (%d) errno: (%d) %s\n", ret, errno
						, strerror(errno));
					if (ret < 0 && errno == EAGAIN) {
						usleep(100000);
						continue;
					}
					break;
				}
				TORRENT_ASSERT(r.bytes_sent + ret<= r.request_size);
				r.bytes_sent += ret;
				r.state = request_t::waiting_for_libtorrent;

				left_to_send -= ret;
//				printf("sent: %d bytes [%d]\n", amount_to_send, i);
				offset += ret;
				amount_to_send -= ret;
			}
			if (ret <= 0) break;
			offset = 0;
		}

		std::set<piece_index_t> still_in_use;
		m_dispatch->unsubscribe(info_hash, &pq, &still_in_use);

		for (piece_index_t k = pq.begin; k < priority_cursor; ++k)
		{
			if (still_in_use.count(k)) continue;
			printf("reset_piece_deadline: %d\n", static_cast<int>(k));
			h.reset_piece_deadline(k);
		}
//		printf("done, sent %" PRId64 " bytes\n", r.bytes_sent);

		// TODO: this doesn't work right if there are overlapping requests

		// restore piece priorities
		for (auto& e : pieces_in_req)
			e.second = default_priority;
		h.prioritize_pieces(pieces_in_req);

		return true;
	}

	void file_downloader::debug_print_requests() const
	{
		time_point now = clock_type::now();
		std::unique_lock<std::mutex> l(m_mutex);
		for (std::set<request_t*>::const_iterator i = m_requests.begin()
			, end(m_requests.end()); i != end; ++i)
		{
			request_t const& r = **i;
			r.debug_print(now);
		}
	}
}

