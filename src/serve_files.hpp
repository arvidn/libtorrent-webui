/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string_view>
#include <string>

#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

#include "webui.hpp" // for http_handler

namespace ltweb {

struct serve_files : http_handler {
	serve_files(std::string_view prefix, std::string_view root_directory);

	std::string path_prefix() const override;

	void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

private:
	std::string m_root;
	std::string m_prefix;
};

} // namespace ltweb
