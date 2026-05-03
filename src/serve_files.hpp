/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_SERVE_FILES_HPP
#define LTWEB_SERVE_FILES_HPP

#include <string_view>
#include <string>
#include <optional>
#include <filesystem>

#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

#include "webui.hpp" // for http_handler
#include "auth_interface.hpp"

namespace ltweb {

namespace aux {

// Resolve `relative` against `root` and return the absolute path if and
// only if the result is contained within `root`. Returns nullopt for an
// absolute `relative` (which would re-root the join), for paths that
// escape via "..", and for paths whose canonical form lies outside `root`
// (e.g. via symlinks). An empty `relative` resolves to <root>/index.html.
//
// Precondition: `root` is already canonical. serve_files canonicalizes
// its root once in the constructor and stores it in m_root, so the
// production caller satisfies this trivially. Tests must canonicalize
// their fixture roots themselves (e.g. /tmp may be a symlink).
std::optional<std::filesystem::path>
resolve_served_path(std::filesystem::path const& root, std::filesystem::path const& relative);

} // namespace aux

// HTTP handler that serves static files from root_directory under the
// given path prefix. Every request is authenticated via the supplied
// auth_interface; on failure the response is 303 See Other to
// login_url.
struct serve_files : http_handler {
	serve_files(
		std::string_view prefix,
		std::string_view root_directory,
		auth_interface const& auth,
		std::string login_url
	);

	std::string path_prefix() const override;

	void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

private:
	std::filesystem::path m_root;
	std::string m_prefix;
	auth_interface const& m_auth;
	std::string m_login_url;
};

} // namespace ltweb

#endif
