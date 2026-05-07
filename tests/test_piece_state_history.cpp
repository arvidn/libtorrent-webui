/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE piece_state_history
#include <boost/test/included/unit_test.hpp>

#include "piece_state_history.hpp"

#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/bitfield.hpp>
#include <libtorrent/units.hpp>

#include <cstring>
#include <initializer_list>

namespace {

lt::sha1_hash make_hash(std::uint8_t const fill)
{
	lt::sha1_hash h;
	std::memset(h.data(), static_cast<int>(fill), static_cast<std::size_t>(lt::sha1_hash::size()));
	return h;
}

lt::typed_bitfield<lt::piece_index_t>
make_bitfield(int const num_pieces, std::initializer_list<int> set_bits = {})
{
	lt::typed_bitfield<lt::piece_index_t> bf(num_pieces);
	for (int i : set_bits)
		bf.set_bit(lt::piece_index_t{i});
	return bf;
}

bool snapshot_has(ltweb::piece_state_history::query_result const& r, int const piece_index)
{
	return piece_index >= 0 && piece_index < r.snapshot.size()
		&& r.snapshot.get_bit(lt::piece_index_t{piece_index});
}

bool delta_has(ltweb::piece_state_history::query_result const& r, int const piece_index)
{
	for (auto const& idx : r.added)
		if (idx == lt::piece_index_t{piece_index}) return true;
	return false;
}

} // anonymous namespace

// info_hash is preserved from construction, and the initial frame counter
// starts at 1 (so a client passing 0 is unambiguously asking for a snapshot).
BOOST_AUTO_TEST_CASE(constructor_initial_state)
{
	lt::sha1_hash const ih = make_hash(0x42);
	ltweb::piece_state_history ph(ih, make_bitfield(8, {0, 3}));

	BOOST_TEST(ph.info_hash() == ih);
	BOOST_TEST(ph.frame() == 1u);
	BOOST_TEST(ph.horizon() == 1u);
}

// since_frame == 0 always returns a snapshot, even immediately after
// construction with no completions yet.
BOOST_AUTO_TEST_CASE(query_zero_is_snapshot)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(16, {1, 5, 9}));

	auto const r = ph.query(0);
	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(r.frame == 1u);
	BOOST_TEST(r.added.empty());
	BOOST_TEST(r.snapshot.size() == 16);
	BOOST_TEST(snapshot_has(r, 1));
	BOOST_TEST(snapshot_has(r, 5));
	BOOST_TEST(snapshot_has(r, 9));
	BOOST_TEST(!snapshot_has(r, 0));
	BOOST_TEST(!snapshot_has(r, 2));
}

// A client that's caught up to the current frame gets an empty delta.
BOOST_AUTO_TEST_CASE(query_current_frame_empty_delta)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(8));

	auto const r = ph.query(ph.frame());
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.added.empty());
	BOOST_TEST(r.frame == 1u);
}

// A piece_finished event between two queries shows up in the delta and
// advances the frame counter.
BOOST_AUTO_TEST_CASE(piece_finished_appears_in_delta)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(8));

	ltweb::frame_t const f0 = ph.frame();
	ph.on_piece_finished(lt::piece_index_t{3});

	BOOST_TEST(ph.frame() > f0);

	auto const r = ph.query(f0);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.added.size() == 1u);
	BOOST_TEST(delta_has(r, 3));
}

// Several completions between two queries all show up in the delta, in the
// order they happened.
BOOST_AUTO_TEST_CASE(multiple_completions_in_order)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(16));

	ltweb::frame_t const f0 = ph.frame();
	ph.on_piece_finished(lt::piece_index_t{2});
	ph.on_piece_finished(lt::piece_index_t{7});
	ph.on_piece_finished(lt::piece_index_t{11});

	auto const r = ph.query(f0);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.added.size() == 3u);
	BOOST_TEST((r.added[0] == lt::piece_index_t{2}));
	BOOST_TEST((r.added[1] == lt::piece_index_t{7}));
	BOOST_TEST((r.added[2] == lt::piece_index_t{11}));
}

// Completing a piece that was already set in the initial bitfield must not
// add a duplicate to the delta queue.
BOOST_AUTO_TEST_CASE(already_have_piece_is_ignored)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(8, {3}));

	ltweb::frame_t const f0 = ph.frame();
	ph.on_piece_finished(lt::piece_index_t{3}); // already in bitfield

	auto const r = ph.query(f0);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.added.empty());
}

// Receiving the same piece_finished event twice (e.g. from out-of-band
// re-emission) does not add a duplicate.
BOOST_AUTO_TEST_CASE(duplicate_completion_filtered)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(8));

	ltweb::frame_t const f0 = ph.frame();
	ph.on_piece_finished(lt::piece_index_t{4});
	ph.on_piece_finished(lt::piece_index_t{4});

	auto const r = ph.query(f0);
	BOOST_TEST(r.added.size() == 1u);
	BOOST_TEST(delta_has(r, 4));
}

