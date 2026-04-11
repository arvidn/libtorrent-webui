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
