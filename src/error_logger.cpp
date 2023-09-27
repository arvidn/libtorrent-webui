/*

Copyright (c) 2013, Arvid Norberg
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

#include "error_logger.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/error_code.hpp"
#include <boost/asio/ssl.hpp>

#include "alert_handler.hpp"

#include <string>
#include <errno.h>
#include <stdlib.h>

namespace libtorrent
{
	error_logger::error_logger(alert_handler* alerts, std::string const& log_file, bool redirect_stderr)
		: m_file(NULL)
		, m_alerts(alerts)
	{
		if (!log_file.empty())
		{
			m_file = fopen(log_file.c_str(), "a");
			if (m_file == NULL)
			{
				fprintf(stderr, "failed to open error log \"%s\": (%d) %s\n"
					, log_file.c_str(), errno, strerror(errno));
			}
			else if (redirect_stderr)
			{
				dup2(fileno(m_file), STDOUT_FILENO);
				dup2(fileno(m_file), STDERR_FILENO);
			}
			m_alerts->subscribe(this, 0
				, peer_disconnected_alert::alert_type
				, peer_error_alert::alert_type
				, save_resume_data_failed_alert::alert_type
				, torrent_delete_failed_alert::alert_type
				, storage_moved_failed_alert::alert_type
				, file_rename_failed_alert::alert_type
				, torrent_error_alert::alert_type
				, hash_failed_alert::alert_type
				, file_error_alert::alert_type
				, metadata_failed_alert::alert_type
				, udp_error_alert::alert_type
				, listen_failed_alert::alert_type
				, invalid_request_alert::alert_type
				, 0);
		}
	}

	error_logger::~error_logger()
	{
		m_alerts->unsubscribe(this);
		if (m_file) fclose(m_file);
	}

	void error_logger::handle_alert(alert const* a)
	{
		if (m_file == NULL) return;
		time_t now = time(NULL);
		std::array<char, 256> timestamp;
		strncpy(timestamp.data(), ctime(&now), timestamp.size());
		for (char& c : timestamp)
		{
			if (c != '\n' && c != '\r') continue;
			c = '\0';
			break;
		}

		switch (a->type())
		{
			case peer_error_alert::alert_type:
			{
				peer_error_alert const* pe = alert_cast<peer_error_alert>(a);
#ifdef TORRENT_USE_OPENSSL
				// unknown protocol
				if (pe->error != error_code(336027900, boost::asio::error::get_ssl_category()))
#endif
				{
					fprintf(m_file, "%s\terror [%s] (%s:%d) %s\n", timestamp.data()
						, print_endpoint(pe->endpoint).c_str(), pe->error.category().name()
						, pe->error.value(), pe->error.message().c_str());
				}
				break;
			}
			case peer_disconnected_alert::alert_type:
			{
				peer_disconnected_alert const* pd = alert_cast<peer_disconnected_alert>(a);
				if (pd
					&& pd->error != boost::system::errc::connection_reset
					&& pd->error != boost::system::errc::connection_aborted
					&& pd->error != boost::system::errc::connection_refused
					&& pd->error != boost::system::errc::timed_out
					&& pd->error != boost::asio::error::eof
					&& pd->error != boost::asio::error::host_unreachable
					&& pd->error != boost::asio::error::network_unreachable
					&& pd->error != boost::asio::error::broken_pipe
#ifdef TORRENT_USE_OPENSSL
					// unknown protocol
					&& pd->error != error_code(336027900, boost::asio::error::get_ssl_category())
#endif
					&& pd->error != error_code(libtorrent::errors::self_connection)
					&& pd->error != error_code(libtorrent::errors::torrent_removed)
					&& pd->error != error_code(libtorrent::errors::torrent_paused)
					&& pd->error != error_code(libtorrent::errors::torrent_aborted)
					&& pd->error != error_code(libtorrent::errors::stopping_torrent)
					&& pd->error != error_code(libtorrent::errors::session_closing)
					&& pd->error != error_code(libtorrent::errors::duplicate_peer_id)
					&& pd->error != error_code(libtorrent::errors::uninteresting_upload_peer)
					&& pd->error != error_code(libtorrent::errors::unsupported_encryption_mode)
					&& pd->error != error_code(libtorrent::errors::torrent_finished)
					&& pd->error != error_code(libtorrent::errors::timed_out)
					&& pd->error != error_code(libtorrent::errors::timed_out_inactivity)
					&& pd->error != error_code(libtorrent::errors::timed_out_no_request)
					&& pd->error != error_code(libtorrent::errors::timed_out_no_handshake)
					&& pd->error != error_code(libtorrent::errors::upload_upload_connection))
					fprintf(m_file, "%s\tdisconnect [%s][%s] (%s:%d) %s\n", timestamp.data()
						, print_endpoint(pd->endpoint).c_str(), operation_name(pd->op)
						, pd->error.category().name(), pd->error.value(), pd->error.message().c_str());
				break;
			}
			case save_resume_data_failed_alert::alert_type:
			{
				save_resume_data_failed_alert const* rs= alert_cast<save_resume_data_failed_alert>(a);
				if (rs && rs->error != error_code(libtorrent::errors::resume_data_not_modified))
					fprintf(m_file, "%s\tsave-resume-failed (%s:%d) %s\n", timestamp.data()
						, rs->error.category().name(), rs->error.value()
						, rs->message().c_str());
			}
			case torrent_delete_failed_alert::alert_type:
			{
				torrent_delete_failed_alert const* td = alert_cast<torrent_delete_failed_alert>(a);
				if (td)
					fprintf(m_file, "%s\tstorage-delete-failed (%s:%d) %s\n", timestamp.data()
						, td->error.category().name(), td->error.value()
						, td->message().c_str());
			}
			case storage_moved_failed_alert::alert_type:
			{
				storage_moved_failed_alert const* sm = alert_cast<storage_moved_failed_alert>(a);
				if (sm)
					fprintf(m_file, "%s\tstorage-move-failed (%s:%d) %s\n", timestamp.data()
						, sm->error.category().name(), sm->error.value()
						, sm->message().c_str());
			}
			case file_rename_failed_alert::alert_type:
			{
				file_rename_failed_alert const* rn = alert_cast<file_rename_failed_alert>(a);
				if (rn)
					fprintf(m_file, "%s\tfile-rename-failed (%s:%d) %s\n", timestamp.data()
						, rn->error.category().name(), rn->error.value()
						, rn->message().c_str());
			}
			case torrent_error_alert::alert_type:
			{
				torrent_error_alert const* te = alert_cast<torrent_error_alert>(a);
				if (te)
					fprintf(m_file, "%s\ttorrent-error (%s:%d) %s\n", timestamp.data()
						, te->error.category().name(), te->error.value()
						, te->message().c_str());
			}
			case hash_failed_alert::alert_type:
			{
				hash_failed_alert const* hf = alert_cast<hash_failed_alert>(a);
				if (hf)
					fprintf(m_file, "%s\thash-failed %s\n", timestamp.data()
						, hf->message().c_str());
			}
			case file_error_alert::alert_type:
			{
				file_error_alert const* fe = alert_cast<file_error_alert>(a);
				if (fe)
					fprintf(m_file, "%s\tfile-error (%s:%d) %s\n", timestamp.data()
						, fe->error.category().name(), fe->error.value()
						, fe->message().c_str());
			}
			case metadata_failed_alert::alert_type:
			{
				metadata_failed_alert const* mf = alert_cast<metadata_failed_alert>(a);
				if (mf)
					fprintf(m_file, "%s\tmetadata-error (%s:%d) %s\n", timestamp.data()
						, mf->error.category().name(), mf->error.value()
						, mf->message().c_str());
			}
			case udp_error_alert::alert_type:
			{
				udp_error_alert const* ue = alert_cast<udp_error_alert>(a);
				if (ue)
					fprintf(m_file, "%s\tudp-error (%s:%d) %s %s\n", timestamp.data()
						, ue->error.category().name(), ue->error.value()
						, print_endpoint(ue->endpoint).c_str()
						, ue->error.message().c_str());
			}
			case listen_failed_alert::alert_type:
			{
				listen_failed_alert const* lf = alert_cast<listen_failed_alert>(a);
				if (lf)
					fprintf(m_file, "%s\tlisten-error (%s:%d) %s\n", timestamp.data()
						, lf->error.category().name(), lf->error.value()
						, lf->message().c_str());
			}
			case invalid_request_alert::alert_type:
			{
				invalid_request_alert const* ira = alert_cast<invalid_request_alert>(a);
				if (ira)
					fprintf(m_file, "%s\tinvalid-request %s\n", timestamp.data()
						, ira->message().c_str());
			}
		}
	}
}

