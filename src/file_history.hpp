/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_FILE_HISTORY_HPP
#define LTWEB_FILE_HISTORY_HPP

#include "torrent_history.hpp"       // frame_t
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/download_priority.hpp"
#include "libtorrent/disk_interface.hpp" // open_file_state

#include <vector>
#include <cstdint>

namespace ltweb {

struct file_history_entry
{
	std::int64_t progress = 0;
	lt::download_priority_t priority = lt::default_priority;
	std::uint8_t open_mode = 0;

	// frame when each dynamic field last changed (0 = never updated)
	frame_t progress_frame = 1;
	frame_t priority_frame = 1;
	frame_t open_mode_frame = 1;
};

// Tracks per-file delta state for a single torrent.
// Each instance is permanently associated with one info-hash.
//
// Field semantics:
//   Static fields (flags=0x01, name=0x02, size=0x04): live in file_storage,
//     never change. Conceptually updated at frame 1; sent iff since_frame==0.
//   Dynamic fields (progress=0x08, priority=0x10, open_mode=0x20): tracked
//     per-file; sent when their stored frame > since_frame.
struct file_history
{
	// fs is used only at construction time (for num_files()).
	file_history(lt::sha1_hash const& ih, lt::file_storage const& fs);

	lt::sha1_hash const& info_hash() const { return m_ih; }
	frame_t frame() const { return m_frame; }

	// Feed a fresh snapshot of the dynamic fields.
	// Pass nullptr for any field not fetched this round — it is treated as
	// unchanged and its frame counter is left alone.
	// Always increments the internal frame counter and returns the new value.
	frame_t update(
		std::vector<std::int64_t> const* progress,
		std::vector<lt::download_priority_t> const* priorities,
		std::vector<lt::open_file_state> const* open_modes);

	// For each file index in [0, num_files), return the subset of
	// requested_mask that should be sent to a client whose last known frame
	// is since_frame.
	//
	//   since_frame == 0 => static fields are included for every file.
	//   since_frame  > 0 => only dynamic fields whose frame > since_frame.
	//
	// A zero entry means "no update for this file".
	std::vector<std::uint16_t> query(frame_t since_frame,
		std::uint16_t requested_mask) const;

	// Value accessors — call after update(), before the next update().
	std::int64_t progress (int fi) const { return m_files[fi].progress; }
	lt::download_priority_t priority (int fi) const { return m_files[fi].priority; }
	std::uint8_t open_mode(int fi) const { return m_files[fi].open_mode; }

private:
	lt::sha1_hash const m_ih;
	frame_t m_frame = 1;
	std::vector<file_history_entry> m_files; // indexed by file_index
};

} // namespace ltweb

#endif
