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
	// Minimal session: networking disabled, not needed for these tests
	lt::settings_pack sp;
	sp.set_bool(lt::settings_pack::enable_dht, false);
	sp.set_bool(lt::settings_pack::enable_lsd, false);
	sp.set_bool(lt::settings_pack::enable_upnp, false);
	sp.set_bool(lt::settings_pack::enable_natpmp, false);
	sp.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
	lt::session ses(sp);

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	// Four distinct hash identities covering all torrent types:
	//   v1_hash  -- v1-only (sha1 only)
	//   v2_hash  -- v2-only (sha256 only, v1 left zero)
	//   hy_v1/hy_v2 -- hybrid (both sha1 and sha256 set)
	//   tr_v2    -- v2 hash whose first 20 bytes will be stored as a v1 "truncated" hash
	lt::sha1_hash const v1_hash = make_v1(0x11);
	lt::sha256_hash const v2_hash = make_v2(0x22);
	lt::sha1_hash const hy_v1 = make_v1(0x33);
	lt::sha256_hash const hy_v2 = make_v2(0x44);
	lt::sha256_hash const tr_v2 = make_v2(0x55);
	// The truncated form stores only the first 20 bytes of tr_v2 as a sha1
	lt::sha1_hash const tr_as_v1(tr_v2.data());

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

	// Add a torrent whose "v1" hash is really the first 20 bytes of a v2 hash.
	p.info_hashes = lt::info_hash_t(tr_as_v1);
	lt::torrent_handle h4 = ses.add_torrent(p);
	(void)h4;

	wait_for(ses, handler, 4, lt::add_torrent_alert::alert_type);

	// All four torrents must appear in the history
	{
		std::vector<lt::torrent_status> all;
		history.updated_since(0, all);
		BOOST_TEST(all.size() == 4);
	}

	// v1-only lookup by sha1 hash
	{
		auto const st = history.get_torrent_status(v1_hash);
		BOOST_TEST((st.info_hashes == lt::info_hash_t(v1_hash)));
	}

	// v2-only lookup by full sha256 hash (direct match path)
	{
		auto const st = history.get_torrent_status(v2_hash);
		BOOST_TEST((st.info_hashes == lt::info_hash_t(v2_hash)));
	}

	// Truncated-v2 lookup: given a full sha256 hash, find a torrent that
	// was stored with only its first 20 bytes as a sha1 (v2 is zero).
	{
		auto const st = history.get_torrent_status(tr_v2);
		BOOST_TEST((st.info_hashes == lt::info_hash_t(tr_as_v1)));
	}

	// Hybrid torrent appears in updated_since
	{
		std::vector<lt::torrent_status> all;
		history.updated_since(0, all);
		bool found_hybrid = false;
		for (auto const& s : all)
			if (s.info_hashes == lt::info_hash_t(hy_v1, hy_v2))
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

	// updated_since with the current frame returns nothing new
	{
		ltweb::frame_t const f = history.frame();
		std::vector<lt::torrent_status> empty;
		history.updated_since(f, empty);
		BOOST_TEST(empty.empty());
	}

	// Removing a torrent records it in removed_since
	{
		ltweb::frame_t const f = history.frame();
		ses.remove_torrent(h1);
		wait_for(ses, handler, 1, lt::torrent_removed_alert::alert_type);

		auto const removed = history.removed_since(f);
		BOOST_TEST(removed.size() == 1);
		if (!removed.empty())
			BOOST_TEST((removed[0] == lt::info_hash_t(v1_hash)));
	}

	// After removal the torrent no longer shows up in updated_since
	{
		std::vector<lt::torrent_status> remaining;
		history.updated_since(0, remaining);
		BOOST_TEST(remaining.size() == 3);
		for (auto const& s : remaining)
			BOOST_TEST((s.info_hashes != lt::info_hash_t(v1_hash)));
	}
}
