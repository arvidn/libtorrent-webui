/*

Copyright (c) 2026, Muhammad Hassan Raza
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "peer_history.hpp"
#include "libtorrent/hasher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace ltweb {
namespace {

std::uint32_t peer_identifier(lt::peer_info const& pi)
{
	lt::hasher h;
	h.update(pi.pid);
#if TORRENT_USE_I2P
	if (pi.flags & lt::peer_info::i2p_socket) {
		lt::sha256_hash const dest = pi.i2p_destination();
		h.update(dest);
	} else
#endif
	{
		auto const ep = pi.remote_endpoint();
		if (ep.address().is_v6()) {
			auto const b = ep.address().to_v6().to_bytes();
			h.update({reinterpret_cast<char const*>(b.data()), std::ptrdiff_t(b.size())});
		} else {
			auto const b = ep.address().to_v4().to_bytes();
			h.update({reinterpret_cast<char const*>(b.data()), std::ptrdiff_t(b.size())});
		}
		std::uint8_t const port_bytes[2] = {std::uint8_t(ep.port() >> 8), std::uint8_t(ep.port())};
		h.update({reinterpret_cast<char const*>(port_bytes), 2});
	}
	lt::sha1_hash const digest = h.final();
	std::uint32_t ret;
	std::memcpy(&ret, digest.data(), sizeof(ret));
	return ret;
}

bool same_endpoints(lt::peer_info const& lhs, lt::peer_info const& rhs)
{
#if TORRENT_USE_I2P
	bool const lhs_i2p = bool(lhs.flags & lt::peer_info::i2p_socket);
	bool const rhs_i2p = bool(rhs.flags & lt::peer_info::i2p_socket);
	if (lhs_i2p || rhs_i2p) {
		if (lhs_i2p != rhs_i2p) return false;
		return lhs.i2p_destination() == rhs.i2p_destination();
	}
#endif
	return lhs.local_endpoint() == rhs.local_endpoint()
		&& lhs.remote_endpoint() == rhs.remote_endpoint();
}

std::uint64_t changed_fields(
	peer_history_entry const& entry, frame_t const since_frame, std::uint64_t requested_mask
)
{
	std::uint64_t ret = 0;
	for (int i = 0; i < peer_history_entry::num_fields; ++i) {
		std::uint64_t const bit = std::uint64_t(1) << i;
		if ((requested_mask & bit) && entry.frame[std::size_t(i)] > since_frame) ret |= bit;
	}
	return ret;
}

peer_history::peer_update
make_update(peer_history_entry const& entry, std::uint64_t const field_mask)
{
	return {entry.id, entry.info, field_mask};
}
} // namespace

peer_history::peer_history(lt::sha1_hash const& ih, std::size_t max_tombstones)
	: m_ih(ih)
	, m_max_tombstones(max_tombstones)
{
}

frame_t peer_history::update(std::vector<lt::peer_info> peers)
{
	frame_t const frame = ++m_frame;

	// Precompute identifiers and a permutation that puts peers in id order.
	// We sort the lightweight (id, original-index) pairs rather than peers
	// themselves, so each sort swap moves 12 bytes instead of a peer_info.
	using indexed = std::pair<std::uint32_t, std::size_t>;
	std::vector<indexed> idx;
	idx.reserve(peers.size());
	for (std::size_t i = 0; i < peers.size(); ++i)
		idx.emplace_back(peer_identifier(peers[i]), i);

	std::sort(idx.begin(), idx.end(), [](indexed const& a, indexed const& b) {
		return a.first < b.first;
	});
	// Guard against two peers hashing to the same id within one poll
	// (effectively impossible, but keeps the merge below well-defined).
	auto const dedup_end =
		std::unique(idx.begin(), idx.end(), [](indexed const& a, indexed const& b) {
			return a.first == b.first;
		});
	idx.erase(dedup_end, idx.end());

	// Fast path: same id-set as last frame, in the same order. This is the
	// expected common case (peers persist across polls). Skips the rebuild.
	bool same_layout = (idx.size() == m_peers.size());
	for (std::size_t i = 0; same_layout && i < idx.size(); ++i)
		same_layout = (idx[i].first == m_peers[i].id);
	if (same_layout) {
		for (std::size_t i = 0; i < idx.size(); ++i)
			m_peers[i].update_info(std::move(peers[idx[i].second]), frame);
		return frame;
	}

	// Slow path: structural change. Merge-walk into a fresh vector.
	std::vector<peer_history_entry> next;
	next.reserve(idx.size());

	auto add_new = [&](std::uint32_t const id, lt::peer_info& info) {
		peer_history_entry entry;
		entry.id = id;
		entry.info = std::move(info);
		entry.added_frame = frame;
		entry.frame.fill(frame);
		next.push_back(std::move(entry));

		auto const rem_end =
			std::remove_if(m_removed.begin(), m_removed.end(), [id](removed_entry const& r) {
				return r.id == id;
			});
		m_removed.erase(rem_end, m_removed.end());
	};

	auto p_it = m_peers.begin();
	auto i_it = idx.begin();
	while (p_it != m_peers.end() && i_it != idx.end()) {
		if (p_it->id < i_it->first) {
			m_removed.push_front({frame, p_it->added_frame, p_it->id});
			++p_it;
		} else if (p_it->id > i_it->first) {
			add_new(i_it->first, peers[i_it->second]);
			++i_it;
		} else {
			p_it->update_info(std::move(peers[i_it->second]), frame);
			next.push_back(std::move(*p_it));
			++p_it;
			++i_it;
		}
	}
	for (; p_it != m_peers.end(); ++p_it)
		m_removed.push_front({frame, p_it->added_frame, p_it->id});
	for (; i_it != idx.end(); ++i_it)
		add_new(i_it->first, peers[i_it->second]);

	m_peers = std::move(next);

	while (m_removed.size() > m_max_tombstones) {
		m_horizon = std::max(m_horizon, m_removed.back().removed_frame + 1);
		m_removed.pop_back();
	}

	return frame;
}

peer_history::query_result
peer_history::query(frame_t since_frame, std::uint64_t requested_mask) const
{
	query_result result;

	requested_mask &= (std::uint64_t(1) << peer_history_entry::num_fields) - 1;
	if (since_frame < m_horizon) since_frame = 0;
	result.is_snapshot = (since_frame == 0);

	if (result.is_snapshot) {
		for (auto const& entry : m_peers)
			result.updated.push_back(make_update(entry, requested_mask));
		return result;
	}

	for (auto const& e : m_removed) {
		if (e.removed_frame <= since_frame) break;
		if (e.added_frame <= since_frame) result.removed.push_back(e.id);
	}

	for (auto const& entry : m_peers) {
		if (entry.added_frame > since_frame) {
			result.updated.push_back(make_update(entry, requested_mask));
			continue;
		}

		std::uint64_t const field_mask = changed_fields(entry, since_frame, requested_mask);
		if (field_mask != 0) result.updated.push_back(make_update(entry, field_mask));
	}

	return result;
}

void peer_history_entry::update_info(lt::peer_info pi, frame_t const f)
{
	if (pi.flags != info.flags) frame[flags] = f;
	if (pi.source != info.source) frame[source] = f;
	if (pi.read_state != info.read_state) frame[read_state] = f;
	if (pi.write_state != info.write_state) frame[write_state] = f;
	if (pi.client != info.client) frame[client] = f;
	if (pi.num_pieces != info.num_pieces) frame[num_pieces] = f;
	if (pi.pending_disk_bytes != info.pending_disk_bytes) frame[pending_disk_bytes] = f;
	if (pi.pending_disk_read_bytes != info.pending_disk_read_bytes)
		frame[pending_disk_read_bytes] = f;
	if (pi.num_hashfails != info.num_hashfails) frame[hashfails] = f;
	if (pi.payload_down_speed != info.payload_down_speed) frame[down_rate] = f;
	if (pi.payload_up_speed != info.payload_up_speed) frame[up_rate] = f;
	if (pi.pid != info.pid) frame[peer_id] = f;
	if (pi.download_queue_length != info.download_queue_length) frame[download_queue] = f;
	if (pi.upload_queue_length != info.upload_queue_length) frame[upload_queue] = f;
	if (pi.timed_out_requests != info.timed_out_requests) frame[timed_out_reqs] = f;
	if (pi.progress_ppm != info.progress_ppm) frame[progress] = f;
	if (!same_endpoints(pi, info)) frame[endpoints] = f;
	if (!(pi.pieces == info.pieces)) frame[pieces] = f;
	if (pi.total_download != info.total_download) frame[total_download] = f;
	if (pi.total_upload != info.total_upload) frame[total_upload] = f;

	info = std::move(pi);
}

} // namespace ltweb
