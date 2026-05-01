/*

Copyright (c) 2012-2013, 2015, 2017, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string>
#include <string.h>
#include <stdio.h>
#include <boost/cstdint.hpp>
#include <iconv.h>
#include <vector>
#include <mutex>

#include "escape_json.hpp"
#include "libtorrent/assert.hpp"

namespace ltweb {

std::string escape_json(lt::string_view input)
{
	if (input.empty()) return "";
	std::string ret;
	for (auto const s : input) {
		// char may be signed or unsigned, we don't know. So cast it before the
		// comparison to make sure it's always correct
		if (static_cast<unsigned char>(s) > 0x1f && static_cast<unsigned char>(s) < 0x80 && s != '"'
			&& s != '\\') {
			ret += s;
		} else {
			ret += '\\';
			switch (s) {
				case '"':
					ret += '"';
					break;
				case '\\':
					ret += '\\';
					break;
				case '\n':
					ret += '\n';
					break;
				default: {
					char buf[20];
					snprintf(buf, sizeof(buf), "u%04x", std::uint16_t(s));
					ret += buf;
				}
			}
		}
	}
	return ret;
}

} // namespace ltweb
