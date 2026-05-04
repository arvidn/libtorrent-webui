/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "piece_state_history.hpp"

namespace ltweb {

piece_state_history::piece_state_history(
	lt::sha1_hash const& ih, lt::typed_bitfield<lt::piece_index_t> initial, std::size_t max_queue
)
	: m_ih(ih)
	, m_bitfield(std::move(initial))
	, m_max_queue(max_queue)
	, m_horizon(1)
{
}

void piece_state_history::on_piece_finished(lt::piece_index_t const idx)
{
	int const i = static_cast<int>(idx);
	// Ignore pieces outside the known range. The bitfield is sized when the
	// history is created; if it's empty (metadata not yet resolved), we
	// have nowhere to record the bit. Once metadata arrives, the resulting
	// torrent_checked_alert drops this history and the next query recreates
	// it from a properly-sized bitfield.
	if (i < 0 || i >= m_bitfield.size()) return;

	// Already known. m_bitfield is authoritative for the current have-set,
	// so a single bit check rules out duplicates from the queue too.
	if (m_bitfield.get_bit(idx)) return;

	m_bitfield.set_bit(idx);
	m_queue.push_back(idx);

	// Cap the delta history. Each pop advances the horizon by one, so the
	// queue's frame range stays [m_horizon+1, m_horizon+m_queue.size()].
	while (m_queue.size() > m_max_queue) {
		++m_horizon;
		m_queue.pop_front();
	}
}

piece_state_history::query_result piece_state_history::query(frame_t const since_frame) const
{
	query_result r;
	frame_t const cur_frame = frame();
	r.frame = cur_frame;

	// Snapshot fallback cases: caller asked for everything, the requested
	// frame predates our retained delta history, or the requested frame is
	// from a previous incarnation of this history (server restart, torrent
	// re-add). m_bitfield is authoritative current state; no overlay needed.
	if (since_frame == 0 || since_frame < m_horizon || since_frame > cur_frame) {
		r.is_snapshot = true;
		r.snapshot = m_bitfield;
		return r;
	}

	// Delta. Frames are implied by queue position: m_queue[k] has frame
	// m_horizon + 1 + k. The first entry strictly newer than since_frame
	// is at index (since_frame - m_horizon).
	r.is_snapshot = false;
	std::size_t const skip = static_cast<std::size_t>(since_frame - m_horizon);
	for (std::size_t k = skip; k < m_queue.size(); ++k)
		r.added.push_back(m_queue[k]);
	return r;
}

} // namespace ltweb
