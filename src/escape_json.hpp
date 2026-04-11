/*

Copyright (c) 2012-2013, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_ESCAPE_JSON_HPP
#define LTWEB_ESCAPE_JSON_HPP

#include <string>
#include "libtorrent/fwd.hpp"
#include "libtorrent/string_view.hpp"

namespace ltweb
{
	std::string escape_json(lt::string_view in);
}

#endif

