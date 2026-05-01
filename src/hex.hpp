/*

Copyright (c) 2017, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_HEX_HPP
#define LTWEB_HEX_HPP

#include "libtorrent/span.hpp"
#include "libtorrent/info_hash.hpp"
#include <string>
#include <type_traits>

namespace ltweb {

std::string to_hex(lt::span<char const> in);
void to_hex(lt::span<char const> in, char* out);
bool from_hex(lt::span<char const> in, char* out);

template <typename T>
typename std::enable_if<std::is_same<T, lt::info_hash_t>::value, std::string>::type
to_hex(T const& ih)
{
	return to_hex(ih.get_best());
}

} // namespace ltweb

#endif
