/*

Copyright (c) 2013-2014, 2017-2020, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_LIBTORRENT_WEBUI_HPP
#define LTWEB_LIBTORRENT_WEBUI_HPP

#include "torrent_history.hpp" // for frame_t
#include "piece_history.hpp"
#include "file_history.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/fwd.hpp"
#include "alert_observer.hpp"
#include "webui.hpp"

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/fwd.hpp"

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/http.hpp>

namespace ltweb
{
	struct permissions_interface;
	struct torrent_history;
	struct auth_interface;
	struct alert_handler;
	struct websocket_conn;
	struct save_settings_interface;

	struct function_call;

	struct libtorrent_webui : http_handler, alert_observer
	{
		libtorrent_webui(lt::session& ses, torrent_history const& hist
			, auth_interface const& auth, alert_handler& alerts
			, save_settings_interface& sett);
		~libtorrent_webui();

		// internal
		bool get_torrent_updates(websocket_conn* st, function_call f);
		bool start(websocket_conn* st, function_call f);
		bool stop(websocket_conn* st, function_call f);
		bool set_auto_managed(websocket_conn* st, function_call f);
		bool clear_auto_managed(websocket_conn* st, function_call f);
		bool queue_up(websocket_conn* st, function_call f);
		bool queue_down(websocket_conn* st, function_call f);
		bool queue_top(websocket_conn* st, function_call f);
		bool queue_bottom(websocket_conn* st, function_call f);
		bool remove(websocket_conn* st, function_call f);
		bool remove_and_data(websocket_conn* st, function_call f);
		bool force_recheck(websocket_conn* st, function_call f);
		bool set_sequential_download(websocket_conn* st, function_call f);
		bool clear_sequential_download(websocket_conn* st, function_call f);
		bool list_settings(websocket_conn* st, function_call f);
		bool set_settings(websocket_conn* st, function_call f);
		bool get_settings(websocket_conn* st, function_call f);
		bool list_stats(websocket_conn* st, function_call f);
		bool get_stats(websocket_conn* st, function_call f);
		bool get_file_updates(websocket_conn* st, function_call f);
		bool add_torrent(websocket_conn* st, function_call f);
		bool get_peers_updates(websocket_conn* st, function_call f);
		bool get_piece_updates(websocket_conn* st, function_call f);
		bool set_file_priority(websocket_conn* st, function_call f);

		bool on_websocket_read(websocket_conn* st, lt::span<char const> data);

		void shutdown() override;

	private:

		std::string path_prefix() const override;

		void handle_http(http::request<http::string_body> request
			, beast::ssl_stream<beast::tcp_stream>& socket
			, std::function<void(bool)> done) override;

		void handle_alert(lt::alert const* a) override;

		bool respond(websocket_conn* st, function_call f, int error, int val);

		// respond with an error to an RPC
		bool error(websocket_conn* st, function_call f, int error);

		// parse the arguments to the simple torrent commands
		template <typename Fun>
		bool apply_torrent_fun(websocket_conn* st, function_call f, Fun const& fun);

		lt::session& m_ses;
		// TODO: all of these should be protected by individual mutexes.
		// websockets are serviced from a thread pool
		torrent_history const& m_hist;
		auth_interface const& m_auth;
		alert_handler& m_alert;
		save_settings_interface& m_settings;

		// LRU cache of piece histories, most-recently-used at the front.
		// Capped at 10 entries; the least-recently-used is evicted when full.
		// m_piece_mutex protects both the list structure and the entries in it.
		std::mutex m_piece_mutex;
		std::list<piece_history> m_piece_histories;

		// LRU cache of file histories, same eviction policy.
		// m_file_mutex protects both the list structure and the entries in it.
		std::mutex m_file_mutex;
		std::list<file_history> m_file_histories;

		std::mutex m_conns_mutex;
		std::vector<std::weak_ptr<websocket_conn>> m_connections;

		std::mutex m_stats_mutex;
		// TODO: factor this out into its own class
		// the frame numbers where the stats counters changed
		std::vector<std::pair<std::int64_t, frame_t> > m_stats;
		// the current stats frame (incremented every time) stats
		// are requested
		frame_t m_stats_frame = 0;
	};
}

#endif


