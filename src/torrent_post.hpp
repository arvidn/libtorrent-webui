/*

Copyright (c) 2012-2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_TORRENT_POST_HPP
#define LTWEB_TORRENT_POST_HPP

#include "webui.hpp"
#include "save_settings.hpp"
#include "auth_interface.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/add_torrent_params.hpp"

#include <cstdint>
#include <limits>
#include <vector>

struct torrent_post_limits
{
	// stop accepting parts once this many torrents have been collected
	int max_torrent_count = std::numeric_limits<int>::max();
	// stop accepting parts once this many bytes of .torrent payload have been seen
	std::int64_t max_payload_bytes = std::numeric_limits<std::int64_t>::max();
	// reject the entire request if the HTTP body exceeds this size
	std::int64_t max_body_bytes = 10LL * 1024 * 1024;
};

// Parse a multipart/form-data POST request and return one add_torrent_params
// per accepted .torrent part. Sets ec on request-level errors (wrong content-type,
// missing boundary, body too large, no valid parts found).
// Per-part parse failures are silently skipped so the remaining parts still succeed.
// Parts beyond the per-request limits in `limits` are silently ignored.
std::vector<lt::add_torrent_params> parse_torrent_post(
	http::request<http::string_body> const& req, lt::error_code& ec,
	torrent_post_limits const& limits = {});

namespace ltweb {

// HTTP handler that accepts .torrent file uploads at /bt/add.
// Supports multiple files in a single multipart/form-data request.
// Responds 204 No Content on success, 400 or 413 on error.
struct torrent_post_handler : http_handler
{
	torrent_post_handler(lt::session& ses
		, auth_interface const* auth
		, save_settings_interface* settings = nullptr);

	std::string path_prefix() const override;

	void handle_http(http::request<http::string_body> req
		, beast::ssl_stream<beast::tcp_stream>& socket
		, std::function<void(bool)> done) override;

private:
	lt::session& m_ses;
	auth_interface const* m_auth;
	save_settings_interface* m_settings;
};

} // namespace ltweb

#endif
