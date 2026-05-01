/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_LOGIN_HPP
#define LTWEB_LOGIN_HPP

#include "webui.hpp"
#include "auth_interface.hpp"

#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Test seam: defined in tests/test_login.cpp. Granted friend access to
// `login` so unit tests can drive try_login() directly without having
// to construct a real Beast SSL stream.
struct login_fixture;

namespace ltweb {

struct session_authenticator;
struct login_throttler;

// HTTP handler responsible for the entire login flow at path_prefix:
//
//   GET  <path_prefix> serves the login form HTML with a freshly
//   minted CSRF token baked into a hidden input AND set as a cookie.
//   The form posts to the same URL.
//
//   POST <path_prefix> consults the supplied login_throttler to
//   reject brute-force attempts, validates the CSRF pair (cookie
//   value must equal the form's csrf field), then verifies
//   username/password via the supplied user_account, mints a
//   session via the session_authenticator, and replies 303 See
//   Other to welcome_url with a Set-Cookie header for the session.
//   Failures are recorded back into the throttler so repeated
//   attempts from the same source network are blocked.
//
// Credential verification is delegated to a user_account
// implementation - the login class does not know how passwords are
// stored or hashed.
//
// The user's group_id (returned by user_account::verify) indexes into
// the groups vector to resolve a permissions_interface for the new
// session. Out-of-range or null entries reject the login.
struct login : http_handler {
	// template_html is the entire contents of the login.html file.
	// It MUST contain exactly one occurrence of the literal string
	// "__CSRF_TOKEN__", wrapped by this exact hidden input tag:
	//   <input type="hidden" name="csrf" value="__CSRF_TOKEN__" />
	// Both the prefix (the opening of the tag up to and including
	// value=") and the suffix ("/>) are validated at construction.
	// The constructor throws if the placeholder is missing,
	// duplicated, or surrounded by anything else - so a future edit
	// that would let the token leak (eg into an <a href> or
	// <script>), drop the closing quote, reorder attributes, or
	// otherwise drift from the canonical wrapping fails the binary
	// at startup rather than silently shipping broken HTML.
	login(
		std::string path_prefix,
		std::string template_html,
		user_account const& accounts,
		session_authenticator& sessions,
		login_throttler& throttler,
		std::string welcome_url,
		std::vector<permissions_interface const*> groups
	);

	login(login const&) = delete;
	login& operator=(login const&) = delete;

	std::string path_prefix() const override;
	void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

	// Number of groups configured at construction. For tests and
	// diagnostics.
	std::size_t group_count() const { return m_groups.size(); }

private:
	friend struct ::login_fixture;

	// Result of credential-validation, separate from the I/O of
	// building the response. The int alternative is the group_id of
	// the authenticated user (success). The http::status alternative
	// is the failure status to return to the client - one of
	// method_not_allowed, payload_too_large, bad_request,
	// unauthorized, or forbidden (the latter for CSRF failures).
	// Internal; reachable from unit tests via the login_fixture
	// friend declaration above.
	using attempt_result = std::variant<http::status, int>;

	// Validate a request: method, body size, CSRF (cookie/form
	// token pair), form fields, credential check via user_account,
	// group_id range. Does NOT mint a session or build a response.
	attempt_result try_login(http::request<http::string_body> const& request) const;

	std::string m_path_prefix;
	user_account const& m_accounts;
	session_authenticator& m_sessions;
	login_throttler& m_throttler;
	std::string m_welcome_url;
	std::vector<permissions_interface const*> m_groups;

	// login.html split around the CSRF placeholder. The token is
	// only ever rendered as: m_html_before + token + m_html_after.
	// The split point is fixed at construction by parse_login_template
	// after validating the placeholder's surrounding bytes.
	std::string m_html_before;
	std::string m_html_after;
};

// Parses an application/x-www-form-urlencoded body into a key->value
// map. URL-decodes both keys and values; treats '+' as space and
// decodes %XX sequences. Silently skips malformed pairs (no '=').
// Exposed primarily for testing.
std::map<std::string, std::string> parse_form(std::string_view body);

// Result of pre-parsing the login HTML template. The token is
// rendered into the page as: before + token + after.
struct login_template_parts {
	std::string before;
	std::string after;
};

// Validate a login.html template and split it around the CSRF
// placeholder. Throws std::runtime_error if the placeholder is
// missing, appears more than once, or is not surrounded by the
// blessed hidden input tag. Exposed for testing.
login_template_parts parse_login_template(std::string html);

} // namespace ltweb

#endif
