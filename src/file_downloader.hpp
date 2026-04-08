/*

Copyright (c) 2012, Arvid Norberg
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

#ifndef TORRENT_FILE_DOWNLOADER_HPP
#define TORRENT_FILE_DOWNLOADER_HPP

#include <memory>
#include <set>

#include "webui.hpp"
#include "alert_observer.hpp"

#include "libtorrent/torrent_handle.hpp"

namespace libtorrent
{
	struct alert_handler;
	struct piece_alert_dispatch;
	struct auth_interface;
	struct piece_alert_dispatch;
	struct request_t;
	class session;
	struct file_request_conn;

	struct file_downloader : http_handler, alert_observer
	{
		file_downloader(session& s, alert_handler* alerts, auth_interface const* auth = nullptr);
		~file_downloader();

		void set_disposition(bool attachment) { m_attachment = attachment; }

	private:

		std::string path_prefix() const override;

		void handle_http(http::request<http::string_body> request
			, beast::ssl_stream<beast::tcp_stream>& socket
			, std::function<void(bool)> done) override;

		void handle_alert(alert const* a) override;

		session& m_ses;
		auth_interface const* m_auth;

		// controls the content disposition of files. Defaults to true
		// which asks the browser to save the file rather than to render it.
		bool m_attachment;

		alert_handler* m_alert;

		std::mutex m_mutex;
		std::multimap<lt::torrent_handle, std::shared_ptr<file_request_conn>> m_outstanding_requests;
	};
}

#endif

