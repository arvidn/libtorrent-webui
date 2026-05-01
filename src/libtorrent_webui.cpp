/*

Copyright (c) 2013-2015, 2017-2020, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstring>
#include <memory>
#include <chrono>
#include <string_view>
#include <vector>
#include <algorithm>
#include <iterator>

#include "libtorrent_webui.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/announce_entry.hpp"

#include "auth.hpp"
#include "save_settings.hpp"
#include "torrent_history.hpp"
#include "websocket_conn.hpp"

#include <boost/beast/websocket.hpp>
#include <boost/beast/core/multi_buffer.hpp>

#include "alert_handler.hpp"

namespace ws = boost::beast::websocket;

using namespace std::literals::chrono_literals;
using namespace lt::literals;

namespace ltweb {
namespace {

std::uint32_t peer_identifier(lt::peer_info const& pi)
{
	lt::hasher h;
	h.update(pi.pid);
#if TORRENT_USE_I2P
	if (pi.flags & lt::peer_info::i2p_socket) {
		lt::sha256_hash const dest = pi.i2p_destination();
		h.update(dest);
	} else
#endif
	{
		auto const ep = pi.remote_endpoint();
		if (ep.address().is_v6()) {
			auto const b = ep.address().to_v6().to_bytes();
			h.update({reinterpret_cast<char const*>(b.data()), std::ptrdiff_t(b.size())});
		} else {
			auto const b = ep.address().to_v4().to_bytes();
			h.update({reinterpret_cast<char const*>(b.data()), std::ptrdiff_t(b.size())});
		}
		std::uint8_t const port_bytes[2] = {std::uint8_t(ep.port() >> 8), std::uint8_t(ep.port())};
		h.update({reinterpret_cast<char const*>(port_bytes), 2});
	}
	lt::sha1_hash const digest = h.final();
	std::uint32_t ret;
	std::memcpy(&ret, digest.data(), sizeof(ret));
	return ret;
}

template <typename It>
std::uint8_t read_uint8(It& p)
{
	return static_cast<std::uint8_t>(*p++);
}
template <typename It>
std::uint16_t read_uint16(It& p)
{
	std::uint16_t const hi = read_uint8(p);
	std::uint16_t const lo = read_uint8(p);
	return std::uint16_t((hi << 8) | lo);
}
template <typename It>
std::uint32_t read_uint32(It& p)
{
	std::uint32_t const b3 = read_uint8(p);
	std::uint32_t const b2 = read_uint8(p);
	std::uint32_t const b1 = read_uint8(p);
	std::uint32_t const b0 = read_uint8(p);
	return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}
template <typename It>
std::int32_t read_int32(It& p)
{
	return static_cast<std::int32_t>(read_uint32(p));
}
template <typename It>
std::uint64_t read_uint64(It& p)
{
	std::uint64_t const hi = read_uint32(p);
	std::uint64_t const lo = read_uint32(p);
	return (hi << 32) | lo;
}
template <typename It>
void write_uint8(std::uint8_t v, It& p)
{
	*p++ = static_cast<char>(v);
}
template <typename It>
void write_uint16(std::uint16_t v, It& p)
{
	write_uint8(std::uint8_t(v >> 8), p);
	write_uint8(std::uint8_t(v), p);
}
template <typename It>
void write_uint32(std::uint32_t v, It& p)
{
	write_uint8(std::uint8_t(v >> 24), p);
	write_uint8(std::uint8_t(v >> 16), p);
	write_uint8(std::uint8_t(v >> 8), p);
	write_uint8(std::uint8_t(v), p);
}
template <typename It>
void write_uint64(std::uint64_t v, It& p)
{
	write_uint32(std::uint32_t(v >> 32), p);
	write_uint32(std::uint32_t(v), p);
}

struct rpc_entry {
	char const* name;
	bool (libtorrent_webui::*handler)(websocket_conn*, function_call);
};

static std::array<rpc_entry, 25> const functions = {{
	{"get-torrent-updates", &libtorrent_webui::get_torrent_updates},
	{"start", &libtorrent_webui::start},
	{"stop", &libtorrent_webui::stop},
	{"set-auto-managed", &libtorrent_webui::set_auto_managed},
	{"clear-auto-managed", &libtorrent_webui::clear_auto_managed},
	{"queue-up", &libtorrent_webui::queue_up},
	{"queue-down", &libtorrent_webui::queue_down},
	{"queue-top", &libtorrent_webui::queue_top},
	{"queue-bottom", &libtorrent_webui::queue_bottom},
	{"remove", &libtorrent_webui::remove},
	{"remove_and_data", &libtorrent_webui::remove_and_data},
	{"force_recheck", &libtorrent_webui::force_recheck},
	{"set-sequential-download", &libtorrent_webui::set_sequential_download},
	{"clear-sequential-download", &libtorrent_webui::clear_sequential_download},
	{"list-settings", &libtorrent_webui::list_settings},
	{"get-settings", &libtorrent_webui::get_settings},
	{"set-settings", &libtorrent_webui::set_settings},
	{"list-stats", &libtorrent_webui::list_stats},
	{"get-stats", &libtorrent_webui::get_stats},
	{"get-file-updates", &libtorrent_webui::get_file_updates},
	{"add-torrent", &libtorrent_webui::add_torrent},
	{"get-peers-updates", &libtorrent_webui::get_peers_updates},
	{"get-piece-updates", &libtorrent_webui::get_piece_updates},
	{"set-file-priority", &libtorrent_webui::set_file_priority},
	{"get-tracker-updates", &libtorrent_webui::get_tracker_updates},
}};

// maps torrent field to RPC field. These fields are the ones defined in
// torrent_history_entry
std::array<int const, torrent_history_entry::num_fields> const torrent_field_map = {{
	20, // state,
	0, // flags,
	0, // is_seeding,
	0, // is_finished,
	0, // has_metadata,
	-1, // progress,
	8, // progress_ppm,
	9, // errc,
	23, // error_file,
	-1, // save_path,
	1, // name,
	-1, // next_announce,
	-1, // current_tracker,
	3, // total_download,
	2, // total_upload,
	-1, // total_payload_download,
	-1, // total_payload_upload,
	21, // total_failed_bytes,
	22, // total_redundant_bytes,
	7, // download_rate,
	6, // upload_rate,
	-1, // download_payload_rate,
	-1, // upload_payload_rate,
	11, // num_seeds,
	10, // num_peers,
	-1, // num_complete,
	-1, // num_incomplete,
	-1, // list_seeds,
	-1, // list_peers,
	-1, // connect_candidates,
	12, // num_pieces,
	-1, // total_done,
	-1, // total,
	13, // total_wanted_done,
	-1, // total_wanted,
	14, // distributed_full_copies,
	14, // distributed_fraction,
	-1, // block_size,
	17, // num_uploads,
	18, // num_connections,
	-1, // num_undead_peers,
	-1, // uploads_limit,
	-1, // connections_limit,
	-1, // storage_mode,
	-1, // up_bandwidth_queue,
	-1, // down_bandwidth_queue,
	15, // all_time_upload,
	16, // all_time_download,
	-1, // active_duration,
	-1, // finished_duration,
	-1, // seeding_duration,
	-1, // seed_rank,
	0, // has_incoming,
	4, // added_time,
	5, // completed_time,
	-1, // last_seen_complete,
	-1, // last_upload,
	-1, // last_download,
	19, // queue_position,
	0, // moving_storage,
	0, // announcing_to_trackers,
	0, // announcing_to_lsd,
	0, // announcing_to_dht,
}};

struct add_torrent_user_data {
	std::shared_ptr<websocket_conn> st;
	int function_id;
	std::uint16_t transaction_id;
};

enum error_t {
	no_error,
	no_such_function,
	invalid_number_of_args,
	invalid_argument_type,
	invalid_argument,
	truncated_message,
	resource_not_found,
	parse_error,
	permission_denied,
	failed,
};

} // anonymous namespace

struct function_call {
	int function_id;
	std::uint16_t transaction_id;

	// TODO: this should probably be a span
	char const* data;
	int len;
};

libtorrent_webui::libtorrent_webui(
	lt::session& ses,
	torrent_history const& hist,
	auth_interface const& auth,
	alert_handler& alert,
	save_settings_interface& sett
)
	: m_ses(ses)
	, m_hist(hist)
	, m_auth(auth)
	, m_alert(alert)
	, m_settings(sett)
{

	if (m_stats.size() < lt::counters::num_counters)
		m_stats.resize(lt::counters::num_counters, std::pair<std::int64_t, frame_t>(0, 0));

	m_alert.subscribe(
		this, 0, lt::session_stats_alert::alert_type, lt::add_torrent_alert::alert_type, 0
	);
}

libtorrent_webui::~libtorrent_webui() { m_alert.unsubscribe(this); }

std::string libtorrent_webui::path_prefix() const { return "/bt/control"; }

void libtorrent_webui::handle_http(
	http::request<http::string_body> request,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	// authenticate
	permissions_interface const* perms = parse_http_auth(request, &m_auth);
	if (!perms)
		return send_http(socket, std::move(done), http_error(request, http::status::unauthorized));

	// we only provide access to /bt/control
	if (request.target() != "/bt/control"_sv)
		return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	if (!ws::is_upgrade(request))
		return send_http(socket, std::move(done), http_error(request, http::status::bad_request));

	ws::stream<beast::ssl_stream<beast::tcp_stream>> conn(std::move(socket));
	conn.binary(true);
	auto st = std::make_shared<websocket_conn>(this, perms, std::move(conn), std::move(done));
	{
		std::lock_guard<std::mutex> l(m_conns_mutex);
		// Prune expired entries to keep the list bounded.
		m_connections.erase(
			std::remove_if(
				m_connections.begin(),
				m_connections.end(),
				[](auto const& w) { return w.expired(); }
			),
			m_connections.end()
		);
		m_connections.push_back(st);
	}
	st->start_accept(request);
}

void libtorrent_webui::shutdown()
{
	std::lock_guard<std::mutex> l(m_conns_mutex);
	for (auto const& w : m_connections) {
		if (auto conn = w.lock()) conn->close();
	}
	m_connections.clear();
}

// this is one of the key functions in the interface. It goes to
// some length to ensure we only send relevant information back,
// and in a compact format
bool libtorrent_webui::get_torrent_updates(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_list()) return error(st, f, permission_denied);

	if (f.len < 12) return error(st, f, truncated_message);

	frame_t const frame = read_uint32(f.data);
	std::uint64_t user_mask = read_uint64(f.data);
	f.len -= 12;

	auto const r = m_hist.query(frame);
	auto const& torrents = r.updated;
	auto const& removed_torrents = r.removed;

	std::vector<char> response;
	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	// frame number (uint32)
	write_uint32(r.current_frame, ptr);

	// allocate space for torrent count
	// this will be filled in later when we know
	int num_torrents = 0;
	int const num_torrents_pos = response.size();
	write_uint32(num_torrents, ptr);

	write_uint32(r.is_snapshot ? 0xffffffff : removed_torrents.size(), ptr);

	for (std::vector<torrent_history_entry>::const_iterator i = torrents.begin(),
															end(torrents.end());
		 i != end;
		 ++i) {
		std::uint64_t bitmask = 0;

		// look at which fields actually have a newer frame number
		// than the caller. Don't return fields that haven't changed.
		for (int k = 0; k < torrent_history_entry::num_fields; ++k) {
			int f = torrent_field_map[k];
			if (f < 0) continue;
			if (i->frame[k] <= frame && !r.is_snapshot) continue;

			// this field has changed and should be included in this update
			bitmask |= 1 << f;
		}

		// only return fields the caller asked for
		bitmask &= user_mask;

		if (bitmask == 0) continue;

		++num_torrents;
		// first write the info-hash
		auto const ih = i->status.info_hashes.get_best();
		std::copy(ih.begin(), ih.end(), ptr);
		// then 64 bits of bitmask, indicating which fields
		// are included in the update for this torrent
		write_uint64(bitmask, ptr);

		lt::torrent_status const& s = i->status;

		for (int f = 0; f < 23; ++f) {
			if ((bitmask & (1 << f)) == 0) continue;

			// write field f to buffer
			switch (f) {
				case 0: // flags
				{
					std::uint64_t const flags = ((s.flags & lt::torrent_flags::paused) ? 0x001 : 0)
						| ((s.flags & lt::torrent_flags::auto_managed) ? 0x002 : 0)
						| ((s.flags & lt::torrent_flags::sequential_download) ? 0x004 : 0)
						| (s.is_seeding ? 0x008 : 0)
						| (s.is_finished ? 0x010 : 0)
						// 0x20 is unused
						| (s.has_metadata ? 0x040 : 0) | (s.has_incoming ? 0x080 : 0)
						| ((s.flags & lt::torrent_flags::seed_mode) ? 0x100 : 0)
						| ((s.flags & lt::torrent_flags::upload_mode) ? 0x200 : 0)
						| ((s.flags & lt::torrent_flags::share_mode) ? 0x400 : 0)
						| ((s.flags & lt::torrent_flags::super_seeding) ? 0x800 : 0)
						| (s.moving_storage ? 0x1000 : 0) | (s.announcing_to_trackers ? 0x2000 : 0)
						| (s.announcing_to_lsd ? 0x4000 : 0) | (s.announcing_to_dht ? 0x8000 : 0)
						| (s.has_metadata ? 0x10000 : 0);

					write_uint64(flags, ptr);
					break;
				}
				case 1: // name
				{
					std::string name = s.name;
					if (name.size() > 65535) name.resize(65535);
					write_uint16(name.size(), ptr);
					std::copy(name.begin(), name.end(), ptr);
					break;
				}
				case 2: // total-uploaded
					write_uint64(s.total_upload, ptr);
					break;
				case 3: // total-downloaded
					write_uint64(s.total_download, ptr);
					break;
				case 4: // added-time
					write_uint64(s.added_time, ptr);
					break;
				case 5: // completed_time
					write_uint64(s.completed_time, ptr);
					break;
				case 6: // upload-rate
					write_uint32(s.upload_rate, ptr);
					break;
				case 7: // download-rate
					write_uint32(s.download_rate, ptr);
					break;
				case 8: // progress
					write_uint32(s.progress_ppm, ptr);
					break;
				case 9: // error
				{
					std::string e = s.errc.message();
					if (e.size() > 65535) e.resize(65535);
					write_uint16(e.size(), ptr);
					std::copy(e.begin(), e.end(), ptr);
					break;
				}
				case 10: // connected-peers
					write_uint32(s.num_peers, ptr);
					break;
				case 11: // connected-seeds
					write_uint32(s.num_seeds, ptr);
					break;
				case 12: // downloaded-pieces
					write_uint32(s.num_pieces, ptr);
					break;
				case 13: // total-done
					write_uint64(s.total_wanted_done, ptr);
					break;
				case 14: // distributed-copies
					write_uint32(s.distributed_full_copies, ptr);
					write_uint32(s.distributed_fraction, ptr);
					break;
				case 15: // all-time-upload
					write_uint64(s.all_time_upload, ptr);
					break;
				case 16: // all-time-download
					write_uint64(s.all_time_download, ptr);
					break;
				case 17: // unchoked-peers
					write_uint32(s.num_uploads, ptr);
					break;
				case 18: // num-connections
					write_uint32(s.num_connections, ptr);
					break;
				case 19: // queue-position
					write_uint32(static_cast<int>(s.queue_position), ptr);
					break;
				case 20: // state
				{
					int state;
					switch (s.state) {
						case lt::torrent_status::checking_files:
						case lt::torrent_status::checking_resume_data:
							state = 0; // checking-files
							break;
						case lt::torrent_status::downloading_metadata:
							state = 1; // downloading-metadata
							break;
						case lt::torrent_status::downloading:
						default:
							state = 2; // downloading
							break;
						case lt::torrent_status::finished:
						case lt::torrent_status::seeding:
							state = 3; // seeding
							break;
					};
					write_uint8(state, ptr);
					break;
				}
				case 21: // failed-bytes
					write_uint64(s.total_failed_bytes, ptr);
					break;
				case 22: // redundant-bytes
					write_uint64(s.total_redundant_bytes, ptr);
					break;
				default:
					TORRENT_ASSERT(false);
			}
		}
	}

	// now that we know how many torrents we wrote, fill in the
	// counter
	char* ptr2 = &response[num_torrents_pos];
	write_uint32(num_torrents, ptr2);

	// send list of removed torrents
	for (auto const& ih : removed_torrents)
		std::copy(ih.begin(), ih.end(), ptr);

	return st->send_packet(response.data(), response.size());
}

template <typename Fun>
bool libtorrent_webui::apply_torrent_fun(websocket_conn* st, function_call f, Fun const& fun)
{
	char const* ptr = f.data;
	int num_torrents = read_uint16(ptr);

	// there are only supposed to be one ore more info-hashes as arguments. Each info-hash is
	// in its binary representation, and hence 20 bytes long.
	if ((f.len < num_torrents * 20)) return error(st, f, invalid_argument_type);

	int counter = 0;
	for (int i = 0; i < num_torrents; ++i) {
		// TODO: we should use short, 32 bit, indices for torrents, rather
		// than the full info-hash. This would also simplify support for
		// bittorrent-v2
		lt::sha1_hash const h(ptr + i * 20);

		lt::torrent_status ts = m_hist.get_torrent_status(h);
		if (!ts.handle.is_valid()) continue;
		fun(ts.handle);
		++counter;
	}
	return respond(st, f, 0, counter);
}

bool libtorrent_webui::start(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_start()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.set_flags(lt::torrent_flags::auto_managed);
		handle.clear_error();
		handle.resume();
	});
}

bool libtorrent_webui::stop(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_stop()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.unset_flags(lt::torrent_flags::auto_managed);
		handle.pause();
	});
}

bool libtorrent_webui::set_auto_managed(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_stop()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.set_flags(lt::torrent_flags::auto_managed);
	});
}
bool libtorrent_webui::clear_auto_managed(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_start()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.unset_flags(lt::torrent_flags::auto_managed);
	});
}
bool libtorrent_webui::queue_up(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_queue_change()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.queue_position_up();
	});
}
bool libtorrent_webui::queue_down(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_queue_change()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.queue_position_down();
	});
}
bool libtorrent_webui::queue_top(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_queue_change()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.queue_position_top();
	});
}
bool libtorrent_webui::queue_bottom(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_queue_change()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.queue_position_bottom();
	});
}
bool libtorrent_webui::remove(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_remove()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [this](lt::torrent_handle const& handle) {
		m_ses.remove_torrent(handle);
	});
}
bool libtorrent_webui::remove_and_data(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_remove() || !st->perms()->allow_remove_data())
		return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [this](lt::torrent_handle const& handle) {
		m_ses.remove_torrent(handle, lt::session::delete_files);
	});
}
bool libtorrent_webui::force_recheck(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_recheck()) return error(st, f, permission_denied);

	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.force_recheck();
	});
}
bool libtorrent_webui::set_sequential_download(websocket_conn* st, function_call f)
{
	// TODO: permissions
	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.set_flags(lt::torrent_flags::sequential_download);
	});
}
bool libtorrent_webui::clear_sequential_download(websocket_conn* st, function_call f)
{
	// TODO: permissions
	return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
		handle.unset_flags(lt::torrent_flags::sequential_download);
	});
}

bool libtorrent_webui::list_settings(websocket_conn* st, function_call f)
{
	std::vector<char> response;
	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	std::size_t const string_count_offset = response.size();
	write_uint32(lt::settings_pack::num_string_settings, ptr);
	std::size_t const int_count_offset = response.size();
	write_uint32(lt::settings_pack::num_int_settings, ptr);
	std::size_t const bool_count_offset = response.size();
	write_uint32(lt::settings_pack::num_bool_settings, ptr);

	int count = 0;
	for (int i = lt::settings_pack::string_type_base;
		 i < lt::settings_pack::max_string_setting_internal;
		 ++i) {
		// these should not be exposed to users
		if (i == lt::settings_pack::user_agent || i == lt::settings_pack::peer_fingerprint)
			continue;

		if (!st->perms()->allow_get_settings(i)) continue;

		char const* n = lt::name_for_setting(i);
		int len = strlen(n);
		// ignore deprecated settings
		if (len == 0) continue;
		TORRENT_ASSERT(len < 256);
		write_uint8(len, ptr);
		std::copy(n, n + len, ptr);
		TORRENT_ASSERT(i < 65536);
		write_uint16(i, ptr);
		++count;
	}
	char* patch = response.data() + string_count_offset;
	write_uint32(count, patch);

	count = 0;
	for (int i = lt::settings_pack::int_type_base; i < lt::settings_pack::max_int_setting_internal;
		 ++i) {
		if (!st->perms()->allow_get_settings(i)) continue;

		char const* n = lt::name_for_setting(i);
		int len = strlen(n);
		// ignore deprecated settings
		if (len == 0) continue;
		TORRENT_ASSERT(len < 256);
		write_uint8(len, ptr);
		std::copy(n, n + len, ptr);
		TORRENT_ASSERT(i < 65536);
		write_uint16(i, ptr);
		++count;
	}
	patch = response.data() + int_count_offset;
	write_uint32(count, patch);

	count = 0;
	for (int i = lt::settings_pack::bool_type_base;
		 i < lt::settings_pack::max_bool_setting_internal;
		 ++i) {
		if (!st->perms()->allow_get_settings(i)) continue;

		char const* n = lt::name_for_setting(i);
		int len = strlen(n);
		// ignore deprecated settings
		if (len == 0) continue;
		TORRENT_ASSERT(len < 256);
		write_uint8(len, ptr);
		std::copy(n, n + len, ptr);
		TORRENT_ASSERT(i < 65536);
		write_uint16(i, ptr);
		++count;
	}
	patch = response.data() + bool_count_offset;
	write_uint32(count, patch);
	return st->send_packet(&response[0], response.size());
}

bool libtorrent_webui::set_settings(websocket_conn* st, function_call f)
{
	char const* ptr = f.data;
	if (f.len < 2) return error(st, f, invalid_number_of_args);

	int num_settings = read_uint16(ptr);
	f.len -= 2;

	lt::settings_pack pack;

	for (int i = 0; i < num_settings; ++i) {
		if (f.len < 2) return error(st, f, invalid_number_of_args);
		int sett = read_uint16(ptr);
		f.len -= 2;

		if (!st->perms()->allow_set_settings(sett)) return error(st, f, permission_denied);

		if (sett == lt::settings_pack::user_agent || sett == lt::settings_pack::peer_fingerprint)
			return error(st, f, permission_denied);

		if (strlen(lt::name_for_setting(sett)) == 0) return error(st, f, invalid_argument);
		if (sett >= lt::settings_pack::string_type_base
			&& sett < lt::settings_pack::max_string_setting_internal) {
			if (f.len < 2) return error(st, f, invalid_number_of_args);
			int len = read_uint16(ptr);
			f.len -= 2;
			std::string str;
			str.resize(len);
			if (f.len < len) return error(st, f, invalid_number_of_args);
			std::copy(ptr, ptr + len, str.begin());
			ptr += len;
			f.len -= len;
			pack.set_str(sett, str);
		} else if (sett >= lt::settings_pack::int_type_base
				   && sett < lt::settings_pack::max_int_setting_internal) {
			if (f.len < 4) return error(st, f, invalid_number_of_args);
			pack.set_int(sett, read_uint32(ptr));
			f.len -= 4;
		} else if (sett >= lt::settings_pack::bool_type_base
				   && sett < lt::settings_pack::max_bool_setting_internal) {
			if (f.len < 1) return error(st, f, invalid_number_of_args);
			pack.set_bool(sett, read_uint8(ptr));
			f.len -= 1;
		} else {
			return error(st, f, invalid_argument);
		}
	}

	m_ses.apply_settings(pack);

	return error(st, f, no_error);
}

bool libtorrent_webui::get_settings(websocket_conn* st, function_call f)
{
	char const* iptr = f.data;
	if (f.len < 2) return error(st, f, invalid_number_of_args);
	int num_settings = read_uint16(iptr);
	f.len -= 2;

	if (f.len < num_settings * 2) return error(st, f, invalid_argument_type);

	std::vector<char> response;
	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	write_uint16(num_settings, ptr);

	lt::settings_pack s = m_ses.get_settings();

	for (int i = 0; i < num_settings; ++i) {
		int const sett = read_uint16(iptr);

		if (!st->perms()->allow_get_settings(sett)) return error(st, f, permission_denied);

		if (sett == lt::settings_pack::user_agent || sett == lt::settings_pack::peer_fingerprint)
			return error(st, f, permission_denied);

		if (sett >= lt::settings_pack::string_type_base
			&& sett < lt::settings_pack::max_string_setting_internal) {
			char const* n = lt::name_for_setting(sett);
			int len = strlen(n);
			// can't request deprecated settings
			if (len == 0) return error(st, f, invalid_argument);
			std::string const& v = s.get_str(sett);
			write_uint16(v.length(), ptr);
			std::copy(v.begin(), v.end(), ptr);
		} else if (sett >= lt::settings_pack::int_type_base
				   && sett < lt::settings_pack::max_int_setting_internal) {
			char const* n = lt::name_for_setting(sett);
			int len = strlen(n);
			// can't request deprecated settings
			if (len == 0) return error(st, f, invalid_argument);
			write_uint32(s.get_int(sett), ptr);
		} else if (sett >= lt::settings_pack::bool_type_base
				   && sett < lt::settings_pack::max_bool_setting_internal) {
			char const* n = lt::name_for_setting(sett);
			int len = strlen(n);
			// can't request deprecated settings
			if (len == 0) return error(st, f, invalid_argument);
			write_uint8(s.get_bool(sett), ptr);
		} else {
			return error(st, f, invalid_argument);
		}
	}

	return st->send_packet(&response[0], response.size());
}

bool libtorrent_webui::list_stats(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_session_status()) return error(st, f, permission_denied);

	std::vector<char> response;
	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	std::vector<lt::stats_metric> stats = lt::session_stats_metrics();
	write_uint16(stats.size(), ptr);

	for (auto const& s : stats) {
		write_uint16(s.value_index, ptr);
		write_uint8(static_cast<std::uint8_t>(s.type), ptr);
		int len = strlen(s.name);
		TORRENT_ASSERT(len < 256);
		write_uint8(len, ptr);
		std::copy(s.name, s.name + len, ptr);
	}

	return st->send_packet(&response[0], response.size());
}

void libtorrent_webui::handle_alert(lt::alert const* a)
{
	if (auto* ss = lt::alert_cast<lt::session_stats_alert>(a)) {
		std::unique_lock<std::mutex> l(m_stats_mutex);

		++m_stats_frame;
		lt::span<std::int64_t const> stats = ss->counters();

		// first update our copy of the stats, and update their frame counters
		for (int i = 0; i < lt::counters::num_counters; ++i) {
			if (m_stats[i].first != stats[i]) {
				m_stats[i].second = m_stats_frame;
				m_stats[i].first = stats[i];
			}
		}

		// TODO: notify handler?
	} else if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a)) {
		auto* ud = static_cast<add_torrent_user_data*>(at->params.userdata);
		if (ud == nullptr) return;

		std::unique_ptr<add_torrent_user_data> deleter(ud);

		char rpc[4];
		char* ptr = &rpc[0];
		write_uint8(ud->function_id | 0x80, ptr);
		write_uint16(ud->transaction_id, ptr);
		if (at->error)
			write_uint8(failed, ptr);
		else
			write_uint8(no_error, ptr);

		ud->st->send_packet(rpc, sizeof(rpc));
	}
}

bool libtorrent_webui::get_stats(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_session_status()) return error(st, f, permission_denied);

	char const* iptr = f.data;
	if (f.len < 6) return error(st, f, invalid_number_of_args);
	frame_t const frame = read_uint32(iptr);
	int num_stats = read_uint16(iptr);
	f.len -= 6;

	if (f.len < num_stats * 2) return error(st, f, invalid_number_of_args);

	m_ses.post_session_stats();

	// TODO: have a queue of calls to be made the next time we receive a
	// lt::session_stats_alert (that holds owning websocket_conn pointers). Don't
	// respond right now, but when we actually get fresh numbers

	std::vector<char> response;
	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	std::unique_lock<std::mutex> l(m_stats_mutex);
	write_uint32(m_stats_frame, ptr);

	// we'll fill in the counter later
	int const counter_pos = response.size();
	write_uint16(0, ptr);

	int num_updates = 0;
	for (int i = 0; i < num_stats; ++i) {
		int c = read_uint16(iptr);
		if (c < 0 || c > int(m_stats.size())) return error(st, f, invalid_argument);

		if (m_stats[c].second <= frame) continue;
		write_uint16(c, ptr);
		write_uint64(m_stats[c].first, ptr);
		++num_updates;
	}

	// TODO: wait for the alert and respond later

	// now that we know what the number of updates is, fill it in
	char* counter_ptr = &response[counter_pos];
	write_uint16(num_updates, counter_ptr);

	return st->send_packet(&response[0], response.size());
}

bool libtorrent_webui::get_file_updates(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_list()) return error(st, f, permission_denied);

	char const* iptr = f.data;
	if (f.len != 26) return error(st, f, invalid_number_of_args);
	lt::sha1_hash ih(iptr);
	iptr += 20;
	frame_t const client_frame = read_uint32(iptr);
	std::uint16_t const field_mask = read_uint16(iptr);

	lt::torrent_handle h = m_hist.get_torrent_status(ih).handle;
	if (!h.is_valid()) return error(st, f, invalid_argument);

	std::shared_ptr<const lt::torrent_info> t = h.torrent_file();
	if (!t) {
		// if this is a magnet link that doesn't have metadata yet, send an empty list
		std::vector<char> response;
		response.reserve(12);
		auto ptr = std::back_inserter(response);
		write_uint8(f.function_id | 0x80, ptr);
		write_uint16(f.transaction_id, ptr);
		write_uint8(no_error, ptr);
		write_uint32(client_frame, ptr); // frame number
		write_uint32(0, ptr); // number of files
		return st->send_packet(response.data(), response.size());
	}

	lt::file_storage const& fs = t->layout();

	// Only fetch data for the fields the client requested.
	// file_progress(), get_file_priorities() and file_status() are all
	// synchronous calls; skip them when not needed.
	std::vector<std::int64_t> fp;
	if (field_mask & 0x08) {
		fp = h.file_progress(lt::torrent_handle::piece_granularity);
		fp.resize(fs.num_files(), 0);
	}

	std::vector<lt::download_priority_t> fprio;
	if (field_mask & 0x10) {
		fprio = h.get_file_priorities();
		fprio.resize(fs.num_files(), lt::default_priority);
	}

	std::vector<lt::open_file_state> fstatus;
	if (field_mask & 0x20) fstatus = h.file_status();

	// Find or create the file_history for this info-hash in the LRU cache.
	// Hold the mutex through response serialisation; release before send_packet.
	std::vector<char> response;
	std::unique_lock<std::mutex> l(m_file_mutex);
	auto fh_it =
		std::find_if(m_file_histories.begin(), m_file_histories.end(), [&](file_history const& fh) {
			return fh.info_hash() == ih;
		});
	if (fh_it != m_file_histories.end())
		m_file_histories.splice(m_file_histories.begin(), m_file_histories, fh_it);
	else {
		m_file_histories.emplace_front(ih, fs);
		if (m_file_histories.size() > 10) m_file_histories.pop_back();
	}
	file_history& fh = m_file_histories.front();

	frame_t const new_frame = fh.update(
		(field_mask & 0x08) ? &fp : nullptr,
		(field_mask & 0x10) ? &fprio : nullptr,
		(field_mask & 0x20) ? &fstatus : nullptr
	);

	auto const per_file_masks = fh.query(client_frame, field_mask);

	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	write_uint32(new_frame, ptr);

	// number of files
	write_uint32(fs.num_files(), ptr);

	for (auto const fi : fs.file_range()) {
		int const i = static_cast<int>(fi);

		if ((i % 8) == 0) {
			// Build the 8-file presence bitmask using per-file masks.
			int const remaining = fs.num_files() - i;
			int const chunk = remaining < 8 ? remaining : 8;
			std::uint8_t presence = 0;
			for (int k = 0; k < chunk; ++k)
				if (per_file_masks[i + k] != 0) presence |= std::uint8_t(0x80 >> k);
			write_uint8(presence, ptr);
		}

		std::uint16_t const fmask = per_file_masks[i];
		if (fmask == 0) continue;

		write_uint16(fmask, ptr);

		if (fmask & 0x01) write_uint8(static_cast<std::uint8_t>(fs.file_flags(fi)), ptr);

		if (fmask & 0x02) {
			std::string name = fs.file_path(fi);
			if (name.size() > 65535) name.resize(65535);
			write_uint16(name.size(), ptr);
			std::copy(name.begin(), name.end(), ptr);
		}

		if (fmask & 0x04) write_uint64(fs.file_size(fi), ptr);

		if (fmask & 0x08) write_uint64(fh.progress(i), ptr);

		if (fmask & 0x10) write_uint8(static_cast<std::uint8_t>(fh.priority(i)), ptr);

		if (fmask & 0x20) write_uint8(fh.open_mode(i), ptr);
	}
	l.unlock();

	return st->send_packet(response.data(), response.size());
}

bool libtorrent_webui::get_peers_updates(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_list()) return error(st, f, permission_denied);

	char const* iptr = f.data;
	if (f.len != 32) return error(st, f, invalid_number_of_args);
	lt::sha1_hash const ih(iptr);
	iptr += 20;
	frame_t const frame = read_uint32(iptr);
	(void)frame;
	std::uint64_t const field_mask = read_uint64(iptr);

	lt::torrent_handle h = m_hist.get_torrent_status(ih).handle;
	if (!h.is_valid()) return error(st, f, invalid_argument);

	std::vector<lt::peer_info> peers;
	// TODO: get_peer_info() is a synchronous call. use the async. call
	h.get_peer_info(peers);

	// filter connections that haven't been established yet
	// TODO: use remove_if() when we update to C++20
	auto new_end = std::remove_if(peers.begin(), peers.end(), [](lt::peer_info& pi) {
		return (pi.flags & lt::peer_info::connecting) || (pi.flags & lt::peer_info::handshake);
	});
	peers.erase(new_end, peers.end());

	std::vector<char> response;
	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	// frame-number
	// TODO: implement delta tracking
	write_uint32(0, ptr);

	// num-updates
	write_uint32(static_cast<std::uint32_t>(peers.size()), ptr);

	// num-removed: 0xffffffff means "all peers not in this update disconnected"
	write_uint32(0xffffffff, ptr);

	for (lt::peer_info const& pi : peers) {
		write_uint32(peer_identifier(pi), ptr);

		// build bitmask of fields we will include
		std::uint64_t bitmask = field_mask & 0xfffff; // 20 defined fields (bits 0-19)
		write_uint64(bitmask, ptr);

		// field 0: flags - peer_flags_t
		if (bitmask & (1ULL << 0)) write_uint32(static_cast<std::uint32_t>(pi.flags), ptr);

		// field 1: source
		if (bitmask & (1ULL << 1)) write_uint8(static_cast<std::uint8_t>(pi.source), ptr);

		// field 2: read-state (raw bitmask: bw_idle=0x01, bw_limit=0x02, bw_network=0x04, bw_disk=0x10)
		if (bitmask & (1ULL << 2)) write_uint8(static_cast<std::uint8_t>(pi.read_state), ptr);

		// field 3: write-state (same encoding as read-state)
		if (bitmask & (1ULL << 3)) write_uint8(static_cast<std::uint8_t>(pi.write_state), ptr);

		// field 4: client (length-prefixed string)
		if (bitmask & (1ULL << 4)) {
			std::string const& client = pi.client;
			std::size_t const len = std::min(client.size(), std::size_t(255));
			write_uint8(static_cast<std::uint8_t>(len), ptr);
			std::copy(client.begin(), client.begin() + len, ptr);
		}

		// field 5: num-pieces
		if (bitmask & (1ULL << 5)) write_uint32(static_cast<std::uint32_t>(pi.num_pieces), ptr);

		// field 6: pending-disk-bytes
		if (bitmask & (1ULL << 6))
			write_uint32(static_cast<std::uint32_t>(pi.pending_disk_bytes), ptr);

		// field 7: pending-disk-read-bytes
		if (bitmask & (1ULL << 7))
			write_uint32(static_cast<std::uint32_t>(pi.pending_disk_read_bytes), ptr);

		// field 8: hashfails
		if (bitmask & (1ULL << 8)) write_uint32(static_cast<std::uint32_t>(pi.num_hashfails), ptr);

		// field 9: down-rate (payload)
		if (bitmask & (1ULL << 9))
			write_uint32(static_cast<std::uint32_t>(pi.payload_down_speed), ptr);

		// field 10: up-rate (payload)
		if (bitmask & (1ULL << 10))
			write_uint32(static_cast<std::uint32_t>(pi.payload_up_speed), ptr);

		// field 11: peer-id (20 bytes)
		if (bitmask & (1ULL << 11)) std::copy(pi.pid.begin(), pi.pid.end(), ptr);

		// field 12: download-queue length
		if (bitmask & (1ULL << 12))
			write_uint32(static_cast<std::uint32_t>(pi.download_queue_length), ptr);

		// field 13: upload-queue length
		if (bitmask & (1ULL << 13))
			write_uint32(static_cast<std::uint32_t>(pi.upload_queue_length), ptr);

		// field 14: timed-out-reqs
		if (bitmask & (1ULL << 14))
			write_uint32(static_cast<std::uint32_t>(pi.timed_out_requests), ptr);

		// field 15: progress [0, 1000000]
		if (bitmask & (1ULL << 15)) write_uint32(static_cast<std::uint32_t>(pi.progress_ppm), ptr);

		// field 16: endpoints
		if (bitmask & (1ULL << 16)) {
#if TORRENT_USE_I2P
			if (pi.flags & lt::peer_info::i2p_socket) {
				write_uint8(2, ptr); // I2P
				lt::sha256_hash const dest = pi.i2p_destination();
				std::copy(dest.begin(), dest.end(), ptr);
			} else
#endif
			{
				auto const remote = pi.remote_endpoint();
				auto const local = pi.local_endpoint();
				bool const is_v6 = remote.address().is_v6();
				write_uint8(is_v6 ? 1 : 0, ptr);

				auto write_endpoint = [&](boost::asio::ip::tcp::endpoint const& ep) {
					if (is_v6) {
						auto const bytes = ep.address().to_v6().to_bytes();
						std::copy(bytes.begin(), bytes.end(), ptr);
					} else {
						auto const bytes = ep.address().to_v4().to_bytes();
						std::copy(bytes.begin(), bytes.end(), ptr);
					}
					write_uint16(static_cast<std::uint16_t>(ep.port()), ptr);
				};
				write_endpoint(local);
				write_endpoint(remote);
			}
		}

		// field 17: pieces bitfield
		if (bitmask & (1ULL << 17)) {
			lt::typed_bitfield<lt::piece_index_t> const& pieces = pi.pieces;
			std::uint32_t const num_bytes = static_cast<std::uint32_t>((pieces.size() + 7) / 8);
			write_uint32(num_bytes, ptr);
			for (std::uint32_t byte_idx = 0; byte_idx < num_bytes; ++byte_idx) {
				std::uint8_t byte = 0;
				for (int bit = 0; bit < 8; ++bit) {
					int piece = static_cast<int>(byte_idx * 8 + bit);
					if (piece < pieces.size() && pieces.get_bit(lt::piece_index_t{piece}))
						byte |= static_cast<std::uint8_t>(0x80 >> bit);
				}
				write_uint8(byte, ptr);
			}
		}

		// field 18: total-download
		if (bitmask & (1ULL << 18))
			write_uint64(static_cast<std::uint64_t>(pi.total_download), ptr);

		// field 19: total-upload
		if (bitmask & (1ULL << 19)) write_uint64(static_cast<std::uint64_t>(pi.total_upload), ptr);
	}

	return st->send_packet(response.data(), response.size());
}

bool libtorrent_webui::get_piece_updates(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_list()) return error(st, f, permission_denied);

	char const* iptr = f.data;
	if (f.len != 24) return error(st, f, invalid_number_of_args);
	lt::sha1_hash const ih(iptr);
	iptr += 20;
	frame_t const client_frame = read_uint32(iptr);

	lt::torrent_handle h = m_hist.get_torrent_status(ih).handle;
	if (!h.is_valid()) return error(st, f, invalid_argument);

	// Find or create the piece_history for this info-hash in the LRU cache.
	// get_download_queue() returns block_info pointers into libtorrent's shared
	// internal storage; concurrent calls are not safe. Hold the mutex across
	// the call and all access to its result, releasing before send_packet.
	std::vector<char> response;
	std::unique_lock<std::mutex> l(m_piece_mutex);
	auto pieces = h.get_download_queue();
	auto it = std::find_if(
		m_piece_histories.begin(),
		m_piece_histories.end(),
		[&](piece_history const& ph) { return ph.info_hash() == ih; }
	);
	if (it != m_piece_histories.end())
		m_piece_histories.splice(m_piece_histories.begin(), m_piece_histories, it);
	else {
		m_piece_histories.emplace_front(ih);
		if (m_piece_histories.size() > 10) m_piece_histories.pop_back();
	}
	frame_t const new_frame = m_piece_histories.front().update(pieces);

	auto const r = m_piece_histories.front().query(client_frame);

	// cap all counts to 16-bit
	std::uint16_t const n_full =
		static_cast<std::uint16_t>(std::min(r.full_pieces.size(), std::size_t(0xffffu)));
	std::uint16_t const n_updates =
		static_cast<std::uint16_t>(std::min(r.block_updates.size(), std::size_t(0xffffu)));
	std::uint16_t const n_removed =
		static_cast<std::uint16_t>(std::min(r.removed.size(), std::size_t(0xffffu)));

	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);
	write_uint32(new_frame, ptr);
	write_uint16(n_full, ptr);
	write_uint16(n_updates, ptr);
	write_uint16(r.is_snapshot ? std::uint16_t(0xffffu) : n_removed, ptr);

	for (std::uint16_t i = 0; i < n_full; ++i) {
		auto const* e = r.full_pieces[i];
		write_uint32(static_cast<int>(e->piece_index), ptr);
		write_uint16(static_cast<std::uint16_t>(e->blocks.size()), ptr);
		for (auto const& b : e->blocks)
			write_uint8(b.state, ptr);
	}
	for (std::uint16_t i = 0; i < n_updates; ++i) {
		auto const& bu = r.block_updates[i];
		write_uint32(static_cast<int>(bu.piece_index), ptr);
		write_uint16(static_cast<std::uint16_t>(bu.block_index), ptr);
		write_uint8(bu.state, ptr);
	}
	if (!r.is_snapshot) {
		for (std::uint16_t i = 0; i < n_removed; ++i)
			write_uint32(static_cast<int>(r.removed[i]), ptr);
	}
	l.unlock();

	return st->send_packet(response.data(), response.size());
}

bool libtorrent_webui::set_file_priority(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_set_file_prio()) return error(st, f, permission_denied);

	char const* iptr = f.data;

	// minimum: 20-byte info-hash + 4-byte num-updates
	if (f.len < 24) return error(st, f, invalid_number_of_args);

	lt::sha1_hash const ih(iptr);
	iptr += 20;
	std::uint32_t const num_updates = read_uint32(iptr);

	if (num_updates > 0xffffff) return error(st, f, invalid_number_of_args);

	// each update is 5 bytes: uint32_t file-index + uint8_t priority
	if (f.len != 24 + int(num_updates) * 5) return error(st, f, invalid_number_of_args);

	lt::torrent_handle h = m_hist.get_torrent_status(ih).handle;
	if (!h.is_valid()) return error(st, f, invalid_argument);

	for (std::uint32_t i = 0; i < num_updates; ++i) {
		// libtorrent will ignore invalid file indices, and clamp the
		// priority to a valid value
		lt::file_index_t const file_idx(read_int32(iptr));
		lt::download_priority_t const prio(read_uint8(iptr));
		h.file_priority(file_idx, prio);
	}

	return error(st, f, no_error);
}

bool libtorrent_webui::get_tracker_updates(websocket_conn* st, function_call f)
{
	if (!st->perms()->allow_list()) return error(st, f, permission_denied);

	char const* iptr = f.data;
	if (f.len != 24) return error(st, f, invalid_number_of_args);
	lt::sha1_hash const ih(iptr);
	iptr += 20;
	/* client_frame = */ read_uint32(iptr); // delta tracking not yet implemented

	lt::torrent_handle h = m_hist.get_torrent_status(ih).handle;
	if (!h.is_valid()) return error(st, f, invalid_argument);

	std::vector<lt::announce_entry> const trackers = h.trackers();

	std::vector<char> response;
	std::back_insert_iterator<std::vector<char>> ptr(response);

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);

	// frame-number (0: delta tracking not yet implemented)
	write_uint32(0, ptr);

	// timestamp: lt::clock_type seconds since epoch, reference for next-announce values
	auto const now32 = std::chrono::time_point_cast<lt::seconds32>(lt::clock_type::now());
	write_uint32(static_cast<std::uint32_t>(now32.time_since_epoch().count()), ptr);

	// reserve space for num-updates; fill in after iterating
	std::size_t const num_updates_pos = response.size();
	write_uint16(0, ptr);

	// 0xffff = full snapshot, no removed-id list follows
	write_uint16(0xffff, ptr);

	std::uint16_t num_updates = 0;
	std::uint16_t tracker_id = 0;
	for (lt::announce_entry const& entry : trackers) {
		for (lt::announce_endpoint const& ep : entry.endpoints) {
			for (lt::protocol_version const proto : lt::all_versions) {
				lt::announce_infohash const& aih = ep.info_hashes[proto];

				write_uint16(tracker_id++, ptr);
				write_uint16(0x0fff, ptr); // all 12 fields (bits 0-11)

				// field 0: url (uint16_t length + bytes)
				{
					std::size_t const len = std::min(entry.url.size(), std::size_t(65535));
					write_uint16(static_cast<std::uint16_t>(len), ptr);
					std::copy(entry.url.begin(), entry.url.begin() + len, ptr);
				}

				// field 1: tier
				write_uint8(entry.tier, ptr);

				// field 2: source
				write_uint8(entry.source, ptr);

				// field 3: complete (int32_t; -1 = unknown)
				write_uint32(static_cast<std::uint32_t>(aih.scrape_complete), ptr);

				// field 4: incomplete (int32_t; -1 = unknown)
				write_uint32(static_cast<std::uint32_t>(aih.scrape_incomplete), ptr);

				// field 5: downloaded (int32_t; -1 = unknown)
				write_uint32(static_cast<std::uint32_t>(aih.scrape_downloaded), ptr);

				// field 6: next-announce (lt::clock_type seconds since epoch; 0 = not scheduled)
				{
					std::int32_t ts = 0;
					if (aih.next_announce != lt::time_point32::min())
						ts = aih.next_announce.time_since_epoch().count();
					write_uint32(static_cast<std::uint32_t>(ts), ptr);
				}

				// field 7: min-announce (lt::clock_type seconds since epoch; 0 = not set)
				{
					std::int32_t ts = 0;
					if (aih.min_announce != lt::time_point32::min())
						ts = aih.min_announce.time_since_epoch().count();
					write_uint32(static_cast<std::uint32_t>(ts), ptr);
				}

				// field 8: last-error (uint8_t length + bytes, max 255)
				{
					std::string const msg =
						aih.last_error ? aih.last_error.message() : std::string{};
					std::size_t const len = std::min(msg.size(), std::size_t(255));
					write_uint8(static_cast<std::uint8_t>(len), ptr);
					std::copy(msg.begin(), msg.begin() + len, ptr);
				}

				// field 9: message (uint8_t length + bytes, max 255)
				{
					std::size_t const len = std::min(aih.message.size(), std::size_t(255));
					write_uint8(static_cast<std::uint8_t>(len), ptr);
					std::copy(aih.message.begin(), aih.message.begin() + len, ptr);
				}

				// field 10: flags
				// 0x01=updating, 0x02=complete-sent, 0x04=verified, 0x08=enabled, 0x10=v2
				{
					std::uint8_t flags = 0;
					if (aih.updating) flags |= 0x01;
					if (aih.complete_sent) flags |= 0x02;
					if (entry.verified) flags |= 0x04;
					if (ep.enabled) flags |= 0x08;
					if (proto == lt::protocol_version::V2) flags |= 0x10;
					write_uint8(flags, ptr);
				}

				// field 11: local-endpoint (uint8_t type + addr bytes + uint16_t port)
				// type byte: 0 = IPv4 (4 addr bytes), 1 = IPv6 (16 addr bytes)
				{
					auto const& addr = ep.local_endpoint.address();
					if (addr.is_v6()) {
						write_uint8(1, ptr);
						auto const bytes = addr.to_v6().to_bytes();
						std::copy(bytes.begin(), bytes.end(), ptr);
					} else {
						write_uint8(0, ptr);
						auto const bytes = addr.to_v4().to_bytes();
						std::copy(bytes.begin(), bytes.end(), ptr);
					}
					write_uint16(static_cast<std::uint16_t>(ep.local_endpoint.port()), ptr);
				}

				++num_updates;
			}
		}
	}

	char* patch = response.data() + num_updates_pos;
	write_uint16(num_updates, patch);

	return st->send_packet(response.data(), response.size());
}

