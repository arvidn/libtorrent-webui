/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PRIORITIZE_HEADERS_HPP
#define LTWEB_PRIORITIZE_HEADERS_HPP

#include "alert_observer.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/download_priority.hpp"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace ltweb {

struct alert_handler;

namespace aux {

// One below lt::top_priority (which is 7), so callers that explicitly raise
// individual pieces to the absolute top still outrank the auto-promoted
// header pieces.
constexpr lt::download_priority_t header_piece_priority{6};

// Returns true if name has an extension (case-insensitive) that suggests
// the file is video, audio, or image content. Files without an extension,
// or with an extension that is not in the known media set, return false.
bool is_media_file_extension(std::string_view name);

// Appends (piece_index, header_piece_priority) pairs covering the first
// header_size bytes of the file at index fi to out. The span is clipped to
// the actual file size, so a file shorter than header_size appends fewer
// entries; a zero-size file appends none. Existing entries in out are not
// touched.
//
// Preconditions: fs.piece_length() > 0; fi is a valid index into fs.
void header_pieces(
	lt::file_storage const& fs,
	lt::file_index_t fi,
	std::int64_t header_size,
	std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>>& out
);

} // namespace aux

// Subscribes to alerts and, whenever a torrent gains metadata or its file
// priorities change, raises the priority of pieces covering the first
// 128 kiB of every video/audio/image file. Files with priority 0 are
// skipped, so their pieces remain at 0.
struct prioritize_headers : alert_observer {

	static constexpr std::int64_t default_header_size = 128 * 1024;

	explicit prioritize_headers(alert_handler* h, std::int64_t header_size = default_header_size);
	~prioritize_headers();

	void handle_alert(lt::alert const* a) override;

private:
	// No-op if the torrent has no metadata yet or the handle has been
	// invalidated. Otherwise computes the header-piece overrides for every
	// applicable file and submits them in a single prioritize_pieces() call.
	void apply(lt::torrent_handle const& h) const;

	alert_handler* m_alerts;
	std::int64_t m_header_size;
};

} // namespace ltweb

#endif
