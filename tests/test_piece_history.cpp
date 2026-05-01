/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE piece_history
#include <boost/test/included/unit_test.hpp>

#include "piece_history.hpp"

#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <cstring>
#include <vector>
#include <initializer_list>

namespace {

lt::sha1_hash make_hash(unsigned char fill)
{
	lt::sha1_hash h;
	std::memset(h.data(), static_cast<int>(fill), static_cast<std::size_t>(lt::sha1_hash::size()));
	return h;
}

// partial_piece_info::blocks is a raw pointer into session-owned memory in
// production. In tests we own the storage ourselves. fake_queue builds a
// vector<partial_piece_info> and keeps the underlying block_info arrays alive
// for the lifetime of the object.
struct fake_queue {
	std::vector<lt::partial_piece_info> pieces;

	void add(int idx, std::initializer_list<int> states)
	{
		block_storage_.emplace_back();
		auto& bs = block_storage_.back();
		for (int s : states) {
			lt::block_info bi;
			std::memset(&bi, 0, sizeof(bi));
			bi.state = static_cast<std::uint32_t>(s);
			bs.push_back(bi);
		}

		lt::partial_piece_info ppi;
		std::memset(&ppi, 0, sizeof(ppi));
		ppi.piece_index = lt::piece_index_t(idx);
		ppi.blocks_in_piece = static_cast<int>(bs.size());
		ppi.blocks = bs.data();
		pieces.push_back(ppi);
	}

private:
	// one inner vector per piece; pointers in pieces[] point into these
	std::vector<std::vector<lt::block_info>> block_storage_;
};

} // anonymous namespace

// since_frame == 0 means full snapshot: every piece goes to full_pieces,
// removed is empty (caller signals the client with num_removed = 0xffff).
BOOST_AUTO_TEST_CASE(snapshot)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q;
	q.add(0, {0, 1, 2, 3});
	q.add(5, {1, 1});

	ph.update(q.pieces);
	auto const r = ph.query(0);

	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(r.full_pieces.size() == 2u);
	BOOST_TEST(r.block_updates.empty());
	BOOST_TEST(r.removed.empty());
}

// If nothing has changed between two updates, a delta query returns nothing.
BOOST_AUTO_TEST_CASE(delta_no_change)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q;
	q.add(0, {1, 1, 1, 1});

	auto const f1 = ph.update(q.pieces);
	ph.update(q.pieces); // identical state

	auto const r = ph.query(f1);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.full_pieces.empty());
	BOOST_TEST(r.block_updates.empty());
	BOOST_TEST(r.removed.empty());
}

// When few blocks change, individual block updates are cheaper than resending
// the whole piece.  Encoding cost: (num_blocks + 6) vs (changed * 7).
// 4 blocks, 1 changed: 10 > 7 -> block_updates.
BOOST_AUTO_TEST_CASE(delta_sends_individual_block_updates)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q1, q2;
	q1.add(0, {0, 0, 0, 0});
	q2.add(0, {0, 1, 0, 0}); // only block 1 changes

	auto const f1 = ph.update(q1.pieces);
	ph.update(q2.pieces);

	auto const r = ph.query(f1);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.full_pieces.empty());
	BOOST_TEST(r.block_updates.size() == 1u);
	BOOST_TEST(r.removed.empty());

	if (!r.block_updates.empty()) {
		BOOST_TEST((r.block_updates[0].piece_index == lt::piece_index_t(0)));
		BOOST_TEST(r.block_updates[0].block_index == 1);
		BOOST_TEST(r.block_updates[0].state == 1u);
	}
}

// When most blocks change, resending the full piece is cheaper.
// 4 blocks, all 4 changed: 10 <= 28 -> full_pieces.
BOOST_AUTO_TEST_CASE(delta_sends_full_piece_when_cheaper)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q1, q2;
	q1.add(0, {0, 0, 0, 0});
	q2.add(0, {1, 2, 3, 3}); // all four blocks change

	auto const f1 = ph.update(q1.pieces);
	ph.update(q2.pieces);

	auto const r = ph.query(f1);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.full_pieces.size() == 1u);
	BOOST_TEST(r.block_updates.empty());
	BOOST_TEST(r.removed.empty());
}

// A piece that appears for the first time since the client's last frame must
// be sent as a full piece (client has no prior state for it).
BOOST_AUTO_TEST_CASE(new_piece_sent_as_full_piece)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q1, q2;
	q1.add(0, {1, 1});
	q2.add(0, {1, 1});
	q2.add(7, {0, 1}); // piece 7 is new

	auto const f1 = ph.update(q1.pieces);
	ph.update(q2.pieces);

	auto const r = ph.query(f1);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.full_pieces.size() == 1u);
	BOOST_TEST(r.block_updates.empty());
	BOOST_TEST(r.removed.empty());

	if (!r.full_pieces.empty()) BOOST_TEST((r.full_pieces[0]->piece_index == lt::piece_index_t(7)));
}

