/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE utils
#include <boost/test/included/unit_test.hpp>

#include "utils.hpp"

#include <string>
#include <string_view>

using namespace ltweb;
using namespace std::string_view_literals;

BOOST_AUTO_TEST_SUITE(is_whitespace)

BOOST_AUTO_TEST_CASE(space_and_tab)
{
	BOOST_TEST(ltweb::is_whitespace(' '));
	BOOST_TEST(ltweb::is_whitespace('\t'));
}

BOOST_AUTO_TEST_CASE(non_whitespace)
{
	BOOST_TEST(!ltweb::is_whitespace('a'));
	BOOST_TEST(!ltweb::is_whitespace('\n'));
	BOOST_TEST(!ltweb::is_whitespace('\r'));
	BOOST_TEST(!ltweb::is_whitespace('\0'));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(trim)

BOOST_AUTO_TEST_CASE(no_whitespace)
{
	BOOST_TEST(ltweb::trim("hello"sv) == "hello");
}

BOOST_AUTO_TEST_CASE(leading)
{
	BOOST_TEST(ltweb::trim("   hello"sv) == "hello");
}

BOOST_AUTO_TEST_CASE(trailing)
{
	BOOST_TEST(ltweb::trim("hello   "sv) == "hello");
}

BOOST_AUTO_TEST_CASE(both_sides)
{
	BOOST_TEST(ltweb::trim("  hello world  "sv) == "hello world");
}

BOOST_AUTO_TEST_CASE(tabs)
{
	BOOST_TEST(ltweb::trim("\thello\t"sv) == "hello");
	BOOST_TEST(ltweb::trim(" \t hello \t "sv) == "hello");
}

BOOST_AUTO_TEST_CASE(all_whitespace)
{
	BOOST_TEST(ltweb::trim("   "sv) == "");
}

BOOST_AUTO_TEST_CASE(empty)
{
	BOOST_TEST(ltweb::trim(""sv) == "");
}

BOOST_AUTO_TEST_CASE(internal_whitespace_preserved)
{
	BOOST_TEST(ltweb::trim("  a  b  "sv) == "a  b");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(extension)

BOOST_AUTO_TEST_CASE(normal)
{
	BOOST_TEST(ltweb::extension("file.txt"sv) == ".txt");
}

BOOST_AUTO_TEST_CASE(multiple_dots)
{
	BOOST_TEST(ltweb::extension("archive.tar.gz"sv) == ".gz");
}

BOOST_AUTO_TEST_CASE(no_extension)
{
	BOOST_TEST(ltweb::extension("README"sv) == "");
}

BOOST_AUTO_TEST_CASE(dot_at_end)
{
	BOOST_TEST(ltweb::extension("file."sv) == ".");
}

BOOST_AUTO_TEST_CASE(dot_file)
{
	BOOST_TEST(ltweb::extension(".hidden"sv) == ".hidden");
}

BOOST_AUTO_TEST_CASE(with_path)
{
	BOOST_TEST(ltweb::extension("path/to/file.cpp"sv) == ".cpp");
}

BOOST_AUTO_TEST_CASE(empty)
{
	BOOST_TEST(ltweb::extension(""sv) == "");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(split)

BOOST_AUTO_TEST_CASE(basic)
{
	auto [a, b] = ltweb::split("user:pass"sv, ':');
	BOOST_TEST(a == "user");
	BOOST_TEST(b == "pass");
}

BOOST_AUTO_TEST_CASE(first_delimiter_only)
{
	auto [a, b] = ltweb::split("a:b:c"sv, ':');
	BOOST_TEST(a == "a");
	BOOST_TEST(b == "b:c");
}

BOOST_AUTO_TEST_CASE(delimiter_absent)
{
	auto [a, b] = ltweb::split("nodot"sv, '.');
	BOOST_TEST(a == "nodot");
	BOOST_TEST(b == "");
}

BOOST_AUTO_TEST_CASE(delimiter_at_start)
{
	auto [a, b] = ltweb::split(":value"sv, ':');
	BOOST_TEST(a == "");
	BOOST_TEST(b == "value");
}

BOOST_AUTO_TEST_CASE(delimiter_at_end)
{
	auto [a, b] = ltweb::split("key:"sv, ':');
	BOOST_TEST(a == "key");
	BOOST_TEST(b == "");
}

BOOST_AUTO_TEST_CASE(empty)
{
	auto [a, b] = ltweb::split(""sv, ':');
	BOOST_TEST(a == "");
	BOOST_TEST(b == "");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(parse_quoted_string)

BOOST_AUTO_TEST_CASE(basic_value)
{
	auto r = ltweb::parse_quoted_string("\"hello\""sv);
	BOOST_REQUIRE(r.has_value());
	BOOST_TEST(*r == "hello");
}

BOOST_AUTO_TEST_CASE(empty_quoted)
{
	auto r = ltweb::parse_quoted_string("\"\""sv);
	BOOST_REQUIRE(r.has_value());
	BOOST_TEST(*r == "");
}

BOOST_AUTO_TEST_CASE(escaped_backslash)
{
	auto r = ltweb::parse_quoted_string("\"a\\\\b\""sv);
	BOOST_REQUIRE(r.has_value());
	BOOST_TEST(*r == "a\\b");
}

BOOST_AUTO_TEST_CASE(escaped_quote)
{
	auto r = ltweb::parse_quoted_string("\"fo\\\"o\""sv);
	BOOST_REQUIRE(r.has_value());
	BOOST_TEST(*r == "fo\"o");
}

BOOST_AUTO_TEST_CASE(trailing_ows)
{
	auto r = ltweb::parse_quoted_string("\"X\"  "sv);
	BOOST_REQUIRE(r.has_value());
	BOOST_TEST(*r == "X");
}

BOOST_AUTO_TEST_CASE(no_opening_quote)
{
	BOOST_TEST(!ltweb::parse_quoted_string("hello"sv).has_value());
}

BOOST_AUTO_TEST_CASE(unterminated)
{
	BOOST_TEST(!ltweb::parse_quoted_string("\"foo"sv).has_value());
}

BOOST_AUTO_TEST_CASE(trailing_backslash)
{
	BOOST_TEST(!ltweb::parse_quoted_string("\"X\\"sv).has_value());
}

BOOST_AUTO_TEST_CASE(junk_after_closing_quote)
{
	BOOST_TEST(!ltweb::parse_quoted_string("\"X\"junk"sv).has_value());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(ci_find)

BOOST_AUTO_TEST_CASE(exact_match)
{
	BOOST_TEST(ltweb::ci_find("multipart/form-data"sv, "multipart/form-data"sv) == 0);
}

BOOST_AUTO_TEST_CASE(haystack_mixed_case)
{
	BOOST_TEST(ltweb::ci_find("Multipart/Form-Data"sv, "multipart/form-data"sv) == 0);
}

BOOST_AUTO_TEST_CASE(needle_mixed_case)
{
	BOOST_TEST(ltweb::ci_find("multipart/form-data"sv, "Multipart/Form-Data"sv) == 0);
}

BOOST_AUTO_TEST_CASE(match_in_middle)
{
	BOOST_TEST(ltweb::ci_find("multipart/form-data; boundary=X"sv, "boundary="sv) == 21);
}

BOOST_AUTO_TEST_CASE(match_at_end)
{
	BOOST_TEST(ltweb::ci_find("foo BOUNDARY="sv, "boundary="sv) == 4);
}

BOOST_AUTO_TEST_CASE(uppercase_haystack)
{
	BOOST_TEST(ltweb::ci_find("MULTIPART/FORM-DATA; BOUNDARY=X"sv, "boundary="sv) == 21);
}

BOOST_AUTO_TEST_CASE(needle_not_present)
{
	BOOST_TEST(ltweb::ci_find("multipart/form-data"sv, "boundary="sv) == std::string_view::npos);
}

BOOST_AUTO_TEST_CASE(haystack_too_short)
{
	BOOST_TEST(ltweb::ci_find("abc"sv, "abcd"sv) == std::string_view::npos);
}

BOOST_AUTO_TEST_CASE(empty_haystack)
{
	BOOST_TEST(ltweb::ci_find(""sv, "x"sv) == std::string_view::npos);
}

BOOST_AUTO_TEST_CASE(empty_needle)
{
	BOOST_TEST(ltweb::ci_find("hello"sv, ""sv) == 0);
}

BOOST_AUTO_TEST_CASE(both_empty)
{
	// hay.begin() == hay.end(), so the not-found check triggers -> npos
	BOOST_TEST(ltweb::ci_find(""sv, ""sv) == std::string_view::npos);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(str)

BOOST_AUTO_TEST_CASE(single_string)
{
	BOOST_TEST(ltweb::str("hello") == "hello");
}

BOOST_AUTO_TEST_CASE(integer_concat)
{
	BOOST_TEST(ltweb::str("port=", 8080) == "port=8080");
}

BOOST_AUTO_TEST_CASE(multiple_mixed)
{
	BOOST_TEST(ltweb::str("x=", 1, " y=", 2) == "x=1 y=2");
}

BOOST_AUTO_TEST_CASE(no_arguments)
{
	BOOST_TEST(ltweb::str() == "");
}

BOOST_AUTO_TEST_CASE(float_point)
{
	BOOST_TEST(ltweb::str("v=", 3.14) == "v=3.14");
}

BOOST_AUTO_TEST_SUITE_END()
