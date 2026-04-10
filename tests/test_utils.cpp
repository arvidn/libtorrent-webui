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

#include "utils.hpp"
#include "test.hpp"

#include <string>
#include <string_view>

int main_ret = 0;

int main()
{
	using namespace ltweb;
	using namespace std::string_view_literals;

	// is_whitespace()
	TEST_CHECK(is_whitespace(' '));
	TEST_CHECK(is_whitespace('\t'));
	TEST_CHECK(!is_whitespace('a'));
	TEST_CHECK(!is_whitespace('\n'));
	TEST_CHECK(!is_whitespace('\r'));
	TEST_CHECK(!is_whitespace('\0'));

	// trim()

	// no whitespace
	TEST_CHECK(trim("hello"sv) == "hello");

	// leading spaces
	TEST_CHECK(trim("   hello"sv) == "hello");

	// trailing spaces
	TEST_CHECK(trim("hello   "sv) == "hello");

	// both sides
	TEST_CHECK(trim("  hello world  "sv) == "hello world");

	// tabs are whitespace
	TEST_CHECK(trim("\thello\t"sv) == "hello");

	// mixed spaces and tabs
	TEST_CHECK(trim(" \t hello \t "sv) == "hello");

	// all whitespace -> empty
	TEST_CHECK(trim("   "sv) == "");

	// empty input -> empty
	TEST_CHECK(trim(""sv) == "");

	// internal whitespace is preserved
	TEST_CHECK(trim("  a  b  "sv) == "a  b");

	// extension()

	// normal file extension
	TEST_CHECK(extension("file.txt"sv) == ".txt");

	// longer extension
	TEST_CHECK(extension("archive.tar.gz"sv) == ".gz");

	// no extension -> empty
	TEST_CHECK(extension("README"sv) == "");

	// dot at the very end -> extension is "."
	TEST_CHECK(extension("file."sv) == ".");

	// dot-file (Unix hidden file) with no further extension
	TEST_CHECK(extension(".hidden"sv) == ".hidden");

	// path with directory separators
	TEST_CHECK(extension("path/to/file.cpp"sv) == ".cpp");

	// empty input -> empty
	TEST_CHECK(extension(""sv) == "");

	// split()

	// basic split
	{
		auto [a, b] = split("user:pass"sv, ':');
		TEST_CHECK(a == "user");
		TEST_CHECK(b == "pass");
	}

	// only first delimiter is used; remainder stays in second part
	{
		auto [a, b] = split("a:b:c"sv, ':');
		TEST_CHECK(a == "a");
		TEST_CHECK(b == "b:c");
	}

	// delimiter not present -> second part is empty
	{
		auto [a, b] = split("nodot"sv, '.');
		TEST_CHECK(a == "nodot");
		TEST_CHECK(b == "");
	}

	// delimiter at the start -> first part is empty
	{
		auto [a, b] = split(":value"sv, ':');
		TEST_CHECK(a == "");
		TEST_CHECK(b == "value");
	}

	// delimiter at the end -> second part is empty
	{
		auto [a, b] = split("key:"sv, ':');
		TEST_CHECK(a == "key");
		TEST_CHECK(b == "");
	}

	// empty input -> both parts empty
	{
		auto [a, b] = split(""sv, ':');
		TEST_CHECK(a == "");
		TEST_CHECK(b == "");
	}

	// parse_quoted_string()

	// basic value
	TEST_CHECK(parse_quoted_string("\"hello\""sv) == "hello");

	// empty quoted string
	TEST_CHECK(parse_quoted_string("\"\""sv) == "");

	// escaped backslash: \\ -> single backslash
	TEST_CHECK(parse_quoted_string("\"a\\\\b\""sv) == "a\\b");

	// escaped quote: \" -> "
	TEST_CHECK(parse_quoted_string("\"fo\\\"o\""sv) == "fo\"o");

	// OWS after closing quote is allowed
	TEST_CHECK(parse_quoted_string("\"X\"  "sv) == "X");

	// not a quoted string (no opening quote)
	TEST_CHECK(parse_quoted_string("hello"sv) == std::nullopt);

	// unterminated: no closing quote
	TEST_CHECK(parse_quoted_string("\"foo"sv) == std::nullopt);

	// trailing backslash: no char to escape
	TEST_CHECK(parse_quoted_string("\"X\\"sv) == std::nullopt);

	// junk after closing quote
	TEST_CHECK(parse_quoted_string("\"X\"junk"sv) == std::nullopt);

	// ci_find()

	// exact match
	TEST_CHECK(ci_find("multipart/form-data"sv, "multipart/form-data"sv) == 0);

	// needle all-lowercase, haystack mixed case
	TEST_CHECK(ci_find("Multipart/Form-Data"sv, "multipart/form-data"sv) == 0);

	// needle mixed case, haystack lowercase
	TEST_CHECK(ci_find("multipart/form-data"sv, "Multipart/Form-Data"sv) == 0);

	// match in the middle
	TEST_CHECK(ci_find("multipart/form-data; boundary=X"sv, "boundary="sv) == 21);

	// match at the end
	TEST_CHECK(ci_find("foo BOUNDARY="sv, "boundary="sv) == 4);

	// case-insensitive match with all-uppercase haystack
	TEST_CHECK(ci_find("MULTIPART/FORM-DATA; BOUNDARY=X"sv, "boundary="sv) == 21);

	// needle not present
	TEST_CHECK(ci_find("multipart/form-data"sv, "boundary="sv) == std::string_view::npos);

	// haystack too short for needle
	TEST_CHECK(ci_find("abc"sv, "abcd"sv) == std::string_view::npos);

	// empty haystack
	TEST_CHECK(ci_find(""sv, "x"sv) == std::string_view::npos);

	// empty needle matches at position 0
	TEST_CHECK(ci_find("hello"sv, ""sv) == 0);

	// both empty: hay.begin() == hay.end(), so the not-found check triggers -> npos
	TEST_CHECK(ci_find(""sv, ""sv) == std::string_view::npos);

	// str()

	// single string argument
	TEST_CHECK(str("hello") == "hello");

	// integer concatenation
	TEST_CHECK(str("port=", 8080) == "port=8080");

	// multiple mixed types
	TEST_CHECK(str("x=", 1, " y=", 2) == "x=1 y=2");

	// no arguments -> empty string
	TEST_CHECK(str() == "");

	// floating point
	TEST_CHECK(str("v=", 3.14) == "v=3.14");

	return main_ret;
}
