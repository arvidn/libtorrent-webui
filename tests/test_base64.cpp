/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE base64
#include <boost/test/included/unit_test.hpp>

#include "base64.hpp"
#include <libtorrent/config.hpp>
#include <libtorrent/string_view.hpp>

BOOST_AUTO_TEST_CASE(encode_decode)
{
	using namespace lt::literals;

	std::pair<lt::string_view, lt::string_view> test_vectors[] = {
		{""_sv,""_sv},
		{"f"_sv,"Zg=="_sv},
		{"fo"_sv,"Zm8="_sv},
		{"foo"_sv,"Zm9v"_sv},
		{"foob"_sv,"Zm9vYg=="_sv},
		{"fooba"_sv,"Zm9vYmE="_sv},
		{"foobar"_sv,"Zm9vYmFy"_sv},
	};

	for (auto const& [input, output] : test_vectors)
	{
		BOOST_TEST(ltweb::base64decode(std::string(output)) == input);
	}
}
