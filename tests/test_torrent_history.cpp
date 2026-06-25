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
	while (n > 0) {
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
	std::memset(
		h.data(), static_cast<int>(fill), static_cast<std::size_t>(lt::sha256_hash::size())
	);
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
			if (e.status.info_hashes == lt::info_hash_t(hy_v1, hy_v2)) found_hybrid = true;
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
		if (!r.removed.empty()) BOOST_TEST((r.removed[0] == v1_hash));
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
		if (!r.removed.empty()) BOOST_TEST((r.removed[0] == make_v1(0xbb)));
	}
}

// query() must advance any deferred add/remove frame under the same lock as
// the payload snapshot, so the returned frame can be used as the next poll
// cursor without duplicating or skipping those events.
BOOST_AUTO_TEST_CASE(query_current_frame_matches_deferred_snapshot)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih = make_v1(0xaa);
	p.info_hashes = lt::info_hash_t(ih);

	ltweb::frame_t const f0 = history.frame();

	lt::torrent_handle h = ses.add_torrent(p);
	wait_for(ses, handler, 1, lt::add_torrent_alert::alert_type);

	auto const added = history.query(f0);
	BOOST_TEST(added.current_frame > f0);
	BOOST_TEST(added.updated.size() == 1u);
	if (!added.updated.empty())
		BOOST_TEST((added.updated[0].status.info_hashes == lt::info_hash_t(ih)));
	BOOST_TEST(history.frame() == added.current_frame);

	{
		auto const r = history.query(added.current_frame);
		BOOST_TEST(r.updated.empty());
		BOOST_TEST(r.removed.empty());
	}

	ses.remove_torrent(h);
	wait_for(ses, handler, 1, lt::torrent_removed_alert::alert_type);

	auto const removed = history.query(added.current_frame);
	BOOST_TEST(removed.current_frame > added.current_frame);
	BOOST_TEST(removed.updated.empty());
	BOOST_TEST(removed.removed.size() == 1u);
	if (!removed.removed.empty()) BOOST_TEST((removed.removed[0] == ih));
	BOOST_TEST(history.frame() == removed.current_frame);

	{
		auto const r = history.query(removed.current_frame);
		BOOST_TEST(r.updated.empty());
		BOOST_TEST(r.removed.empty());
	}
}

// Basic round-trip: set_tag returns true on a real change, get_tag returns the
// value that was stored, and get_tag returns 0 for any handle that has never
// had a tag set.
BOOST_AUTO_TEST_CASE(tag_basic_round_trip)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih = make_v1(0x10);
	p.info_hashes = lt::info_hash_t(ih);
	lt::torrent_handle h = ses.add_torrent(p);
	wait_for(ses, handler, 1, lt::add_torrent_alert::alert_type);

	// Default tag for a never-tagged torrent is 0.
	BOOST_TEST(history.get_tag(h) == 0u);

	// Setting a real value returns true.
	BOOST_TEST(history.set_tag(ih, 0x00ff, ~std::uint64_t(0)));
	BOOST_TEST(history.get_tag(h) == 0x00ffu);

	// Setting the same value via the same full mask is a no-op (returns false).
	BOOST_TEST(!history.set_tag(ih, 0x00ff, ~std::uint64_t(0)));
	BOOST_TEST(history.get_tag(h) == 0x00ffu);

	// Setting an unknown info-hash returns false and does not pollute m_tags.
	BOOST_TEST(!history.set_tag(make_v1(0xfe), 0x1, ~std::uint64_t(0)));
}

