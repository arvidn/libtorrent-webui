/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE torrent_history
#include <boost/test/included/unit_test.hpp>

#include "torrent_history.hpp"
#include "alert_handler.hpp"

#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_status.hpp>

#include <chrono>
#include <cstring>
#include <vector>

namespace {

// Pop and dispatch all pending alerts, returning only after at least `n`
// alerts of the given `type` have been dispatched.
void wait_for(lt::session& ses, ltweb::alert_handler& handler, int n, int const type)
{
	while (n > 0)
	{
		ses.wait_for_alert(std::chrono::seconds(10));
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
			if (a->type() == type) --n;
		handler.dispatch_alerts(alerts);
	}
}

lt::settings_pack make_settings_pack()
{
	// Minimal session: networking disabled, not needed for these tests
	lt::settings_pack sp;
	sp.set_bool(lt::settings_pack::enable_dht, false);
	sp.set_bool(lt::settings_pack::enable_lsd, false);
	sp.set_bool(lt::settings_pack::enable_upnp, false);
	sp.set_bool(lt::settings_pack::enable_natpmp, false);
	sp.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
	return sp;
}

lt::sha1_hash make_v1(unsigned char fill)
{
	lt::sha1_hash h;
	std::memset(h.data(), static_cast<int>(fill), static_cast<std::size_t>(lt::sha1_hash::size()));
	return h;
}

lt::sha256_hash make_v2(unsigned char fill)
{
	lt::sha256_hash h;
	std::memset(h.data(), static_cast<int>(fill), static_cast<std::size_t>(lt::sha256_hash::size()));
	return h;
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(integration)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	// Three distinct hash identities covering all torrent types:
	//   v1_hash  -- v1-only (sha1 only)
	//   v2_hash  -- v2-only (sha256 only, v1 left zero)
	//   hy_v1/hy_v2 -- hybrid (both sha1 and sha256 set)
	lt::sha1_hash const v1_hash = make_v1(0x11);
	lt::sha256_hash const v2_hash = make_v2(0x22);
	lt::sha1_hash const hy_v1 = make_v1(0x33);
	lt::sha256_hash const hy_v2 = make_v2(0x44);

	lt::add_torrent_params p;
	p.save_path = ".";

	// Add v1-only torrent
	p.info_hashes = lt::info_hash_t(v1_hash);
	lt::torrent_handle h1 = ses.add_torrent(p);

	// Add v2-only torrent (v1 stays zero-initialised)
	p.info_hashes = lt::info_hash_t(v2_hash);
	lt::torrent_handle h2 = ses.add_torrent(p);
	(void)h2;

	// Add hybrid torrent (both v1 and v2 set)
	p.info_hashes = lt::info_hash_t(hy_v1, hy_v2);
	lt::torrent_handle h3 = ses.add_torrent(p);
	(void)h3;

	wait_for(ses, handler, 3, lt::add_torrent_alert::alert_type);

	// All three torrents must appear in the history
	{
		auto const r = history.query(0);
		BOOST_TEST(r.updated.size() == 3);
	}

	// v1-only lookup by sha1 hash
	{
		auto const st = history.get_torrent_status(v1_hash);
		BOOST_TEST((st.info_hashes == lt::info_hash_t(v1_hash)));
	}

	// Hybrid torrent appears in query
	{
		auto const r = history.query(0);
		bool found_hybrid = false;
		for (auto const& e : r.updated)
			if (e.status.info_hashes == lt::info_hash_t(hy_v1, hy_v2))
				found_hybrid = true;
		BOOST_TEST(found_hybrid);
	}

	// Lookup of an unknown hash returns a torrent_status with an invalid handle
	{
		auto const st = history.get_torrent_status(make_v1(0xff));
		BOOST_TEST(!st.handle.is_valid());
	}

	// state_update_alert advances the frame counter
	ltweb::frame_t const frame_before_update = history.frame();
	ses.post_torrent_updates();
	wait_for(ses, handler, 1, lt::state_update_alert::alert_type);
	{
		ltweb::frame_t const f = history.frame();
		BOOST_TEST(f > frame_before_update);
	}

	// query with the current frame returns nothing new
	{
		ltweb::frame_t const f = history.frame();
		auto const r = history.query(f);
		BOOST_TEST(r.updated.empty());
	}

	// Removing a torrent records it in query().removed
	{
		ltweb::frame_t const f = history.frame();
		ses.remove_torrent(h1);
		wait_for(ses, handler, 1, lt::torrent_removed_alert::alert_type);

		auto const r = history.query(f);
		BOOST_TEST(r.removed.size() == 1);
		if (!r.removed.empty())
			BOOST_TEST((r.removed[0] == v1_hash));
	}

	// After removal the torrent no longer shows up in query
	{
		auto const r = history.query(0);
		BOOST_TEST(r.updated.size() == 2);
		for (auto const& e : r.updated)
			BOOST_TEST((e.status.info_hashes != lt::info_hash_t(v1_hash)));
	}
}

// A torrent that is added and removed between two client polls should not
// appear in "removed" for a client whose last frame predates the add.
BOOST_AUTO_TEST_CASE(removed_before_client_saw_add)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	// Snapshot the frame before the torrent ever existed.
	ltweb::frame_t const f0 = history.frame();

