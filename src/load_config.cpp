/*

Copyright (c) 2013, 2018, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string>
#include <sys/stat.h>
#include <syslog.h>
#include <stdio.h>
#include "libtorrent/settings_pack.hpp"
#include "load_config.hpp"


namespace ltweb
{

// this function lets you load libtorrent configurations straight from a
// simple text file, where each line is a key value pair. The keys are
// the keys used by libtorrent. The values are either strings, integers
// or booleans.
void load_config(std::string const& config_file, lt::settings_pack& p, lt::error_code& ec)
{
	static time_t last_load = 0;

	struct ::stat st;
	if (::stat(config_file.c_str(), &st) < 0)
	{
		ec = lt::error_code(errno, boost::system::system_category());
		return;
	}

	// if the config file hasn't changed, don't do anything
	if (st.st_mtime == last_load) return;
	last_load = st.st_mtime;

	FILE* f = fopen(config_file.c_str(), "r");
	if (f == NULL)
	{
		ec = lt::error_code(errno, boost::system::system_category());
		return;
	}

	char key[512];
	char value[512];

	while (fscanf(f, "%512s %512s\n", key, value) == 2)
	{
		int setting_name = lt::setting_by_name(key);
		if (setting_name < 0) continue;

		int type = setting_name & lt::settings_pack::type_mask;
		switch (type)
		{
			case lt::settings_pack::string_type_base:
				p.set_str(setting_name, value);
				break;
			case lt::settings_pack::int_type_base:
				p.set_int(setting_name, atoi(value));
				break;
			case lt::settings_pack::bool_type_base:
				p.set_bool(setting_name, atoi(value));
				break;
		};
	}

	fclose(f);
}

}

