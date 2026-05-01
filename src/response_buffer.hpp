/*

Copyright (c) 2012-2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_RESPONSE_BUFFER_HPP
#define LTWEB_RESPONSE_BUFFER_HPP

#include <vector>
#include <stdarg.h>

namespace ltweb {
inline void appendf(std::vector<char>& target, char const* fmt, ...)
{
	char* buf;
	va_list args;
	va_start(args, fmt);
	int len = vasprintf(&buf, fmt, args);
	va_end(args);

	if (len < 0) return;

	target.insert(target.end(), buf, buf + len);
	free(buf);
}
} // namespace ltweb

#endif
