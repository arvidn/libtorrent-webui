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

		bool operator==(torrent_history_entry const& e) const { return e.status.info_hashes == status.info_hashes; }

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

		torrent_history_entry() {}

		torrent_history_entry(lt::torrent_status const& st, frame_t const f)
			: status(st)
		{
			frame.fill(f);
		}

		void debug_print(frame_t  current_frame) const;
	};

	inline std::size_t hash_value(torrent_history_entry const& te)
	{ return std::hash<lt::info_hash_t>{}(te.status.info_hashes); }

	struct torrent_history : alert_observer
	{

		torrent_history(alert_handler* h);
		~torrent_history();

		// returns the info-hashes of the torrents that have been
		// removed since the specified frame number
		std::vector<lt::info_hash_t> removed_since(frame_t frame) const;

		// returns the lt::torrent_status structures for the torrents
		// that have changed since the specified frame number
		void updated_since(frame_t frame, std::vector<lt::torrent_status>& torrents) const;

		void updated_fields_since(frame_t frame, std::vector<torrent_history_entry>& torrents) const;

		lt::torrent_status get_torrent_status(lt::sha1_hash const& ih) const;
		lt::torrent_status get_torrent_status(lt::sha256_hash const& ih) const;

		// the current frame number
		frame_t frame() const;

		virtual void handle_alert(lt::alert const* a);

	private:

		// first is the frame this torrent was last
		// seen modified in, second is the information
		// about the torrent that was modified
		typedef boost::bimap<boost::bimaps::list_of<frame_t>
			, boost::bimaps::unordered_set_of<torrent_history_entry> > queue_t;

		mutable std::mutex m_mutex;

		queue_t m_queue;

		std::deque<std::pair<frame_t, lt::info_hash_t> > m_removed;

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
	};
}

#endif

