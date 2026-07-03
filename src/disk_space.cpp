/*

Copyright (c) 2012-2013, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#ifndef STRICT
#define STRICT
#endif
#include <windows.h>
#else
// for statfs()
#include <sys/param.h>
#include <sys/mount.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif
#endif

#include "disk_space.hpp"

namespace ltweb {

#ifdef _WIN32
namespace {
// returns false if "s" is not valid UTF-8
bool utf8_to_wide(std::string const& s, std::wstring& out)
{
	if (s.empty()) {
		out.clear();
		return true;
	}
	int const len = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), nullptr, 0
	);
	if (len <= 0) return false;
	out.resize(len);
	int const ret = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), out.data(), len
	);
	if (ret <= 0) return false;
	return true;
}
} // namespace
#endif

std::int64_t free_disk_space(std::string const& path)
{
#ifdef _WIN32
	std::wstring wide_path;
	if (!utf8_to_wide(path, wide_path)) return -1;

	// GetDiskFreeSpaceExW resolves an arbitrary directory path to whichever
	// volume hosts it, the same role statfs() plays on POSIX below.
	ULARGE_INTEGER free_bytes;
	if (!GetDiskFreeSpaceExW(wide_path.c_str(), &free_bytes, nullptr, nullptr)) return -1;

	return std::int64_t(free_bytes.QuadPart);
#else
	struct statfs fs;
	int ret = statfs(path.c_str(), &fs);
	if (ret < 0) return -1;

	return std::int64_t(fs.f_bavail) * fs.f_bsize;
#endif
}

} // namespace ltweb
