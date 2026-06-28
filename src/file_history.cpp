/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "file_history.hpp"
#include <algorithm>
#include <libtorrent/assert.hpp>

namespace ltweb {

file_history::file_history(lt::sha1_hash const& ih, lt::file_storage const& fs)
	: m_ih(ih)
	, m_files(static_cast<std::size_t>(fs.num_files()))
{
}

void file_history::update_open_modes(std::vector<lt::open_file_state> current, frame_t frame)
{
	int const n = static_cast<int>(m_files.size());

	std::sort(
		current.begin(),
		current.end(),
		[](lt::open_file_state const& a, lt::open_file_state const& b) {
			return a.file_index < b.file_index;
		}
	);

	TORRENT_ASSERT(
		std::adjacent_find(
			current.begin(),
			current.end(),
			[](lt::open_file_state const& a, lt::open_file_state const& b) {
				return a.file_index == b.file_index;
			}
		)
		== current.end()
	);

	std::vector<lt::file_index_t> next_open;
	next_open.reserve(current.size());

	auto old_it = m_open_files.begin();
	auto new_it = current.begin();

	while (old_it != m_open_files.end() || new_it != current.end()) {
		bool const have_old = old_it != m_open_files.end();
		bool const have_new = new_it != current.end();

		if (have_old && have_new && *old_it == new_it->file_index) {
			// File was open and still is: check mode change.
			lt::file_index_t const fi = *old_it;
			TORRENT_ASSERT(static_cast<int>(fi) >= 0 && static_cast<int>(fi) < n);
			auto& e = m_files[static_cast<std::size_t>(static_cast<int>(fi))];
			if (new_it->open_mode != e.open_mode) {
				e.open_mode = new_it->open_mode;
				e.open_mode_frame = frame;
			}
			next_open.push_back(fi);
			++old_it;
			++new_it;
		} else if (have_old && (!have_new || *old_it < new_it->file_index)) {
			// File was open, now closed.
			lt::file_index_t const fi = *old_it;
			TORRENT_ASSERT(static_cast<int>(fi) >= 0 && static_cast<int>(fi) < n);
			auto& e = m_files[static_cast<std::size_t>(static_cast<int>(fi))];
			e.open_mode = lt::file_open_mode_t{};
			e.open_mode_frame = frame;
			++old_it;
		} else {
			// File was closed, now open.
			lt::file_index_t const fi = new_it->file_index;
			if (static_cast<int>(fi) >= 0 && static_cast<int>(fi) < n) {
				auto& e = m_files[static_cast<std::size_t>(static_cast<int>(fi))];
				e.open_mode = new_it->open_mode;
				e.open_mode_frame = frame;
				next_open.push_back(fi);
			}
			++new_it;
		}
	}

	m_open_files = std::move(next_open);
}

frame_t file_history::update(
	std::vector<std::int64_t> const* progress,
	std::vector<lt::download_priority_t> const* priorities,
	std::vector<lt::open_file_state> const* open_modes
)
{
	frame_t const frame = ++m_frame;
	int const n = static_cast<int>(m_files.size());

	if (progress != nullptr) {
		for (int fi = 0; fi < n && fi < static_cast<int>(progress->size()); ++fi) {
			if ((*progress)[fi] != m_files[fi].progress) {
				m_files[fi].progress = (*progress)[fi];
				m_files[fi].progress_frame = frame;
			}
		}
	}

	if (priorities != nullptr) {
		for (int fi = 0; fi < n && fi < static_cast<int>(priorities->size()); ++fi) {
			if ((*priorities)[fi] != m_files[fi].priority) {
				m_files[fi].priority = (*priorities)[fi];
				m_files[fi].priority_frame = frame;
			}
		}
	}

	if (open_modes != nullptr) update_open_modes(*open_modes, frame);

	return frame;
}

std::vector<std::uint16_t>
file_history::query(frame_t since_frame, std::uint16_t requested_mask) const
{
	int const n = static_cast<int>(m_files.size());
	std::vector<std::uint16_t> masks(static_cast<std::size_t>(n), 0);

	// Static fields are conceptually at frame 1: include them when the client
	// has never received an update (since_frame == 0).
	std::uint16_t const static_bits =
		(since_frame == 0) ? (requested_mask & 0x07u) : std::uint16_t(0);

	for (int fi = 0; fi < n; ++fi) {
		file_history_entry const& e = m_files[fi];
		std::uint16_t m = static_bits;

		if ((requested_mask & 0x08u) && e.progress_frame > since_frame) m |= 0x08u;
		if ((requested_mask & 0x10u) && e.priority_frame > since_frame) m |= 0x10u;
		if ((requested_mask & 0x20u) && e.open_mode_frame > since_frame) m |= 0x20u;

		masks[fi] = m;
	}

	return masks;
}

} // namespace ltweb
