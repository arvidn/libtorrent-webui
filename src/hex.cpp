/*

Copyright (c) 2017, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "hex.hpp"
#include <string>

namespace ltweb {

	int hex_to_int(char in)
	{
		if (in >= '0' && in <= '9') return int(in) - '0';
		if (in >= 'A' && in <= 'F') return int(in) - 'A' + 10;
		if (in >= 'a' && in <= 'f') return int(in) - 'a' + 10;
		return -1;
	}

	bool is_hex(lt::span<char const> in)
	{
		for (char const c : in)
		{
			int const t = hex_to_int(c);
			if (t == -1) return false;
		}
		return true;
	}

	bool from_hex(lt::span<char const> in, char* out)
	{
		for (auto i = in.begin(), end = in.end(); i != end; ++i, ++out)
		{
			int const t1 = hex_to_int(*i);
			if (t1 == -1) return false;
			*out = char(t1 << 4);
			++i;
			int const t2 = hex_to_int(*i);
			if (t2 == -1) return false;
			*out |= t2 & 15;
		}
		return true;
	}

	extern char const hex_chars[];

	char const hex_chars[] = "0123456789abcdef";
	void to_hex(char const* in, size_t const len, char* out)
	{
		int idx = 0;
		for (size_t i=0; i < len; ++i)
		{
			out[idx++] = hex_chars[std::uint8_t(in[i]) >> 4];
			out[idx++] = hex_chars[std::uint8_t(in[i]) & 0xf];
		}
	}

	std::string to_hex(lt::span<char const> in)
	{
		std::string ret;
		if (!in.empty())
		{
			ret.resize(in.size() * 2);
			to_hex(in.data(), in.size(), &ret[0]);
		}
		return ret;
	}

	void to_hex(lt::span<char const> in, char* out)
	{
		to_hex(in.data(), in.size(), out);
		out[in.size() * 2] = '\0';
	}

}

