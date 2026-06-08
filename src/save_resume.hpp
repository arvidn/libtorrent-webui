/*

Copyright (c) 2012-2014, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_SAVE_RESUME_HPP
#define LTWEB_SAVE_RESUME_HPP

#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "alert_observer.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <mutex>
#include <unordered_map>

#include <sqlite3.h>

namespace ltweb {
struct alert_handler;
struct torrent_history;

struct save_resume : alert_observer {
	save_resume(
		lt::session& s, std::string const& resume_file, alert_handler* alerts, torrent_history& hist
	);
	~save_resume();

	void load(lt::error_code& ec);

	// implements alert_observer
	virtual void handle_alert(lt::alert const* a);

	void tick();
	void save_all();
	bool ok_to_quit() const;

private:
	lt::session& m_ses;
	alert_handler* m_alerts;
	torrent_history& m_hist;
	sqlite3* m_db;

	// all torrents currently loaded
	std::set<lt::torrent_handle> m_torrents;

	// Last queue_position value we persisted to the DB, keyed by info-hash.
	// queue_position is not part of the resume blob, so libtorrent's
	// only_if_modified check never triggers a save_resume_data_alert for a
	// pure reorder. We watch state_update_alert and emit a targeted UPDATE
	// of the QUEUE_POSITION column when the value diverges from this cache.
	// Only downloading torrents are tracked -- seeds have no_pos (-1) and
	// stay that way for life, so they are evicted after the transition is
	// persisted.
	std::unordered_map<lt::sha1_hash, int> m_last_queue_pos;

	// Tags read from disk in load() but not yet applied to torrent_history,
	// keyed by info-hash. The matching add_torrent_alert is the trigger that
	// drains an entry from this map into m_hist.set_tag(). Entries that never
	// have an add_torrent_alert (eg the torrent failed to load) leak the
	// uint64 value here until process exit -- acceptable since the map is
	// bounded by the resume file's row count.
	std::unordered_map<lt::sha1_hash, std::uint64_t> m_pending_tags;

	// the next torrent to save (may point to end)
	std::set<lt::torrent_handle>::iterator m_cursor;

	// the last time we visited a torrent to potentially
	// save its fast-resume
	lt::time_point m_last_save;

	// save resum data for all torrents every X seconds
	// must be at least 1
	lt::time_duration m_interval;

	int m_num_in_flight;

	// when set, we stop saving periodically, and just wait
	// for all outstanding saves to return.
	bool m_shutting_down;
};
} // namespace ltweb

#endif
