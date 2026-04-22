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
#include <mutex> // for mutex
#include <deque>

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

namespace ltweb
{
	struct alert_handler;

	using frame_t = std::uint32_t;

	// this is the type that keeps track of frame counters for each
	// field in lt::torrent_status. The frame counters indicate which frame
	// they were last modified in. This is used to send minimal updates
	// of changes to torrents.
	struct torrent_history_entry
	{
		// this is the current state of the torrent
		lt::torrent_status status;

		void update_status(lt::torrent_status const& s, frame_t frame);

		bool operator==(torrent_history_entry const& e) const { return e.status.info_hashes.get_best() == status.info_hashes.get_best(); }

		enum
		{
			state,
			flags,
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

		void debug_print(frame_t  current_frame) const;
	};

	inline std::size_t hash_value(torrent_history_entry const& te)
	{ return std::hash<lt::sha1_hash>{}(te.status.info_hashes.get_best()); }

	struct torrent_history : alert_observer
	{

		torrent_history(alert_handler* h, std::size_t max_tombstones = 1000);
		~torrent_history();

		struct query_result
		{
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

		lt::torrent_status get_torrent_status(lt::sha1_hash const& ih) const;

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

		// first is the frame this torrent was last
		// seen modified in, second is the information
		// about the torrent that was modified
		typedef boost::bimap<boost::bimaps::list_of<frame_t>
			, boost::bimaps::unordered_set_of<torrent_history_entry> > queue_t;

		mutable std::mutex m_mutex;

		queue_t m_queue;

		struct removed_entry
		{
			frame_t removed_frame;
			frame_t added_frame;
			lt::sha1_hash ih;
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
}

#endif
