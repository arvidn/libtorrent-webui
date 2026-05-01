/*

Copyright (c) 2013, 2018, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string>
#include <fstream>
#include <filesystem>
#include <cerrno>
#include <syslog.h>
#include "libtorrent/settings_pack.hpp"
#include "load_config.hpp"


namespace ltweb {

// this function lets you load libtorrent configurations straight from a
// simple text file, where each line is a key value pair. The keys are
// the keys used by libtorrent. The values are either strings, integers
// or booleans.
void load_config(std::string const& config_file, lt::settings_pack& p, std::error_code& ec)
{
	static std::filesystem::file_time_type last_load{};

	auto const mtime = std::filesystem::last_write_time(config_file, ec);
	if (ec) return;

	// if the config file hasn't changed, don't do anything
	if (mtime == last_load) return;
	last_load = mtime;

	std::ifstream f(config_file);
	if (!f) {
		ec = std::error_code(errno, std::system_category());
		return;
	}

	std::string key, value;
	while (f >> key >> value) {
		int setting_name = lt::setting_by_name(key.c_str());
		if (setting_name < 0) continue;

		int type = setting_name & lt::settings_pack::type_mask;
		switch (type) {
			case lt::settings_pack::string_type_base:
				p.set_str(setting_name, value);
				break;
			case lt::settings_pack::int_type_base:
				p.set_int(setting_name, std::stoi(value));
				break;
			case lt::settings_pack::bool_type_base:
				p.set_bool(setting_name, std::stoi(value) != 0);
				break;
		};
	}
}

} // namespace ltweb
