/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE path_matches_exact
#include <boost/test/included/unit_test.hpp>

#include "webui.hpp"

using ltweb::aux::path_matches_exact;

// The path equals the prefix exactly.
BOOST_AUTO_TEST_CASE(exact_match) { BOOST_TEST(path_matches_exact("/logout", "/logout")); }

// A sub-path under the prefix must not match. This is the case that
// motivated the helper: /logout/foo should not destroy the session.
BOOST_AUTO_TEST_CASE(sub_path_rejected)
{
	BOOST_TEST(!path_matches_exact("/logout/", "/logout"));
	BOOST_TEST(!path_matches_exact("/logout/foo", "/logout"));
	BOOST_TEST(!path_matches_exact("/logout/foo/bar", "/logout"));
}

// A path that starts with the prefix but is not a path-component boundary
// must not match either. /logoutx is a different endpoint from /logout.
BOOST_AUTO_TEST_CASE(prefix_but_not_path_rejected)
{
	BOOST_TEST(!path_matches_exact("/logoutx", "/logout"));
	BOOST_TEST(!path_matches_exact("/logout2", "/logout"));
}

// A query string after the exact path is allowed; the path portion is
// everything up to the first '?'.
BOOST_AUTO_TEST_CASE(query_string_allowed)
{
	BOOST_TEST(path_matches_exact("/logout?", "/logout"));
	BOOST_TEST(path_matches_exact("/logout?reason=expired", "/logout"));
	BOOST_TEST(path_matches_exact("/logout?a=1&b=2", "/logout"));
}

// A query string does not rescue a non-matching path.
BOOST_AUTO_TEST_CASE(query_string_on_subpath_rejected)
{
	BOOST_TEST(!path_matches_exact("/logout/foo?bar=1", "/logout"));
	BOOST_TEST(!path_matches_exact("/logoutx?y=1", "/logout"));
}

// A '#' fragment never reaches the server in a real HTTP request, but
// for the purposes of this helper it is part of the path - we only
// split on '?'. Document the behavior so any future change is deliberate.
BOOST_AUTO_TEST_CASE(fragment_treated_as_path)
{
	BOOST_TEST(!path_matches_exact("/logout#frag", "/logout"));
}

// Empty target never matches a non-empty prefix.
BOOST_AUTO_TEST_CASE(empty_target) { BOOST_TEST(!path_matches_exact("", "/logout")); }

// A bare query string ("?foo") has an empty path portion, so it only
// matches an empty prefix.
BOOST_AUTO_TEST_CASE(bare_query_string)
{
	BOOST_TEST(path_matches_exact("?foo=1", ""));
	BOOST_TEST(!path_matches_exact("?foo=1", "/logout"));
}

// Trailing slash on the target is a different path from the bare
// prefix; this is how the dispatcher distinguishes /login from /login/.
BOOST_AUTO_TEST_CASE(trailing_slash_distinct)
{
	BOOST_TEST(!path_matches_exact("/login/", "/login"));
	BOOST_TEST(path_matches_exact("/login/", "/login/"));
}
