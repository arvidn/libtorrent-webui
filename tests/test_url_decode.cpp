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

#define BOOST_TEST_MODULE url_decode
#include <boost/test/included/unit_test.hpp>

#include "url_decode.hpp"

#include <boost/system/error_code.hpp>

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

BOOST_AUTO_TEST_CASE(plain_text)
{
	// plain text passes through unchanged
	BOOST_TEST(decode("hello") == "hello");

	// empty string
	BOOST_TEST(decode("") == "");
}

BOOST_AUTO_TEST_CASE(plus_sign)
{
	// '+' decoded as space
	BOOST_TEST(decode("hello+world") == "hello world");

	// multiple '+' in a row
	BOOST_TEST(decode("a+++b") == "a   b");
}

BOOST_AUTO_TEST_CASE(percent_sequences)
{
	// %20 -- space via percent-encoding
	BOOST_TEST(decode("hello%20world") == "hello world");

	// lowercase hex digits are accepted
	BOOST_TEST(decode("%2f") == "/");
	BOOST_TEST(decode("%2F") == "/");

	// %00 -- null byte
	BOOST_TEST(decode("%00") == std::string(1, '\0'));

	// %FF -- high byte
	BOOST_TEST(decode("%FF") == std::string(1, '\xff'));

	// round-trip with percent_encode output (uppercase hex)
	BOOST_TEST(decode("%2Fb%5Cc") == "/b\\c");

	// sequence at end of string
	BOOST_TEST(decode("end%21") == "end!");

	// sequence at start of string
	BOOST_TEST(decode("%21start") == "!start");

	// multiple sequences
	BOOST_TEST(decode("%61%62%63") == "abc");

	// mix of plain, '+', and %XX
	BOOST_TEST(decode("a+b%3Dc") == "a b=c");
}

BOOST_AUTO_TEST_CASE(truncated_sequence)
{
	// truncated sequence at end -- '%' with only one char left: no decode, treat as literal
	BOOST_TEST(decode("x%2") == "x%2");
	BOOST_TEST(decode("x%") == "x%");
}

BOOST_AUTO_TEST_CASE(invalid_hex)
{
	// malformed %XX (non-hex chars) sets ec and returns empty
	BOOST_TEST(decode_fails("%GG"));
	BOOST_TEST(decode_fails("%0Z"));
}
