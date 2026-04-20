/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE find_longest_prefix
#include <boost/test/included/unit_test.hpp>

#include "webui.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

using table_t = std::vector<std::pair<std::string, int>>;

// Return the value at the best match, or -1 if no match.
int lookup(table_t const& t, std::string const& path)
{
	auto it = ltweb::aux::find_longest_prefix(t, path);
	return it == t.end() ? -1 : it->second;
}

} // anonymous namespace

// Empty container always returns end().
BOOST_AUTO_TEST_CASE(empty_container)
{
	table_t t;
	BOOST_TEST((ltweb::aux::find_longest_prefix(t, "/foo") == t.end()));
}

// Single entry whose key matches the whole path.
BOOST_AUTO_TEST_CASE(exact_match)
{
	table_t t = {{"/gui", 1}};
	BOOST_TEST(lookup(t, "/gui") == 1);
}

// Single entry whose key is a strict prefix of the path.
BOOST_AUTO_TEST_CASE(prefix_match)
{
	table_t t = {{"/download", 1}};
	BOOST_TEST(lookup(t, "/download/abc/0") == 1);
}

// Path that matches no entry returns end().
BOOST_AUTO_TEST_CASE(no_match)
{
	table_t t = {{"/gui", 1}, {"/download", 2}};
	BOOST_TEST(lookup(t, "/other") == -1);
}

// When multiple entries are prefixes, the longest one wins.
BOOST_AUTO_TEST_CASE(longest_wins)
{
	table_t t = {{"/", 1}, {"/download", 2}, {"/download/extra", 3}};
	BOOST_TEST(lookup(t, "/download/extra/file") == 3);
	BOOST_TEST(lookup(t, "/download/other") == 2);
	BOOST_TEST(lookup(t, "/gui") == 1);
}

// A key that is longer than the path cannot be a prefix.
BOOST_AUTO_TEST_CASE(key_longer_than_path)
{
	table_t t = {{"/download/very/long/path", 1}, {"/download", 2}};
	BOOST_TEST(lookup(t, "/download") == 2);
}

// A root-only handler ("/") matches any path.
BOOST_AUTO_TEST_CASE(root_prefix_matches_everything)
{
	table_t t = {{"/", 42}};
	BOOST_TEST(lookup(t, "/anything/at/all") == 42);
}

// Order of entries in the container does not affect the result.
BOOST_AUTO_TEST_CASE(order_independent)
{
	table_t forward = {{"/", 1}, {"/a", 2}, {"/a/b", 3}};
	table_t reversed = {{"/a/b", 3}, {"/a", 2}, {"/", 1}};
	BOOST_TEST(lookup(forward, "/a/b/c") == lookup(reversed, "/a/b/c"));
	BOOST_TEST(lookup(forward, "/a/x") == lookup(reversed, "/a/x"));
	BOOST_TEST(lookup(forward, "/z") == lookup(reversed, "/z"));
}
