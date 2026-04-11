/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_MIME_TYPE
#define TORRENT_MIME_TYPE

#include <string_view>

namespace ltweb {

std::string_view mime_type(std::string_view ext);

}

#endif