// Get-modify-set: the resulting tag is (old & ~mask) | (value & mask). Bits
// outside the mask are preserved, so two clients writing disjoint masks never
// step on each other.
BOOST_AUTO_TEST_CASE(tag_mask_semantics)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih = make_v1(0x20);
	p.info_hashes = lt::info_hash_t(ih);
	lt::torrent_handle h = ses.add_torrent(p);
	wait_for(ses, handler, 1, lt::add_torrent_alert::alert_type);

	// Start with 0xff in the low byte.
	BOOST_TEST(history.set_tag(ih, 0x00ff, 0x00ff));
	BOOST_TEST(history.get_tag(h) == 0x00ffu);

	// Clear the low nibble only: value=0, mask=0x0f.
	// new = (0xff & ~0x0f) | (0x00 & 0x0f) = 0xf0
	BOOST_TEST(history.set_tag(ih, 0x0000, 0x000f));
	BOOST_TEST(history.get_tag(h) == 0x00f0u);

	// Set the high nibble of the second byte without touching anything else.
	// new = (0x00f0 & ~0xf000) | (0xa000 & 0xf000) = 0xa0f0
	BOOST_TEST(history.set_tag(ih, 0xa000, 0xf000));
	BOOST_TEST(history.get_tag(h) == 0xa0f0u);

	// mask == 0 is a deliberate no-op regardless of value.
	BOOST_TEST(!history.set_tag(ih, ~std::uint64_t(0), 0));
	BOOST_TEST(history.get_tag(h) == 0xa0f0u);

	// Setting bits to the values they already have is also a no-op.
	BOOST_TEST(!history.set_tag(ih, 0xa000, 0xf000));
	BOOST_TEST(history.get_tag(h) == 0xa0f0u);

	// Driving the tag back to 0 still returns true (a real change) and the
	// next get_tag reads 0 again, matching the sparse-map convention where
	// "absent" and "zero" are indistinguishable.
	BOOST_TEST(history.set_tag(ih, 0, ~std::uint64_t(0)));
	BOOST_TEST(history.get_tag(h) == 0u);
}

// A tag change must surface in the next delta query: frame[tag] is bumped to
// a value greater than the caller's previous frame, and the entry is relocated
// to the head of m_queue so the early-break iteration finds it. Untouched
// torrents must not appear in the delta.
BOOST_AUTO_TEST_CASE(tag_delta_visible_in_query)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih_a = make_v1(0x30);
	lt::sha1_hash const ih_b = make_v1(0x31);
	p.info_hashes = lt::info_hash_t(ih_a);
	lt::torrent_handle ha = ses.add_torrent(p);
	(void)ha;
	p.info_hashes = lt::info_hash_t(ih_b);
	lt::torrent_handle hb = ses.add_torrent(p);
	(void)hb;
	wait_for(ses, handler, 2, lt::add_torrent_alert::alert_type);

	// Drain both torrents into a known frame the client has already seen.
	auto const initial = history.query(0);
	BOOST_TEST(initial.updated.size() == 2u);
	ltweb::frame_t const f_client = initial.current_frame;

	// No further changes -> query is empty.
	{
		auto const r = history.query(f_client);
		BOOST_TEST(r.updated.empty());
	}

	// Tag only A.
	BOOST_TEST(history.set_tag(ih_a, 0x42, ~std::uint64_t(0)));

	// The next delta should contain exactly A (B was not touched).
	{
		auto const r = history.query(f_client);
		BOOST_TEST(r.updated.size() == 1u);
		if (r.updated.size() == 1u) {
			BOOST_TEST((r.updated[0].status.info_hashes == lt::info_hash_t(ih_a)));
			// frame[tag] must be strictly greater than the caller's frame —
			// that's what makes the entry "new" from the client's perspective.
			BOOST_TEST(r.updated[0].frame[ltweb::torrent_history_entry::tag] > f_client);
			// of set_tag. The flag slot is now split into status_flags +
			// other_flags so the filter stability check can ignore changes
			// to flag bits it doesn't care about; check both slots here.
			BOOST_TEST(r.updated[0].frame[ltweb::torrent_history_entry::state] <= f_client);
			BOOST_TEST(r.updated[0].frame[ltweb::torrent_history_entry::status_flags] <= f_client);
			BOOST_TEST(r.updated[0].frame[ltweb::torrent_history_entry::other_flags] <= f_client);
		}
	}
}

