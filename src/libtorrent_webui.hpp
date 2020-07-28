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

#ifndef TORRENT_LIBTORRENT_WEBUI_HPP
#define TORRENT_LIBTORRENT_WEBUI_HPP

#include "torrent_history.hpp" // for frame_t
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/fwd.hpp"
#include "alert_observer.hpp"
#include "webui.hpp"

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/fwd.hpp"

#include <atomic>

#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/http.hpp>

namespace libtorrent
{
	struct permissions_interface;
	struct torrent_history;
	struct auth_interface;
	struct alert_handler;
	struct websocket_conn;

	struct function_call;

	struct libtorrent_webui : http_handler, alert_observer
	{
		libtorrent_webui(lt::session& ses, torrent_history const* hist
			, auth_interface const* auth, alert_handler* alerts);
		~libtorrent_webui();

		std::string path_prefix() override;

		void handle_http(http::request<http::string_body> request
			, beast::ssl_stream<beast::tcp_stream>& socket
			, std::function<void(bool)> done) override;

		void handle_alert(alert const* a) override;

		void set_params_model(add_torrent_params const& p)
		{ m_params_model = p; }

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

		bool on_websocket_read(websocket_conn* st, char const* data, size_t length);

	private:

//		bool call_rpc(websocket_conn* st, int function, char const* data, int len);

		bool respond(websocket_conn* st, function_call f, int error, int val);

		// respond with an error to an RPC
		bool error(websocket_conn* st, function_call f, int error);

		// parse the arguments to the simple torrent commands
		template <typename Fun>
		bool apply_torrent_fun(websocket_conn* st, function_call f, Fun const& fun);

		session& m_ses;
		torrent_history const* m_hist;
		auth_interface const* m_auth;
		alert_handler* m_alert;

		std::mutex m_stats_mutex;
		// TODO: factor this out into its own class
		// the frame numbers where the stats counters changed
		std::vector<std::pair<std::int64_t, frame_t> > m_stats;
		// the current stats frame (incremented every time) stats
		// are requested
		frame_t m_stats_frame = 0;

		add_torrent_params m_params_model;
	};
}

#endif


