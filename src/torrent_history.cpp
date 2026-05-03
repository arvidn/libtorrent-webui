/*

Copyright (c) 2012-2014, 2017-2019, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "torrent_history.hpp"
#include "libtorrent/alert_types.hpp"
#include "alert_handler.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/flags.hpp"
#include <chrono>
#include <iostream>

namespace ltweb {
torrent_history::torrent_history(alert_handler* h, std::size_t max_tombstones)
	: m_alerts(h)
	, m_frame(1)
	, m_deferred_frame_count(false)
	, m_max_tombstones(max_tombstones)
{
	m_alerts->subscribe(
		this,
		0,
		lt::add_torrent_alert::alert_type,
		lt::torrent_removed_alert::alert_type,
		lt::state_update_alert::alert_type,
		0
	);
}

torrent_history::~torrent_history() { m_alerts->unsubscribe(this); }

void torrent_history::handle_alert(lt::alert const* a)
try {
	if (lt::add_torrent_alert const* ta = lt::alert_cast<lt::add_torrent_alert>(a)) {
		// TODO: This is a synchronous call. use post_torrent_updates() instead
		lt::torrent_status st = ta->handle.status();
		TORRENT_ASSERT(st.info_hashes == st.handle.info_hashes());
		TORRENT_ASSERT(st.handle == ta->handle);

		std::unique_lock<std::mutex> l(m_mutex);
		m_queue.left.push_front(
			std::make_pair(m_frame + 1, torrent_history_entry(std::move(st), m_frame + 1))
		);
		m_deferred_frame_count = true;
	} else if (lt::torrent_removed_alert const* td = lt::alert_cast<lt::torrent_removed_alert>(a)) {
		std::unique_lock<std::mutex> l(m_mutex);

		torrent_history_entry st;
		st.status.info_hashes = td->info_hashes;

		// Determine when this torrent was first seen, so that removed_since()
		// can skip notifying clients that never received an add for it.
		frame_t added_frame = m_frame + 1;
		auto const it = m_queue.right.find(st);
		if (it != m_queue.right.end()) {
			added_frame = it->first.added_frame;
			m_queue.right.erase(it);
		}

		m_removed.push_front({m_frame + 1, added_frame, td->info_hashes.get_best()});

		// Evict oldest tombstones when over the limit. The deque is
		// newest-first, so back() is always the oldest entry.
		while (m_removed.size() > m_max_tombstones) {
			m_horizon = std::max(m_horizon, m_removed.back().removed_frame + 1);
			m_removed.pop_back();
		}

		m_deferred_frame_count = true;
	} else if (lt::state_update_alert const* su = lt::alert_cast<lt::state_update_alert>(a)) {
		std::unique_lock<std::mutex> l(m_mutex);

		++m_frame;
		m_deferred_frame_count = false;

		std::vector<lt::torrent_status> const& st = su->status;
		for (auto const& t : st) {
			torrent_history_entry e;
			e.status.info_hashes = t.info_hashes;

			queue_t::right_iterator it = m_queue.right.find(e);
			if (it == m_queue.right.end()) continue;
			const_cast<torrent_history_entry&>(it->first).update_status(t, m_frame);
			m_queue.right.replace_data(it, m_frame);
			// bump this torrent to the beginning of the list
			m_queue.left.relocate(m_queue.left.begin(), m_queue.project_left(it));
		}
		/*
			printf("===== frame: %d =====\n", m_frame);
			for (auto const& e : m_queue.left)
			{
				e.second.debug_print(m_frame);
//				printf("%3d: (%s) %s\n", e.first, e.second.error.c_str(), e.second.name.c_str());
			}
*/
	}
} catch (std::exception const&) {
}

torrent_history::query_result torrent_history::query(frame_t since_frame) const
{
	query_result result;
	std::unique_lock<std::mutex> l(m_mutex);
	result.current_frame = current_frame_locked();
	if (since_frame < m_horizon) since_frame = 0;
	result.is_snapshot = (since_frame == 0);

	for (auto const& e : m_queue.left) {
		if (e.first <= since_frame) break;
		result.updated.push_back(e.second);
	}

	if (!result.is_snapshot) {
		for (auto const& e : m_removed) {
			if (e.removed_frame <= since_frame) break;
			if (e.added_frame <= since_frame) result.removed.push_back(e.ih);
		}
	}

	return result;
}

frame_t torrent_history::horizon() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	return m_horizon;
}

lt::torrent_status torrent_history::get_torrent_status(lt::sha1_hash const& ih) const
{
	torrent_history_entry st;
	st.status.info_hashes.v1 = ih;

	std::unique_lock<std::mutex> l(m_mutex);

	queue_t::right_const_iterator it = m_queue.right.find(st);
	if (it != m_queue.right.end()) return it->first.status;
	return st.status;
}

frame_t torrent_history::frame() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	return current_frame_locked();
}

frame_t torrent_history::current_frame_locked() const
{
	if (m_deferred_frame_count) {
		m_deferred_frame_count = false;
		++m_frame;
	}
	return m_frame;
}