bool libtorrent_webui::add_torrent(websocket_conn* st, function_call f)
{
	char const* iptr = f.data;
	int len = f.len;

	if (!st->perms()->allow_add()) return error(st, f, permission_denied);

	// 2 bytes length-prefix
	// magnet:?xt=urn:btih:<40 bytes info-hash>
	if (len < 62) return error(st, f, truncated_message);

	int magnet_len = read_uint16(iptr);
	len -= 2;
	if (len < magnet_len) return error(st, f, truncated_message);

	lt::string_view magnet_link(iptr, magnet_len);

	lt::add_torrent_params atp;

	lt::error_code ec;
	lt::parse_magnet_uri(magnet_link, atp, ec);
	if (ec) return error(st, f, parse_error);

	atp.save_path = m_settings.get_str("save_path", "./downloads");
	if (m_settings.get_int("start_paused", 0))
		atp.flags = (atp.flags & ~lt::torrent_flags::auto_managed) | lt::torrent_flags::paused;
	else
		atp.flags = (atp.flags & ~lt::torrent_flags::paused) | lt::torrent_flags::auto_managed;
	atp.flags |= lt::torrent_flags::duplicate_is_error;

	atp.userdata =
		new add_torrent_user_data{st->shared_from_this(), f.function_id, f.transaction_id};
	m_ses.async_add_torrent(atp);
	return true;
}

