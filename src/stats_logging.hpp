/*

Copyright (c) 2014, 2017, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_STATS_LOGGING_HPP
#define LTWEB_STATS_LOGGING_HPP

#include "alert_observer.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/fwd.hpp"
#include <stdio.h>

namespace ltweb
{
	struct alert_handler;

/// writes logs to directory 'session_stats' in current working directory.
/// logs are rotated each hour. Use parse_session_stats.py to parse logs.
struct stats_logging : alert_observer
{
	stats_logging(lt::session& s, alert_handler* h);
	~stats_logging();

private:

	void rotate_stats_log();
	void handle_alert(lt::alert const* a);

	alert_handler* m_alerts;

	// the last time we rotated the log file
	lt::time_point m_last_log_rotation;

	lt::session& m_ses;

	FILE* m_stats_logger;
	// sequence number for log file. Log files are
	// rotated every hour and the sequence number is
	// incremented by one
	int m_log_seq;
};

}

#endif

