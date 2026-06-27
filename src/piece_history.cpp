/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "piece_history.hpp"
#include "libtorrent/assert.hpp"
#include <algorithm>

namespace ltweb {

piece_history::piece_history(lt::sha1_hash const& ih, std::size_t max_tombstones)
	: m_ih(ih)
	, m_max_tombstones(max_tombstones)
{
}

frame_t piece_history::update(std::vector<lt::partial_piece_info> pieces)
{
	frame_t const frame = ++m_frame;

	std::sort(
		pieces.begin(),
		pieces.end(),
		[](lt::partial_piece_info const& a, lt::partial_piece_info const& b) {
			return a.piece_index < b.piece_index;
		}
	);
	// libtorrent guarantees one entry per piece in the download queue
	TORRENT_ASSERT(
		std::adjacent_find(
			pieces.begin(),
			pieces.end(),
			[](lt::partial_piece_info const& a, lt::partial_piece_info const& b) {
				return a.piece_index == b.piece_index;
			}
		)
		== pieces.end()
	);

	// Remove pieces that are no longer in the download queue.
	auto incoming_it = pieces.begin();
	for (auto it = m_pieces.begin(); it != m_pieces.end();) {
		while (incoming_it != pieces.end() && incoming_it->piece_index < it->first)
			++incoming_it;
		if (incoming_it == pieces.end() || incoming_it->piece_index != it->first) {
			m_removed.push_front({frame, it->second.added_frame, it->first});
			it = m_pieces.erase(it);
		} else {
			++incoming_it;
			++it;
		}
	}

	// Add or update pieces.
	bool any_inserted = false;
	for (auto const& p : pieces) {
		auto [it, inserted] = m_pieces.emplace(p.piece_index, piece_history_entry{});
		piece_history_entry& entry = it->second;

		if (inserted) {
			entry.piece_index = p.piece_index;
			entry.added_frame = frame;
			entry.blocks.resize(p.blocks_in_piece);
			for (int i = 0; i < p.blocks_in_piece; ++i)
				entry.blocks[i] = {std::uint8_t(p.blocks[i].state), frame};
			any_inserted = true;
		} else {
			// Grow the block array if the piece reports more blocks than stored
			// (shouldn't normally happen, but be defensive).
			if (static_cast<int>(entry.blocks.size()) < p.blocks_in_piece)
				entry.blocks.resize(p.blocks_in_piece, {0, frame});

			for (int i = 0; i < p.blocks_in_piece; ++i) {
				auto const new_state = std::uint8_t(p.blocks[i].state);
				if (entry.blocks[i].state != new_state) entry.blocks[i] = {new_state, frame};
			}
		}
	}

	// A tombstone whose piece_index is back in m_pieces was re-inserted this
	// call and must be cleared — a piece can only hold a tombstone if it was
	// previously erased from m_pieces. Skip the scan when nothing was inserted
	// since m_pieces and m_removed are always disjoint.
	if (!m_removed.empty() && any_inserted) {
		auto const rem_end = std::remove_if(m_removed.begin(), m_removed.end(), [&](auto const& r) {
			return m_pieces.contains(r.piece_index);
		});
		m_removed.erase(rem_end, m_removed.end());
	}

	// Evict oldest tombstones when over the limit (deque is newest-first).
	while (m_removed.size() > m_max_tombstones) {
		m_horizon = std::max(m_horizon, m_removed.back().removed_frame + 1);
		m_removed.pop_back();
	}

	return frame;
}

piece_history::query_result piece_history::query(frame_t since_frame) const
{
	query_result result;

	if (since_frame < m_horizon) since_frame = 0;
	result.is_snapshot = (since_frame == 0);

	if (result.is_snapshot) {
		for (auto const& [idx, entry] : m_pieces)
			result.full_pieces.push_back(&entry);
		return result;
	}

	// Removed pieces since since_frame (deque is newest-first).
	// Only include pieces the client had already seen (added_frame <= since_frame).
	for (auto const& e : m_removed) {
		if (e.removed_frame <= since_frame) break;
		if (e.added_frame <= since_frame) result.removed.push_back(e.piece_index);
	}

	// Changed pieces.
	for (auto const& [idx, entry] : m_pieces) {
		if (entry.added_frame > since_frame) {
			result.full_pieces.push_back(&entry);
			continue;
		}

		int const num_blocks = static_cast<int>(entry.blocks.size());
		int changed = 0;
		for (auto const& b : entry.blocks)
			if (b.frame > since_frame) ++changed;

		if (changed == 0) continue;

		if (num_blocks + 6 <= changed * 7) {
			result.full_pieces.push_back(&entry);
		} else {
			for (int i = 0; i < num_blocks; ++i) {
				if (entry.blocks[i].frame > since_frame)
					result.block_updates.push_back({idx, i, entry.blocks[i].state});
			}
		}
	}

	return result;
}

} // namespace ltweb
