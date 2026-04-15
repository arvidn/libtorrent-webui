/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "piece_history.hpp"
#include <algorithm>
#include <set>

namespace ltweb {

piece_history::piece_history(lt::sha1_hash const& ih)
	: m_ih(ih)
{}

frame_t piece_history::update(
	std::vector<lt::partial_piece_info> const& pieces)
{
	frame_t const frame = ++m_frame;

	// Build the set of incoming piece indices.
	std::set<lt::piece_index_t> incoming;
	for (auto const& p : pieces) incoming.insert(p.piece_index);

	// Remove pieces that are no longer in the download queue.
	for (auto it = m_pieces.begin(); it != m_pieces.end(); )
	{
		if (incoming.count(it->first) == 0)
		{
			m_removed.push_front({frame, it->second.added_frame, it->first});
			it = m_pieces.erase(it);
		}
		else { ++it; }
	}

	// Add or update pieces.
	for (auto const& p : pieces)
	{
		auto [it, inserted] = m_pieces.emplace(p.piece_index, piece_history_entry{});
		piece_history_entry& entry = it->second;

		if (inserted)
		{
			entry.piece_index = p.piece_index;
			entry.added_frame = frame;
			entry.blocks.resize(p.blocks_in_piece);
			for (int i = 0; i < p.blocks_in_piece; ++i)
				entry.blocks[i] = {std::uint8_t(p.blocks[i].state), frame};

			// If this piece re-appeared after removal, clean it from m_removed.
			auto const rem_end = std::remove_if(m_removed.begin(), m_removed.end(),
				[&](auto const& r) { return r.piece_index == p.piece_index; });
			m_removed.erase(rem_end, m_removed.end());
		}
		else
		{
			// Grow the block array if the piece reports more blocks than stored
			// (shouldn't normally happen, but be defensive).
			if (static_cast<int>(entry.blocks.size()) < p.blocks_in_piece)
				entry.blocks.resize(p.blocks_in_piece, {0, frame});

			for (int i = 0; i < p.blocks_in_piece; ++i)
			{
				auto const new_state = std::uint8_t(p.blocks[i].state);
				if (entry.blocks[i].state != new_state)
					entry.blocks[i] = {new_state, frame};
			}
		}
	}

	// Prune old removal entries to keep the deque bounded.
	if (m_removed.size() > 1000)
	{
		auto const it = std::remove_if(m_removed.begin(), m_removed.end(),
			[&](auto const& r) { return frame - r.removed_frame > 60; });
		m_removed.erase(it, m_removed.end());
	}

	return frame;
}

piece_history::query_result piece_history::query(frame_t since_frame) const
{
	query_result result;

	if (since_frame == 0)
	{
		// Full snapshot: every current piece as a full update.
		for (auto const& [idx, entry] : m_pieces)
			result.full_pieces.push_back(&entry);
		return result;
	}

	// Removed pieces since since_frame (deque is newest-first).
	// Only include pieces the client had already seen (added_frame <= since_frame).
	for (auto const& e : m_removed)
	{
		if (e.removed_frame <= since_frame) break;
		if (e.added_frame <= since_frame)
			result.removed.push_back(e.piece_index);
	}

	// Changed pieces.
	for (auto const& [idx, entry] : m_pieces)
	{
		if (entry.added_frame > since_frame)
		{
			// New piece: client doesn't know about it; must send all blocks.
			result.full_pieces.push_back(&entry);
			continue;
		}

		int const num_blocks = static_cast<int>(entry.blocks.size());
		int changed = 0;
		for (auto const& b : entry.blocks)
			if (b.frame > since_frame) ++changed;

		if (changed == 0) continue;

		// Full update costs (num_blocks + 6) bytes.
		// Individual block updates cost (changed * 7) bytes.
		if (num_blocks + 6 <= changed * 7)
		{
			result.full_pieces.push_back(&entry);
		}
		else
		{
			for (int i = 0; i < num_blocks; ++i)
			{
				if (entry.blocks[i].frame > since_frame)
					result.block_updates.push_back({idx, i, entry.blocks[i].state});
			}
		}
	}

	return result;
}

} // namespace ltweb
