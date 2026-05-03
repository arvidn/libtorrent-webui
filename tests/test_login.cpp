/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE login
#include <boost/test/included/unit_test.hpp>

#include "login.hpp"
#include "login_throttler.hpp"
#include "session_authenticator.hpp"
#include "auth_interface.hpp"

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace http = boost::beast::http;
using namespace ltweb;

namespace {

full_permissions const g_full;
read_only_permissions const g_ro;

// Minimal valid login.html template - just the blessed hidden input
// wrapped in an otherwise empty form. Tests don't render the page,
// so we only need enough HTML for parse_login_template to accept.
// The hidden input must match the canonical wrapping exactly; any
// drift (extra attributes, swapped quote style, missing slash) is
// rejected at construction.
constexpr char const* valid_template =
	"<!doctype html><html><body>"
	"<form method=\"POST\" action=\"/login\">"
	"<input type=\"hidden\" name=\"csrf\" value=\"__CSRF_TOKEN__\" />"
	"<input name=\"username\"><input name=\"password\" type=\"password\">"
	"<button type=\"submit\">Sign in</button>"
	"</form></body></html>";

// In-memory user_account stub. Stores plaintext password + group;
// real backends hash, but for testing the login class itself the
// in-memory variant is enough.
struct mock_accounts : user_account {
	std::map<std::string, std::pair<std::string, int>> users;

	std::optional<int> verify(std::string_view username, std::string_view password) const override
	{
		auto it = users.find(std::string(username));
		if (it == users.end()) return std::nullopt;
		if (it->second.first != std::string(password)) return std::nullopt;
		return it->second.second;
	}
};

// Build a POST body containing a valid CSRF pair (cookie + form).
// The token value itself is arbitrary as far as try_login is
// concerned - only equality between the cookie and the form field
// matters.
http::request<http::string_body> make_post(std::string body, char const* cookie = nullptr)
{
	http::request<http::string_body> req{http::verb::post, "/login", 11};
	req.set(http::field::content_type, "application/x-www-form-urlencoded");
	if (cookie) req.set(http::field::cookie, cookie);
	req.body() = std::move(body);
	req.prepare_payload();
	return req;
}

// Helper that supplies a matching CSRF cookie + form field so the
// caller only has to think about the credential side of things.
http::request<http::string_body> make_authed_post(std::string credentials_body)
{
	std::string body = "csrf=tok123&" + credentials_body;
	return make_post(std::move(body), "csrf=tok123");
}

} // anonymous namespace

// ---------- parse_form ----------

BOOST_AUTO_TEST_CASE(form_parser_basic)
{
	auto m = parse_form("username=bob&password=hunter2");
	BOOST_TEST(m.size() == std::size_t(2));
	BOOST_TEST(m["username"] == "bob");
	BOOST_TEST(m["password"] == "hunter2");
}

BOOST_AUTO_TEST_CASE(form_parser_plus_and_percent)
{
	auto m = parse_form("name=hello+world&pw=p%40ss");
	BOOST_TEST(m["name"] == "hello world");
	BOOST_TEST(m["pw"] == "p@ss");
}

BOOST_AUTO_TEST_CASE(form_parser_empty_body)
{
	auto m = parse_form("");
	BOOST_TEST(m.empty());
}

BOOST_AUTO_TEST_CASE(form_parser_missing_equals_skipped)
{
	auto m = parse_form("novaluepair&username=alice");
	BOOST_TEST(m.size() == std::size_t(1));
	BOOST_TEST(m["username"] == "alice");
}

BOOST_AUTO_TEST_CASE(form_parser_trailing_amp)
{
	auto m = parse_form("a=1&b=2&");
	BOOST_TEST(m.size() == std::size_t(2));
	BOOST_TEST(m["a"] == "1");
	BOOST_TEST(m["b"] == "2");
}

BOOST_AUTO_TEST_CASE(form_parser_empty_value)
{
	auto m = parse_form("a=&b=2");
	BOOST_TEST(m["a"] == "");
	BOOST_TEST(m["b"] == "2");
}

// ---------- parse_login_template ----------

BOOST_AUTO_TEST_CASE(template_valid_splits_around_marker)
{
	auto parts = parse_login_template(valid_template);
	// The before/after split should reconstruct the original when
	// the placeholder is glued back in.
	std::string rebuilt = parts.before + "__CSRF_TOKEN__" + parts.after;
	BOOST_TEST(rebuilt == std::string(valid_template));
}

