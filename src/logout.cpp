/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "logout.hpp"
#include "parse_http_auth.hpp"
#include "session_authenticator.hpp"

#include <string_view>
#include <utility>

namespace ltweb {

logout::logout(std::string path_prefix_, session_authenticator& sessions, std::string redirect_url)
	: m_path_prefix(std::move(path_prefix_))
	, m_sessions(sessions)
	, m_redirect_url(std::move(redirect_url))
{
}

std::string logout::path_prefix() const { return m_path_prefix; }

http::status logout::try_logout(http::request<http::string_body> const& req) const
{
	// Only the exact path is the logout endpoint. Sub-paths under the
	// same prefix would otherwise also destroy the session, which is
	// surprising. A query string is allowed.
	if (!aux::path_matches_exact(req.target(), m_path_prefix)) return http::status::not_found;

	auto const cookie_it = req.find(http::field::cookie);
	if (cookie_it != req.end()) {
		std::string_view const sid = extract_cookie(cookie_it->value(), "session");
		if (!sid.empty()) m_sessions.destroy(sid);
	}
	return http::status::see_other;
}

void logout::handle_http(
	http::request<http::string_body> req,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	auto const status = try_logout(req);
	if (status != http::status::see_other) {
		send_http(socket, std::move(done), http_error(req, status));
		return;
	}

	http::response<http::empty_body> res{status, req.version()};
	res.set(http::field::location, m_redirect_url);
	// Match the attributes the session cookie was set with so the
	// browser actually overwrites it. Path=/ in particular - the
	// session cookie is scoped to the whole site.
	res.set(
		http::field::set_cookie, "session=; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=0"
	);
	res.keep_alive(req.keep_alive());
	send_http(socket, std::move(done), std::move(res));
}

} // namespace ltweb
