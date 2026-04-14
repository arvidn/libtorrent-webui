/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PIECE_HISTORY_HPP
#define LTWEB_PIECE_HISTORY_HPP

#include "torrent_history.hpp"       // frame_t
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/units.hpp"      // piece_index_t
#include "libtorrent/torrent_handle.hpp" // partial_piece_info

#include <vector>
#include <map>
#include <deque>
#include <cstdint>

namespace ltweb {

struct block_history_entry
{
	std::uint8_t state; // current block state (0-3)
	frame_t      frame; // frame this block last changed
};

struct piece_history_entry
{
	lt::piece_index_t                piece_index;
	std::vector<block_history_entry> blocks;      // one per block in piece
	frame_t                          added_frame; // frame when this piece was added
};

// Tracks per-block state history for the pieces of a single downloading torrent.
// Each instance is permanently associated with one info-hash; create a new
// instance for a different torrent.
struct piece_history
{
	explicit piece_history(lt::sha1_hash const& ih);

	lt::sha1_hash const& info_hash() const { return m_ih; }

	// Feed a fresh download queue snapshot at the given global frame number.
	void update(frame_t frame,
		std::vector<lt::partial_piece_info> const& pieces);

	struct block_update
	{
		lt::piece_index_t piece_index;
		int               block_index;
		std::uint8_t      state;
	};

	struct query_result
	{
		// Pieces whose full block array should be sent.
		// Includes: pieces new since since_frame, and pieces where sending all
		// blocks is cheaper than individual block updates.
		std::vector<piece_history_entry const*> full_pieces;

		// Individual block updates for pieces with few changed blocks.
		std::vector<block_update> block_updates;

		// Piece indices removed since since_frame.
		// Only includes pieces whose added_frame <= since_frame (i.e. the client
		// had already seen the piece before it was removed).
		// Empty when since_frame == 0 (snapshot mode; caller sends 0xffff).
		std::vector<lt::piece_index_t> removed;
	};

	// Return the changes since since_frame.
	//   since_frame == 0  -> full snapshot: all current pieces in full_pieces,
	//                        removed is empty (caller signals with num_removed=0xffff).
	//   since_frame  > 0  -> delta update.
	//
	// Encoding cost comparison per changed piece:
	//   full piece update:  num_blocks + 6 bytes
	//   k block updates:    k * 7 bytes
	// A piece goes to full_pieces when (num_blocks + 6) <= (changed * 7).
	// Newly-added pieces always go to full_pieces.
	query_result query(frame_t since_frame) const;

private:
	lt::sha1_hash const m_ih;

	// ordered by piece_index for deterministic iteration
	std::map<lt::piece_index_t, piece_history_entry> m_pieces;

	struct removed_entry
	{
		frame_t           removed_frame;
		frame_t           added_frame;
		lt::piece_index_t piece_index;
	};

	// newest-first: push_front on removal, break-on-old in query
	std::deque<removed_entry> m_removed;
};

} // namespace ltweb

#endif