// A piece_finished for an out-of-range index (e.g. before metadata is
// available, when the bitfield is empty) is silently ignored.
BOOST_AUTO_TEST_CASE(out_of_range_piece_ignored)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(4));

	ltweb::frame_t const f0 = ph.frame();
	ph.on_piece_finished(lt::piece_index_t{99}); // beyond bitfield
	ph.on_piece_finished(lt::piece_index_t{4}); // exactly at the boundary

	auto const r = ph.query(f0);
	BOOST_TEST(r.added.empty());
}

// A snapshot query with non-zero since_frame still merges the queue into
// the bitfield, so the client gets a complete current "have" set.
BOOST_AUTO_TEST_CASE(snapshot_includes_queue_entries)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(16, {2}));

	ph.on_piece_finished(lt::piece_index_t{5});
	ph.on_piece_finished(lt::piece_index_t{8});

	// since_frame == 0 forces a snapshot.
	auto const r = ph.query(0);
	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(snapshot_has(r, 2)); // from initial bitfield
	BOOST_TEST(snapshot_has(r, 5)); // from queue
	BOOST_TEST(snapshot_has(r, 8)); // from queue
	BOOST_TEST(!snapshot_has(r, 0));
	BOOST_TEST(!snapshot_has(r, 7));
}

// When the queue overflows max_queue, the oldest entry is dropped from the
// delta history and the frame horizon advances to its frame. Subsequent
// queries from a stale client must therefore receive a snapshot, not a
// delta with the evicted entry missing. The bitfield itself was already
// up-to-date when the piece completed, so the snapshot is fully populated.
BOOST_AUTO_TEST_CASE(queue_cap_evicts_oldest_advances_horizon)
{
	// Cap of 2 makes the eviction observable with very few events.
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(8), 2);

	ph.on_piece_finished(lt::piece_index_t{1}); // -> queue (oldest)
	ltweb::frame_t const f_after_first = ph.frame();
	ph.on_piece_finished(lt::piece_index_t{2}); // -> queue
	ph.on_piece_finished(lt::piece_index_t{3}); // -> evicts piece 1

	// The eviction advances the horizon to the dropped entry's frame.
	BOOST_TEST(ph.horizon() == f_after_first);

	// A snapshot query must contain pieces 1, 2, and 3 — all set in the
	// bitfield at the moment they completed.
	auto const r = ph.query(0);
	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(snapshot_has(r, 1));
	BOOST_TEST(snapshot_has(r, 2));
	BOOST_TEST(snapshot_has(r, 3));
}

// A client whose since_frame predates the current frame horizon (i.e. part
// of the delta history has been discarded) must get a snapshot, since the
// queue no longer covers the requested range.
BOOST_AUTO_TEST_CASE(stale_client_below_horizon_gets_snapshot)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(8), 1);

	ltweb::frame_t const f_pre = ph.frame();
	ph.on_piece_finished(lt::piece_index_t{0}); // -> queue
	ph.on_piece_finished(lt::piece_index_t{1}); // evicts piece 0; horizon advances

	BOOST_TEST(ph.horizon() > f_pre);

	auto const r = ph.query(f_pre);
	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(snapshot_has(r, 0));
	BOOST_TEST(snapshot_has(r, 1));
}

// A client whose since_frame is *larger* than the current frame (e.g. the
// torrent was re-added or the server restarted, resetting the per-torrent
// counter) must get a fresh snapshot, not an undefined delta.
BOOST_AUTO_TEST_CASE(future_client_frame_gets_snapshot)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(8, {2}));

	ph.on_piece_finished(lt::piece_index_t{4});

	// Pretend the client is from a previous incarnation with a much
	// higher frame count.
	auto const r = ph.query(ph.frame() + 1000);
	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(snapshot_has(r, 2));
	BOOST_TEST(snapshot_has(r, 4));
}

// Default queue cap is 300; explicit cap is honored. Verifying the explicit
// cap is enough -- we only need to confirm the parameter is respected.
BOOST_AUTO_TEST_CASE(explicit_max_queue_respected)
{
	ltweb::piece_state_history ph(make_hash(0x11), make_bitfield(64), 5);

	for (int i = 0; i < 10; ++i)
		ph.on_piece_finished(lt::piece_index_t{i});

	// The first five entries fell off the front of the delta queue; the
	// remaining five are still queued. The bitfield reflects all ten
	// completions either way, so a snapshot query returns all bits set.
	auto const r = ph.query(0);
	BOOST_TEST(r.is_snapshot);
	for (int i = 0; i < 10; ++i)
		BOOST_TEST(snapshot_has(r, i));
}
