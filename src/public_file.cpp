// Copyright (c) 2026, Arvid Norberg
// All rights reserved.
//
// You may use, distribute and modify this code under the terms of the BSD license,
// see LICENSE file.

#include <filesystem>

#include "public_file.hpp"
#include "file_response.hpp"

namespace fs = std::filesystem;

namespace ltweb {

public_file::public_file(std::string server_path, std::string local_path)
	: m_server_path(std::move(server_path))
	, m_local_path(std::move(local_path))
{
}

std::string public_file::path_prefix() const { return m_server_path; }

void public_file::handle_http(
	http::request<http::string_body> request,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	std::string_view const path =
		aux::strip_query(std::string_view(request.target().data(), request.target().size()));

	// the router matches by prefix, so reject anything that isn't an
	// exact hit on the registered server path (eg /favicon.ico/extra).
	if (path != m_server_path)
		return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	aux::serve_local_file(request, fs::path(m_local_path), socket, std::move(done));
}

} // namespace ltweb
