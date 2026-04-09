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

#include "percent_encode.hpp"
#include "test.hpp"

int main_ret = 0;

int main()
{
	// unreserved characters pass through unchanged
	TEST_CHECK(ltweb::percent_encode("abcABC123") == "abcABC123");
	TEST_CHECK(ltweb::percent_encode("-_.~") == "-_.~");

	// empty string
	TEST_CHECK(ltweb::percent_encode("") == "");

	// space → %20
	TEST_CHECK(ltweb::percent_encode("hello world") == "hello%20world");

	// slash and backslash
	TEST_CHECK(ltweb::percent_encode("a/b") == "a%2Fb");
	TEST_CHECK(ltweb::percent_encode("a\\b") == "a%5Cb");

	// characters that commonly appear in filenames
	TEST_CHECK(ltweb::percent_encode("file (1).torrent") == "file%20%281%29.torrent");
	TEST_CHECK(ltweb::percent_encode("résumé.torrent") == "r%C3%A9sum%C3%A9.torrent");

	// all bytes in [0x00, 0x1f] and 0x7f are encoded
	TEST_CHECK(ltweb::percent_encode(std::string(1, '\0')) == "%00");
	TEST_CHECK(ltweb::percent_encode(std::string(1, '\x1f')) == "%1F");
	TEST_CHECK(ltweb::percent_encode(std::string(1, '\x7f')) == "%7F");

	// high bytes (>= 0x80) are encoded
	TEST_CHECK(ltweb::percent_encode(std::string(1, '\x80')) == "%80");
	TEST_CHECK(ltweb::percent_encode(std::string(1, '\xff')) == "%FF");

	// hex digits are uppercase
	TEST_CHECK(ltweb::percent_encode(std::string(1, '\xab')) == "%AB");

	// dot is unreserved — must NOT be encoded (relevant for extensions)
	TEST_CHECK(ltweb::percent_encode("file.torrent") == "file.torrent");

	return main_ret;
}
