/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "parse_http_auth.hpp"

namespace ltweb {

std::string_view extract_cookie(std::string_view cookie_header, std::string_view name)
{
	std::size_t pos = 0;
	while (pos < cookie_header.size()) {
		while (pos < cookie_header.size()
			   && (cookie_header[pos] == ' ' || cookie_header[pos] == '\t')) {
			++pos;
		}
		// Match name + "=" exactly. Without the explicit '=' check
		// "csrf" would falsely match a cookie called "csrf-token".
		if (cookie_header.size() - pos > name.size()
			&& cookie_header.compare(pos, name.size(), name) == 0
			&& cookie_header[pos + name.size()] == '=') {
			pos += name.size() + 1;
			auto end = cookie_header.find(';', pos);
			return cookie_header.substr(
				pos, end == std::string_view::npos ? std::string_view::npos : end - pos
			);
		}
		auto next = cookie_header.find(';', pos);
		if (next == std::string_view::npos) break;
		pos = next + 1;
	}
	return {};
}

permissions_interface const*
parse_http_auth(http::request<http::string_body> const& request, auth_interface const& auth)
{
	std::string_view session_cookie;
	auto const cookie_it = request.find(http::field::cookie);
	if (cookie_it != request.end()) {
		session_cookie = extract_cookie(cookie_it->value(), "session");
	}

	return auth.authenticate(session_cookie);
}

} // namespace ltweb
