/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_WIRE_FLAGS_HPP
#define LTWEB_WIRE_FLAGS_HPP

#include "libtorrent/flags.hpp"

namespace ltweb {
namespace aux {

// Wire-protocol bit values for the torrent flags field, shared by
// get-torrent-updates (field 0) and add-torrent. Read-only flags are set by
// the server and ignored on add-torrent; write-only flags are accepted by
// add-torrent and never appear in get-torrent-updates.
using wire_flags_t = libtorrent::flags::bitfield_flag<std::uint32_t, struct wire_flags_tag>;
using libtorrent::operator""_bit;

namespace wire {
constexpr wire_flags_t stopped = 0_bit;
constexpr wire_flags_t auto_managed = 1_bit;
constexpr wire_flags_t sequential_download = 2_bit;
constexpr wire_flags_t seeding = 3_bit; // read-only
constexpr wire_flags_t finished = 4_bit; // read-only
// bit 5 unused
constexpr wire_flags_t has_metadata = 6_bit; // read-only
constexpr wire_flags_t has_incoming = 7_bit; // read-only
constexpr wire_flags_t seed_mode = 8_bit;
constexpr wire_flags_t upload_mode = 9_bit;
constexpr wire_flags_t share_mode = 10_bit;
constexpr wire_flags_t super_seeding = 11_bit;
constexpr wire_flags_t moving_storage = 12_bit; // read-only
constexpr wire_flags_t announcing_to_trackers = 13_bit; // read-only
constexpr wire_flags_t announcing_to_lsd = 14_bit; // read-only
constexpr wire_flags_t announcing_to_dht = 15_bit; // read-only
constexpr wire_flags_t disable_pex = 16_bit;
constexpr wire_flags_t disable_dht = 17_bit;
constexpr wire_flags_t disable_lsd = 18_bit;
constexpr wire_flags_t disable_v1_hashes = 19_bit;
constexpr wire_flags_t i2p_torrent = 20_bit;
constexpr wire_flags_t default_dont_download = 21_bit; // write-only
constexpr wire_flags_t metadata_only = 22_bit; // write-only
} // namespace wire

} // namespace aux
} // namespace ltweb

#endif // LTWEB_WIRE_FLAGS_HPP
