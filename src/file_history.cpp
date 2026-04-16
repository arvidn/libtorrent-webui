/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "file_history.hpp"
#include <unordered_map>

namespace ltweb {

file_history::file_history(lt::sha1_hash const& ih, lt::file_storage const& fs)
	: m_ih(ih)
	, m_files(static_cast<std::size_t>(fs.num_files()))
{}

frame_t file_history::update(
	std::vector<std::int64_t> const* progress,
	std::vector<lt::download_priority_t> const* priorities,
	std::vector<lt::open_file_state> const* open_modes)
{
	frame_t const frame = ++m_frame;
	int const n = static_cast<int>(m_files.size());

	if (progress != nullptr)
	{
		for (int fi = 0; fi < n && fi < static_cast<int>(progress->size()); ++fi)
		{
			if ((*progress)[fi] != m_files[fi].progress)
			{
				m_files[fi].progress = (*progress)[fi];
				m_files[fi].progress_frame = frame;
			}
		}
	}

	if (priorities != nullptr)
	{
		for (int fi = 0; fi < n && fi < static_cast<int>(priorities->size()); ++fi)
		{
			if ((*priorities)[fi] != m_files[fi].priority)
			{
				m_files[fi].priority = (*priorities)[fi];
				m_files[fi].priority_frame = frame;
			}
		}
	}

	if (open_modes != nullptr)
	{
		// open_modes is sparse: only currently-open files appear.
		// Build a dense lookup for this update round.
		std::unordered_map<int, std::uint8_t> om;
		om.reserve(open_modes->size());
		for (auto const& s : *open_modes)
			om[static_cast<int>(s.file_index)] = static_cast<std::uint8_t>(s.open_mode);

		for (int fi = 0; fi < n; ++fi)
		{
			auto const it = om.find(fi);
			std::uint8_t const new_mode = (it != om.end()) ? it->second : std::uint8_t(0);
			if (new_mode != m_files[fi].open_mode)
			{
				m_files[fi].open_mode = new_mode;
				m_files[fi].open_mode_frame = frame;
			}
		}
	}

	return frame;
}

std::vector<std::uint16_t> file_history::query(
	frame_t since_frame,
	std::uint16_t requested_mask) const
{
	int const n = static_cast<int>(m_files.size());
	std::vector<std::uint16_t> masks(static_cast<std::size_t>(n), 0);

	// Static fields are conceptually at frame 1: include them when the client
	// has never received an update (since_frame == 0).
	std::uint16_t const static_bits = (since_frame == 0)
		? (requested_mask & 0x07u)
		: std::uint16_t(0);

	for (int fi = 0; fi < n; ++fi)
	{
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
