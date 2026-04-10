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

#ifndef LTWEB_MIME_PART_HPP
#define LTWEB_MIME_PART_HPP

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace ltweb {

// Locate the body of a MIME part and extract its Content-Type header value.
// Returns a string_view of the body (immediately after the blank header line
// \r\n\r\n), or a null string_view (data() == nullptr) if the part is malformed.
// content_type is set to the value of the Content-Type header if found.
inline std::string_view parse_mime_part(std::string_view part, std::string& content_type)
{
	// find the blank line separating headers from body
	auto const sep = part.find("\r\n\r\n");
	if (sep == std::string_view::npos) return {};

	std::string_view const headers = part.substr(0, sep);
	std::string_view const body    = part.substr(sep + 4);

	// scan header lines for Content-Type (case-insensitive)
	static constexpr std::string_view ct_name = "content-type:";
	std::string_view remaining = headers;
	while (!remaining.empty())
	{
		auto const eol = remaining.find('\r');
		std::string_view const line = remaining.substr(0, eol);

		if (line.size() > ct_name.size())
		{
			bool match = true;
			for (std::size_t i = 0; i < ct_name.size() && match; ++i)
				match = std::tolower(static_cast<unsigned char>(line[i])) == ct_name[i];
			if (match)
			{
				std::string_view value = line.substr(ct_name.size());
				while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
				content_type.assign(value);
			}
		}

		if (eol == std::string_view::npos) break;
		remaining = remaining.substr(eol + 1);
		if (!remaining.empty() && remaining.front() == '\n') remaining.remove_prefix(1);
	}
	return body;
}

} // namespace ltweb

#endif // LTWEB_MIME_PART_HPP
