/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE prioritize_headers
#include <boost/test/included/unit_test.hpp>

#include "prioritize_headers.hpp"

#include <libtorrent/file_storage.hpp>
#include <libtorrent/units.hpp>
#include <libtorrent/download_priority.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

// Build a file_storage with explicit piece_length, so the index math in
// header_pieces() is well-defined.
lt::file_storage make_fs(int piece_length, std::vector<std::int64_t> const& sizes)
{
	lt::file_storage fs;
	fs.set_piece_length(piece_length);
	int idx = 0;
	for (auto const s : sizes)
		fs.add_file("file" + std::to_string(idx++) + ".bin", s);

	std::int64_t total = 0;
	for (auto const s : sizes)
		total += s;
	int const num = static_cast<int>((total + piece_length - 1) / piece_length);
	fs.set_num_pieces(num);
	return fs;
}

} // anonymous namespace

// is_media_file_extension matches a representative video, audio, and image
// extension, regardless of case, and rejects unknown or absent extensions.
BOOST_AUTO_TEST_CASE(extension_recognition)
{
	using ltweb::aux::is_media_file_extension;

	// video / audio / image: positive
	BOOST_TEST(is_media_file_extension("movie.mp4"));
	BOOST_TEST(is_media_file_extension("clip.MKV"));
	BOOST_TEST(is_media_file_extension("song.flac"));
	BOOST_TEST(is_media_file_extension("photo.JPG"));
	BOOST_TEST(is_media_file_extension("image.webp"));
	BOOST_TEST(is_media_file_extension("dir/sub/anim.GIF"));

	// negative
	BOOST_TEST(!is_media_file_extension("notes.txt"));
	BOOST_TEST(!is_media_file_extension("README"));
	BOOST_TEST(!is_media_file_extension("archive.zip"));
	BOOST_TEST(!is_media_file_extension(""));

	// the extension is the part after the last dot, not any earlier component
	BOOST_TEST(!is_media_file_extension("movie.mp4.txt"));
	BOOST_TEST(is_media_file_extension("movie.txt.mp4"));
}

using piece_pri_t = std::pair<lt::piece_index_t, lt::download_priority_t>;

// A single file aligned at offset 0 with a small header_size produces just
// the first piece, regardless of how many pieces the file occupies.
BOOST_AUTO_TEST_CASE(single_file_at_offset_zero)
{
	auto const fs = make_fs(256 * 1024, {1024 * 1024}); // 4 pieces

	std::vector<piece_pri_t> r;
	ltweb::aux::header_pieces(fs, lt::file_index_t(0), 128 * 1024, r);
	BOOST_TEST_REQUIRE(r.size() == 1u);
	BOOST_TEST(static_cast<int>(r[0].first) == 0);
	BOOST_TEST((r[0].second == ltweb::aux::header_piece_priority));
}

// When header_size straddles a piece boundary, both adjacent pieces are
// appended.
BOOST_AUTO_TEST_CASE(header_spans_two_pieces)
{
	// piece_length = 64 kiB. Header size = 128 kiB -> exactly two pieces.
	auto const fs = make_fs(64 * 1024, {1024 * 1024});

	std::vector<piece_pri_t> r;
	ltweb::aux::header_pieces(fs, lt::file_index_t(0), 128 * 1024, r);
	BOOST_TEST_REQUIRE(r.size() == 2u);
	BOOST_TEST(static_cast<int>(r[0].first) == 0);
	BOOST_TEST(static_cast<int>(r[1].first) == 1);
}

// A second file's first 128 kiB starts inside the piece where the previous
// file ended; the override begins at that piece, not piece 0.
BOOST_AUTO_TEST_CASE(second_file_starts_mid_torrent)
{
	// piece_length = 256 kiB. file 0 = 1 MiB (4 pieces), file 1 = 1 MiB.
	// file 1 starts at byte 1 MiB = piece 4.
	auto const fs = make_fs(256 * 1024, {1024 * 1024, 1024 * 1024});

	std::vector<piece_pri_t> r;
	ltweb::aux::header_pieces(fs, lt::file_index_t(1), 128 * 1024, r);
	BOOST_TEST_REQUIRE(r.size() == 1u);
	BOOST_TEST(static_cast<int>(r[0].first) == 4);
}

// A file smaller than header_size only contributes pieces that actually
// cover its data; trailing pieces past the file end are not added.
BOOST_AUTO_TEST_CASE(file_smaller_than_header)
{
	// piece_length = 64 kiB; file = 50 kiB; header_size = 128 kiB.
	auto const fs = make_fs(64 * 1024, {50 * 1024});

	std::vector<piece_pri_t> r;
	ltweb::aux::header_pieces(fs, lt::file_index_t(0), 128 * 1024, r);
	BOOST_TEST_REQUIRE(r.size() == 1u);
	BOOST_TEST(static_cast<int>(r[0].first) == 0);
}

// A zero-size file appends no overrides, even with a non-zero header_size.
BOOST_AUTO_TEST_CASE(zero_size_file)
{
	auto const fs = make_fs(64 * 1024, {0, 1024 * 1024});

	std::vector<piece_pri_t> r;
	ltweb::aux::header_pieces(fs, lt::file_index_t(0), 128 * 1024, r);
	BOOST_TEST(r.empty());
}

// header_size of 0 appends no pieces, even when the file has data.
BOOST_AUTO_TEST_CASE(zero_header_size)
{
	auto const fs = make_fs(64 * 1024, {1024 * 1024});

	std::vector<piece_pri_t> r;
	ltweb::aux::header_pieces(fs, lt::file_index_t(0), 0, r);
	BOOST_TEST(r.empty());
}

// The override list never extends past the last piece, even when the file
// is the last in the torrent and shorter than header_size.
BOOST_AUTO_TEST_CASE(does_not_exceed_last_piece)
{
	// piece_length = 256 kiB; one file of 100 kiB; total pieces = 1.
	auto const fs = make_fs(256 * 1024, {100 * 1024});

	std::vector<piece_pri_t> r;
	ltweb::aux::header_pieces(fs, lt::file_index_t(0), 1024 * 1024, r);
	BOOST_TEST_REQUIRE(r.size() == 1u);
	BOOST_TEST(static_cast<int>(r[0].first) == 0);
}

// Successive calls accumulate into the same destination vector and do not
// erase entries appended by previous calls.
BOOST_AUTO_TEST_CASE(appends_without_clearing)
{
	auto const fs = make_fs(256 * 1024, {1024 * 1024, 1024 * 1024});

	std::vector<piece_pri_t> r;
	r.emplace_back(lt::piece_index_t(99), lt::top_priority); // sentinel
	ltweb::aux::header_pieces(fs, lt::file_index_t(0), 128 * 1024, r);
	ltweb::aux::header_pieces(fs, lt::file_index_t(1), 128 * 1024, r);
	BOOST_TEST_REQUIRE(r.size() == 3u);
	BOOST_TEST(static_cast<int>(r[0].first) == 99);
	BOOST_TEST(static_cast<int>(r[1].first) == 0);
	BOOST_TEST(static_cast<int>(r[2].first) == 4);
}
