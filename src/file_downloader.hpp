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
#include "webui.hpp"
#include <mutex>
#include <set>

#include "libtorrent/fwd.hpp"

namespace libtorrent
{
	struct auth_interface;
	struct piece_alert_dispatch;
	struct request_t;

	struct file_downloader : http_handler
	{
		file_downloader(session& s, auth_interface const* auth = nullptr);

		virtual bool handle_http(mg_connection* conn,
			mg_request_info const* request_info);

		void set_disposition(bool attachment) { m_attachment = attachment; }
		void debug_print_requests() const;

	private:

		session& m_ses;
		auth_interface const* m_auth;

		std::shared_ptr<piece_alert_dispatch> m_dispatch;

		int m_queue_size;

		// controls the content disposition of files. Defaults to true
		// which asks the browser to save the file rather than to render it.
		bool m_attachment;

		mutable std::mutex m_mutex;
		std::set<request_t*> m_requests;
	};
}

#endif

