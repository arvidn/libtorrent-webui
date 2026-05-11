/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "file_history.hpp"
#include <algorithm>

namespace ltweb {
namespace {

struct open_file_entry {
	int file_index;
	std::uint8_t open_mode;
};

} // anonymous namespace

file_history::file_history(lt::sha1_hash const& ih, lt::file_storage const& fs)
	: m_ih(ih)
	, m_files(static_cast<std::size_t>(fs.num_files()))
{
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

void file_history::update_open_modes(
	std::vector<lt::open_file_state> const& open_modes, frame_t const frame
)
{
	int const n = static_cast<int>(m_files.size());
	std::vector<open_file_entry> current;
	current.reserve(open_modes.size());

	for (auto const& s : open_modes) {
		int const fi = static_cast<int>(s.file_index);
		if (fi < 0 || fi >= n) continue;
		current.push_back({fi, static_cast<std::uint8_t>(s.open_mode)});
	}

	std::stable_sort(current.begin(), current.end(), [](auto const& lhs, auto const& rhs) {
		return lhs.file_index < rhs.file_index;
	});

	auto out = current.begin();
	for (auto it = current.begin(); it != current.end();) {
		int const fi = it->file_index;
		std::uint8_t mode = it->open_mode;
		for (++it; it != current.end() && it->file_index == fi; ++it)
			mode = it->open_mode;
		*out++ = {fi, mode};
	}
	current.erase(out, current.end());

	std::vector<int> next_open_files;
	next_open_files.reserve(current.size());

	auto old = m_open_files.begin();
	auto cur = current.begin();
	while (old != m_open_files.end() || cur != current.end()) {
		int file_index;
		std::uint8_t new_mode;

		if (old == m_open_files.end() || (cur != current.end() && cur->file_index < *old)) {
			file_index = cur->file_index;
			new_mode = cur->open_mode;
			++cur;
		} else if (cur == current.end() || *old < cur->file_index) {
			file_index = *old;
			new_mode = 0;
			++old;
		} else {
			file_index = *old;
			new_mode = cur->open_mode;
			++old;
			++cur;
		}

		if (m_files[file_index].open_mode != new_mode) {
			m_files[file_index].open_mode = new_mode;
			m_files[file_index].open_mode_frame = frame;
		}
		if (new_mode != 0) next_open_files.push_back(file_index);
	}

	m_open_files.swap(next_open_files);
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
