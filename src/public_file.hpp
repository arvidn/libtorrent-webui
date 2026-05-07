// Copyright (c) 2026, Arvid Norberg
// All rights reserved.
//
// You may use, distribute and modify this code under the terms of the BSD license,
// see LICENSE file.

#ifndef LTWEB_PUBLIC_FILE_HPP
#define LTWEB_PUBLIC_FILE_HPP

#include <string>
#include <functional>

#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

#include "webui.hpp"

namespace ltweb {

// HTTP handler that serves a single file at a fixed server path. Unlike
// serve_files, it requires no authentication and does not walk a
// directory tree, so it is intended only for assets that must be
// reachable without a session cookie (eg /favicon.ico, public
// stylesheets). Register one instance per file.
struct public_file : http_handler {
	public_file(std::string server_path, std::string local_path);

	std::string path_prefix() const override;

	void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

private:
	std::string m_server_path;
	std::string m_local_path;
};

} // namespace ltweb

#endif
