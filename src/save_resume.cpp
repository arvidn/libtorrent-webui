/*

Copyright (c) 2012-2014, 2017-2018, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "save_resume.hpp"
#include "save_settings.hpp" // for load_file and save_file

#include <functional>

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"

#include "alert_handler.hpp"
#include "hex.hpp"

namespace s = std::placeholders;

namespace ltweb
{

namespace {

// Produce a 40-hex-char key compatible with the existing DB schema.
// get_best() returns the v1 SHA-1 hash when present, or the first 20 bytes
// of the v2 SHA-256 hash for v2-only torrents — always 20 bytes / 40 hex chars.
std::string info_hash_key(lt::info_hash_t const& ih)
{
	lt::sha1_hash const best = ih.get_best();
	return to_hex(lt::span<char const>{best.data(), lt::sha1_hash::size()});
}

} // anonymous namespace

save_resume::save_resume(lt::session& s, std::string const& resume_file, alert_handler* alerts)
	: m_ses(s)
	, m_alerts(alerts)
	, m_cursor(m_torrents.begin())
	, m_last_save(lt::clock_type::now())
	, m_interval(lt::minutes(15))
	, m_num_in_flight(0)
	, m_shutting_down(false)
{
	int ret = sqlite3_open(resume_file.c_str(), &m_db);
	if (ret != SQLITE_OK)
	{
		fprintf(stderr, "Can't open resume file [%s]: %s\n"
			, resume_file.c_str(), sqlite3_errmsg(m_db));
		return;
	}

	m_alerts->subscribe(this, 0, lt::add_torrent_alert::alert_type
		, lt::torrent_removed_alert::alert_type
		, lt::save_resume_data_alert::alert_type
		, lt::save_resume_data_failed_alert::alert_type
		, lt::metadata_received_alert::alert_type
		, lt::torrent_finished_alert::alert_type
		, 0);

	ret = sqlite3_exec(m_db, "CREATE TABLE TORRENTS("
		"INFOHASH STRING PRIMARY KEY NOT NULL,"
		"RESUME BLOB NOT NULL);", nullptr, 0, nullptr);
	if (ret != SQLITE_OK)
	{
		fprintf(stderr, "Failed to create table: %s\n"
			, sqlite3_errmsg(m_db));
	}

	// ignore errors, since the table is likely to already
	// exist (and sqlite doesn't give a reasonable way to
	// know what failed programatically).
}

save_resume::~save_resume()
{
	m_alerts->unsubscribe(this);
	sqlite3_close(m_db);
	m_db = nullptr;
}

void save_resume::handle_alert(lt::alert const* a) try
{
	lt::add_torrent_alert const* ta = lt::alert_cast<lt::add_torrent_alert>(a);
	lt::torrent_removed_alert const* td = lt::alert_cast<lt::torrent_removed_alert>(a);
	lt::save_resume_data_alert const* sr = lt::alert_cast<lt::save_resume_data_alert>(a);
	lt::save_resume_data_failed_alert const* sf = lt::alert_cast<lt::save_resume_data_failed_alert>(a);
	lt::metadata_received_alert const* mr = lt::alert_cast<lt::metadata_received_alert>(a);
	lt::torrent_finished_alert const* tf = lt::alert_cast<lt::torrent_finished_alert>(a);
	if (ta)
	{
		lt::torrent_status st = ta->handle.status(lt::torrent_handle::query_name);
		printf("added torrent: %s\n", st.name.c_str());
		m_torrents.insert(ta->handle);
		if (st.has_metadata)
		{
			ta->handle.save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified);
			++m_num_in_flight;
		}

		if (m_cursor == m_torrents.end())
			m_cursor = m_torrents.begin();
	}
	else if (mr)
	{
		mr->handle.save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified);
		++m_num_in_flight;
	}
	else if (tf)
	{
		tf->handle.save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified);
		++m_num_in_flight;
	}
	else if (td)
	{
		bool wrapped = false;
		auto i = m_torrents.find(td->handle);
		if (i == m_torrents.end())
		{
			// this is a bit sad...
			// maybe this should be a bimap, so we can look it up by info-hash as well
			for (i = m_torrents.begin(); i != m_torrents.end(); ++i)
			{
				if (!i->is_valid()) continue;
				if (i->info_hashes() == td->info_hashes)
					break;
			}
			if (i == m_torrents.end()) return;
		}
		if (m_cursor == i)
		{
			++m_cursor;
			if (m_cursor == m_torrents.end())
				wrapped = true;
		}

		m_torrents.erase(i);
		if (wrapped)
			m_cursor = m_torrents.begin();

		// we need to delete the resume file from the resume directory
		// as well, to prevent it from being reloaded on next startup
		sqlite3_stmt* stmt = nullptr;
		int ret = sqlite3_prepare_v2(m_db, "DELETE FROM TORRENTS WHERE INFOHASH = :ih;", -1, &stmt, nullptr);
		if (ret != SQLITE_OK)
		{
			printf("failed to prepare remove statement: %s\n", sqlite3_errmsg(m_db));
			return;
		}
		std::string const ih = info_hash_key(td->info_hashes);
		ret = sqlite3_bind_text(stmt, 1, ih.c_str(), 40, SQLITE_STATIC);
		if (ret != SQLITE_OK)
		{
			printf("failed to bind remove statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		ret = sqlite3_step(stmt);
		if (ret != SQLITE_DONE)
		{
			printf("failed to step remove statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}

		sqlite3_finalize(stmt);
		printf("removing %s\n", ih.c_str());
	}
	else if (sr)
	{
		TORRENT_ASSERT(m_num_in_flight > 0);
		--m_num_in_flight;
		std::vector<char> buf = write_resume_data_buf(sr->params);

		sqlite3_stmt* stmt = nullptr;
		int ret = sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO TORRENTS(INFOHASH,RESUME) "
			"VALUES(?, ?);", -1, &stmt, nullptr);
		if (ret != SQLITE_OK)
		{
			fprintf(stderr, "failed to prepare insert statement: %s\n", sqlite3_errmsg(m_db));
			return;
		}

		std::string const ih = info_hash_key(sr->params.info_hashes);
		ret = sqlite3_bind_text(stmt, 1, ih.c_str(), 40, SQLITE_STATIC);
		if (ret != SQLITE_OK)
		{
			printf("failed to bind insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		ret = sqlite3_bind_blob(stmt, 2, buf.data(), buf.size(), SQLITE_STATIC);
		if (ret != SQLITE_OK)
		{
			printf("failed to bind insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}

		ret = sqlite3_step(stmt);
		if (ret != SQLITE_DONE)
		{
			printf("failed to step insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		sqlite3_finalize(stmt);
		printf("saving %s\n", ih.c_str());
	}
	else if (sf)
	{
		TORRENT_ASSERT(m_num_in_flight > 0);
		--m_num_in_flight;
	}
}
catch (std::exception const&) {}

void save_resume::tick() try
{
	// is it time to save resume data for another torrent?
	if (m_torrents.empty()) return;

	if (m_shutting_down) return;

	// calculate how many torrents we should save this tick. It depends on
	// how long since we last tried to save one. Every m_interval seconds,
	// we should have saved all torrents
	int num_torrents = m_torrents.size();
	int seconds_since_last = lt::total_seconds(lt::clock_type::now() - m_last_save);
	int num_to_save = num_torrents * seconds_since_last /
		lt::total_seconds(m_interval);

	// never save more than all torrents
	num_to_save = (std::min)(num_to_save, num_torrents);

	if (num_to_save > 0)
	{
		printf("saving resume data. [ time: %ds num-torrents: %d interval: %ds ]\n"
			, seconds_since_last, num_to_save, int(lt::total_seconds(m_interval)));
	}

	while (num_to_save > 0)
	{
		if (m_cursor == m_torrents.end())
			m_cursor = m_torrents.begin();

		m_cursor->save_resume_data(lt::torrent_handle::save_info_dict
			| lt::torrent_handle::only_if_modified);
		printf("saving resume data for: %s\n", m_cursor->status().name.c_str());
		++m_num_in_flight;
		--num_to_save;
		++m_cursor;

		m_last_save = lt::clock_type::now();
	}
}
catch (std::exception const&) {}

void save_resume::save_all()
{
	for (auto i = m_torrents.begin(), end(m_torrents.end()); i != end; ++i)
	{
		i->save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified);
		++m_num_in_flight;
	}
	m_shutting_down = true;
}

bool save_resume::ok_to_quit() const
{
	static int spinner = 0;
	static const char bar[] = "|/-\\";
	printf("\r%d %c\x1b[K", m_num_in_flight, bar[spinner]);
	fflush(stdout);
	spinner = (spinner + 1) & 3;
	return m_num_in_flight == 0;
}

void save_resume::load(lt::error_code& ec)
{
	ec.clear();
	sqlite3_stmt* stmt = nullptr;
	int ret = sqlite3_prepare_v2(m_db, "SELECT RESUME FROM TORRENTS;", -1, &stmt, nullptr);
	if (ret != SQLITE_OK)
	{
		// TODO: improve error reporting
		fprintf(stderr, "failed to prepare select statement: %s\n", sqlite3_errmsg(m_db));
		return;
	}

	ret = sqlite3_step(stmt);
	while (ret == SQLITE_ROW)
	{
		int bytes = sqlite3_column_bytes(stmt, 0);
		if (bytes > 0)
		{
			void const* buffer = sqlite3_column_blob(stmt, 0);
			lt::error_code ec;
			lt::add_torrent_params p = lt::read_resume_data({static_cast<char const*>(buffer), bytes}, ec);
			if (ec) continue;
			m_ses.async_add_torrent(std::move(p));
		}

		ret = sqlite3_step(stmt);
	}
	if (ret != SQLITE_DONE)
	{
		// TODO: improve error reporting
		printf("failed to step select statement: %s\n", sqlite3_errmsg(m_db));
		sqlite3_finalize(stmt);
		return;
	}
	sqlite3_finalize(stmt);
}

}

