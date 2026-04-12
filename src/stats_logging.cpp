/*

Copyright (c) 2014-2015, 2017-2018, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <functional>
#include <iomanip>
#include <cstdio>

#include "stats_logging.hpp"
#include "libtorrent/session.hpp"
#include "alert_handler.hpp"
#include "libtorrent/session_stats.hpp"
#include <filesystem>

namespace ltweb {

using namespace std::placeholders;


stats_logging::stats_logging(lt::session& s, alert_handler* h)
	: m_alerts(h)
	, m_ses(s)
	, m_log_seq(0)
{
	m_alerts->subscribe(this, 0
		, lt::session_stats_alert::alert_type
		, 0);
	rotate_stats_log();
}

stats_logging::~stats_logging()
{
	m_alerts->unsubscribe(this);
}

void stats_logging::rotate_stats_log()
{
	if (m_stats_logger.is_open())
	{
		++m_log_seq;
		m_stats_logger.close();
	}

	{
		std::error_code fec;
		std::filesystem::create_directory("session_stats", fec);
		// TODO: report errors
	}
#ifdef TORRENT_WINDOWS
	const int pid = GetCurrentProcessId();
#else
	const int pid = getpid();
#endif
	char filename[100];
	snprintf(filename, sizeof(filename), "session_stats/%d.%04d.log", pid, m_log_seq);
	m_stats_logger.open(filename, std::ios::out | std::ios::trunc);
	m_last_log_rotation = lt::clock_type::now();
	if (!m_stats_logger)
	{
		std::fprintf(stderr, "Failed to create lt::session stats log file \"%s\"\n", filename);
		return;
	}

	std::vector<lt::stats_metric> cnts = lt::session_stats_metrics();
	std::sort(cnts.begin(), cnts.end()
		, [](lt::stats_metric const& lhs, lt::stats_metric const& rhs)
		{ return lhs.value_index < rhs.value_index; });

	int idx = 0;
	m_stats_logger << "second";
	for (auto const c : cnts)
	{
		// just in case there are some indices that don't have names
		// (it shouldn't really happen)
		while (idx < c.value_index)
		{
			m_stats_logger << ':';
			++idx;
		}

		m_stats_logger << ':' << c.name;
		++idx;
	}
	m_stats_logger << "\n\n";
}

void stats_logging::handle_alert(lt::alert const* a)
{
	lt::session_stats_alert const* s = lt::alert_cast<lt::session_stats_alert>(a);
	if (s == nullptr) return;

	if (lt::clock_type::now() - m_last_log_rotation > lt::hours(1))
		rotate_stats_log();

	if (!m_stats_logger)
		return;

	m_stats_logger << std::fixed << std::setprecision(6)
		<< double(lt::total_microseconds(s->timestamp() - m_last_log_rotation)) / 1000000.0;
	for (auto const v : s->counters())
		m_stats_logger << '\t' << v;
	m_stats_logger << '\n';
}

}