char const* fun_name(int const function_id)
{
	if (function_id < 0 || function_id >= int(functions.size())) {
		return "unknown function";
	}

	return functions[function_id].name;
}

bool libtorrent_webui::on_websocket_read(websocket_conn* st, lt::span<char const> data)
{
	// parse RPC message

	// RPC call is always at least 3 bytes.
	if (data.size() < 3) {
		fprintf(
			stderr, "ERROR: received packet that's smaller than 3 bytes (%d)\n", int(data.size())
		);
		return false;
	}

	function_call f;
	f.data = data.data();
	f.function_id = read_uint8(f.data);
	f.transaction_id = read_uint16(f.data);

	if (f.function_id & 0x80) {
		// RPC responses is at least 4 bytes
		if (data.size() < 4) {
			fprintf(
				stderr,
				"ERROR: received RPC response that's smaller than 4 bytes (%d)\n",
				int(data.size())
			);
			return false;
		}
		int status = read_uint8(f.data);
		// this is a response to a function call
		fprintf(stderr, "RETURNED: %s (status: %d)\n", fun_name(f.function_id & 0x7f), status);
	} else {
		f.len = data.data() + data.size() - f.data;

		fprintf(stderr, "CALL: %s (%d bytes arguments)\n", fun_name(f.function_id), f.len);
		if (f.function_id >= 0 && f.function_id < int(functions.size())) {
			return (this->*functions[f.function_id].handler)(st, f);
		} else {
			fprintf(stderr, " ID: %d\n", f.function_id);
			return error(st, f, no_such_function);
		}
	}
	return true;
}

bool libtorrent_webui::respond(websocket_conn* st, function_call f, int error, int val)
{
	char rpc[6];
	char* ptr = rpc;

	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(no_error, ptr);
	write_uint16(val, ptr);

	return st->send_packet(rpc, sizeof(rpc));
}

bool libtorrent_webui::error(websocket_conn* st, function_call f, int error)
{
	char rpc[4];
	char* ptr = &rpc[0];
	write_uint8(f.function_id | 0x80, ptr);
	write_uint16(f.transaction_id, ptr);
	write_uint8(error, ptr);

	return st->send_packet(rpc, sizeof(rpc));
}
} // namespace ltweb
