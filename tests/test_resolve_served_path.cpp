/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE resolve_served_path
#include <boost/test/included/unit_test.hpp>

#include "serve_files.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <unistd.h>

namespace fs = std::filesystem;
using ltweb::aux::resolve_served_path;

namespace {

// RAII tmp directory for tests that need real filesystem state (symlinks).
// `path` is always canonical -- some platforms ship /tmp as a symlink
// (e.g. /tmp -> /private/tmp on macOS), and resolve_served_path requires
// a canonical root.
struct tmp_root {
	fs::path path;
	tmp_root()
		: path(
			  fs::weakly_canonical(fs::temp_directory_path())
			  / ("ltweb_serve_files_" + std::to_string(::getpid()) + "_"
				 + std::to_string(reinterpret_cast<std::uintptr_t>(this)))
		  )
	{
		fs::create_directories(path);
	}
	~tmp_root()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
	tmp_root(tmp_root const&) = delete;
	tmp_root& operator=(tmp_root const&) = delete;
};

void touch(fs::path const& p)
{
	std::ofstream f(p);
	f << "x";
}

} // anonymous namespace

// Absolute relative_path would re-root the filesystem join, since
// fs::path::operator/ discards its left operand when the right is
// absolute. This is also the form that arises from the "double slash"
// bypass: a request like "/<prefix>//etc/passwd" produces an absolute
// tail after substr(prefix.size()).
BOOST_AUTO_TEST_CASE(absolute_path_rejected)
{
	BOOST_TEST(!resolve_served_path("/srv/web", "/etc/passwd"));
}

// ".." that escapes the root is rejected.
BOOST_AUTO_TEST_CASE(parent_escape_rejected)
{
	BOOST_TEST(!resolve_served_path("/srv/web", "../etc/passwd"));
}

// ".." that stays inside the root is allowed and resolves correctly.
BOOST_AUTO_TEST_CASE(parent_inside_allowed)
{
	auto const p = resolve_served_path("/srv/web", "a/../b.html");
	BOOST_TEST(p.has_value());
	if (p) BOOST_TEST(p->filename() == "b.html");
}

// Empty relative_path resolves to <root>/index.html.
BOOST_AUTO_TEST_CASE(empty_serves_index)
{
	auto const p = resolve_served_path("/srv/web", "");
	BOOST_TEST(p.has_value());
	if (p) BOOST_TEST(p->filename() == "index.html");
}

// A plain file under the root resolves to the joined path.
BOOST_AUTO_TEST_CASE(simple_file_allowed)
{
	auto const p = resolve_served_path("/srv/web", "foo.html");
	BOOST_TEST(p.has_value());
	if (p) BOOST_TEST(p->filename() == "foo.html");
}

// Nested subdirectory access is allowed.
BOOST_AUTO_TEST_CASE(nested_path_allowed)
{
	auto const p = resolve_served_path("/srv/web", "sub/dir/file.css");
	BOOST_TEST(p.has_value());
	if (p) BOOST_TEST(p->filename() == "file.css");
}

// A trailing-only ".." that lands exactly on the root resolves to the
// root path itself. The resolver allows it (the path is contained); it
// is the file_body open() that will fail downstream because a directory
// cannot be served as a file. The point of this test is to pin the
// no-escape behavior: the resolver does not return a path outside root.
BOOST_AUTO_TEST_CASE(climb_to_root_resolves_to_root)
{
	fs::path const root = "/srv/web";
	auto const p = resolve_served_path(root, "a/..");
	BOOST_TEST(p.has_value());
	if (p) {
		// Compare via lexically_relative so the test does not depend on
		// whether weakly_canonical preserves a trailing separator.
		fs::path const rel = p->lexically_relative(root);
		BOOST_TEST_MESSAGE("resolved=" << *p << " rel=" << rel);
		BOOST_TEST(rel == fs::path("."));
	}
}

// Filename literally containing ".." (as a substring, not a path segment)
// must NOT be rejected -- the old substring check was too coarse.
BOOST_AUTO_TEST_CASE(filename_with_dotdot_substring_allowed)
{
	auto const p = resolve_served_path("/srv/web", "a..b.html");
	BOOST_TEST(p.has_value());
	if (p) BOOST_TEST(p->filename() == "a..b.html");
}

// A symlink inside the root that points outside the root must be
// rejected by canonicalization.
BOOST_AUTO_TEST_CASE(symlink_escape_rejected)
{
	tmp_root tmp;
	fs::path const root = tmp.path / "root";
	fs::path const outside = tmp.path / "outside";
	fs::create_directories(root);
	fs::create_directories(outside);
	touch(outside / "secret");

	std::error_code ec;
	fs::create_symlink(outside / "secret", root / "evil", ec);
	BOOST_REQUIRE(!ec);

	BOOST_TEST(!resolve_served_path(root, "evil"));
}

// A symlink inside the root that points to another file inside the root
// must be allowed.
BOOST_AUTO_TEST_CASE(symlink_internal_allowed)
{
	tmp_root tmp;
	fs::path const root = tmp.path / "root";
	fs::create_directories(root / "real");
	touch(root / "real" / "file");

	std::error_code ec;
	fs::create_symlink(root / "real" / "file", root / "alias", ec);
	BOOST_REQUIRE(!ec);

	auto const p = resolve_served_path(root, "alias");
	BOOST_TEST(p.has_value());
}
