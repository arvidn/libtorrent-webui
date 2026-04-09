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

#include "url_decode.hpp"
#include "test.hpp"

#include <boost/system/error_code.hpp>

int main_ret = 0;

namespace {

std::string decode(std::string const& s)
{
	boost::system::error_code ec;
	return ltweb::url_decode(s, ec);
}

bool decode_fails(std::string const& s)
{
	boost::system::error_code ec;
	ltweb::url_decode(s, ec);
	return !!ec;
}

} // anonymous namespace

int main()
{
	// plain text passes through unchanged
	TEST_CHECK(decode("hello") == "hello");

	// empty string
	TEST_CHECK(decode("") == "");

	// '+' decoded as space
	TEST_CHECK(decode("hello+world") == "hello world");

	// multiple '+' in a row
	TEST_CHECK(decode("a+++b") == "a   b");

	// %20 — space via percent-encoding
	TEST_CHECK(decode("hello%20world") == "hello world");

	// lowercase hex digits are accepted
	TEST_CHECK(decode("%2f") == "/");
	TEST_CHECK(decode("%2F") == "/");

	// %00 — null byte
	TEST_CHECK(decode("%00") == std::string(1, '\0'));

	// %FF — high byte
	TEST_CHECK(decode("%FF") == std::string(1, '\xff'));

	// round-trip with percent_encode output (uppercase hex)
	TEST_CHECK(decode("%2Fb%5Cc") == "/b\\c");

	// sequence at end of string
	TEST_CHECK(decode("end%21") == "end!");

	// sequence at start of string
	TEST_CHECK(decode("%21start") == "!start");

	// multiple sequences
	TEST_CHECK(decode("%61%62%63") == "abc");

	// mix of plain, '+', and %XX
	TEST_CHECK(decode("a+b%3Dc") == "a b=c");

	// truncated sequence at end — '%' with only one char left: no decode, treat as literal
	// (the condition requires i+2 < s.size(), so a lone '%' at end passes through)
	// TODO: should these be errors instead?
	TEST_CHECK(decode("x%2") == "x%2");
	TEST_CHECK(decode("x%") == "x%");

	// malformed %XX (non-hex chars) sets ec and returns empty
	TEST_CHECK(decode_fails("%GG"));
	TEST_CHECK(decode_fails("%0Z"));

	return main_ret;
}