// A piece that disappears after the client's last frame must appear in the
// removed list.
BOOST_AUTO_TEST_CASE(removed_piece_reported_to_client)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q1, q2;
	q1.add(3, {1, 1, 1});
	// q2 is empty: piece 3 is gone

	auto const f1 = ph.update(q1.pieces); // piece 3 added
	ph.update(q2.pieces); // piece 3 removed

	// Client last polled at f1 -- it saw piece 3 -- so it must be told
	// about the removal.
	auto const r = ph.query(f1);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.removed.size() == 1u);
	if (!r.removed.empty()) BOOST_TEST((r.removed[0] == lt::piece_index_t(3)));
}

// If a piece is added and removed between two client polls, the client never
// knew about it, so it must NOT appear in the removed list.
BOOST_AUTO_TEST_CASE(removed_piece_not_reported_if_client_never_saw_add)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q0, q1, q2;
	q1.add(3, {1, 1});

	auto const f0 = ph.update(q0.pieces); // empty; client polls here
	auto const f1 = ph.update(q1.pieces); // piece 3 appears
	ph.update(q2.pieces); // piece 3 disappears

	// Client at f0: added_frame > f0 -> not in removed
	auto const r0 = ph.query(f0);
	BOOST_TEST(r0.removed.empty());

	// Client at f1: added_frame <= f1 -> should be in removed
	auto const r1 = ph.query(f1);
	BOOST_TEST(r1.removed.size() == 1u);
	if (!r1.removed.empty()) BOOST_TEST((r1.removed[0] == lt::piece_index_t(3)));
}

// When a piece reappears after being removed, its removal entry must be
// cleaned from the removed list so the client is not told to delete it.
BOOST_AUTO_TEST_CASE(piece_reappears_after_removal)
{
	ltweb::piece_history ph(make_hash(0x11));

	fake_queue q1, q2, q3;
	q1.add(2, {1, 1});
	q3.add(2, {0, 0}); // piece 2 comes back

	auto const f1 = ph.update(q1.pieces); // piece 2 added
	ph.update(q2.pieces); // piece 2 removed
	ph.update(q3.pieces); // piece 2 re-added (removed entry erased)

	// Client at f1: piece 2 was removed then re-added.
	// The removal entry was cleaned up on re-add, so removed must be empty.
	auto const r = ph.query(f1);

	bool in_removed = false;
	for (auto idx : r.removed)
		if (idx == lt::piece_index_t(2)) in_removed = true;
	BOOST_TEST(!in_removed);

	// The re-added piece has added_frame > f1, so it must appear as a full
	// piece update (client has no state for the new incarnation).
	bool in_full = false;
	for (auto const* e : r.full_pieces)
		if (e->piece_index == lt::piece_index_t(2)) in_full = true;
	BOOST_TEST(in_full);
}

// When tombstones overflow the limit the oldest are evicted and the horizon
// advances.  A query with since_frame < horizon() must behave like a full
// snapshot (since_frame == 0): all current pieces in full_pieces, removed empty.
BOOST_AUTO_TEST_CASE(horizon_after_tombstone_eviction)
{
	// Limit to 2 tombstones so the third removal triggers eviction.
	ltweb::piece_history ph(make_hash(0x11), 2);

	BOOST_TEST(ph.horizon() == 0u);

	// Frame 1: add pieces 0, 1, 2.
	fake_queue q_add;
	q_add.add(0, {1, 1});
	q_add.add(1, {1, 1});
	q_add.add(2, {1, 1});
	auto const f_client = ph.update(q_add.pieces);

	// Frame 2: remove all three pieces (empty queue).
	// The third tombstone overflows the limit; one is evicted and horizon advances.
	fake_queue q_empty;
	ph.update(q_empty.pieces);

	BOOST_TEST(ph.horizon() > 0u);

	// Frame 3: add a new piece so there is something to snapshot.
	fake_queue q_new;
	q_new.add(5, {0, 1});
	ph.update(q_new.pieces);

	// A stale client at f_client (< horizon) gets a full snapshot:
	// piece 5 in full_pieces, removed is empty.
	auto const r = ph.query(f_client);
	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(r.removed.empty());
	BOOST_TEST(r.full_pieces.size() == 1u);
	if (!r.full_pieces.empty()) BOOST_TEST((r.full_pieces[0]->piece_index == lt::piece_index_t(5)));
}

// Before any eviction the horizon is 0 and deltas work normally.
BOOST_AUTO_TEST_CASE(horizon_zero_before_eviction)
{
	ltweb::piece_history ph(make_hash(0x11), 10);
	BOOST_TEST(ph.horizon() == 0u);

	fake_queue q1, q2;
	q1.add(0, {1, 1});
	auto const f1 = ph.update(q1.pieces);
	ph.update(q2.pieces); // piece 0 removed

	// Still under the limit, so horizon stays 0.
	BOOST_TEST(ph.horizon() == 0u);

	// Normal delta: piece 0 should appear in removed.
	auto const r = ph.query(f1);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.removed.size() == 1u);
}
