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

#include "mime_part.hpp"
#include "test.hpp"

#include <string>
#include <string_view>

int main_ret = 0;

namespace {

// Call ltweb::parse_mime_part on a std::string_view for convenience.
char const* parse(std::string_view part, std::string& ct)
{
	return ltweb::parse_mime_part(part.data(), part.data() + part.size(), ct);
}

} // anonymous namespace

int main()
{
	// Basic: single Content-Type header, body contains binary-ish data
	{
		std::string_view part =
			"Content-Disposition: form-data; name=\"file\"\r\n"
			"Content-Type: application/x-bittorrent\r\n"
			"\r\n"
			"BODY_DATA";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body != nullptr);
		TEST_CHECK(ct == "application/x-bittorrent");
		TEST_CHECK(std::string_view(body) == "BODY_DATA");
	}

	// octet-stream content type
	{
		std::string_view part =
			"Content-Type: application/octet-stream\r\n"
			"\r\n"
			"\x01\x02\x03";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body != nullptr);
		TEST_CHECK(ct == "application/octet-stream");
	}

	// Content-Type header is case-insensitive (mixed case)
	{
		std::string_view part =
			"CONTENT-TYPE: application/x-bittorrent\r\n"
			"\r\n"
			"data";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body != nullptr);
		TEST_CHECK(ct == "application/x-bittorrent");
	}

	// Leading whitespace after the colon is stripped
	{
		std::string_view part =
			"Content-Type:   text/plain\r\n"
			"\r\n"
			"hello";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body != nullptr);
		TEST_CHECK(ct == "text/plain");
	}

	// No Content-Type header: body is still found, ct left empty
	{
		std::string_view part =
			"Content-Disposition: form-data; name=\"field\"\r\n"
			"\r\n"
			"value";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body != nullptr);
		TEST_CHECK(ct.empty());
		TEST_CHECK(std::string_view(body) == "value");
	}

	// Empty body (blank line only, nothing after it)
	{
		std::string_view part =
			"Content-Type: application/octet-stream\r\n"
			"\r\n";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body != nullptr);
		TEST_CHECK(ct == "application/octet-stream");
		// body points one past the end of the input — size is zero
		TEST_CHECK(body == part.data() + part.size());
	}

	// Malformed: no blank line separator — returns nullptr
	{
		std::string_view part =
			"Content-Type: application/x-bittorrent\r\n"
			"no-blank-line-here";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body == nullptr);
	}

	// Completely empty input — returns nullptr
	{
		std::string ct;
		char const* body = parse("", ct);
		TEST_CHECK(body == nullptr);
	}

	// Multiple headers: Content-Type is not first
	{
		std::string_view part =
			"Content-Disposition: form-data; name=\"torrent\"\r\n"
			"Content-Transfer-Encoding: binary\r\n"
			"Content-Type: application/x-bittorrent\r\n"
			"\r\n"
			"TORRENT";

		std::string ct;
		char const* body = parse(part, ct);
		TEST_CHECK(body != nullptr);
		TEST_CHECK(ct == "application/x-bittorrent");
		TEST_CHECK(std::string_view(body) == "TORRENT");
	}

	return main_ret;
}
