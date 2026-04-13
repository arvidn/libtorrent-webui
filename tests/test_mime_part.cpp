/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE mime_part
#include <boost/test/included/unit_test.hpp>

#include "mime_part.hpp"

#include <string>
#include <string_view>

BOOST_AUTO_TEST_CASE(basic)
{
	std::string_view part =
		"Content-Disposition: form-data; name=\"file\"\r\n"
		"Content-Type: application/x-bittorrent\r\n"
		"\r\n"
		"BODY_DATA";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() != nullptr);
	BOOST_TEST(ct == "application/x-bittorrent");
	BOOST_TEST(body == "BODY_DATA");
}

BOOST_AUTO_TEST_CASE(octet_stream)
{
	std::string_view part =
		"Content-Type: application/octet-stream\r\n"
		"\r\n"
		"\x01\x02\x03";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() != nullptr);
	BOOST_TEST(ct == "application/octet-stream");
}

BOOST_AUTO_TEST_CASE(case_insensitive_header)
{
	// Content-Type header name is case-insensitive (mixed case)
	std::string_view part =
		"CONTENT-TYPE: application/x-bittorrent\r\n"
		"\r\n"
		"data";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() != nullptr);
	BOOST_TEST(ct == "application/x-bittorrent");
}

BOOST_AUTO_TEST_CASE(leading_whitespace)
{
	// Leading whitespace after the colon is stripped
	std::string_view part =
		"Content-Type:   text/plain\r\n"
		"\r\n"
		"hello";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() != nullptr);
	BOOST_TEST(ct == "text/plain");
}

BOOST_AUTO_TEST_CASE(no_content_type)
{
	// No Content-Type header: body is still found, ct left empty
	std::string_view part =
		"Content-Disposition: form-data; name=\"field\"\r\n"
		"\r\n"
		"value";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() != nullptr);
	BOOST_TEST(ct.empty());
	BOOST_TEST(body == "value");
}

BOOST_AUTO_TEST_CASE(empty_body)
{
	// Empty body (blank line only, nothing after it)
	std::string_view part =
		"Content-Type: application/octet-stream\r\n"
		"\r\n";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() != nullptr);
	BOOST_TEST(ct == "application/octet-stream");
	BOOST_TEST(body.empty());
}

BOOST_AUTO_TEST_CASE(malformed_no_blank_line)
{
	// Malformed: no blank line separator -- returns null string_view
	std::string_view part =
		"Content-Type: application/x-bittorrent\r\n"
		"no-blank-line-here";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() == nullptr);
}

BOOST_AUTO_TEST_CASE(empty_input)
{
	// Completely empty input -- returns null string_view
	std::string ct;
	std::string_view body = ltweb::parse_mime_part("", ct);
	BOOST_TEST(body.data() == nullptr);
}

BOOST_AUTO_TEST_CASE(multiple_headers)
{
	// Multiple headers: Content-Type is not first
	std::string_view part =
		"Content-Disposition: form-data; name=\"torrent\"\r\n"
		"Content-Transfer-Encoding: binary\r\n"
		"Content-Type: application/x-bittorrent\r\n"
		"\r\n"
		"TORRENT";

	std::string ct;
	std::string_view body = ltweb::parse_mime_part(part, ct);
	BOOST_TEST(body.data() != nullptr);
	BOOST_TEST(ct == "application/x-bittorrent");
	BOOST_TEST(body == "TORRENT");
}