// When a torrent is removed from the session, its m_tags entry must be erased
// too. Otherwise the map would slowly grow with stale handles. We probe this
// by reading get_tag(handle) after the removal — even if the handle object
// still exists in the caller's scope, the underlying torrent is gone and the
// tag must read back as 0.
BOOST_AUTO_TEST_CASE(tag_cleared_on_torrent_removal)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih = make_v1(0x40);
	p.info_hashes = lt::info_hash_t(ih);
	lt::torrent_handle h = ses.add_torrent(p);
	wait_for(ses, handler, 1, lt::add_torrent_alert::alert_type);

	BOOST_TEST(history.set_tag(ih, 0xdead, ~std::uint64_t(0)));
	BOOST_TEST(history.get_tag(h) == 0xdeadu);

	// Capture a copy of the handle before removal. The shared_ptr keeps the
	// object alive enough to use as a map key after the session drops its
	// own reference.
	lt::torrent_handle h_copy = h;

	ses.remove_torrent(h);
	wait_for(ses, handler, 1, lt::torrent_removed_alert::alert_type);

	// After removal the entry in m_tags must be gone.
	BOOST_TEST(history.get_tag(h_copy) == 0u);

	// And set_tag on the now-unknown info-hash must return false (the entry
	// is no longer in m_queue, so there is nothing to write to).
	BOOST_TEST(!history.set_tag(ih, 0x1, ~std::uint64_t(0)));
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

// status_bits projects the chosen 8 bits from torrent_status. Exercised here
// without a session because it's a pure projection on the input struct.
BOOST_AUTO_TEST_CASE(status_bits_projection)
{
	lt::torrent_status s;

	// Each wire-state value lights exactly one bit in the high nibble.
	s.state = lt::torrent_status::checking_files;
	BOOST_TEST(ltweb::status_bits(s) == 0x10);
	s.state = lt::torrent_status::downloading_metadata;
	BOOST_TEST(ltweb::status_bits(s) == 0x20);
	s.state = lt::torrent_status::downloading;
	BOOST_TEST(ltweb::status_bits(s) == 0x40);
	s.state = lt::torrent_status::seeding;
	BOOST_TEST(ltweb::status_bits(s) == 0x80);

	// checking_resume_data collapses to the checking-files bit, finished
	// collapses to seeding -- the same enum-pair mapping the case-20
	// response serializer uses.
	s.state = lt::torrent_status::checking_resume_data;
	BOOST_TEST(ltweb::status_bits(s) == 0x10);
	s.state = lt::torrent_status::finished;
	BOOST_TEST(ltweb::status_bits(s) == 0x80);

	// Flag bits OR with the state bit.
	s.state = lt::torrent_status::downloading;
	s.flags = lt::torrent_flags::paused;
	BOOST_TEST(ltweb::status_bits(s) == (0x01 | 0x40));
	s.flags = lt::torrent_flags::auto_managed;
	BOOST_TEST(ltweb::status_bits(s) == (0x02 | 0x40));
	s.flags = lt::torrent_flags::paused | lt::torrent_flags::auto_managed;
	BOOST_TEST(ltweb::status_bits(s) == (0x01 | 0x02 | 0x40));

	// 0x04 is reserved and must never be set, even with every flag bit on.
	s.flags = ~lt::torrent_flags_t{};
	BOOST_TEST((ltweb::status_bits(s) & 0x04) == 0);

	// Any non-zero errc lights bit 3.
	s.flags = {};
	s.state = lt::torrent_status::downloading;
	s.errc = lt::error_code(1, boost::system::generic_category());
	BOOST_TEST(ltweb::status_bits(s) & 0x08);
}

