/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "auth.hpp"
#include "auth_interface.hpp"
#include "test.hpp"

#include <boost/beast/http.hpp>

namespace http = boost::beast::http;
using namespace ltweb;

int main_ret = 0;

namespace {

// Mock auth_interface that records the last find_user() call and
// returns a pre-configured permissions pointer.
struct mock_auth : auth_interface
{
	// set before each call to control the return value
	permissions_interface const* result = nullptr;

	// populated by find_user(); mutable because find_user() is const
	mutable std::string last_user;
	mutable std::string last_password;
	mutable bool find_user_called = false;

	permissions_interface const* find_user(std::string user, std::string password) const override
	{
		find_user_called = true;
		last_user = std::move(user);
		last_password = std::move(password);
		return result;
	}
};

// Build a GET / HTTP/1.1 request, optionally with an Authorization header.
http::request<http::string_body> make_request(char const* auth_value = nullptr)
{
	http::request<http::string_body> req{http::verb::get, "/", 11};
	if (auth_value)
		req.set(http::field::authorization, auth_value);
	return req;
}

full_permissions const g_full;

} // anonymous namespace

int main()
{
	// No Authorization header: find_user("", "") is called, result propagated.
	{
		mock_auth auth;
		auth.result = nullptr;
		auto* p = parse_http_auth(make_request(), &auth);
		TEST_CHECK(p == nullptr);
		TEST_CHECK(auth.find_user_called);
		TEST_CHECK(auth.last_user == "");
		TEST_CHECK(auth.last_password == "");
	}

	// No Authorization header, auth accepts anonymous access.
	{
		mock_auth auth;
		auth.result = &g_full;
		auto* p = parse_http_auth(make_request(), &auth);
		TEST_CHECK(p == &g_full);
		TEST_CHECK(auth.find_user_called);
		TEST_CHECK(auth.last_user == "");
		TEST_CHECK(auth.last_password == "");
	}

	// Valid "basic " credentials: "user:pass" -> base64 "dXNlcjpwYXNz"
	{
		mock_auth auth;
		auth.result = &g_full;
		auto* p = parse_http_auth(make_request("basic dXNlcjpwYXNz"), &auth);
		TEST_CHECK(p == &g_full);
		TEST_CHECK(auth.last_user == "user");
		TEST_CHECK(auth.last_password == "pass");
	}

	// Credentials with a colon in the password: "alice:p:ss" -> base64 "YWxpY2U6cDpzcw=="
	// The split is on the first colon only, so the password is "p:ss".
	{
		mock_auth auth;
		auth.result = &g_full;
		auto* p = parse_http_auth(make_request("basic YWxpY2U6cDpzcw=="), &auth);
		TEST_CHECK(p == &g_full);
		TEST_CHECK(auth.last_user == "alice");
		TEST_CHECK(auth.last_password == "p:ss");
	}

	// Empty password: "bob:" -> base64 "Ym9iOg=="
	{
		mock_auth auth;
		auth.result = &g_full;
		auto* p = parse_http_auth(make_request("basic Ym9iOg=="), &auth);
		TEST_CHECK(p == &g_full);
		TEST_CHECK(auth.last_user == "bob");
		TEST_CHECK(auth.last_password == "");
	}

	// Whitespace around the base64 token is trimmed: leading space after "basic ".
	{
		mock_auth auth;
		auth.result = &g_full;
		auto* p = parse_http_auth(make_request("basic  dXNlcjpwYXNz"), &auth);
		TEST_CHECK(p == &g_full);
		TEST_CHECK(auth.last_user == "user");
		TEST_CHECK(auth.last_password == "pass");
	}

	// Wrong scheme ("Bearer"): not parsed as Basic, find_user("", "") called.
	{
		mock_auth auth;
		auth.result = nullptr;
		auto* p = parse_http_auth(make_request("Bearer sometoken"), &auth);
		TEST_CHECK(p == nullptr);
		TEST_CHECK(auth.find_user_called);
		TEST_CHECK(auth.last_user == "");
		TEST_CHECK(auth.last_password == "");
	}

	// "Basic" with capital B: the implementation checks for "basic " (lowercase),
	{
		mock_auth auth;
		auth.result = nullptr;
		auto* p = parse_http_auth(make_request("Basic dXNlcjpwYXNz"), &auth);
		TEST_CHECK(auth.last_user == "user");
		TEST_CHECK(auth.last_password == "pass");
	}

	// "basic" is case insensitive
	{
		mock_auth auth;
		auth.result = nullptr;
		auto* p = parse_http_auth(make_request("BaSiC dXNlcjpwYXNz"), &auth);
		TEST_CHECK(auth.last_user == "user");
		TEST_CHECK(auth.last_password == "pass");
	}

	// Auth rejects the credentials: find_user returns nullptr, propagated.
	{
		mock_auth auth;
		auth.result = nullptr;
		auto* p = parse_http_auth(make_request("basic dXNlcjpwYXNz"), &auth);
		TEST_CHECK(p == nullptr);
		TEST_CHECK(auth.last_user == "user");
		TEST_CHECK(auth.last_password == "pass");
	}

	return main_ret;
}
