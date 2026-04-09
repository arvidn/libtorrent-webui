/*

Copyright (c) 2013, 2020, Arvid Norberg
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

#include <cstring>
#include <memory>
#include <chrono>
#include <string_view>

#include "libtorrent_webui.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/magnet_uri.hpp"

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

	template <typename It> std::uint8_t read_uint8(It& p) { return static_cast<std::uint8_t>(*p++); }
	template <typename It> std::uint16_t read_uint16(It& p) {
		std::uint16_t const hi = read_uint8(p);
		std::uint16_t const lo = read_uint8(p);
		return std::uint16_t((hi << 8) | lo);
	}
	template <typename It> std::uint32_t read_uint32(It& p) {
		std::uint32_t const b3 = read_uint8(p);
		std::uint32_t const b2 = read_uint8(p);
		std::uint32_t const b1 = read_uint8(p);
		std::uint32_t const b0 = read_uint8(p);
		return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
	}
	template <typename It> std::int32_t read_int32(It& p) { return static_cast<std::int32_t>(read_uint32(p)); }
	template <typename It> std::uint64_t read_uint64(It& p) {
		std::uint64_t const hi = read_uint32(p);
		std::uint64_t const lo = read_uint32(p);
		return (hi << 32) | lo;
	}
	template <typename It> void write_uint8(std::uint8_t v, It& p) { *p++ = static_cast<char>(v); }
	template <typename It> void write_uint16(std::uint16_t v, It& p) {
		write_uint8(std::uint8_t(v >> 8), p);
		write_uint8(std::uint8_t(v), p);
	}
	template <typename It> void write_uint32(std::uint32_t v, It& p) {
		write_uint8(std::uint8_t(v >> 24), p);
		write_uint8(std::uint8_t(v >> 16), p);
		write_uint8(std::uint8_t(v >>  8), p);
		write_uint8(std::uint8_t(v), p);
	}
	template <typename It> void write_uint64(std::uint64_t v, It& p) {
		write_uint32(std::uint32_t(v >> 32), p);
		write_uint32(std::uint32_t(v), p);
	}

	struct rpc_entry
	{
		char const* name;
		bool (libtorrent_webui::*handler)(websocket_conn*, function_call);
	};

	static std::array<rpc_entry, 21> const functions =
	{{
		{ "get-torrent-updates", &libtorrent_webui::get_torrent_updates },
		{ "start", &libtorrent_webui::start },
		{ "stop", &libtorrent_webui::stop },
		{ "set-auto-managed", &libtorrent_webui::set_auto_managed },
		{ "clear-auto-managed", &libtorrent_webui::clear_auto_managed },
		{ "queue-up", &libtorrent_webui::queue_up },
		{ "queue-down", &libtorrent_webui::queue_down },
		{ "queue-top", &libtorrent_webui::queue_top },
		{ "queue-bottom", &libtorrent_webui::queue_bottom },
		{ "remove", &libtorrent_webui::remove },
		{ "remove_and_data", &libtorrent_webui::remove_and_data },
		{ "force_recheck", &libtorrent_webui::force_recheck },
		{ "set-sequential-download", &libtorrent_webui::set_sequential_download },
		{ "clear-sequential-download", &libtorrent_webui::clear_sequential_download },
		{ "list-settings", &libtorrent_webui::list_settings },
		{ "get-settings", &libtorrent_webui::get_settings },
		{ "set-settings", &libtorrent_webui::set_settings },
		{ "list-stats", &libtorrent_webui::list_stats },
		{ "get-stats", &libtorrent_webui::get_stats },
		{ "get-file-updates", &libtorrent_webui::get_file_updates },
		{ "add-torrent", &libtorrent_webui::add_torrent },
	}};

	// maps torrent field to RPC field. These fields are the ones defined in
	// torrent_history_entry
	std::array<int const, torrent_history_entry::num_fields> const torrent_field_map =
	{{
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

	struct add_torrent_user_data
	{
		std::shared_ptr<websocket_conn> st;
		int function_id;
		std::uint16_t transaction_id;
	};

	enum error_t
	{
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

	struct function_call
	{
		int function_id;
		std::uint16_t transaction_id;

		// TODO: this should probably be a span
		char const* data;
		int len;
	};

	libtorrent_webui::libtorrent_webui(lt::session& ses, torrent_history const* hist
		, auth_interface const* auth, alert_handler* alert
		, save_settings_interface* sett)
		: m_ses(ses)
		, m_hist(hist)
		, m_auth(auth)
		, m_alert(alert)
		, m_settings(sett)
	{

		if (m_stats.size() < lt::counters::num_counters)
			m_stats.resize(lt::counters::num_counters
				, std::pair<std::int64_t, frame_t>(0, 0));

		m_alert->subscribe(this, 0
			, lt::session_stats_alert::alert_type
			, lt::add_torrent_alert::alert_type
			, 0);
	}

	libtorrent_webui::~libtorrent_webui()
	{
		m_alert->unsubscribe(this);
	}

	std::string libtorrent_webui::path_prefix() const
	{
		return "/bt/control";
	}

	void libtorrent_webui::handle_http(http::request<http::string_body> request
		, beast::ssl_stream<beast::tcp_stream>& socket
		, std::function<void(bool)> done)
	{
		// authenticate
		permissions_interface const* perms = parse_http_auth(request, m_auth);
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
		st->start_accept(request);
	}

	// this is one of the key functions in the interface. It goes to
	// some length to ensure we only send relevant information back,
	// and in a compact format
	bool libtorrent_webui::get_torrent_updates(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_list())
			return error(st, f, permission_denied);

		if (f.len < 12) return error(st, f, truncated_message);

		frame_t const frame = read_uint32(f.data);
		std::uint64_t user_mask = read_uint64(f.data);
		f.len -= 12;

		std::vector<torrent_history_entry> torrents;
		m_hist->updated_fields_since(frame, torrents);

		std::vector<lt::info_hash_t> const removed_torrents = m_hist->removed_since(frame);

		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		write_uint8(f.function_id | 0x80, ptr);
		write_uint16(f.transaction_id, ptr);
		write_uint8(no_error, ptr);

		// frame number (uint32)
		write_uint32(m_hist->frame(), ptr);

		// allocate space for torrent count
		// this will be filled in later when we know
		int num_torrents = 0;
		int const num_torrents_pos = response.size();
		write_uint32(num_torrents, ptr);

		write_uint32(removed_torrents.size(), ptr);

		for (std::vector<torrent_history_entry>::iterator i = torrents.begin()
			, end(torrents.end()); i != end; ++i)
		{
			std::uint64_t bitmask = 0;

			// look at which fields actually have a newer frame number
			// than the caller. Don't return fields that haven't changed.
			for (int k = 0; k < torrent_history_entry::num_fields; ++k)
			{
				int f = torrent_field_map[k];
				if (f < 0) continue;
				if (i->frame[k] <= frame) continue;

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

			for (int f = 0; f < 23; ++f)
			{
				if ((bitmask & (1 << f)) == 0) continue;

				// write field f to buffer
				switch (f)
				{
					case 0: // flags
					{
						std::uint64_t const flags =
							((s.flags & lt::torrent_flags::paused) ? 0x001 : 0)
							| ((s.flags & lt::torrent_flags::auto_managed) ? 0x002 : 0)
							| ((s.flags & lt::torrent_flags::sequential_download) ? 0x004 : 0)
							| (s.is_seeding ? 0x008 : 0)
							| (s.is_finished ? 0x010 : 0)
							// 0x20 is unused
							| (s.has_metadata ? 0x040 : 0)
							| (s.has_incoming ? 0x080 : 0)
							| ((s.flags & lt::torrent_flags::seed_mode) ? 0x100 : 0)
							| ((s.flags & lt::torrent_flags::upload_mode) ? 0x200 : 0)
							| ((s.flags & lt::torrent_flags::share_mode) ? 0x400 : 0)
							| ((s.flags & lt::torrent_flags::super_seeding) ? 0x800 : 0)
							| (s.moving_storage  ? 0x1000 : 0)
							| (s.announcing_to_trackers ? 0x2000 : 0)
							| (s.announcing_to_lsd ? 0x4000 : 0)
							| (s.announcing_to_dht ? 0x8000 : 0)
							| (s.has_metadata ? 0x10000 : 0)
							;

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
						write_uint32(s.all_time_download, ptr);
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
						switch (s.state)
						{
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
		for (auto const& i : removed_torrents)
		{
			auto const ih = i.get_best();
			std::copy(ih.begin(), ih.end(), ptr);
		}

		return st->send_packet(response.data(), response.size());
	}

	template <typename Fun>
	bool libtorrent_webui::apply_torrent_fun(websocket_conn* st, function_call f, Fun const& fun)
	{
		char const* ptr = f.data;
		int num_torrents = read_uint16(ptr);

		// there are only supposed to be one ore more info-hashes as arguments. Each info-hash is
		// in its binary representation, and hence 20 bytes long.
		if ((f.len < num_torrents * 20))
			return error(st, f, invalid_argument_type);

		int counter = 0;
		for (int i = 0; i < num_torrents; ++i)
		{
			// TODO: we should use short, 32 bit, indices for torrents, rather
			// than the full info-hash. This would also simplify support for
			// bittorrent-v2
			lt::sha1_hash const h(ptr + i*20);

			lt::torrent_status ts = m_hist->get_torrent_status(h);
			if (!ts.handle.is_valid()) continue;
			fun(ts.handle);
			++counter;
		}
		return respond(st, f, 0, counter);
	}

	bool libtorrent_webui::start(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_start())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.set_flags(lt::torrent_flags::auto_managed);
			handle.clear_error();
			handle.resume();
		});
	}

	bool libtorrent_webui::stop(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_stop())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.unset_flags(lt::torrent_flags::auto_managed);
			handle.pause();
		});
	}

	bool libtorrent_webui::set_auto_managed(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_stop())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.set_flags(lt::torrent_flags::auto_managed);
		});
	}
	bool libtorrent_webui::clear_auto_managed(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_start())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.unset_flags(lt::torrent_flags::auto_managed);
		});
	}
	bool libtorrent_webui::queue_up(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_queue_change())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.queue_position_up();
		});
	}
	bool libtorrent_webui::queue_down(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_queue_change())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.queue_position_down();
		});
	}
	bool libtorrent_webui::queue_top(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_queue_change())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.queue_position_top();
		});
	}
	bool libtorrent_webui::queue_bottom(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_queue_change())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [](lt::torrent_handle const& handle) {
			handle.queue_position_bottom();
		});
	}
	bool libtorrent_webui::remove(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_remove())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [this](lt::torrent_handle const& handle) {
			m_ses.remove_torrent(handle);
		});
	}
	bool libtorrent_webui::remove_and_data(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_remove()
			|| !st->perms()->allow_remove_data())
			return error(st, f, permission_denied);

		return apply_torrent_fun(st, f, [this](lt::torrent_handle const& handle) {
			m_ses.remove_torrent(handle, lt::session::delete_files);
		});
	}
	bool libtorrent_webui::force_recheck(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_recheck())
			return error(st, f, permission_denied);

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
		std::back_insert_iterator<std::vector<char> > ptr(response);

		write_uint8(f.function_id | 0x80, ptr);
		write_uint16(f.transaction_id, ptr);
		write_uint8(no_error, ptr);

		write_uint32(lt::settings_pack::num_string_settings, ptr);
		write_uint32(lt::settings_pack::num_int_settings, ptr);
		write_uint32(lt::settings_pack::num_bool_settings, ptr);

		for (int i = lt::settings_pack::string_type_base;
			i < lt::settings_pack::max_string_setting_internal; ++i)
		{
			if (!st->perms()->allow_get_settings(i))
				continue;

			char const* n = lt::name_for_setting(i);
			int len = strlen(n);
			TORRENT_ASSERT(len < 256);
			write_uint8(len, ptr);
			std::copy(n, n + len, ptr);
			TORRENT_ASSERT(i < 65536);
			write_uint16(i, ptr);
		}

		for (int i = lt::settings_pack::int_type_base;
			i < lt::settings_pack::max_int_setting_internal; ++i)
		{
			if (!st->perms()->allow_get_settings(i))
				continue;

			char const* n = lt::name_for_setting(i);
			int len = strlen(n);
			TORRENT_ASSERT(len < 256);
			write_uint8(len, ptr);
			std::copy(n, n + len, ptr);
			TORRENT_ASSERT(i < 65536);
			write_uint16(i, ptr);
		}

		for (int i = lt::settings_pack::bool_type_base;
			i < lt::settings_pack::max_bool_setting_internal; ++i)
		{
			if (!st->perms()->allow_get_settings(i))
				continue;

			char const* n = lt::name_for_setting(i);
			int len = strlen(n);
			TORRENT_ASSERT(len < 256);
			write_uint8(len, ptr);
			std::copy(n, n + len, ptr);
			TORRENT_ASSERT(i < 65536);
			write_uint16(i, ptr);
		}
		return st->send_packet(&response[0], response.size());
	}

	bool libtorrent_webui::set_settings(websocket_conn* st, function_call f)
	{
		char const* ptr = f.data;
		if (f.len < 2) return error(st, f, invalid_number_of_args);

		int num_settings = read_uint16(ptr);
		f.len -= 2;

		lt::settings_pack pack;

		for (int i = 0; i < num_settings; ++i)
		{
			if (f.len < 2) return error(st, f, invalid_number_of_args);
			int sett = read_uint16(ptr);
			f.len -= 2;

			if (!st->perms()->allow_set_settings(sett))
				return error(st, f, permission_denied);

			if (sett >= lt::settings_pack::string_type_base && sett < lt::settings_pack::max_string_setting_internal)
			{
				if (f.len < 2) return error(st, f, invalid_number_of_args);
				int len = read_uint16(ptr);
				f.len -= 2;
				std::string str;
				str.resize(len);
				if (f.len < len) return error(st, f, invalid_number_of_args);
				std::copy(ptr, ptr + len, str.begin());
				ptr += len;
				pack.set_str(sett, str);
			}
			else if (sett >= lt::settings_pack::int_type_base && sett < lt::settings_pack::max_int_setting_internal)
			{
				if (f.len < 4) return error(st, f, invalid_number_of_args);
				pack.set_int(sett, read_uint32(ptr));
				f.len -= 4;
			}
			else if (sett >= lt::settings_pack::bool_type_base && sett < lt::settings_pack::max_bool_setting_internal)
			{
				if (f.len < 1) return error(st, f, invalid_number_of_args);
				pack.set_bool(sett, read_uint8(ptr));
				f.len -= 1;
			}
			else
			{
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
		std::back_insert_iterator<std::vector<char> > ptr(response);

		write_uint8(f.function_id | 0x80, ptr);
		write_uint16(f.transaction_id, ptr);
		write_uint8(no_error, ptr);

		write_uint16(num_settings, ptr);

		lt::settings_pack s = m_ses.get_settings();

		for (int i = 0; i < num_settings; ++i)
		{
			int const sett = read_uint16(iptr);

			if (!st->perms()->allow_get_settings(sett))
				return error(st, f, permission_denied);

			if (sett >= lt::settings_pack::string_type_base && sett < lt::settings_pack::max_string_setting_internal)
			{
				std::string const& v = s.get_str(sett);
				write_uint16(v.length(), ptr);
				std::copy(v.begin(), v.end(), ptr);
			}
			else if (sett >= lt::settings_pack::int_type_base && sett < lt::settings_pack::max_int_setting_internal)
			{
				write_uint32(s.get_int(sett), ptr);
			}
			else if (sett >= lt::settings_pack::bool_type_base && sett < lt::settings_pack::max_bool_setting_internal)
			{
				write_uint8(s.get_bool(sett), ptr);
			}
			else
			{
				return error(st, f, invalid_argument);
			}
		}

		return st->send_packet(&response[0], response.size());
	}

	bool libtorrent_webui::list_stats(websocket_conn* st, function_call f)
	{
		if (!st->perms()->allow_session_status())
			return error(st, f, permission_denied);

		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		write_uint8(f.function_id | 0x80, ptr);
		write_uint16(f.transaction_id, ptr);
		write_uint8(no_error, ptr);

		std::vector<lt::stats_metric> stats = lt::session_stats_metrics();
		write_uint16(stats.size(), ptr);

		for (auto const& s : stats)
		{
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
		if (auto* ss = lt::alert_cast<lt::session_stats_alert>(a))
		{
			std::unique_lock<std::mutex> l(m_stats_mutex);

			++m_stats_frame;
			lt::span<std::int64_t const> stats = ss->counters();

			// first update our copy of the stats, and update their frame counters
			for (int i = 0; i < lt::counters::num_counters; ++i)
			{
				if (m_stats[i].first != stats[i])
				{
					m_stats[i].second = m_stats_frame;
					m_stats[i].first = stats[i];
				}
			}

			// TODO: notify handler?
		}
		else if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a))
		{
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
		if (!st->perms()->allow_session_status())
			return error(st, f, permission_denied);

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
		std::back_insert_iterator<std::vector<char> > ptr(response);

		write_uint8(f.function_id | 0x80, ptr);
		write_uint16(f.transaction_id, ptr);
		write_uint8(no_error, ptr);

		std::unique_lock<std::mutex> l(m_stats_mutex);
		write_uint32(m_stats_frame, ptr);

		// we'll fill in the counter later
		int const counter_pos = response.size();
		write_uint16(0, ptr);

		int num_updates = 0;
		for (int i = 0; i < num_stats; ++i)
		{
			int c = read_uint16(iptr);
			if (c < 0 || c > int(m_stats.size()))
				return error(st, f, invalid_argument);

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
		if (!st->perms()->allow_list())
			return error(st, f, permission_denied);

		char const* iptr = f.data;
		if (f.len != 24) return error(st, f, invalid_number_of_args);
		lt::sha1_hash ih(iptr);
		iptr += 20;
		frame_t const frame = read_int32(iptr);
		(void)frame;

		lt::torrent_handle h = m_ses.find_torrent(ih);
		if (!h.is_valid()) return error(st, f, invalid_argument);

		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		write_uint8(f.function_id | 0x80, ptr);
		write_uint16(f.transaction_id, ptr);
		write_uint8(no_error, ptr);

		std::vector<std::int64_t> fp;
		h.file_progress(fp, lt::torrent_handle::piece_granularity);

		std::shared_ptr<const lt::torrent_info> t = h.torrent_file();
		if (!t) return error(st, f, resource_not_found);

		lt::file_storage const& fs = t->layout();

		// just in case
		fp.resize(fs.num_files(), 0);

		// frame number
		write_uint32(0, ptr);

		// number of files
		write_uint32(fs.num_files(), ptr);

		// TODO: we should really just send differences since last time
		// for now, just send full updates
		for (auto const fi : fs.file_range())
		{
			if ((static_cast<int>(fi) % 8) == 0)
			{
				std::uint8_t mask = 0xff;
				if (fs.num_files() - static_cast<int>(fi) < 8)
					mask <<= 8 - fs.num_files() + static_cast<int>(fi);
				write_uint8(mask, ptr);
			}

			// file update bitmask (all 4 fields)
			write_uint16(0xf, ptr);

			// flags
			write_uint8(static_cast<std::uint8_t>(fs.file_flags(fi)), ptr);

			// name
			std::string name = fs.file_path(fi);
			if (name.size() > 65535) name.resize(65535);
			write_uint16(name.size(), ptr);
			std::copy(name.begin(), name.end(), ptr);

			// total-size
			write_uint64(fs.file_size(fi), ptr);

			// total downloaded
			write_uint64(fp[static_cast<int>(fi)], ptr);
		}

		return st->send_packet(&response[0], response.size());
	}

	bool libtorrent_webui::add_torrent(websocket_conn* st, function_call f)
	{
		char const* iptr = f.data;
		int len = f.len;

		if (!st->perms()->allow_add())
			return error(st, f, permission_denied);

		// 2 bytes length-prefix
		// magnet:?xt=urn:btih:<40 bytes info-hash>
		if (len < 62) return error(st, f, truncated_message);

		int magnet_len = read_uint16(iptr);
		len -= 2;
		if (len < magnet_len)
			return error(st, f, truncated_message);

		lt::string_view magnet_link(iptr, magnet_len);

		lt::add_torrent_params atp;

		lt::error_code ec;
		lt::parse_magnet_uri(magnet_link, atp, ec);
		if (ec) return error(st, f, parse_error);

		atp.save_path = m_settings ? m_settings->get_str("save_path", "./downloads") : "./downloads";
		if (m_settings && m_settings->get_int("start_paused", 0))
			atp.flags = (atp.flags & ~lt::torrent_flags::auto_managed) | lt::torrent_flags::paused;
		else
			atp.flags = (atp.flags & ~lt::torrent_flags::paused) | lt::torrent_flags::auto_managed;
		atp.flags |= lt::torrent_flags::duplicate_is_error;

		atp.userdata = new add_torrent_user_data{st->shared_from_this()
			, f.function_id
			, f.transaction_id};
		m_ses.async_add_torrent(atp);
		return true;
	}

	char const* fun_name(int const function_id)
	{
		if (function_id < 0 || function_id >= int(functions.size()))
		{
			return "unknown function";
		}

		return functions[function_id].name;
	}

	bool libtorrent_webui::on_websocket_read(websocket_conn* st, lt::span<char const> data)
	{
		// parse RPC message

		// RPC call is always at least 3 bytes.
		if (data.size() < 3)
		{
			fprintf(stderr, "ERROR: received packet that's smaller than 3 bytes (%d)\n"
				, int(data.size()));
			return false;
		}

		function_call f;
		f.data = data.data();
		f.function_id = read_uint8(f.data);
		f.transaction_id = read_uint16(f.data);

		if (f.function_id & 0x80)
		{
			// RPC responses is at least 4 bytes
			if (data.size() < 4)
			{
				fprintf(stderr, "ERROR: received RPC response that's smaller than 4 bytes (%d)\n"
					, int(data.size()));
				return false;
			}
			int status = read_uint8(f.data);
			// this is a response to a function call
			fprintf(stderr, "RETURNED: %s (status: %d)\n", fun_name(f.function_id & 0x7f), status);
		}
		else
		{
			f.len = data.data() + data.size() - f.data;

			fprintf(stderr, "CALL: %s (%d bytes arguments)\n", fun_name(f.function_id), f.len);
			if (f.function_id >= 0 && f.function_id < int(functions.size()))
			{
				return (this->*functions[f.function_id].handler)(st, f);
			}
			else
			{
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
}