// query_filtered with two empty filter specs must return the same shape as
// the plain query() at the same frame.
BOOST_AUTO_TEST_CASE(query_filtered_empty_degenerates_to_query)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	p.info_hashes = lt::info_hash_t(make_v1(0x70));
	ses.add_torrent(p);
	wait_for(ses, handler, 1, lt::add_torrent_alert::alert_type);

	auto const r_q = history.query(0);
	auto const r_f = history.query_filtered(0, ltweb::filter_spec{}, ltweb::filter_spec{});
	BOOST_TEST(r_q.is_snapshot == r_f.is_snapshot);
	BOOST_TEST(r_q.current_frame == r_f.current_frame);
	BOOST_TEST(r_q.updated.size() == r_f.updated.size());
	BOOST_TEST(r_q.removed.size() == r_f.removed.size());
}

// Widening the filter (or changing it such that a previously-excluded
// torrent now matches) puts that torrent in updated with frame[] filled to
// current_frame, so the serializer treats every requested field as new.
// Verifies the four-outcome rule's "false -> true" branch.
BOOST_AUTO_TEST_CASE(query_filtered_widening_forces_full_update)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih_a = make_v1(0x50);
	lt::sha1_hash const ih_b = make_v1(0x51);
	p.info_hashes = lt::info_hash_t(ih_a);
	ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(ih_b);
	ses.add_torrent(p);
	wait_for(ses, handler, 2, lt::add_torrent_alert::alert_type);

	BOOST_TEST(history.set_tag(ih_a, 0x1, ~std::uint64_t(0)));

	// Drain to a known frame the client has already seen.
	auto const initial = history.query(0);
	BOOST_TEST(initial.updated.size() == 2u);
	ltweb::frame_t const f_client = initial.current_frame;

	// f_old matches nothing (bit 1, which neither torrent carries),
	// f_new matches A. Filter changed -> slow path.
	ltweb::filter_spec f_old;
	f_old.tag_mask = 0x2;
	ltweb::filter_spec f_new;
	f_new.tag_mask = 0x1;

	auto const r = history.query_filtered(f_client, f_old, f_new);

	BOOST_TEST(r.updated.size() == 1u);
	if (r.updated.size() == 1u) {
		BOOST_TEST((r.updated[0].status.info_hashes == lt::info_hash_t(ih_a)));
		// Every per-field frame counter should be strictly greater than the
		// caller's since_frame, so the serializer's "field changed since K"
		// check (frame[k] <= since_frame -> skip) includes every requested
		// field. Strictly-greater is what makes this work even when an idle
		// session has current_frame == since_frame.
		for (auto const fr : r.updated[0].frame)
			BOOST_TEST(fr > f_client);
	}
	// B was never matched -- skipped, not put in removed.
	BOOST_TEST(r.removed.empty());
}

// Narrowing the filter pushes torrents that fall out of view into the
// removed list (sharing the on-wire stream with session-level tombstones).
// Verifies the "true -> false" branch of the four-outcome rule.
BOOST_AUTO_TEST_CASE(query_filtered_narrowing_emits_removal)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih_a = make_v1(0x60);
	lt::sha1_hash const ih_b = make_v1(0x61);
	p.info_hashes = lt::info_hash_t(ih_a);
	ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(ih_b);
	ses.add_torrent(p);
	wait_for(ses, handler, 2, lt::add_torrent_alert::alert_type);

	BOOST_TEST(history.set_tag(ih_a, 0x1, ~std::uint64_t(0)));
	BOOST_TEST(history.set_tag(ih_b, 0x2, ~std::uint64_t(0)));

	auto const initial = history.query(0);
	ltweb::frame_t const f_client = initial.current_frame;

	// f_old matches both (bits 0 or 1), f_new only matches A.
	ltweb::filter_spec f_old;
	f_old.tag_mask = 0x3;
	ltweb::filter_spec f_new;
	f_new.tag_mask = 0x1;

	auto const r = history.query_filtered(f_client, f_old, f_new);

	BOOST_TEST(r.removed.size() == 1u);
	if (r.removed.size() == 1u) BOOST_TEST((r.removed[0] == ih_b));

	// A remained matched -- present in updated as a stable delta.
	BOOST_TEST(r.updated.size() == 1u);
	if (r.updated.size() == 1u)
		BOOST_TEST((r.updated[0].status.info_hashes == lt::info_hash_t(ih_a)));
}

