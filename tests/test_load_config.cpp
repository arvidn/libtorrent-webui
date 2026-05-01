/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE load_config
#include <boost/test/included/unit_test.hpp>

#include "load_config.hpp"
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/error_code.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <system_error>

namespace {

// Each test writes to a uniquely-named temp file AND assigns it a unique
// mtime. load_config() has a static `last_load` that skips re-parsing when
// st_mtime hasn't changed; files created rapidly share the same 1-second
// resolution timestamp, which would cause all but the first test to be
// silently skipped.
std::string make_temp_config(char const* name, char const* contents)
{
	namespace fs = std::filesystem;
	auto path = fs::temp_directory_path() / ("test_load_config_" + std::string(name));
	{
		std::ofstream f(path, std::ios::out | std::ios::trunc);
		f << contents;
	}
	// Assign a unique mtime far enough in the past that no two test files
	// collide. A simple counter gives each file a distinct second.
	static int seq = 0;
	auto unique_time = fs::file_time_type::clock::now() - std::chrono::seconds(++seq * 10);
	fs::last_write_time(path, unique_time);
	return path.string();
}

} // namespace

BOOST_AUTO_TEST_CASE(missing_file)
{
	lt::settings_pack p;
	std::error_code ec;
	ltweb::load_config("/tmp/no_such_file_load_config_test.cfg", p, ec);
	BOOST_TEST(ec != std::error_code());
}

BOOST_AUTO_TEST_CASE(string_setting)
{
	auto path = make_temp_config("string", "user_agent my_client/1.0\n");
	lt::settings_pack p;
	std::error_code ec;
	ltweb::load_config(path, p, ec);
	BOOST_TEST(!ec);
	BOOST_TEST(p.get_str(lt::settings_pack::user_agent) == "my_client/1.0");
}

BOOST_AUTO_TEST_CASE(int_setting)
{
	auto path = make_temp_config("int", "connections_limit 42\n");
	lt::settings_pack p;
	std::error_code ec;
	ltweb::load_config(path, p, ec);
	BOOST_TEST(!ec);
	BOOST_TEST(p.get_int(lt::settings_pack::connections_limit) == 42);
}

BOOST_AUTO_TEST_CASE(bool_setting_true)
{
	auto path = make_temp_config("bool_true", "enable_dht 1\n");
	lt::settings_pack p;
	std::error_code ec;
	ltweb::load_config(path, p, ec);
	BOOST_TEST(!ec);
	BOOST_TEST(p.get_bool(lt::settings_pack::enable_dht) == true);
}

BOOST_AUTO_TEST_CASE(bool_setting_false)
{
	auto path = make_temp_config("bool_false", "enable_dht 0\n");
	lt::settings_pack p;
	std::error_code ec;
	ltweb::load_config(path, p, ec);
	BOOST_TEST(!ec);
	BOOST_TEST(p.get_bool(lt::settings_pack::enable_dht) == false);
}

BOOST_AUTO_TEST_CASE(unknown_key_ignored)
{
	auto path = make_temp_config("unknown", "no_such_setting_xyz 999\n");
	lt::settings_pack p;
	std::error_code ec;
	ltweb::load_config(path, p, ec);
	BOOST_TEST(!ec);
}

BOOST_AUTO_TEST_CASE(multiple_settings)
{
	auto path = make_temp_config(
		"multi",
		"user_agent testclient\n"
		"connections_limit 100\n"
		"enable_upnp 0\n"
	);
	lt::settings_pack p;
	std::error_code ec;
	ltweb::load_config(path, p, ec);
	BOOST_TEST(!ec);
	BOOST_TEST(p.get_str(lt::settings_pack::user_agent) == "testclient");
	BOOST_TEST(p.get_int(lt::settings_pack::connections_limit) == 100);
	BOOST_TEST(p.get_bool(lt::settings_pack::enable_upnp) == false);
}
