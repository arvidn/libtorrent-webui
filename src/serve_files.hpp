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

struct serve_files : http_handler {
	serve_files(std::string_view prefix, std::string_view root_directory);

	std::string path_prefix() const override;

	void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

private:
	std::filesystem::path m_root;
	std::string m_prefix;
};

} // namespace ltweb

#endif
