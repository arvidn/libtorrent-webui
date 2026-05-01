/*

Copyright (c) 2012-2013, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#include "json_util.hpp"
#include <string.h> // for strcmp()
#include <stdlib.h> // for strtoll()

namespace ltweb {

// skip i. if i points to an object or an array, this function
// needs to make recursive calls to skip its members too
jsmntok_t* skip_item(jsmntok_t* i)
{
	int n = i->size;
	++i;
	// if it's a literal, just skip it, and we're done
	if (n == 0) return i;
	// if it's a container, we need to skip n items
	for (int k = 0; k < n; ++k)
		i = skip_item(i);
	return i;
}

jsmntok_t* find_key(jsmntok_t* tokens, char* buf, char const* key, int type)
{
	if (tokens[0].type != JSMN_OBJECT) return NULL;
	// size is the number of tokens at the object level.
	// half of them are keys, the other half
	int num_keys = tokens[0].size / 2;
	// we skip two items at a time, first the key then the value
	for (jsmntok_t* i = &tokens[1]; num_keys > 0; i = skip_item(skip_item(i)), --num_keys) {
		if (i->type != JSMN_STRING) continue;
		buf[i->end] = 0;
		if (strcmp(key, buf + i->start)) continue;
		if (i[1].type != type) continue;
		return i + 1;
	}
	return NULL;
}

char const* find_string(jsmntok_t* tokens, char* buf, char const* key, bool* found)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_STRING);
	if (k == NULL) {
		if (found) *found = false;
		return "";
	}
	if (found) *found = true;
	buf[k->end] = '\0';
	return buf + k->start;
}

std::int64_t find_int(jsmntok_t* tokens, char* buf, char const* key, bool* found)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_PRIMITIVE);
	if (k == NULL) {
		if (found) *found = false;
		return 0;
	}
	buf[k->end] = '\0';
	if (found) *found = true;
	return strtoll(buf + k->start, NULL, 10);
}

bool find_bool(jsmntok_t* tokens, char* buf, char const* key)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_PRIMITIVE);
	if (k == NULL) return false;
	buf[k->end] = '\0';
	return strcmp(buf + k->start, "true") == 0;
}

} // namespace ltweb