BOOST_AUTO_TEST_CASE(template_rejects_missing_marker)
{
	std::string html = "<form><input type=\"hidden\" name=\"csrf\" value=\"\"></form>";
	BOOST_CHECK_THROW(parse_login_template(html), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(template_rejects_duplicate_marker)
{
	std::string html =
		"<form>"
		"<input type=\"hidden\" name=\"csrf\" value=\"__CSRF_TOKEN__\">"
		"<input type=\"hidden\" name=\"csrf\" value=\"__CSRF_TOKEN__\">"
		"</form>";
	BOOST_CHECK_THROW(parse_login_template(html), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(template_rejects_marker_in_anchor_href)
{
	// A future edit that puts the placeholder in an <a href> would
	// leak the token via Referer to the linked site. Reject it.
	std::string html = "<a href=\"__CSRF_TOKEN__\">click</a>";
	BOOST_CHECK_THROW(parse_login_template(html), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(template_rejects_marker_in_script)
{
	// Inline scripts are XSS-readable. Reject placement there.
	std::string html = "<script>var t = \"__CSRF_TOKEN__\";</script>";
	BOOST_CHECK_THROW(parse_login_template(html), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(template_rejects_marker_at_string_start)
{
	// Not enough room for the lhs context.
	std::string html = "__CSRF_TOKEN__\">";
	BOOST_CHECK_THROW(parse_login_template(html), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(template_rejects_wrong_suffix)
{
	// Canonical prefix but suffix uses ">" instead of " />". This
	// is the most likely real-world drift: an editor or formatter
	// "fixing" the self-closing slash. Must be rejected so the
	// mismatch is caught at startup rather than at request time.
	std::string html =
		"<form>"
		"<input type=\"hidden\" name=\"csrf\" value=\"__CSRF_TOKEN__\">"
		"</form>";
	BOOST_CHECK_THROW(parse_login_template(html), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(template_rejects_extra_attribute_in_tag)
{
	// Extra attribute ahead of value=" breaks the strict prefix
	// match, even though the placeholder is still inside an attribute
	// value. Strict matching is the whole point of the validator.
	std::string html =
		"<form>"
		"<input type=\"hidden\" name=\"csrf\" autocomplete=\"off\" value=\"__CSRF_TOKEN__\" />"
		"</form>";
	BOOST_CHECK_THROW(parse_login_template(html), std::runtime_error);
}

// ---------- try_login ----------

struct login_fixture {
	mock_accounts accounts;
	session_authenticator sessions;
	login_throttler throttler;

	login_fixture()
		: sessions(std::chrono::seconds(60))
	{
		accounts.users.emplace("alice", std::make_pair(std::string("secret"), 0));
		accounts.users.emplace("bob", std::make_pair(std::string("readonly"), 1));
	}

	login make_login()
	{
		return login(
			"/login", valid_template, accounts, sessions, throttler, "/index.html", {&g_full, &g_ro}
		);
	}

	// BOOST_FIXTURE_TEST_CASE generates a class that inherits from
	// login_fixture; friendship is not inherited, so test bodies
	// cannot call l.try_login() directly. This delegation method
	// runs in the fixture's own scope, where the friend declaration
	// in login.hpp does grant access.
	auto try_login(login const& l, http::request<http::string_body> const& req) const
	{
		return l.try_login(req);
	}
};

BOOST_FIXTURE_TEST_CASE(try_login_method_not_allowed, login_fixture)
{
	auto l = make_login();

	http::request<http::string_body> get{http::verb::get, "/login", 11};
	auto r = try_login(l, get);
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::method_not_allowed));
}

BOOST_FIXTURE_TEST_CASE(try_login_oversize_body, login_fixture)
{
	auto l = make_login();

	std::string huge(5000, 'x');
	auto r = try_login(l, make_post(std::move(huge)));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::payload_too_large));
}

BOOST_FIXTURE_TEST_CASE(try_login_missing_csrf_cookie, login_fixture)
{
	// Form has csrf, but no Cookie header. Forbidden.
	auto l = make_login();
	auto r = try_login(l, make_post("csrf=tok&username=alice&password=secret"));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::forbidden));
}

BOOST_FIXTURE_TEST_CASE(try_login_missing_csrf_form, login_fixture)
{
	// Cookie has csrf, but form does not. Forbidden.
	auto l = make_login();
	auto r = try_login(l, make_post("username=alice&password=secret", "csrf=tok"));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::forbidden));
}

BOOST_FIXTURE_TEST_CASE(try_login_csrf_mismatch, login_fixture)
{
	// Cookie csrf and form csrf do not match. Forbidden.
	auto l = make_login();
	auto r = try_login(l, make_post("csrf=other&username=alice&password=secret", "csrf=tok"));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::forbidden));
}

BOOST_FIXTURE_TEST_CASE(try_login_empty_csrf_rejected, login_fixture)
{
	// Empty cookie and empty form field "match" string-wise; the
	// implementation must still reject because the pair carries no
	// actual proof of intent.
	auto l = make_login();
	auto r = try_login(l, make_post("csrf=&username=alice&password=secret", "csrf="));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::forbidden));
}

