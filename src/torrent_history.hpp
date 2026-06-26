/*

Copyright (c) 2012-2013, 2015, 2017-2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_TORRENT_HISTORY_HPP
#define LTWEB_TORRENT_HISTORY_HPP

#include "alert_observer.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/torrent_handle.hpp"
#include <mutex> // for mutex
#include <deque>
#include <unordered_map>
#include <utility>

#define BOOST_BIMAP_DISABLE_SERIALIZATION
// boost/bimap/detail/user_interface_config.hpp defines
// BOOST_MULTI_INDEX_DISABLE_SERIALIZATION unconditionally (no #ifndef guard).
// libtorrent's build propagates the same define via its usage-requirements,
// which causes a redefinition error with -Werror. Save and restore so the
// include can redefine it cleanly without leaking the undef to includers.
#pragma push_macro("BOOST_MULTI_INDEX_DISABLE_SERIALIZATION")
#undef BOOST_MULTI_INDEX_DISABLE_SERIALIZATION
#include <boost/bimap.hpp>
#pragma pop_macro("BOOST_MULTI_INDEX_DISABLE_SERIALIZATION")
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>

namespace ltweb {
struct alert_handler;

using frame_t = std::uint32_t;

// 8-bit projection of an lt::torrent_status for the server-side filter.
// Flicker-prone fields (announcing_to_*, has_incoming, moving_storage)
// are excluded so filter_inputs_stable_since stays useful.
std::uint8_t status_bits(lt::torrent_status const& s);

// Filter spec sent by the client in get-torrent-updates. The two axes
// match differently because they're used differently:
//
//   status axis: (sbits & status_mask) == (status_value & status_mask)
//                bits in status_mask must match status_value; bits
//                outside the mask are don't-care. Lets a single button
//                require "bit X set AND bit Y cleared" without inventing
//                composite-meaning bits in status_bits().
//   tag axis:    (tag & tag_mask) != 0
//                any-of within the mask. Matches the additive UX where
//                multiple tag toggles OR together.
//
// Both axes are ANDed. status_mask == 0 disables the status axis;
// tag_mask == 0 disables the tag axis. status_value is ignored when
// status_mask == 0.
struct filter_spec {
	std::uint8_t status_mask = 0;
	std::uint8_t status_value = 0;
	std::uint64_t tag_mask = 0;

	bool empty() const { return status_mask == 0 && tag_mask == 0; }
	bool operator==(filter_spec const& o) const
	{
		return status_mask == o.status_mask && status_value == o.status_value
			&& tag_mask == o.tag_mask;
	}
};

// this is the type that keeps track of frame counters for each
// field in lt::torrent_status. The frame counters indicate which frame
// they were last modified in. This is used to send minimal updates
// of changes to torrents.
struct torrent_history_entry {
	// this is the current state of the torrent
	lt::torrent_status status;

	void update_status(lt::torrent_status const& s, frame_t frame);

	bool operator==(torrent_history_entry const& e) const
	{
		return e.status.info_hashes.get_best() == status.info_hashes.get_best();
	}

	enum {
		state,
		// lt::torrent_status::flags is split so that toggling a flag the
		// filter does not care about (eg upload_mode after a disk error)
		// does not invalidate the stability check. status_flags covers
		// paused and auto_managed (consulted by filter_inputs_stable_since);
		// other_flags covers the rest. Both map to wire field 0.
		status_flags,
		other_flags,
		is_seeding,
		is_finished,
		has_metadata,
		progress,
		progress_ppm,
		errc,
		error_file,
		save_path,
		name,
		next_announce,
		current_tracker,
		total_download,
		total_upload,
		total_payload_download,
		total_payload_upload,
		total_failed_bytes,
		total_redundant_bytes,
		download_rate,
		upload_rate,
		download_payload_rate,
		upload_payload_rate,
		num_seeds,
		num_peers,
		num_complete,
		num_incomplete,
		list_seeds,
		list_peers,
		connect_candidates,
		num_pieces,
		total_done,
		total,
		total_wanted_done,
		total_wanted,
		distributed_full_copies,
		distributed_fraction,
		block_size,
		num_uploads,
		num_connections,
		num_undead_peers,
		uploads_limit,
		connections_limit,
		storage_mode,
		up_bandwidth_queue,
		down_bandwidth_queue,
		all_time_upload,
		all_time_download,
		active_duration,
		finished_duration,
		seeding_duration,
		seed_rank,
		has_incoming,
		added_time,
		completed_time,
		last_seen_complete,
		last_upload,
		last_download,
		queue_position,
		moving_storage,
		announcing_to_trackers,
		announcing_to_lsd,
		announcing_to_dht,

		// application-defined per-torrent bitfield set via the set-tag RPC.
		// not part of lt::torrent_status; the value lives in torrent_history's
		// m_tags map keyed by torrent_handle. only the per-field frame counter
		// is tracked here, so tag changes participate in the normal delta-update
		// machinery (relocate-on-update in m_queue).
		tag,

		num_fields,
	};

	// these are the frames each individual field was last changed
	std::array<frame_t, num_fields> frame;

	// the frame this entry was first added
	frame_t added_frame = 0;

	torrent_history_entry() {}

	torrent_history_entry(lt::torrent_status const& st, frame_t const f)
		: status(st)
		, added_frame(f)
	{
		frame.fill(f);
	}

	// True iff the inputs to status_bits() and tag have all been stable
	// since K, so the previous filter can be evaluated against the live
	// entry. When false, the caller must conservatively treat the client
	// as not having had the entry.
	bool filter_inputs_stable_since(frame_t K) const
	{
		return frame[state] <= K && frame[status_flags] <= K && frame[errc] <= K && frame[tag] <= K;
	}

	void debug_print(frame_t current_frame) const;
};

inline std::size_t hash_value(torrent_history_entry const& te)
{
	return std::hash<lt::sha1_hash>{}(te.status.info_hashes.get_best());
}

struct torrent_history : alert_observer {

	torrent_history(alert_handler* h, std::size_t max_tombstones = 1000);
	~torrent_history();

	struct query_result {
		// Exact frame number captured under the same lock as updated/removed.
		frame_t current_frame = 0;
		bool is_snapshot = false;
		std::vector<torrent_history_entry> updated;
		std::vector<lt::sha1_hash> removed;
	};

	// Returns all torrents updated since since_frame and all info-hashes
	// removed since since_frame, in a single locked operation.
	// If since_frame < horizon(), the result is promoted to a full snapshot:
	// is_snapshot is true, updated contains all live torrents, removed is empty.
	query_result query(frame_t since_frame) const;

	// Delta query that applies the filter inline so non-matching entries
	// are never copied. f_old is the spec used at since_frame, f_new is
	// the spec now; entries that fell out of the filter view join the
	// session-tombstone list in removed. Newly-matched entries are
	// returned with frame[] filled to current_frame so the serializer
	// includes every requested field. Both empty degenerates to query().
	query_result
	query_filtered(frame_t since_frame, filter_spec const& f_old, filter_spec const& f_new) const;

	lt::torrent_status get_torrent_status(lt::sha1_hash const& ih) const;

	// lightweight alternative to get_torrent_status for callers that only
	// need the queue position and handle. Returns {no_pos, invalid_handle}
	// when the info-hash is unknown.
	std::pair<lt::queue_position_t, lt::torrent_handle> get_queue_pos(lt::sha1_hash const& ih
	) const;

	// get-modify-set on the per-torrent tag bitfield.
	//   new_tag = (old_tag & ~mask) | (value & mask)
	// Returns true if the tag value actually changed (so the caller can
	// flag resume data dirty for persistence). Returns false when the
	// info-hash is unknown, when mask is 0, or when the masked bits were
	// already at the requested values. On a real change, the entry's
	// frame[tag] counter is bumped and the entry is relocated to the head
	// of m_queue.left so the next delta query picks it up.
	bool set_tag(lt::sha1_hash const& ih, std::uint64_t value, std::uint64_t mask);

	// Returns the tag value for h, or 0 if absent. Used by the
	// get-torrent-updates serializer and by save_resume.
	std::uint64_t get_tag(lt::torrent_handle const& h) const;

	// the current frame number
	frame_t frame() const;

	// the oldest frame for which delta queries are reliable.
	// Any query with since_frame < horizon() will be treated as a full
	// snapshot (since_frame == 0), because tombstones older than this
	// frame have been evicted.
	frame_t horizon() const;

	virtual void handle_alert(lt::alert const* a);

private:
	// Returns the current frame while holding m_mutex. If add/remove alerts
	// are pending in the deferred frame slot, consume that slot first so the
	// returned frame matches the frames stored on those entries.
	frame_t current_frame_locked() const;

	// Appends tombstones newer than since_frame that were visible at
	// since_frame (added_frame <= since_frame) and matched `filter` at
	// removal time. An empty filter matches everything. Skips the scan
	// when result.is_snapshot is true (no removes in a full snapshot).
	void append_removed(frame_t since_frame, filter_spec const& filter, query_result& result) const;

	// first is the frame this torrent was last
	// seen modified in, second is the information
	// about the torrent that was modified
	typedef boost::bimap<
		boost::bimaps::list_of<frame_t>,
		boost::bimaps::unordered_set_of<torrent_history_entry>>
		queue_t;

	mutable std::mutex m_mutex;

	queue_t m_queue;

	// per-torrent application-defined tag bitfield, set via the set-tag RPC.
	// keyed by torrent_handle (8-byte shared_ptr hash, materially cheaper
	// than a 20-byte sha1_hash). entries are erased when their torrent is
	// removed from the session. absent keys are treated as tag value 0.
	std::unordered_map<lt::torrent_handle, std::uint64_t> m_tags;

	struct removed_entry {
		frame_t removed_frame;
		frame_t added_frame;
		lt::sha1_hash ih;
		std::uint8_t sbits = 0;
		std::uint64_t tag = 0;
	};
	std::deque<removed_entry> m_removed;

	alert_handler* m_alerts;

	// frame counter. This is incremented every
	// time we get a status update for torrents
	mutable frame_t m_frame;

	// if we haven't gotten any status updates
	// but we have received add or delete alerts,
	// we increment the frame counter on access,
	// in order to make added and deleted event also
	// fall into distinct time-slots, instead of being
	// potentially returned twice, once when they
	// happen and once after we've received an
	// update and increment the frame counter
	mutable bool m_deferred_frame_count;

	// The oldest frame for which we still have complete tombstone data.
	// Advanced whenever tombstones are evicted from m_removed.
	frame_t m_horizon = 0;

	// Maximum number of tombstones to keep. Configurable for testing.
	std::size_t m_max_tombstones;
};
} // namespace ltweb

#endif
