/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE percent_encode
#include <boost/test/included/unit_test.hpp>

#include "percent_encode.hpp"

BOOST_AUTO_TEST_CASE(unreserved)
{
	// unreserved characters pass through unchanged
	BOOST_TEST(ltweb::percent_encode("abcABC123") == "abcABC123");
	BOOST_TEST(ltweb::percent_encode("-_.~") == "-_.~");

	// empty string
	BOOST_TEST(ltweb::percent_encode("") == "");

	// dot is unreserved -- must NOT be encoded (relevant for extensions)
	BOOST_TEST(ltweb::percent_encode("file.torrent") == "file.torrent");
}

BOOST_AUTO_TEST_CASE(reserved_chars)
{
	// space -> %20
	BOOST_TEST(ltweb::percent_encode("hello world") == "hello%20world");

	// slash and backslash
	BOOST_TEST(ltweb::percent_encode("a/b") == "a%2Fb");
	BOOST_TEST(ltweb::percent_encode("a\\b") == "a%5Cb");

	// characters that commonly appear in filenames
	BOOST_TEST(ltweb::percent_encode("file (1).torrent") == "file%20%281%29.torrent");
	BOOST_TEST(ltweb::percent_encode("résumé.torrent") == "r%C3%A9sum%C3%A9.torrent");
}

BOOST_AUTO_TEST_CASE(byte_encoding)
{
	// all bytes in [0x00, 0x1f] and 0x7f are encoded
	BOOST_TEST(ltweb::percent_encode(std::string(1, '\0')) == "%00");
	BOOST_TEST(ltweb::percent_encode(std::string(1, '\x1f')) == "%1F");
	BOOST_TEST(ltweb::percent_encode(std::string(1, '\x7f')) == "%7F");

	// high bytes (>= 0x80) are encoded
	BOOST_TEST(ltweb::percent_encode(std::string(1, '\x80')) == "%80");
	BOOST_TEST(ltweb::percent_encode(std::string(1, '\xff')) == "%FF");
}

BOOST_AUTO_TEST_CASE(uppercase_hex)
{
	BOOST_TEST(ltweb::percent_encode(std::string(1, '\xab')) == "%AB");
}