BOOST_FIXTURE_TEST_CASE(try_login_csrf_among_other_cookies, login_fixture)
{
	// CSRF check works when the Cookie header carries multiple
	// cookies separated by "; ".
	auto l = make_login();
	auto r = try_login(
		l, make_post("csrf=tok&username=alice&password=secret", "theme=dark; csrf=tok; lang=en")
	);
	BOOST_TEST(std::holds_alternative<int>(r));
	BOOST_TEST(std::get<int>(r) == 0);
}

BOOST_FIXTURE_TEST_CASE(try_login_missing_field, login_fixture)
{
	auto l = make_login();
	auto r = try_login(l, make_authed_post("username=alice"));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::bad_request));
}

BOOST_FIXTURE_TEST_CASE(try_login_unknown_user, login_fixture)
{
	auto l = make_login();
	auto r = try_login(l, make_authed_post("username=mallory&password=whatever"));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::unauthorized));
}

BOOST_FIXTURE_TEST_CASE(try_login_wrong_password, login_fixture)
{
	auto l = make_login();
	auto r = try_login(l, make_authed_post("username=alice&password=wrong"));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::unauthorized));
}

BOOST_FIXTURE_TEST_CASE(try_login_success_full, login_fixture)
{
	auto l = make_login();
	auto r = try_login(l, make_authed_post("username=alice&password=secret"));
	BOOST_TEST(std::holds_alternative<int>(r));
	BOOST_TEST(std::get<int>(r) == 0);
}

BOOST_FIXTURE_TEST_CASE(try_login_success_readonly, login_fixture)
{
	auto l = make_login();
	auto r = try_login(l, make_authed_post("username=bob&password=readonly"));
	BOOST_TEST(std::holds_alternative<int>(r));
	BOOST_TEST(std::get<int>(r) == 1);
}

BOOST_FIXTURE_TEST_CASE(try_login_out_of_range_group, login_fixture)
{
	// A user_account that returns a group beyond the configured groups
	// vector should produce 401, not crash, not 500.
	accounts.users.emplace("ghost", std::make_pair(std::string("boo"), 99));
	auto l = make_login();
	auto r = try_login(l, make_authed_post("username=ghost&password=boo"));
	BOOST_TEST(int(std::get<http::status>(r)) == int(http::status::unauthorized));
}

BOOST_FIXTURE_TEST_CASE(try_login_form_url_encoded_password, login_fixture)
{
	// Verify that percent-encoded values round-trip through parse_form
	// into the credential check.
	accounts.users.emplace("carol", std::make_pair(std::string("p@ss word"), 0));
	auto l = make_login();
	auto r = try_login(l, make_authed_post("username=carol&password=p%40ss+word"));
	BOOST_TEST(std::holds_alternative<int>(r));
}

BOOST_FIXTURE_TEST_CASE(success_does_not_mint_session_in_try_login, login_fixture)
{
	// try_login MUST NOT mint a session - that is handle_http's job.
	auto l = make_login();

	BOOST_TEST(sessions.size() == std::size_t(0));
	auto r = try_login(l, make_authed_post("username=alice&password=secret"));
	BOOST_TEST(std::holds_alternative<int>(r));
	BOOST_TEST(sessions.size() == std::size_t(0));
}
