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

// Mock auth_interface that records the raw argument to authenticate() and
// returns a pre-configured permissions pointer.
struct mock_auth : auth_interface {
	// set before each call to control the return value
	permissions_interface const* result = nullptr;

	// raw arguments passed to authenticate()
	mutable std::string last_cookie;
	mutable bool authenticate_called = false;

	permissions_interface const* authenticate(std::string_view session_cookie) const override
	{
		authenticate_called = true;
		last_cookie = std::string(session_cookie);
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

BOOST_AUTO_TEST_CASE(session_cookie_extracted)
{
	// A "session=<value>" cookie is pulled out of the Cookie header.
	mock_auth auth;
	auth.result = &g_full;
	(void)parse_http_auth(make_request(nullptr, "session=abc123"), auth);
	BOOST_TEST(auth.last_cookie == "abc123");
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