	lt::add_torrent_params p;
	p.save_path = ".";
	p.info_hashes = lt::info_hash_t(make_v1(0xbb));
	lt::torrent_handle h = ses.add_torrent(p);
	wait_for(ses, handler, 1, lt::add_torrent_alert::alert_type);

	// Advance the frame counter so the add and the snapshot below land in
	// a distinct frame from f0.
	ses.post_torrent_updates();
	wait_for(ses, handler, 1, lt::state_update_alert::alert_type);
	ltweb::frame_t const f1 = history.frame();
	BOOST_TEST(f1 > f0);

	// Now remove the torrent without any client having polled at f1.
	ses.remove_torrent(h);
	wait_for(ses, handler, 1, lt::torrent_removed_alert::alert_type);

	// A client at f0 never saw the torrent — it should not receive a removal.
	{
		auto const r = history.query(f0);
		BOOST_TEST(r.removed.empty());
	}

	// A client at f1 (after the add) should receive the removal.
	{
		auto const r = history.query(f1);
		BOOST_TEST(r.removed.size() == 1);
		if (!r.removed.empty())
			BOOST_TEST((r.removed[0] == make_v1(0xbb)));
	}
}

// When tombstones overflow the limit they are evicted and the horizon advances.
// Any query with since_frame < horizon() must be treated as a full snapshot
BOOST_AUTO_TEST_CASE(horizon_after_tombstone_eviction)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	// Limit tombstones to 2 so that the third removal triggers eviction.
	ltweb::torrent_history history(&handler, 2);

	lt::add_torrent_params p;
	p.save_path = ".";

	// Add four torrents; h4 will survive all removals and act as a witness.
	p.info_hashes = lt::info_hash_t(make_v1(0x0a));
	lt::torrent_handle h1 = ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(make_v1(0x0b));
	lt::torrent_handle h2 = ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(make_v1(0x0c));
	lt::torrent_handle h3 = ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(make_v1(0x0d));
	lt::torrent_handle h4 = ses.add_torrent(p);
	(void)h4;
	wait_for(ses, handler, 4, lt::add_torrent_alert::alert_type);

	ses.post_torrent_updates();
	wait_for(ses, handler, 1, lt::state_update_alert::alert_type);
	ltweb::frame_t const f_client = history.frame();

	// Before any removals the horizon is 0.
	BOOST_TEST(history.horizon() == 0u);

	// Remove three of the four; the third removal pushes m_removed past the
	// limit of 2, evicting the oldest tombstone and advancing the horizon.
	ses.remove_torrent(h1);
	ses.remove_torrent(h2);
	ses.remove_torrent(h3);
	wait_for(ses, handler, 3, lt::torrent_removed_alert::alert_type);

	BOOST_TEST(history.horizon() > 0u);

	// A stale client (f_client < horizon) gets a snapshot: removed is empty
	// (since_frame clamped to 0; no added_frame satisfies added_frame <= 0).
	{
		auto const r = history.query(f_client);
		BOOST_TEST(r.is_snapshot);
		BOOST_TEST(r.removed.empty());
		// updated contains all live torrents (just h4); clamping to 0 is
		// what makes it return h4 rather than nothing.
		BOOST_TEST(r.updated.size() == 1u);
	}

	// Consistent with a genuine snapshot query (frame == 0).
	{
		auto const r = history.query(0);
		BOOST_TEST(r.is_snapshot);
		BOOST_TEST(r.updated.size() == 1u);
	}
}
