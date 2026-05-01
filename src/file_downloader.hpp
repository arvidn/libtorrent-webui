/*

Copyright (c) 2012-2014, 2017, 2020, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_FILE_DOWNLOADER_HPP
#define LTWEB_FILE_DOWNLOADER_HPP

#include <memory>
#include <set>

#include "webui.hpp"
#include "alert_observer.hpp"

#include "libtorrent/torrent_handle.hpp"

namespace ltweb {
struct alert_handler;
struct piece_alert_dispatch;
struct auth_interface;
struct piece_alert_dispatch;
struct file_request_conn;

struct file_downloader
	: http_handler
	, alert_observer {
	file_downloader(lt::session& s, alert_handler* alerts, auth_interface const* auth = nullptr);
	~file_downloader();

	void set_disposition(bool attachment) { m_attachment = attachment; }

private:
	std::string path_prefix() const override;

	void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

	void handle_alert(lt::alert const* a) override;

	lt::session& m_ses;
	auth_interface const* m_auth;

	// controls the content disposition of files. Defaults to true
	// which asks the browser to save the file rather than to render it.
	bool m_attachment;

	alert_handler* m_alert;

	std::mutex m_mutex;
	std::multimap<lt::torrent_handle, std::shared_ptr<file_request_conn>> m_outstanding_requests;
};
} // namespace ltweb

#endif
