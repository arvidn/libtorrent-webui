/*

Copyright (c) 2012-2013, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_DISK_SPACE_HPP
#define LTWEB_DISK_SPACE_HPP

#include <cstdint>
#include <string>

namespace ltweb
{
	std::int64_t free_disk_space(std::string const& path);
}

#endif // LTWEB_DISK_SPACE_HPP

