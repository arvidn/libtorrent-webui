/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PERCENT_ENCODE_HPP
#define LTWEB_PERCENT_ENCODE_HPP

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace ltweb {

// Percent-encode a string for use in a Content-Disposition filename parameter.
// Characters in the unreserved set (RFC 3986) are passed through unchanged;
// everything else is encoded as %XX using uppercase hex digits.
inline std::string percent_encode(std::string_view s)
{
	using namespace std::literals;

	static std::string_view const safe =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~"sv;
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s)
	{
		if (safe.find(c) != std::string_view::npos)
			out += static_cast<char>(c);
		else
		{
			char buf[4];
			std::snprintf(buf, sizeof(buf), "%%%02X", c);
			out += buf;
		}
	}
	return out;
}

} // namespace ltweb

#endif // LTWEB_PERCENT_ENCODE_HPP
