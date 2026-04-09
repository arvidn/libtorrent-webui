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
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '+')
		{
			out += ' ';
		}
		else if (s[i] == '%' && i + 2 < s.size())
		{
			char buf[3] = { s[i + 1], s[i + 2], '\0' };
			char* end = nullptr;
			long const val = std::strtol(buf, &end, 16);
			if (end != buf + 2)
			{
				ec = boost::system::errc::make_error_code(
					boost::system::errc::invalid_argument);
				return {};
			}
			out += static_cast<char>(val);
			i += 2;
		}
		else
		{
			out += s[i];
		}
	}
	return out;
}

} // namespace ltweb

#endif // LTWEB_URL_DECODE_HPP
