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
