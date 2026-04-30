/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string_view>
#include <filesystem>
#include <cinttypes>

#include "serve_files.hpp"
#include "parse_http_auth.hpp"
#include "file_response.hpp"

namespace fs = std::filesystem;

namespace ltweb {

namespace aux {

std::optional<fs::path> resolve_served_path(fs::path const& root, fs::path const& relative)
{
	// reject absolute paths. fs::path::operator/ discards the left operand
	// when the right is absolute, which would let a request like
	// "/<prefix>//etc/passwd" reroute to "/etc/passwd"
	if (relative.is_absolute()) return std::nullopt;

	fs::path full_path = root / relative;

	if (relative.empty()) full_path /= "index.html";

	// resolve symlinks, ".." segments, and "//" sequences in the joined
	// path, then verify the result is still contained within root.
	// weakly_canonical works even when trailing path components do not
	// yet exist. `root` is required to already be canonical -- see header.
	std::error_code ec;
	fs::path const canonical_full = fs::weakly_canonical(full_path, ec);
	if (ec) return std::nullopt;

	fs::path const rel = canonical_full.lexically_relative(root);
	if (rel.empty() || *rel.begin() == "..") return std::nullopt;

	return canonical_full;
}

} // namespace aux

serve_files::serve_files(
	std::string_view prefix,
	std::string_view root_directory,
	auth_interface const& auth,
	std::string login_url
)
	: m_root(fs::weakly_canonical(
		  fs::path(root_directory.empty() ? std::string_view(".") : root_directory)
	  ))
	, m_prefix(prefix)
	, m_auth(auth)
	, m_login_url(std::move(login_url))
{
	if (m_prefix.empty() || m_prefix.back() != '/') m_prefix += '/';

	if (m_prefix.front() != '/') m_prefix.insert(0, "/");
}

// this must return the same string every time. This determines which
// request paths are routed to this handler
std::string serve_files::path_prefix() const { return m_prefix; }

// called for each HTTP request. Once the response has been sent, the done()
// function must be called, to read another request from the client.
void serve_files::handle_http(
	http::request<http::string_body> request,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	permissions_interface const* perms = parse_http_auth(request, m_auth);
	if (!perms) {
		http::response<http::empty_body> res{http::status::see_other, request.version()};
		res.set(http::field::location, m_login_url);
		res.keep_alive(request.keep_alive());
		return send_http(socket, std::move(done), std::move(res));
	}

	if (request.target().size() < m_prefix.size())
		return send_http(
			socket, std::move(done), http_error(request, http::status::internal_server_error)
		);

	assert(request.target().front() == '/');

	fs::path const relative_path(std::string(request.target().substr(m_prefix.size())));

	auto const resolved = aux::resolve_served_path(m_root, relative_path);
	if (!resolved)
		return send_http(socket, std::move(done), http_error(request, http::status::bad_request));

	aux::serve_local_file(request, *resolved, socket, std::move(done));
}

} // namespace ltweb
