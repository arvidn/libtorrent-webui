/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "peer_history.hpp"
#include "libtorrent/hasher.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace ltweb {
namespace {

	std::uint32_t peer_identifier(lt::peer_info const& pi)
	{
		lt::hasher h;
		h.update(pi.pid);
#if TORRENT_USE_I2P
		if (pi.flags & lt::peer_info::i2p_socket)
		{
			lt::sha256_hash const dest = pi.i2p_destination();
			h.update(dest);
		}
		else
#endif
		{
			auto const ep = pi.remote_endpoint();
			if (ep.address().is_v6())
			{
				auto const b = ep.address().to_v6().to_bytes();
				h.update({reinterpret_cast<char const*>(b.data()), std::ptrdiff_t(b.size())});
			}
			else
			{
				auto const b = ep.address().to_v4().to_bytes();
				h.update({reinterpret_cast<char const*>(b.data()), std::ptrdiff_t(b.size())});
			}
			std::uint8_t const port_bytes[2] = {
				std::uint8_t(ep.port() >> 8), std::uint8_t(ep.port())};
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
		if (lhs_i2p || rhs_i2p)
		{
			if (lhs_i2p != rhs_i2p) return false;
			return lhs.i2p_destination() == rhs.i2p_destination();
		}
#endif
		return lhs.local_endpoint() == rhs.local_endpoint()
			&& lhs.remote_endpoint() == rhs.remote_endpoint();
	}

	std::uint64_t changed_fields(peer_history_entry const& entry
		, frame_t const since_frame
		, std::uint64_t requested_mask)
	{
		std::uint64_t ret = 0;
		for (int i = 0; i < peer_history_entry::num_fields; ++i)
		{
			std::uint64_t const bit = std::uint64_t(1) << i;
			if ((requested_mask & bit) && entry.frame[std::size_t(i)] > since_frame)
				ret |= bit;
		}
		return ret;
	}
}

peer_history::peer_history(lt::sha1_hash const& ih, std::size_t max_tombstones)
	: m_ih(ih)
	, m_max_tombstones(max_tombstones)
{}

frame_t peer_history::update(std::vector<lt::peer_info> const& peers)
{
	frame_t const frame = ++m_frame;

	using snapshot_entry = std::pair<std::uint32_t, lt::peer_info const*>;
	std::vector<snapshot_entry> snapshot;
	snapshot.reserve(peers.size());
	std::set<std::uint32_t> incoming;

	for (auto const& pi : peers)
	{
		std::uint32_t const id = peer_identifier(pi);
		incoming.insert(id);
		snapshot.emplace_back(id, &pi);
	}

	for (auto it = m_peers.begin(); it != m_peers.end(); )
	{
		if (incoming.count(it->first) == 0)
		{
			m_removed.push_front({frame, it->second.added_frame, it->first});
			it = m_peers.erase(it);
		}
		else
		{
			++it;
		}
	}

	for (auto const& s : snapshot)
	{
		std::uint32_t const id = s.first;
		lt::peer_info const& pi = *s.second;
		auto [it, inserted] = m_peers.emplace(id, peer_history_entry{});
		peer_history_entry& entry = it->second;

		if (inserted)
		{
			entry.id = id;
			entry.info = pi;
			entry.added_frame = frame;
			entry.frame.fill(frame);

			auto const rem_end = std::remove_if(m_removed.begin(), m_removed.end()
				, [&](removed_entry const& r) { return r.id == id; });
			m_removed.erase(rem_end, m_removed.end());
		}
		else
		{
			entry.update_info(pi, frame);
		}
	}

	while (m_removed.size() > m_max_tombstones)
	{
		m_horizon = std::max(m_horizon, m_removed.back().removed_frame + 1);
		m_removed.pop_back();
	}

	return frame;
}

peer_history::query_result peer_history::query(
	frame_t since_frame
	, std::uint64_t requested_mask) const
{
	query_result result;

	requested_mask &= (std::uint64_t(1) << peer_history_entry::num_fields) - 1;
	if (since_frame < m_horizon) since_frame = 0;
	result.is_snapshot = (since_frame == 0);

	if (result.is_snapshot)
	{
		for (auto const& [id, entry] : m_peers)
		{
			(void)id;
			if (requested_mask == 0) continue;
			result.updated.push_back({&entry, requested_mask});
		}
		return result;
	}

	for (auto const& e : m_removed)
	{
		if (e.removed_frame <= since_frame) break;
		if (e.added_frame <= since_frame)
			result.removed.push_back(e.id);
	}

	for (auto const& [id, entry] : m_peers)
	{
		(void)id;
		if (entry.added_frame > since_frame)
		{
			if (requested_mask == 0) continue;
			result.updated.push_back({&entry, requested_mask});
			continue;
		}

		std::uint64_t const field_mask = changed_fields(entry, since_frame, requested_mask);
		if (field_mask != 0)
			result.updated.push_back({&entry, field_mask});
	}

	return result;
}

void peer_history_entry::update_info(lt::peer_info const& pi, frame_t const f)
{
	if (pi.flags != info.flags) frame[flags] = f;
	if (pi.source != info.source) frame[source] = f;
	if (pi.read_state != info.read_state) frame[read_state] = f;
	if (pi.write_state != info.write_state) frame[write_state] = f;
	if (pi.client != info.client) frame[client] = f;
	if (pi.num_pieces != info.num_pieces) frame[num_pieces] = f;
	if (pi.pending_disk_bytes != info.pending_disk_bytes) frame[pending_disk_bytes] = f;
	if (pi.pending_disk_read_bytes != info.pending_disk_read_bytes) frame[pending_disk_read_bytes] = f;
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

	info = pi;
}

} // namespace ltweb
