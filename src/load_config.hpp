/*

Copyright (c) 2013, 2018, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_LOAD_CONFIG_HPP
#define LTWEB_LOAD_CONFIG_HPP

#include <string>

#include "libtorrent/error_code.hpp"
#include "libtorrent/fwd.hpp"

namespace ltweb
{
	void load_config(std::string const& config_file, lt::settings_pack& pack, lt::error_code& ec);
}

#endif

