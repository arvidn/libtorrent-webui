/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_URL_DECODE_HPP
#define LTWEB_URL_DECODE_HPP

#include <cstdlib>
#include <string>
#include <boost/system/error_code.hpp>
#include <boost/system/errc.hpp>

namespace ltweb {

// Decode a percent-encoded URL string (application/x-www-form-urlencoded).
// '+' is decoded as space; %XX sequences are decoded as bytes.
// Sets ec and returns an empty string on a malformed %XX sequence.
inline std::string url_decode(std::string const& s, boost::system::error_code& ec)
{
	std::string out;
	out.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '+') {
			out += ' ';
		} else if (s[i] == '%' && i + 2 < s.size()) {
			char buf[3] = {s[i + 1], s[i + 2], '\0'};
			char* end = nullptr;
			long const val = std::strtol(buf, &end, 16);
			if (end != buf + 2) {
				ec = boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
				return {};
			}
			out += static_cast<char>(val);
			i += 2;
		} else {
			out += s[i];
		}
	}
	return out;
}

} // namespace ltweb

#endif // LTWEB_URL_DECODE_HPP
