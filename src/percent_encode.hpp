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

#ifndef TORRENT_PERCENT_ENCODE_HPP
#define TORRENT_PERCENT_ENCODE_HPP

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

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

#endif // TORRENT_PERCENT_ENCODE_HPP
