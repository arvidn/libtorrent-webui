/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE file_history
#include <boost/test/included/unit_test.hpp>

#include "file_history.hpp"

#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/disk_interface.hpp> // open_file_state
#include <libtorrent/download_priority.hpp>

#include <string>
#include <cstring>
#include <vector>

namespace {

lt::sha1_hash make_hash(unsigned char fill)
{
	lt::sha1_hash h;
	std::memset(h.data(), static_cast<int>(fill),
		static_cast<std::size_t>(lt::sha1_hash::size()));
	return h;
}

lt::file_storage make_fs(int n)
{
	lt::file_storage fs;
	for (int i = 0; i < n; ++i)
		fs.add_file("file" + std::to_string(i) + ".bin",
			static_cast<std::int64_t>(i + 1) * 1024);
	return fs;
}

lt::open_file_state make_open(int file_index, std::uint8_t mode)
{
	lt::open_file_state s;
	s.file_index = lt::file_index_t(file_index);
	s.open_mode = lt::file_open_mode_t(mode);
	s.last_use = lt::time_point{};
	return s;
}

} // anonymous namespace

// info_hash() returns the hash the object was constructed with.
BOOST_AUTO_TEST_CASE(info_hash_stored)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0xaa), fs);
	BOOST_TEST((fh.info_hash() == make_hash(0xaa)));
}

// frame() advances by 1 on each update() call.
BOOST_AUTO_TEST_CASE(frame_advances_on_update)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0x11), fs);

	BOOST_TEST(fh.frame() == 1u);
	ltweb::frame_t f1 = fh.update(nullptr, nullptr, nullptr);
	BOOST_TEST(f1 == 2u);
	BOOST_TEST(fh.frame() == 2u);
	ltweb::frame_t f2 = fh.update(nullptr, nullptr, nullptr);
	BOOST_TEST(f2 == 3u);
}

// since_frame == 0: static field bits are included for every file.
BOOST_AUTO_TEST_CASE(static_fields_on_snapshot)
{
	auto const fs = make_fs(3);
	ltweb::file_history fh(make_hash(0x11), fs);

	fh.update(nullptr, nullptr, nullptr);

	// Request all three static fields (flags=0x01, name=0x02, size=0x04).
	auto const masks = fh.query(0, 0x07u);
	BOOST_TEST(masks.size() == 3u);
	for (int i = 0; i < 3; ++i)
		BOOST_TEST(masks[i] == 0x07u);
}

// since_frame > 0: static field bits are NOT included, regardless of mask.
BOOST_AUTO_TEST_CASE(static_fields_not_on_delta)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0x11), fs);

	ltweb::frame_t f1 = fh.update(nullptr, nullptr, nullptr);

	auto const masks = fh.query(f1, 0x07u);
	for (std::uint16_t m : masks)
		BOOST_TEST(m == 0u);
}

// Passing nullptr for a field means its frame is never advanced beyond its
// initial value, so a delta query will not report it as changed.
BOOST_AUTO_TEST_CASE(null_field_not_tracked)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0x11), fs);

	// Capture frame before the update.
	ltweb::frame_t f_before = fh.frame();

	// Update with no progress data.
	fh.update(nullptr, nullptr, nullptr);

	// Delta query: progress bit 0x08 must be absent because no progress
	// data was ever passed in -- its frame hasn't advanced past f_before.
	auto const masks = fh.query(f_before, 0x08u);
	for (std::uint16_t m : masks)
		BOOST_TEST((m & 0x08u) == 0u);
}

// After updating progress for one file, only that file gets bit 0x08.
BOOST_AUTO_TEST_CASE(progress_change_reported)
{
	auto const fs = make_fs(3);
	ltweb::file_history fh(make_hash(0x11), fs);

	ltweb::frame_t f_before = fh.frame(); // 1, before any update
	std::vector<std::int64_t> fp = {0, 512, 0};
	fh.update(&fp, nullptr, nullptr);

	// Delta query since f_before: only file 1 changed (0 -> 512).
	auto const masks = fh.query(f_before, 0x08u);
	BOOST_TEST((masks[0] & 0x08u) == 0u); // unchanged
	BOOST_TEST((masks[1] & 0x08u) != 0u); // changed
	BOOST_TEST((masks[2] & 0x08u) == 0u); // unchanged
}

// If progress doesn't change between two updates, a delta query returns nothing.
BOOST_AUTO_TEST_CASE(unchanged_progress_not_reported)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0x11), fs);

	std::vector<std::int64_t> fp = {100, 200};
	ltweb::frame_t f1 = fh.update(&fp, nullptr, nullptr);
	fh.update(&fp, nullptr, nullptr); // identical values

	auto const masks = fh.query(f1, 0x08u);
	for (std::uint16_t m : masks)
		BOOST_TEST((m & 0x08u) == 0u);
}

// Priority change is reported only for the file that changed.
BOOST_AUTO_TEST_CASE(priority_change_reported)
{
	auto const fs = make_fs(3);
	ltweb::file_history fh(make_hash(0x11), fs);

	std::vector<lt::download_priority_t> p1 = {
		lt::default_priority, lt::default_priority, lt::default_priority};
	ltweb::frame_t f1 = fh.update(nullptr, &p1, nullptr);

	std::vector<lt::download_priority_t> p2 = {
		lt::default_priority, lt::download_priority_t(7), lt::default_priority};
	fh.update(nullptr, &p2, nullptr);

	auto const masks = fh.query(f1, 0x10u);
	BOOST_TEST((masks[0] & 0x10u) == 0u); // unchanged
	BOOST_TEST((masks[1] & 0x10u) != 0u); // changed to 7
	BOOST_TEST((masks[2] & 0x10u) == 0u); // unchanged
}

