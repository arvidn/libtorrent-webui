/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
