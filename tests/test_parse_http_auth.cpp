/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE parse_http_auth
#include <boost/test/included/unit_test.hpp>

#include "auth_interface.hpp"
#include "parse_http_auth.hpp"

#include <boost/beast/http.hpp>

namespace http = boost::beast::http;
using namespace ltweb;

namespace {

// Mock auth_interface that records both the raw arguments
// authenticate() received and the decoded Basic credentials, and
// returns a pre-configured permissions pointer.
//
// Decoding is run inside the mock (using the same parse_basic_auth
// helper that real implementations use) so tests can still assert on
// "user" and "password" - this verifies the full chain: header
// extraction in parse_http_auth + Basic decoding in the helper.
struct mock_auth : auth_interface {
	// set before each call to control the return value
	permissions_interface const* result = nullptr;

	// raw arguments passed to authenticate()
	mutable std::string last_cookie;
	mutable std::string last_authorization;

	// decoded Basic credentials (or empty for non-Basic / no header)
	mutable std::string last_user;
	mutable std::string last_password;
	mutable bool authenticate_called = false;

	permissions_interface const*
	authenticate(std::string_view session_cookie, std::string_view authorization) const override
	{
		authenticate_called = true;
		last_cookie = std::string(session_cookie);
		last_authorization = std::string(authorization);
		std::tie(last_user, last_password) = parse_basic_auth(authorization);
		return result;
	}
};

http::request<http::string_body>
make_request(char const* auth_value = nullptr, char const* cookie_value = nullptr)
{
	http::request<http::string_body> req{http::verb::get, "/", 11};
	if (auth_value) req.set(http::field::authorization, auth_value);
	if (cookie_value) req.set(http::field::cookie, cookie_value);
	return req;
}

full_permissions const g_full;

} // anonymous namespace

BOOST_AUTO_TEST_CASE(anonymous_rejected)
{
	// No Authorization header: authenticate("", "") is called, result propagated.
	mock_auth auth;
	auth.result = nullptr;
	auto* p = parse_http_auth(make_request(), auth);
	BOOST_TEST(p == nullptr);
	BOOST_TEST(auth.authenticate_called);
	BOOST_TEST(auth.last_authorization == "");
	BOOST_TEST(auth.last_user == "");
	BOOST_TEST(auth.last_password == "");
}

BOOST_AUTO_TEST_CASE(anonymous_accepted)
{
	// No Authorization header, auth accepts anonymous access.
	mock_auth auth;
	auth.result = &g_full;
	auto* p = parse_http_auth(make_request(), auth);
	BOOST_TEST(p == &g_full);
	BOOST_TEST(auth.authenticate_called);
	BOOST_TEST(auth.last_authorization == "");
	BOOST_TEST(auth.last_user == "");
	BOOST_TEST(auth.last_password == "");
}

BOOST_AUTO_TEST_CASE(valid_basic)
{
	// Valid "basic " credentials: "user:pass" -> base64 "dXNlcjpwYXNz"
	mock_auth auth;
	auth.result = &g_full;
	auto* p = parse_http_auth(make_request("basic dXNlcjpwYXNz"), auth);
	BOOST_TEST(p == &g_full);
	BOOST_TEST(auth.last_authorization == "basic dXNlcjpwYXNz");
	BOOST_TEST(auth.last_user == "user");
	BOOST_TEST(auth.last_password == "pass");
}

BOOST_AUTO_TEST_CASE(colon_in_password)
{
	// Credentials with a colon in the password: "alice:p:ss" -> base64 "YWxpY2U6cDpzcw=="
	// The split is on the first colon only, so the password is "p:ss".
	mock_auth auth;
	auth.result = &g_full;
	auto* p = parse_http_auth(make_request("basic YWxpY2U6cDpzcw=="), auth);
	BOOST_TEST(p == &g_full);
	BOOST_TEST(auth.last_user == "alice");
	BOOST_TEST(auth.last_password == "p:ss");
}

BOOST_AUTO_TEST_CASE(empty_password)
{
	// Empty password: "bob:" -> base64 "Ym9iOg=="
	mock_auth auth;
	auth.result = &g_full;
	auto* p = parse_http_auth(make_request("basic Ym9iOg=="), auth);
	BOOST_TEST(p == &g_full);
	BOOST_TEST(auth.last_user == "bob");
	BOOST_TEST(auth.last_password == "");
}

BOOST_AUTO_TEST_CASE(extra_whitespace)
{
	// Whitespace around the base64 token is trimmed: leading space after "basic ".
	mock_auth auth;
	auth.result = &g_full;
	auto* p = parse_http_auth(make_request("basic  dXNlcjpwYXNz"), auth);
	BOOST_TEST(p == &g_full);
	BOOST_TEST(auth.last_user == "user");
	BOOST_TEST(auth.last_password == "pass");
}

