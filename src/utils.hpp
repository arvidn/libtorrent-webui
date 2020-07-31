/*

Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_UTILS_HPP
#define TORRENT_UTILS_HPP

#include <string_view>
#include <sstream>
#include <boost/algorithm/string/predicate.hpp>

namespace libtorrent {

	using boost::algorithm::starts_with;

	template <typename... Elems>
	std::string str(Elems&&... e)
	{
		std::stringstream ret;
		int dummy[] = {(ret << e, 0)...};
		static_cast<void>(dummy); // unused
		return ret.str();
	}

	template <typename StringView>
	std::pair<std::string_view, std::string_view>
	split(StringView input, char const delimiter)
	{
		std::string_view const in(input.data(), input.size());
		auto const pos = in.find_first_of(delimiter);
		if (pos == std::string_view::npos) return { in, std::string_view{}};
		return {in.substr(0, pos), in.substr(pos + 1)};
	}

	template <typename StringView>
	std::string_view extension(StringView input)
	{
		std::string_view const in(input.data(), input.size());
		auto const pos = in.find_last_of('.');
		if (pos == std::string_view::npos) return std::string_view{};
		return in.substr(pos);
	}

	inline bool is_whitespace(char const c)
	{
		return c == ' ' || c == '\t';
	}

	template <typename StringView>
	std::string_view trim(StringView in)
	{
		std::string_view input(in.data(), in.size());
		while (!input.empty() && is_whitespace(input.front()))
			input.remove_prefix(1);

		while (!input.empty() && is_whitespace(input.back()))
			input.remove_suffix(1);
		return input;
	}
}

#endif
