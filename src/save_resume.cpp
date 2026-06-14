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
#include "torrent_history.hpp"

namespace s = std::placeholders;

namespace ltweb {

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

save_resume::save_resume(
	lt::session& s, std::string const& resume_file, alert_handler* alerts, torrent_history& hist
)
	: m_ses(s)
	, m_alerts(alerts)
	, m_hist(hist)
	, m_cursor(m_torrents.begin())
	, m_last_save(lt::clock_type::now())
	, m_interval(lt::minutes(15))
	, m_num_in_flight(0)
	, m_shutting_down(false)
{
	int ret = sqlite3_open(resume_file.c_str(), &m_db);
	if (ret != SQLITE_OK) {
		fprintf(
			stderr, "Can't open resume file [%s]: %s\n", resume_file.c_str(), sqlite3_errmsg(m_db)
		);
		return;
	}

	m_alerts->subscribe<
		lt::add_torrent_alert,
		lt::torrent_removed_alert,
		lt::save_resume_data_alert,
		lt::save_resume_data_failed_alert,
		lt::metadata_received_alert,
		lt::torrent_finished_alert,
		lt::state_update_alert>(this);

	ret = sqlite3_exec(
		m_db,
		"CREATE TABLE TORRENTS("
		"INFOHASH STRING PRIMARY KEY NOT NULL,"
		"RESUME BLOB NOT NULL,"
		"QUEUE_POSITION INTEGER,"
		"TAG INTEGER);",
		nullptr,
		0,
		nullptr
	);
	if (ret != SQLITE_OK) {
		fprintf(stderr, "Failed to create table: %s\n", sqlite3_errmsg(m_db));
	}

	// ignore errors, since the table is likely to already
	// exist (and sqlite doesn't give a reasonable way to
	// know what failed programatically).

	// Additive schema migration for databases created before QUEUE_POSITION
	// existed. On a fresh DB the column is already in the CREATE above and
	// this ALTER fails with "duplicate column name"; on an already-migrated
	// legacy DB it also fails with the same error. Either way we ignore the
	// result -- the column is nullable, so legacy rows read back as NULL
	// until the next save rewrites them.
	sqlite3_exec(
		m_db, "ALTER TABLE TORRENTS ADD COLUMN QUEUE_POSITION INTEGER;", nullptr, nullptr, nullptr
	);

	// Same additive-migration pattern for the TAG column. Legacy rows read
	// back as NULL until the next save rewrites them; the load path treats
	// NULL as tag 0 (the default for a never-tagged torrent).
	sqlite3_exec(m_db, "ALTER TABLE TORRENTS ADD COLUMN TAG INTEGER;", nullptr, nullptr, nullptr);
}

save_resume::~save_resume()
{
	m_alerts->unsubscribe(this);
	sqlite3_close(m_db);
	m_db = nullptr;
}

void save_resume::handle_alert(lt::alert const* a)
try {
	lt::add_torrent_alert const* ta = lt::alert_cast<lt::add_torrent_alert>(a);
	lt::torrent_removed_alert const* td = lt::alert_cast<lt::torrent_removed_alert>(a);
	lt::save_resume_data_alert const* sr = lt::alert_cast<lt::save_resume_data_alert>(a);
	lt::save_resume_data_failed_alert const* sf =
		lt::alert_cast<lt::save_resume_data_failed_alert>(a);
	lt::metadata_received_alert const* mr = lt::alert_cast<lt::metadata_received_alert>(a);
	lt::torrent_finished_alert const* tf = lt::alert_cast<lt::torrent_finished_alert>(a);
	lt::state_update_alert const* su = lt::alert_cast<lt::state_update_alert>(a);
	if (ta) {
		lt::torrent_status st = ta->handle.status(lt::torrent_handle::query_name);
		printf("added torrent: %s\n", st.name.c_str());
		m_torrents.insert(ta->handle);
		if (st.has_metadata) {
			ta->handle.save_resume_data(
				lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified
			);
			++m_num_in_flight;
		}

		if (m_cursor == m_torrents.end()) m_cursor = m_torrents.begin();

		// If this torrent was loaded from disk and carried a persisted tag,
		// apply it now -- torrent_history's add_torrent_alert handler ran
		// before us (it subscribed first), so the entry is already in
		// m_queue and set_tag can find it. Runtime additions never appear
		// in m_pending_tags, so the lookup is a benign miss.
		lt::sha1_hash const ih = ta->handle.info_hashes().get_best();
		auto const pt = m_pending_tags.find(ih);
		if (pt != m_pending_tags.end()) {
			m_hist.set_tag(ih, pt->second, ~std::uint64_t(0));
			m_pending_tags.erase(pt);
		}
	} else if (mr) {
		mr->handle.save_resume_data(
			lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified
		);
		++m_num_in_flight;
	} else if (tf) {
		tf->handle.save_resume_data(
			lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified
		);
		++m_num_in_flight;
	} else if (td) {
		bool wrapped = false;
		auto i = m_torrents.find(td->handle);
		if (i == m_torrents.end()) {
			// this is a bit sad...
			// maybe this should be a bimap, so we can look it up by info-hash as well
			for (i = m_torrents.begin(); i != m_torrents.end(); ++i) {
				if (!i->is_valid()) continue;
				if (i->info_hashes() == td->info_hashes) break;
			}
			if (i == m_torrents.end()) return;
		}
		if (m_cursor == i) {
			++m_cursor;
			if (m_cursor == m_torrents.end()) wrapped = true;
		}

		m_torrents.erase(i);
		if (wrapped) m_cursor = m_torrents.begin();

		m_last_queue_pos.erase(td->info_hashes.get_best());

		// we need to delete the resume file from the resume directory
		// as well, to prevent it from being reloaded on next startup
		sqlite3_stmt* stmt = nullptr;
		int ret = sqlite3_prepare_v2(
			m_db, "DELETE FROM TORRENTS WHERE INFOHASH = :ih;", -1, &stmt, nullptr
		);
		if (ret != SQLITE_OK) {
			printf("failed to prepare remove statement: %s\n", sqlite3_errmsg(m_db));
			return;
		}
		std::string const ih = info_hash_key(td->info_hashes);
		ret = sqlite3_bind_text(stmt, 1, ih.c_str(), 40, SQLITE_STATIC);
		if (ret != SQLITE_OK) {
			printf("failed to bind remove statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		ret = sqlite3_step(stmt);
		if (ret != SQLITE_DONE) {
			printf("failed to step remove statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}

		sqlite3_finalize(stmt);
		printf("removing %s\n", ih.c_str());
	} else if (sr) {
		TORRENT_ASSERT(m_num_in_flight > 0);
		--m_num_in_flight;
		std::vector<char> buf = write_resume_data_buf(sr->params);

		sqlite3_stmt* stmt = nullptr;
		int ret = sqlite3_prepare_v2(
			m_db,
			"INSERT OR REPLACE INTO TORRENTS(INFOHASH,RESUME,QUEUE_POSITION,TAG) "
			"VALUES(?, ?, ?, ?);",
			-1,
			&stmt,
			nullptr
		);
		if (ret != SQLITE_OK) {
			fprintf(stderr, "failed to prepare insert statement: %s\n", sqlite3_errmsg(m_db));
			return;
		}

		std::string const ih = info_hash_key(sr->params.info_hashes);
		ret = sqlite3_bind_text(stmt, 1, ih.c_str(), 40, SQLITE_STATIC);
		if (ret != SQLITE_OK) {
			printf("failed to bind insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		ret = sqlite3_bind_blob(stmt, 2, buf.data(), buf.size(), SQLITE_STATIC);
		if (ret != SQLITE_OK) {
			printf("failed to bind insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		// queue_position is -1 (lt::no_pos) for seeds/finished torrents; we
		// store the sentinel as-is and rely on the load-side ORDER BY to put
		// such rows after the queued ones. This is a synchronous call into
		// the libtorrent network thread.
		int const qp = static_cast<int>(sr->handle.queue_position());
		lt::sha1_hash const sr_ih = sr->params.info_hashes.get_best();
		if (qp >= 0)
			m_last_queue_pos[sr_ih] = qp;
		else
			m_last_queue_pos.erase(sr_ih);
		ret = sqlite3_bind_int(stmt, 3, qp);
		if (ret != SQLITE_OK) {
			printf("failed to bind insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		// tag bitfield, stored as a 64-bit integer. We always write it, even
		// when 0 -- so a row that had a tag and then gets cleared is rewritten
		// with TAG = 0 rather than left at its previous value.
		std::uint64_t const tag = m_hist.get_tag(sr->handle);
		ret = sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(tag));
		if (ret != SQLITE_OK) {
			printf("failed to bind insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}

		ret = sqlite3_step(stmt);
		if (ret != SQLITE_DONE) {
			printf("failed to step insert statement: %s\n", sqlite3_errmsg(m_db));
			sqlite3_finalize(stmt);
			return;
		}
		sqlite3_finalize(stmt);
		printf("saving %s\n", ih.c_str());
	} else if (su) {
		// Queue position is not part of the resume blob, so a reorder by
		// itself never produces a save_resume_data_alert. We watch the
		// per-torrent status feed instead and emit a single-cell UPDATE
		// when the value diverges from the last one we persisted. Only
		// torrents that libtorrent reports as changed appear in
		// su->status, so we never scan the full set.
		sqlite3_stmt* stmt = nullptr;
		for (lt::torrent_status const& st : su->status) {
			int const qp = static_cast<int>(st.queue_position);
			lt::sha1_hash const ih = st.info_hashes.get_best();

			auto it = m_last_queue_pos.find(ih);
			if (it == m_last_queue_pos.end()) {
				// Seeds are not queued and stay unqueued for life, so
				// there is nothing to track. For a downloading torrent
				// we seed the cache without writing -- the value
				// either matches what we loaded from the DB or will be
				// picked up by the next full save_resume_data path.
				if (qp >= 0) m_last_queue_pos.emplace(ih, qp);
				continue;
			}
			if (it->second == qp) continue;

			if (!stmt) {
				int const pret = sqlite3_prepare_v2(
					m_db,
					"UPDATE TORRENTS SET QUEUE_POSITION = ? WHERE INFOHASH = ?;",
					-1,
					&stmt,
					nullptr
				);
				if (pret != SQLITE_OK) {
					fprintf(stderr, "failed to prepare qp update: %s\n", sqlite3_errmsg(m_db));
					return;
				}
			}
			std::string const ih_hex =
				to_hex(lt::span<char const>{ih.data(), lt::sha1_hash::size()});
			sqlite3_bind_int(stmt, 1, qp);
			sqlite3_bind_text(stmt, 2, ih_hex.c_str(), 40, SQLITE_TRANSIENT);
			if (sqlite3_step(stmt) != SQLITE_DONE) {
				fprintf(stderr, "failed to step qp update: %s\n", sqlite3_errmsg(m_db));
			}
			sqlite3_reset(stmt);
			sqlite3_clear_bindings(stmt);

			// Persist the new value, then drop the entry once the
			// torrent has transitioned to a seed -- it will not get a
			// non-negative queue_position again, so there is no point
			// keeping it in the cache.
			if (qp < 0)
				m_last_queue_pos.erase(it);
			else
				it->second = qp;
		}
		if (stmt) sqlite3_finalize(stmt);
	} else if (sf) {
		TORRENT_ASSERT(m_num_in_flight > 0);
		--m_num_in_flight;
	}
} catch (std::exception const&) {
}

void save_resume::tick()
try {
	// is it time to save resume data for another torrent?
	if (m_torrents.empty()) return;

	if (m_shutting_down) return;

	// calculate how many torrents we should save this tick. It depends on
	// how long since we last tried to save one. Every m_interval seconds,
	// we should have saved all torrents
	int num_torrents = m_torrents.size();
	int seconds_since_last = lt::total_seconds(lt::clock_type::now() - m_last_save);
	int num_to_save = num_torrents * seconds_since_last / lt::total_seconds(m_interval);

	// never save more than all torrents
	num_to_save = (std::min)(num_to_save, num_torrents);

	if (num_to_save > 0) {
		printf(
			"saving resume data. [ time: %ds num-torrents: %d interval: %ds ]\n",
			seconds_since_last,
			num_to_save,
			int(lt::total_seconds(m_interval))
		);
	}

	while (num_to_save > 0) {
		if (m_cursor == m_torrents.end()) m_cursor = m_torrents.begin();

		m_cursor->save_resume_data(
			lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified
		);
		printf("saving resume data for: %s\n", m_cursor->status().name.c_str());
		++m_num_in_flight;
		--num_to_save;
		++m_cursor;

		m_last_save = lt::clock_type::now();
	}
} catch (std::exception const&) {
}

void save_resume::save_all()
{
	for (auto i = m_torrents.begin(), end(m_torrents.end()); i != end; ++i) {
		i->save_resume_data(
			lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified
		);
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
	// Load in queue-position order so libtorrent assigns new positions in
	// the same order the user previously curated. The boolean first key
	// (NULL or negative) sorts legacy rows and seeds/finished torrents
	// after the properly-queued ones; among themselves their relative
	// order is unspecified, which is fine because libtorrent removes
	// seeds from the queue after the resume check.
	int ret = sqlite3_prepare_v2(
		m_db,
		"SELECT RESUME, QUEUE_POSITION, TAG FROM TORRENTS "
		"ORDER BY (QUEUE_POSITION IS NULL OR QUEUE_POSITION < 0), QUEUE_POSITION;",
		-1,
		&stmt,
		nullptr
	);
	if (ret != SQLITE_OK) {
		// TODO: improve error reporting
		fprintf(stderr, "failed to prepare select statement: %s\n", sqlite3_errmsg(m_db));
		return;
	}

	for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
		int bytes = sqlite3_column_bytes(stmt, 0);
		if (bytes <= 0) continue;

		void const* buffer = sqlite3_column_blob(stmt, 0);
		lt::error_code ec;
		lt::add_torrent_params p =
			lt::read_resume_data({static_cast<char const*>(buffer), bytes}, ec);
		if (ec) continue;

		// populate m_last_queue_pos so the first state_update_alert
		// compares against the persisted value. Skip NULL (unknown)
		// and negative sentinels (seeds have no position to track).
		if (sqlite3_column_type(stmt, 1) == SQLITE_INTEGER) {
			int const qp = sqlite3_column_int(stmt, 1);
			if (qp >= 0) m_last_queue_pos.emplace(p.info_hashes.get_best(), qp);
		}

		// Stash the persisted tag for the matching add_torrent_alert to
		// apply once torrent_history has an entry for this info-hash.
		// sqlite3_column_int64 returns 0 on a NULL column (legacy rows
		// that pre-date the TAG column), which is also the never-tagged
		// default, so the single tag != 0 check covers both cases.
		std::uint64_t const tag = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 2));
		if (tag != 0) m_pending_tags.emplace(p.info_hashes.get_best(), tag);

		m_ses.async_add_torrent(std::move(p));
	}
	if (ret != SQLITE_DONE) {
		// TODO: improve error reporting
		printf("failed to step select statement: %s\n", sqlite3_errmsg(m_db));
		sqlite3_finalize(stmt);
		return;
	}
	sqlite3_finalize(stmt);
}

} // namespace ltweb