// open_mode is reported for a file that transitions from closed (0) to open.
BOOST_AUTO_TEST_CASE(open_mode_open_reported)
{
	auto const fs = make_fs(3);
	ltweb::file_history fh(make_hash(0x11), fs);

	ltweb::frame_t f_before = fh.frame(); // 1, before any update
	// File 1 is opened with read-write mode (0x02), files 0 and 2 stay closed.
	std::vector<lt::open_file_state> om = {make_open(1, 0x02)};
	fh.update(nullptr, nullptr, &om);

	// Delta query since f_before: only file 1 changed (closed -> 0x02).
	auto const masks = fh.query(f_before, 0x20u);
	BOOST_TEST((masks[0] & 0x20u) == 0u); // never opened, frame unchanged
	BOOST_TEST((masks[1] & 0x20u) != 0u); // opened
	BOOST_TEST((masks[2] & 0x20u) == 0u); // never opened, frame unchanged
}

// A file that was open and is no longer in the open_modes vector has been
// closed (mode transitions to 0). That transition must be reported.
BOOST_AUTO_TEST_CASE(open_mode_close_reported)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0x11), fs);

	// Frame 1: file 0 opened.
	std::vector<lt::open_file_state> om1 = {make_open(0, 0x02)};
	ltweb::frame_t f1 = fh.update(nullptr, nullptr, &om1);

	// Frame 2: file 0 is now closed (absent from the vector).
	std::vector<lt::open_file_state> om2;
	fh.update(nullptr, nullptr, &om2);

	// Client at f1 must be told file 0 changed (closed).
	auto const masks = fh.query(f1, 0x20u);
	BOOST_TEST((masks[0] & 0x20u) != 0u);
}

// Accessor open_mode() returns 0 after file is closed.
BOOST_AUTO_TEST_CASE(open_mode_accessor_after_close)
{
	auto const fs = make_fs(1);
	ltweb::file_history fh(make_hash(0x11), fs);

	std::vector<lt::open_file_state> om1 = {make_open(0, 0x06)};
	fh.update(nullptr, nullptr, &om1);
	BOOST_TEST(fh.open_mode(0) == 0x06u);

	std::vector<lt::open_file_state> om2; // closed
	fh.update(nullptr, nullptr, &om2);
	BOOST_TEST(fh.open_mode(0) == 0u);
}

// Value accessors return the values last passed to update().
BOOST_AUTO_TEST_CASE(accessors_return_updated_values)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0x11), fs);

	std::vector<std::int64_t> fp = {1000, 2000};
	std::vector<lt::download_priority_t> prio = {
		lt::download_priority_t(3), lt::download_priority_t(6)};
	std::vector<lt::open_file_state> om = {make_open(0, 0x04)};
	fh.update(&fp, &prio, &om);

	BOOST_TEST(fh.progress(0) == 1000);
	BOOST_TEST(fh.progress(1) == 2000);
	BOOST_TEST((fh.priority(0) == lt::download_priority_t(3)));
	BOOST_TEST((fh.priority(1) == lt::download_priority_t(6)));
	BOOST_TEST(fh.open_mode(0) == 0x04u);
	BOOST_TEST(fh.open_mode(1) == 0u);
}

// Two clients at different frames receive the correct delta subsets.
// Client A (at f0) sees a change that happened in frame f1.
// Client B (at f1) does not see it because nothing changed after f1.
BOOST_AUTO_TEST_CASE(multiple_clients_at_different_frames)
{
	auto const fs = make_fs(2);
	ltweb::file_history fh(make_hash(0x11), fs);

	ltweb::frame_t const f0 = fh.frame(); // 1, no updates yet

	// Frame 1: file 0 progress changes.
	std::vector<std::int64_t> fp1 = {500, 0};
	ltweb::frame_t f1 = fh.update(&fp1, nullptr, nullptr);

	// Frame 2: nothing changes.
	fh.update(&fp1, nullptr, nullptr);

	// Client A (f0): must see the progress change.
	auto const a_masks = fh.query(f0, 0x08u);
	BOOST_TEST((a_masks[0] & 0x08u) != 0u);
	BOOST_TEST((a_masks[1] & 0x08u) == 0u);

	// Client B (f1): nothing changed after f1, so no update.
	auto const b_masks = fh.query(f1, 0x08u);
	BOOST_TEST((b_masks[0] & 0x08u) == 0u);
	BOOST_TEST((b_masks[1] & 0x08u) == 0u);
}

// Only the bits set in requested_mask are ever returned.
BOOST_AUTO_TEST_CASE(requested_mask_filters_output)
{
	auto const fs = make_fs(1);
	ltweb::file_history fh(make_hash(0x11), fs);

	std::vector<std::int64_t> fp = {100};
	std::vector<lt::download_priority_t> prio = {lt::download_priority_t(7)};
	fh.update(&fp, &prio, nullptr);

	// Request only static fields: no dynamic bits should appear.
	auto const static_only = fh.query(0, 0x07u);
	BOOST_TEST((static_only[0] & 0x38u) == 0u);

	// Request only progress: no static or priority bits should appear.
	auto const progress_only = fh.query(0, 0x08u);
	BOOST_TEST((progress_only[0] & ~0x08u) == 0u);
}
