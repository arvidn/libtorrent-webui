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
#include "alert_observer.hpp"

#include <set>
#include <string>
#include <mutex>

#include <sqlite3.h>

namespace ltweb
{
	struct alert_handler;

	struct save_resume : alert_observer
	{
		save_resume(lt::session& s, std::string const& resume_file, alert_handler* alerts);
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
		sqlite3* m_db;

		// all torrents currently loaded
		std::set<lt::torrent_handle> m_torrents;

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
}

#endif

