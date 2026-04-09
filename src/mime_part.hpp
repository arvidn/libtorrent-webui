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

#ifndef TORRENT_MIME_PART_HPP
#define TORRENT_MIME_PART_HPP

#include <algorithm>
#include <cctype>
#include <string>

// Locate the body of a MIME part and extract its Content-Type header value.
// [start, end) is the raw bytes of a single multipart boundary section.
// Returns a pointer to the first body byte (immediately after the blank
// header line \r\n\r\n), or nullptr if the part is malformed.
// content_type is set to the value of the Content-Type header if found.
inline char const* parse_mime_part(char const* start, char const* end
	, std::string& content_type)
{
	// find the blank line separating headers from body
	char const* body = nullptr;
	for (char const* p = start; p + 3 < end; ++p)
	{
		if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')
		{ body = p + 4; break; }
	}
	if (body == nullptr) return nullptr;

	// scan header lines for Content-Type (case-insensitive)
	char const* const headers_end = body - 4;
	for (char const* h = start; h < headers_end; )
	{
		char const* eol = std::find(h, headers_end, '\r');
		std::size_t const len = static_cast<std::size_t>(eol - h);
		static constexpr char ct[] = "content-type:";
		if (len > sizeof(ct) - 1)
		{
			bool match = true;
			for (int i = 0; i < int(sizeof(ct) - 1) && match; ++i)
				match = std::tolower(static_cast<unsigned char>(h[i])) == ct[i];
			if (match)
			{
				char const* v = h + sizeof(ct) - 1;
				while (v < eol && *v == ' ') ++v;
				content_type.assign(v, eol);
			}
		}
		h = (eol + 1 < headers_end && eol[1] == '\n') ? eol + 2 : eol + 1;
	}
	return body;
}

#endif // TORRENT_MIME_PART_HPP