// filter_inputs_stable_since(K) tracks the four contributing fields. A
// tag change advances frame[tag] past K, flipping the predicate from true
// to false until K catches up to the new current_frame.
BOOST_AUTO_TEST_CASE(filter_inputs_stable_since_tracks_tag)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih = make_v1(0x80);
	p.info_hashes = lt::info_hash_t(ih);
	ses.add_torrent(p);
	wait_for(ses, handler, 1, lt::add_torrent_alert::alert_type);

	auto const initial = history.query(0);
	ltweb::frame_t const f0 = initial.current_frame;
	BOOST_TEST(initial.updated.size() == 1u);
	if (!initial.updated.empty()) BOOST_TEST(initial.updated[0].filter_inputs_stable_since(f0));

	BOOST_TEST(history.set_tag(ih, 0x1, ~std::uint64_t(0)));

	auto const after = history.query(0);
	BOOST_TEST(after.updated.size() == 1u);
	if (!after.updated.empty()) {
		BOOST_TEST(!after.updated[0].filter_inputs_stable_since(f0));
		// Stable again once K catches up to the new frame.
		BOOST_TEST(after.updated[0].filter_inputs_stable_since(after.current_frame));
	}
}

// A removed torrent that never matched f_old must not appear in the removed
// list -- the client was never told about it so it cannot remove it locally.
// A removed torrent that did match f_old must still appear.
BOOST_AUTO_TEST_CASE(query_filtered_tombstone_respects_f_old)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih_a = make_v1(0x90); // will carry tag 0x1 -- in filter
	lt::sha1_hash const ih_b = make_v1(0x91); // no tag          -- outside filter
	p.info_hashes = lt::info_hash_t(ih_a);
	lt::torrent_handle ha = ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(ih_b);
	lt::torrent_handle hb = ses.add_torrent(p);
	wait_for(ses, handler, 2, lt::add_torrent_alert::alert_type);

	BOOST_TEST(history.set_tag(ih_a, 0x1, ~std::uint64_t(0)));

	// Drain to a stable frame the client has already seen.
	auto const initial = history.query(0);
	ltweb::frame_t const f_client = initial.current_frame;

	// Remove both torrents.
	ses.remove_torrent(ha);
	ses.remove_torrent(hb);
	wait_for(ses, handler, 2, lt::torrent_removed_alert::alert_type);

	// Filter: only torrents with tag bit 0.
	ltweb::filter_spec f;
	f.tag_mask = 0x1;

	auto const r = history.query_filtered(f_client, f, f);

	// A (tag=1) matched f_old -- client must be told it was removed.
	auto const has_a = std::find(r.removed.begin(), r.removed.end(), ih_a) != r.removed.end();
	BOOST_TEST(has_a);

	// B (tag=0) never matched f_old -- must not appear in removed.
	auto const has_b = std::find(r.removed.begin(), r.removed.end(), ih_b) != r.removed.end();
	BOOST_TEST(!has_b);
}