void torrent_history_entry::update_status(lt::torrent_status const& s, frame_t const f)
{
#define CMP_SET(x)                                                                                 \
	if (s.x != status.x) frame[int(x)] = f

	CMP_SET(state);
	CMP_SET(flags);
	CMP_SET(is_seeding);
	CMP_SET(is_finished);
	CMP_SET(has_metadata);
	CMP_SET(progress);
	CMP_SET(progress_ppm);
	CMP_SET(errc);
	CMP_SET(error_file);
	CMP_SET(save_path);
	CMP_SET(name);
	CMP_SET(next_announce);
	CMP_SET(current_tracker);
	CMP_SET(total_download);
	CMP_SET(total_upload);
	CMP_SET(total_payload_download);
	CMP_SET(total_payload_upload);
	CMP_SET(total_failed_bytes);
	CMP_SET(total_redundant_bytes);
	CMP_SET(download_rate);
	CMP_SET(upload_rate);
	CMP_SET(download_payload_rate);
	CMP_SET(upload_payload_rate);
	CMP_SET(num_seeds);
	CMP_SET(num_peers);
	CMP_SET(num_complete);
	CMP_SET(num_incomplete);
	CMP_SET(list_seeds);
	CMP_SET(list_peers);
	CMP_SET(connect_candidates);
	CMP_SET(num_pieces);
	CMP_SET(total_done);
	CMP_SET(total);
	CMP_SET(total_wanted_done);
	CMP_SET(total_wanted);
	CMP_SET(distributed_full_copies);
	CMP_SET(distributed_fraction);
	CMP_SET(block_size);
	CMP_SET(num_uploads);
	CMP_SET(num_connections);
	CMP_SET(uploads_limit);
	CMP_SET(connections_limit);
	CMP_SET(storage_mode);
	CMP_SET(up_bandwidth_queue);
	CMP_SET(down_bandwidth_queue);
	CMP_SET(all_time_upload);
	CMP_SET(all_time_download);
	CMP_SET(active_duration);
	CMP_SET(finished_duration);
	CMP_SET(seeding_duration);
	CMP_SET(seed_rank);
	CMP_SET(has_incoming);
	CMP_SET(added_time);
	CMP_SET(completed_time);
	CMP_SET(last_seen_complete);
	CMP_SET(last_upload);
	CMP_SET(last_download);
	CMP_SET(queue_position);
	CMP_SET(moving_storage);
	CMP_SET(announcing_to_trackers);
	CMP_SET(announcing_to_lsd);
	CMP_SET(announcing_to_dht);

	status = s;
}

namespace {
std::ostream& operator<<(std::ostream& os, lt::torrent_status::state_t s)
{
	return os << static_cast<int>(s);
}

std::ostream& operator<<(std::ostream& os, lt::time_point const t)
{
	return os << std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

std::ostream& operator<<(std::ostream& os, lt::storage_mode_t s)
{
	return os << static_cast<int>(s);
}
} // anonymous namespace

void torrent_history_entry::debug_print(frame_t const current_frame) const
{
	std::ostream& os = std::cout;

#define PRINT(x)                                                                                   \
	do {                                                                                           \
		int const frame_diff = (std::min)(current_frame - frame[x], 20u);                          \
		os << (frame[x] >= current_frame ? "\x1b[41m" : "") << "\x1b[38;5;" << (255 - frame_diff)  \
		   << 'm' << status.x << "\x1b[0m ";                                                       \
	} while (0)

	PRINT(state);
	PRINT(flags);
	PRINT(is_seeding);
	PRINT(is_finished);
	PRINT(has_metadata);
	PRINT(progress);
	PRINT(progress_ppm);
	PRINT(errc);
	PRINT(error_file);
	//		PRINT(save_path);
	PRINT(name);
	//		PRINT(next_announce);
	PRINT(current_tracker);
	PRINT(total_download);
	PRINT(total_upload);
	PRINT(total_payload_download);
	PRINT(total_payload_upload);
	PRINT(total_failed_bytes);
	PRINT(total_redundant_bytes);
	PRINT(download_rate);
	PRINT(upload_rate);
	PRINT(download_payload_rate);
	PRINT(upload_payload_rate);
	PRINT(num_seeds);
	PRINT(num_peers);
	PRINT(num_complete);
	PRINT(num_incomplete);
	PRINT(list_seeds);
	PRINT(list_peers);
	PRINT(connect_candidates);
	PRINT(num_pieces);
	PRINT(total_done);
	PRINT(total);
	PRINT(total_wanted_done);
	PRINT(total_wanted);
	PRINT(distributed_full_copies);
	PRINT(distributed_fraction);
	PRINT(block_size);
	PRINT(num_uploads);
	PRINT(num_connections);
	PRINT(uploads_limit);
	PRINT(connections_limit);
	PRINT(storage_mode);
	PRINT(up_bandwidth_queue);
	PRINT(down_bandwidth_queue);
	PRINT(all_time_upload);
	PRINT(all_time_download);
	PRINT(active_duration);
	PRINT(finished_duration);
	PRINT(seeding_duration);
	PRINT(seed_rank);
	PRINT(has_incoming);
	PRINT(added_time);
	PRINT(completed_time);
	PRINT(last_seen_complete);
	PRINT(last_upload);
	PRINT(last_download);
	PRINT(queue_position);
	PRINT(moving_storage);
	PRINT(announcing_to_trackers);
	PRINT(announcing_to_lsd);
	PRINT(announcing_to_dht);

	os << "\x1b[0m\n";
}
} // namespace ltweb
