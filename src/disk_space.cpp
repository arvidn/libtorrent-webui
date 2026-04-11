/*

Copyright (c) 2012-2013, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// for statfs()
#include <sys/param.h>
#include <sys/mount.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif

#include "disk_space.hpp"

namespace ltweb
{

std::int64_t free_disk_space(std::string const& path)
{
	// TODO: support windows

	struct statfs fs;
	int ret = statfs(path.c_str(), &fs);
	if (ret < 0) return -1;

	return std::int64_t(fs.f_bavail) * fs.f_bsize;
}

}