// When the filter is unchanged and a live torrent's inputs change after
// since_frame such that it exits the filter view, the client must receive a
// removal. The entry IS visited on the first poll (bimap_key > since_frame),
// but the old code skipped it when inputs_certain was false rather than
// emitting a removal. On the next poll since_frame has caught up so the
// early-break fires and the entry is never visited again -- permanent stale view.
BOOST_AUTO_TEST_CASE(query_filtered_unchanged_filter_exit_emits_removal)
{
	lt::session ses(make_settings_pack());
	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih_a = make_v1(0xb0); // starts matching, then leaves
	lt::sha1_hash const ih_b = make_v1(0xb1); // never matches -- must not appear in removed
	p.info_hashes = lt::info_hash_t(ih_a);
	ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(ih_b);
	ses.add_torrent(p);
	wait_for(ses, handler, 2, lt::add_torrent_alert::alert_type);

	BOOST_TEST(history.set_tag(ih_a, 0x1, ~std::uint64_t(0)));

	// Drain to a stable frame where A is in the tag-0x1 filter view.
	auto const initial = history.query(0);
	ltweb::frame_t const f_client = initial.current_frame;

	// Clear A's tag AFTER f_client so filter_inputs_stable_since(f_client)
	// is false for A -- this is the unstable-exit path.
	BOOST_TEST(history.set_tag(ih_a, 0x0, ~std::uint64_t(0)));

	ltweb::filter_spec f;
	f.tag_mask = 0x1;

	auto const r = history.query_filtered(f_client, f, f);

	// A was in the filter view at f_client and has now left it; must be removed.
	auto const has_a = std::find(r.removed.begin(), r.removed.end(), ih_a) != r.removed.end();
	BOOST_TEST(has_a);

	// B (tag=0) never matched -- must not appear in removed.
	auto const has_b = std::find(r.removed.begin(), r.removed.end(), ih_b) != r.removed.end();
	BOOST_TEST(!has_b);
}

// An all-zero f_old (match-all) implies the client held every live torrent
// in their view. A torrent whose filter inputs changed after since_frame (so
// filter_inputs_stable_since() is false) must still appear in removed when it
// does not match f_new -- the stability guard must not suppress the match for
// an empty f_old because matched(f_old,...) is unconditionally true for it.
BOOST_AUTO_TEST_CASE(query_filtered_empty_f_old_skips_stability_guard)
{
	lt::session ses(make_settings_pack());

	ltweb::alert_handler handler(ses);
	ltweb::torrent_history history(&handler);

	lt::add_torrent_params p;
	p.save_path = ".";
	lt::sha1_hash const ih_a = make_v1(0xa0); // tag 0x1 -- matches f_new
	lt::sha1_hash const ih_b = make_v1(0xa1); // tag 0x2 -- outside f_new
	p.info_hashes = lt::info_hash_t(ih_a);
	ses.add_torrent(p);
	p.info_hashes = lt::info_hash_t(ih_b);
	ses.add_torrent(p);
	wait_for(ses, handler, 2, lt::add_torrent_alert::alert_type);

	// Drain to a stable frame representing the client's last-seen state.
	auto const initial = history.query(0);
	ltweb::frame_t const f_client = initial.current_frame;
	BOOST_TEST(initial.updated.size() == 2u);

	// Set both tags AFTER f_client so frame[tag] > f_client for both entries,
	// making filter_inputs_stable_since(f_client) false.
	BOOST_TEST(history.set_tag(ih_a, 0x1, ~std::uint64_t(0)));
	BOOST_TEST(history.set_tag(ih_b, 0x2, ~std::uint64_t(0)));

	// f_old is all-zero (the initial _filter state -- no prior filter was active).
	// f_new restricts to tag bit 0 only.
	ltweb::filter_spec const f_old; // all-zero
	ltweb::filter_spec f_new;
	f_new.tag_mask = 0x1;

	auto const r = history.query_filtered(f_client, f_old, f_new);

	// B (tag=0x2) was in the unfiltered view at f_client but does not match
	// f_new. Its inputs are unstable. The fix ensures f_old.empty() forces
	// then=true without consulting the stability guard.
	auto const has_b = std::find(r.removed.begin(), r.removed.end(), ih_b) != r.removed.end();
	BOOST_TEST(has_b);

	// A (tag=0x1) matches f_new -- must appear in updated.
	BOOST_TEST(r.updated.size() == 1u);
	if (r.updated.size() == 1u)
		BOOST_TEST((r.updated[0].status.info_hashes == lt::info_hash_t(ih_a)));
}
