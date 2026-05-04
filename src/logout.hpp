/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_LOGOUT_HPP
#define LTWEB_LOGOUT_HPP

#include "webui.hpp"

#include <string>

// Test seam: defined in tests/test_login.cpp. Granted friend access to
// `logout` so unit tests can drive try_logout() directly without having
// to mock an SSL stream.
struct logout_fixture;

namespace ltweb {

struct session_authenticator;

// HTTP handler for the logout endpoint at path_prefix. Loading it
// destroys the session referenced by the request's session cookie
// (if any), clears the cookie on the client, and redirects the
// browser to redirect_url (typically the login page).
//
// Idempotent: missing or unknown session cookies still produce the
// same redirect, so the response never leaks whether a given
// session id existed.
struct logout : http_handler {
	logout(std::string path_prefix, session_authenticator& sessions, std::string redirect_url);

	logout(logout const&) = delete;
	logout& operator=(logout const&) = delete;

	std::string path_prefix() const override;
	void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

private:
	friend struct ::logout_fixture;

	// Decide what to do with a logout request, separate from the I/O of
	// building the response. Returns:
	//   - not_found if the request target's path is not exactly
	//     m_path_prefix (no side effect)
	//   - see_other otherwise, and if the request carries a "session"
	//     cookie, destroy that session in m_sessions
	// Internal; reachable from unit tests via the logout_fixture friend
	// declaration above.
	http::status try_logout(http::request<http::string_body> const& request) const;

	std::string m_path_prefix;
	session_authenticator& m_sessions;
	std::string m_redirect_url;
};

} // namespace ltweb

#endif
