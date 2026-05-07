/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PIECE_STATE_HISTORY_HPP
#define LTWEB_PIECE_STATE_HISTORY_HPP

#include "torrent_history.hpp" // frame_t
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/units.hpp" // piece_index_t
#include "libtorrent/bitfield.hpp" // bitfield, typed_bitfield

#include <vector>
#include <deque>
#include <cstddef>

namespace ltweb {

// Tracks the "have" set of a single torrent for the get-piece-states RPC
// (function id 25). m_bitfield holds the authoritative current state; a
// bounded queue retains recent completions so clients can fetch deltas
// keyed by frame number. Frames are implicit in queue position: the entry
// at index k has frame m_horizon + 1 + k. When the queue overflows, the
// oldest entries are discarded and m_horizon advances accordingly.
// Clients whose frame predates the horizon must fall back to a snapshot.
//
// Each instance is permanently associated with one info-hash; create a new
// instance for a different torrent.
struct piece_state_history {
	piece_state_history(
		lt::sha1_hash const& ih,
		lt::typed_bitfield<lt::piece_index_t> initial,
		std::size_t max_queue = 300
	);

	lt::sha1_hash const& info_hash() const { return m_ih; }

	// The current frame counter. Equal to m_horizon plus the queue length;
	// advances by one for each newly-completed piece recorded.
	frame_t frame() const { return m_horizon + static_cast<frame_t>(m_queue.size()); }

	// Oldest since_frame still answerable as a delta. Any query with a
	// since_frame strictly below this is served as a snapshot.
	frame_t horizon() const { return m_horizon; }

	// Record that a piece has been completed. No-op if the piece is already
	// set in m_bitfield. Otherwise the bit is set immediately and the
	// completion is appended to the delta queue. When the queue grows past
	// max_queue, the oldest entry is dropped and m_horizon advances.
	void on_piece_finished(lt::piece_index_t idx);

	struct query_result {
		// When true, a complete bitfield is returned (the response-type 1
		// payload from the spec). When false, only the list of piece indices
		// completed since the requested frame is returned (response-type 0).
		bool is_snapshot = false;

		// Frame number of this response. The client should pass this back as
		// frame_number on the next call.
		frame_t frame = 0;

		// Populated when is_snapshot is true: a copy of the current bitfield.
		// Empty when is_snapshot is false.
		lt::typed_bitfield<lt::piece_index_t> snapshot;

		// Populated when is_snapshot is false: piece indices completed since
		// the requested frame. Empty when is_snapshot is true.
		std::vector<lt::piece_index_t> added;
	};

	// Compute the response. The server falls back to a snapshot when:
	// - since_frame == 0 (caller asked for a full snapshot)
	// - since_frame < m_horizon (the delta queue no longer covers it)
	// - since_frame > current frame (caller's frame is from a previous instance,
	//   after a server restart or a re-add of the torrent)
	query_result query(frame_t since_frame) const;

private:
	lt::sha1_hash const m_ih;

	// Authoritative current have-set. Updated immediately when a piece
	// completes; query(0) returns this directly.
	lt::typed_bitfield<lt::piece_index_t> m_bitfield;

	// Newly-completed piece indices, in completion order. Frames are
	// implied by position: m_queue[k] has frame m_horizon + 1 + k. Bounded
	// by m_max_queue; entries past the cap are discarded from the front.
	std::deque<lt::piece_index_t> m_queue;

	std::size_t const m_max_queue;

	// Oldest since_frame answerable as a delta. Advances by one each time
	// the queue evicts its front: a client below this no longer has the
	// data they'd need to be served a delta and is given a snapshot.
	frame_t m_horizon = 0;
};

} // namespace ltweb

#endif
