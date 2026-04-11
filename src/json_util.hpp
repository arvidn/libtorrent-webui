/*

Copyright (c) 2012-2013, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#ifndef TORRENT_JSON_UTIL_HPP_
#define TORRENT_JSON_UTIL_HPP_

#include <cstdint>

extern "C" {
#include "jsmn.h"
}

#include <boost/cstdint.hpp>

namespace ltweb {

jsmntok_t* skip_item(jsmntok_t* i);
jsmntok_t* find_key(jsmntok_t* tokens, char* buf, char const* key, int type);
char const* find_string(jsmntok_t* tokens, char* buf, char const* key, bool* found = NULL);
std::int64_t find_int(jsmntok_t* tokens, char* buf, char const* key, bool* found = NULL);
bool find_bool(jsmntok_t* tokens, char* buf, char const* key);

}

#endif