BOOST_AUTO_TEST_CASE(wrong_scheme)
{
	// Wrong scheme ("Bearer"): not parsed as Basic, last_user/password empty.
	// The raw header is still forwarded - it is up to the auth_interface
	// implementation what to do with it.
	mock_auth auth;
	auth.result = nullptr;
	auto* p = parse_http_auth(make_request("Bearer sometoken"), auth);
	BOOST_TEST(p == nullptr);
	BOOST_TEST(auth.authenticate_called);
	BOOST_TEST(auth.last_authorization == "Bearer sometoken");
	BOOST_TEST(auth.last_user == "");
	BOOST_TEST(auth.last_password == "");
}

BOOST_AUTO_TEST_CASE(basic_capital_b)
{
	// "Basic" with capital B: the scheme check is case-insensitive.
	mock_auth auth;
	auth.result = nullptr;
	(void)parse_http_auth(make_request("Basic dXNlcjpwYXNz"), auth);
	BOOST_TEST(auth.last_user == "user");
	BOOST_TEST(auth.last_password == "pass");
}

BOOST_AUTO_TEST_CASE(basic_mixed_case)
{
	// "basic" is case insensitive
	mock_auth auth;
	auth.result = nullptr;
	(void)parse_http_auth(make_request("BaSiC dXNlcjpwYXNz"), auth);
	BOOST_TEST(auth.last_user == "user");
	BOOST_TEST(auth.last_password == "pass");
}

BOOST_AUTO_TEST_CASE(rejected_credentials)
{
	// Auth rejects the credentials: authenticate returns nullptr, propagated.
	mock_auth auth;
	auth.result = nullptr;
	auto* p = parse_http_auth(make_request("basic dXNlcjpwYXNz"), auth);
	BOOST_TEST(p == nullptr);
	BOOST_TEST(auth.last_user == "user");
	BOOST_TEST(auth.last_password == "pass");
}

BOOST_AUTO_TEST_CASE(session_cookie_extracted)
{
	// A "session=<value>" cookie is pulled out of the Cookie header.
	mock_auth auth;
	auth.result = &g_full;
	(void)parse_http_auth(make_request(nullptr, "session=abc123"), auth);
	BOOST_TEST(auth.last_cookie == "abc123");
	BOOST_TEST(auth.last_authorization == "");
}

BOOST_AUTO_TEST_CASE(session_cookie_among_others)
{
	// Cookie header may carry multiple "name=value" pairs separated by "; ".
	// Only the session= entry is extracted.
	mock_auth auth;
	auth.result = &g_full;
	(void)parse_http_auth(make_request(nullptr, "theme=dark; session=xyz; lang=en"), auth);
	BOOST_TEST(auth.last_cookie == "xyz");
}

BOOST_AUTO_TEST_CASE(no_session_cookie)
{
	// Cookie header without a "session=" entry passes through as empty.
	mock_auth auth;
	auth.result = nullptr;
	(void)parse_http_auth(make_request(nullptr, "theme=dark; lang=en"), auth);
	BOOST_TEST(auth.last_cookie == "");
}

BOOST_AUTO_TEST_CASE(both_headers_forwarded)
{
	// When both headers are present, both are passed to authenticate().
	mock_auth auth;
	auth.result = &g_full;
	(void)parse_http_auth(make_request("basic dXNlcjpwYXNz", "session=abc"), auth);
	BOOST_TEST(auth.last_cookie == "abc");
	BOOST_TEST(auth.last_authorization == "basic dXNlcjpwYXNz");
	BOOST_TEST(auth.last_user == "user");
	BOOST_TEST(auth.last_password == "pass");
}

// ---------- extract_cookie ----------

BOOST_AUTO_TEST_CASE(extract_cookie_single)
{
	BOOST_TEST(extract_cookie("csrf=tok123", "csrf") == "tok123");
}

BOOST_AUTO_TEST_CASE(extract_cookie_among_others)
{
	BOOST_TEST(extract_cookie("a=1; csrf=tok; b=2", "csrf") == "tok");
}

BOOST_AUTO_TEST_CASE(extract_cookie_missing)
{
	BOOST_TEST(extract_cookie("a=1; b=2", "csrf").empty());
}

BOOST_AUTO_TEST_CASE(extract_cookie_no_prefix_match)
{
	// "csrf-token" must not match a request for "csrf". The "="
	// boundary check makes name extraction strict.
	BOOST_TEST(extract_cookie("csrf-token=foo", "csrf").empty());
}

BOOST_AUTO_TEST_CASE(extract_cookie_empty_value)
{
	BOOST_TEST(extract_cookie("csrf=", "csrf") == "");
	BOOST_TEST(extract_cookie("a=1; csrf=; b=2", "csrf") == "");
}

BOOST_AUTO_TEST_CASE(extract_cookie_no_space_after_semicolon)
{
	// RFC 6265 prescribes "; " but real clients sometimes omit
	// the space. Accept both.
	BOOST_TEST(extract_cookie("a=1;csrf=tok", "csrf") == "tok");
}
