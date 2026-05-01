/*

Copyright (c) 2026, Muhammad Hassan Raza
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PEER_HISTORY_HPP
#define LTWEB_PEER_HISTORY_HPP

#include "torrent_history.hpp"       // frame_t
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/peer_info.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace ltweb {

struct peer_history_entry
{
	enum
	{
		flags,
		source,
		read_state,
		write_state,
		client,
		num_pieces,
		pending_disk_bytes,
		pending_disk_read_bytes,
		hashfails,
		down_rate,
		up_rate,
		peer_id,
		download_queue,
		upload_queue,
		timed_out_reqs,
		progress,
		endpoints,
		pieces,
		total_download,
		total_upload,
		num_fields,
	};

	std::uint32_t id = 0;
	lt::peer_info info;
	frame_t added_frame = 0;
	std::array<frame_t, num_fields> frame{};

	void update_info(lt::peer_info const& pi, frame_t frame);
};

// Tracks per-peer delta state for a single torrent.
// Each instance is permanently associated with one info-hash.
struct peer_history
{
	explicit peer_history(lt::sha1_hash const& ih, std::size_t max_tombstones = 1000);

	lt::sha1_hash const& info_hash() const { return m_ih; }
	frame_t frame() const { return m_frame; }

	// The oldest frame for which delta queries are reliable.
	// Any query with since_frame < horizon() is treated as a full snapshot.
	frame_t horizon() const { return m_horizon; }

	// Feed a fresh peer snapshot.
	// Always increments the internal frame counter and returns the new frame.
	frame_t update(std::vector<lt::peer_info> const& peers);

	struct peer_update
	{
		std::uint32_t id;
		lt::peer_info info;
		std::uint64_t field_mask;
	};

	struct query_result
	{
		// true when since_frame was 0 or was clamped to 0 due to horizon eviction.
		bool is_snapshot = false;

		// Peers whose requested fields should be sent.
		// New peers are included with all requested fields.
		std::vector<peer_update> updated;

		// Peer identifiers removed since since_frame.
		// Only includes peers whose added_frame <= since_frame.
		// Empty when is_snapshot is true (caller sends num_removed=0xffffffff).
		std::vector<std::uint32_t> removed;
	};

	// Return the changes since since_frame.
	//   since_frame == 0 -> full snapshot: all current peers in updated,
	//                       removed is empty (caller signals with num_removed=0xffffffff).
	//   since_frame  > 0 -> delta update.
	query_result query(frame_t since_frame, std::uint64_t requested_mask) const;

private:
	lt::sha1_hash const m_ih;
	frame_t m_frame = 0;
	frame_t m_horizon = 0;
	std::size_t m_max_tombstones;

	// ordered by peer identifier for deterministic iteration
	std::vector<peer_history_entry> m_peers;

	struct removed_entry
	{
		frame_t removed_frame;
		frame_t added_frame;
		std::uint32_t id;
	};

	// newest-first: push_front on removal, break-on-old in query
	std::deque<removed_entry> m_removed;
};

} // namespace ltweb

#endif
