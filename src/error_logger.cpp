/*

Copyright (c) 2013-2014, 2019, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "error_logger.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/error_code.hpp"
#include <boost/asio/ssl.hpp>

#include "alert_handler.hpp"

#include <string>
#include <sstream>
#include <errno.h>
#include <stdlib.h>

namespace {
template <typename Endpoint>
std::string endpoint_str(Endpoint const& ep)
{
	std::ostringstream os;
	os << ep;
	return os.str();
}
} // namespace

namespace ltweb {
error_logger::error_logger(alert_handler* alerts, std::string const& log_file, bool redirect_stderr)
	: m_file(NULL)
	, m_alerts(alerts)
{
	if (!log_file.empty()) {
		m_file = fopen(log_file.c_str(), "a");
		if (m_file == NULL) {
			fprintf(
				stderr,
				"failed to open error log \"%s\": (%d) %s\n",
				log_file.c_str(),
				errno,
				strerror(errno)
			);
		} else if (redirect_stderr) {
			dup2(fileno(m_file), STDOUT_FILENO);
			dup2(fileno(m_file), STDERR_FILENO);
		}
		m_alerts->subscribe(
			this,
			0,
			lt::peer_disconnected_alert::alert_type,
			lt::peer_error_alert::alert_type,
			lt::save_resume_data_failed_alert::alert_type,
			lt::torrent_delete_failed_alert::alert_type,
			lt::storage_moved_failed_alert::alert_type,
			lt::file_rename_failed_alert::alert_type,
			lt::torrent_error_alert::alert_type,
			lt::hash_failed_alert::alert_type,
			lt::file_error_alert::alert_type,
			lt::metadata_failed_alert::alert_type,
			lt::udp_error_alert::alert_type,
			lt::listen_failed_alert::alert_type,
			lt::invalid_request_alert::alert_type,
			0
		);
	}
}

error_logger::~error_logger()
{
	m_alerts->unsubscribe(this);
	if (m_file) fclose(m_file);
}

void error_logger::handle_alert(lt::alert const* a)
{
	if (m_file == NULL) return;
	time_t now = time(NULL);
	std::array<char, 256> timestamp;
	strncpy(timestamp.data(), ctime(&now), timestamp.size());
	for (char& c : timestamp) {
		if (c != '\n' && c != '\r') continue;
		c = '\0';
		break;
	}

	switch (a->type()) {
		case lt::peer_error_alert::alert_type: {
			lt::peer_error_alert const* pe = lt::alert_cast<lt::peer_error_alert>(a);
#ifdef TORRENT_USE_OPENSSL
			// unknown protocol
			if (pe->error != lt::error_code(336027900, boost::asio::error::get_ssl_category()))
#endif
			{
				fprintf(
					m_file,
					"%s\terror [%s] (%s:%d) %s\n",
					timestamp.data(),
					endpoint_str(std::get<0>(pe->ep)).c_str(),
					pe->error.category().name(),
					pe->error.value(),
					pe->error.message().c_str()
				);
			}
			break;
		}
		case lt::peer_disconnected_alert::alert_type: {
			lt::peer_disconnected_alert const* pd = lt::alert_cast<lt::peer_disconnected_alert>(a);
			if (pd && pd->error != boost::system::errc::connection_reset
				&& pd->error != boost::system::errc::connection_aborted
				&& pd->error != boost::system::errc::connection_refused
				&& pd->error != boost::system::errc::timed_out
				&& pd->error != boost::asio::error::eof
				&& pd->error != boost::asio::error::host_unreachable
				&& pd->error != boost::asio::error::network_unreachable
				&& pd->error != boost::asio::error::broken_pipe
#ifdef TORRENT_USE_OPENSSL
				// unknown protocol
				&& pd->error != lt::error_code(336027900, boost::asio::error::get_ssl_category())
#endif
				&& pd->error != lt::error_code(lt::errors::self_connection)
				&& pd->error != lt::error_code(lt::errors::torrent_removed)
				&& pd->error != lt::error_code(lt::errors::torrent_paused)
				&& pd->error != lt::error_code(lt::errors::torrent_aborted)
				&& pd->error != lt::error_code(lt::errors::stopping_torrent)
				&& pd->error != lt::error_code(lt::errors::session_closing)
				&& pd->error != lt::error_code(lt::errors::duplicate_peer_id)
				&& pd->error != lt::error_code(lt::errors::uninteresting_upload_peer)
				&& pd->error != lt::error_code(lt::errors::unsupported_encryption_mode)
				&& pd->error != lt::error_code(lt::errors::torrent_finished)
				&& pd->error != lt::error_code(lt::errors::timed_out)
				&& pd->error != lt::error_code(lt::errors::timed_out_inactivity)
				&& pd->error != lt::error_code(lt::errors::timed_out_no_request)
				&& pd->error != lt::error_code(lt::errors::timed_out_no_handshake)
				&& pd->error != lt::error_code(lt::errors::upload_upload_connection))
				fprintf(
					m_file,
					"%s\tdisconnect [%s][%s] (%s:%d) %s\n",
					timestamp.data(),
					endpoint_str(std::get<0>(pd->ep)).c_str(),
					operation_name(pd->op),
					pd->error.category().name(),
					pd->error.value(),
					pd->error.message().c_str()
				);
			break;
		}
		case lt::save_resume_data_failed_alert::alert_type: {
			lt::save_resume_data_failed_alert const* rs =
				lt::alert_cast<lt::save_resume_data_failed_alert>(a);
			if (rs && rs->error != lt::error_code(lt::errors::resume_data_not_modified))
				fprintf(
					m_file,
					"%s\tsave-resume-failed (%s:%d) %s\n",
					timestamp.data(),
					rs->error.category().name(),
					rs->error.value(),
					rs->message().c_str()
				);
			break;
		}
		case lt::torrent_delete_failed_alert::alert_type: {
			lt::torrent_delete_failed_alert const* td =
				lt::alert_cast<lt::torrent_delete_failed_alert>(a);
			if (td)
				fprintf(
					m_file,
					"%s\tstorage-delete-failed (%s:%d) %s\n",
					timestamp.data(),
					td->error.category().name(),
					td->error.value(),
					td->message().c_str()
				);
			break;
		}
		case lt::storage_moved_failed_alert::alert_type: {
			lt::storage_moved_failed_alert const* sm =
				lt::alert_cast<lt::storage_moved_failed_alert>(a);
			if (sm)
				fprintf(
					m_file,
					"%s\tstorage-move-failed (%s:%d) %s\n",
					timestamp.data(),
					sm->error.category().name(),
					sm->error.value(),
					sm->message().c_str()
				);
			break;
		}
		case lt::file_rename_failed_alert::alert_type: {
			lt::file_rename_failed_alert const* rn =
				lt::alert_cast<lt::file_rename_failed_alert>(a);
			if (rn)
				fprintf(
					m_file,
					"%s\tfile-rename-failed (%s:%d) %s\n",
					timestamp.data(),
					rn->error.category().name(),
					rn->error.value(),
					rn->message().c_str()
				);
			break;
		}
		case lt::torrent_error_alert::alert_type: {
			lt::torrent_error_alert const* te = lt::alert_cast<lt::torrent_error_alert>(a);
			if (te)
				fprintf(
					m_file,
					"%s\ttorrent-error (%s:%d) %s\n",
					timestamp.data(),
					te->error.category().name(),
					te->error.value(),
					te->message().c_str()
				);
			break;
		}
		case lt::hash_failed_alert::alert_type: {
			lt::hash_failed_alert const* hf = lt::alert_cast<lt::hash_failed_alert>(a);
			if (hf)
				fprintf(m_file, "%s\thash-failed %s\n", timestamp.data(), hf->message().c_str());
			break;
		}
		case lt::file_error_alert::alert_type: {
			lt::file_error_alert const* fe = lt::alert_cast<lt::file_error_alert>(a);
			if (fe)
				fprintf(
					m_file,
					"%s\tfile-error (%s:%d) %s\n",
					timestamp.data(),
					fe->error.category().name(),
					fe->error.value(),
					fe->message().c_str()
				);
			break;
		}
		case lt::metadata_failed_alert::alert_type: {
			lt::metadata_failed_alert const* mf = lt::alert_cast<lt::metadata_failed_alert>(a);
			if (mf)
				fprintf(
					m_file,
					"%s\tmetadata-error (%s:%d) %s\n",
					timestamp.data(),
					mf->error.category().name(),
					mf->error.value(),
					mf->message().c_str()
				);
			break;
		}
		case lt::udp_error_alert::alert_type: {
			lt::udp_error_alert const* ue = lt::alert_cast<lt::udp_error_alert>(a);
			if (ue)
				fprintf(
					m_file,
					"%s\tudp-error (%s:%d) %s %s\n",
					timestamp.data(),
					ue->error.category().name(),
					ue->error.value(),
					endpoint_str(ue->endpoint).c_str(),
					ue->error.message().c_str()
				);
			break;
		}
		case lt::listen_failed_alert::alert_type: {
			lt::listen_failed_alert const* lf = lt::alert_cast<lt::listen_failed_alert>(a);
			if (lf)
				fprintf(
					m_file,
					"%s\tlisten-error (%s:%d) %s\n",
					timestamp.data(),
					lf->error.category().name(),
					lf->error.value(),
					lf->message().c_str()
				);
			break;
		}
		case lt::invalid_request_alert::alert_type: {
			lt::invalid_request_alert const* ira = lt::alert_cast<lt::invalid_request_alert>(a);
			if (ira)
				fprintf(
					m_file, "%s\tinvalid-request %s\n", timestamp.data(), ira->message().c_str()
				);
			break;
		}
	}
}
} // namespace ltweb
