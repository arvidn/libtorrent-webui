/*

Copyright (c) 2012-2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string>
#include <string.h>

extern "C" {
#include "cdecode.h"
}

namespace ltweb {

std::string base64decode(std::string const& in)
{
	std::string ret;
	if (in.size() < 4) return ret;

	// approximate length of output
	ret.resize(in.size() * 6 / 8 + 1);

	base64_decodestate ctx;
	base64_init_decodestate(&ctx);
	int len = base64_decode_block(in.c_str(), in.size(), &ret[0], &ctx);
	ret.resize(len);
	return ret;
}

} // namespace ltweb
