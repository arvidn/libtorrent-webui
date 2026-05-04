/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "prioritize_headers.hpp"
#include "alert_handler.hpp"
#include "utils.hpp" // for extension()

#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/assert.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <memory>

namespace ltweb {

namespace aux {

bool is_media_file_extension(std::string_view name)
{
	auto const ext = ltweb::extension(name);
	if (ext.size() < 2) return false;
	auto const e = ext.substr(1);

	// Sorted (ascending, lowercase) so binary_search applies. The static_assert
	// below catches any future edit that breaks the ordering at compile time.
	static constexpr std::string_view exts[] = {
		"3gp",	"aac",	"ac3", "alac", "amr",  "ape",  "asf", "avi",  "avif", "bmp",
		"dts",	"flac", "flv", "gif",  "heic", "heif", "ico", "jpeg", "jpg",  "jxl",
		"m2ts", "m4a",	"m4v", "mka",  "mkv",  "mov",  "mp3", "mp4",  "mpeg", "mpg",
		"mts",	"oga",	"ogg", "ogv",  "opus", "png",  "rm",  "rmvb", "svg",  "tif",
		"tiff", "ts",	"vob", "wav",  "webm", "webp", "wma", "wmv",  "wv",
	};
	static_assert(std::is_sorted(std::begin(exts), std::end(exts)));

	constexpr std::size_t buf_size = 8;
	if (e.size() > buf_size) return false;
	char buf[buf_size];
	for (std::size_t i = 0; i < e.size(); ++i)
		buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(e[i])));
	std::string_view const key(buf, e.size());

	return std::binary_search(std::begin(exts), std::end(exts), key);
}

void header_pieces(
	lt::file_storage const& fs,
	lt::file_index_t const fi,
	std::int64_t const header_size,
	std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>>& out
)
{
	TORRENT_ASSERT(fs.piece_length() > 0);

	std::int64_t const fsize = fs.file_size(fi);
	if (fsize <= 0 || header_size <= 0) return;

	std::int64_t const span = std::min<std::int64_t>(fsize, header_size);
	std::int64_t const offset = fs.file_offset(fi);
	int const piece_length = fs.piece_length();

	int const begin_idx = static_cast<int>(offset / piece_length);
	int const end_idx = static_cast<int>((offset + span - 1) / piece_length) + 1;
	int const last_piece = static_cast<int>(fs.last_piece());

	for (int i = begin_idx; i < end_idx && i <= last_piece; ++i)
		out.emplace_back(lt::piece_index_t(i), header_piece_priority);
}

} // namespace aux

prioritize_headers::prioritize_headers(alert_handler* h, std::int64_t const header_size)
	: m_alerts(h)
	, m_header_size(header_size)
{
	m_alerts->subscribe(
		this,
		0,
		lt::add_torrent_alert::alert_type,
		lt::metadata_received_alert::alert_type,
		lt::file_prio_alert::alert_type,
		0
	);
}

prioritize_headers::~prioritize_headers() { m_alerts->unsubscribe(this); }

void prioritize_headers::handle_alert(lt::alert const* a)
try {
	if (auto const* ta = lt::alert_cast<lt::add_torrent_alert>(a)) {
		if (ta->error) return;
		apply(ta->handle);
	} else if (auto const* mr = lt::alert_cast<lt::metadata_received_alert>(a)) {
		apply(mr->handle);
	} else if (auto const* fp = lt::alert_cast<lt::file_prio_alert>(a)) {
		// If the disk operation failed, libtorrent posts file_error_alert
		// instead, but guard anyway.
		if (fp->error) return;
		apply(fp->handle);
	}
} catch (std::exception const&) {
}

void prioritize_headers::apply(lt::torrent_handle const& h) const
{
	if (!h.is_valid()) return;

	std::shared_ptr<const lt::torrent_info> ti = h.torrent_file();
	if (!ti) return;

	lt::file_storage const& fs = ti->layout();
	if (fs.num_pieces() == 0 || fs.piece_length() <= 0) return;

	std::vector<lt::download_priority_t> const fprio = h.get_file_priorities();

	std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>> overrides;

	for (auto const fi : fs.file_range()) {
		std::size_t const i = static_cast<std::size_t>(static_cast<int>(fi));
		// Files dropped from the priority list (e.g. trailing entries)
		// default to default_priority, so only skip explicit zeros.
		if (i < fprio.size() && fprio[i] == lt::dont_download) continue;
		if (!aux::is_media_file_extension(fs.file_name(fi))) continue;

		aux::header_pieces(fs, fi, m_header_size, overrides);
	}

	if (!overrides.empty()) h.prioritize_pieces(overrides);
}

} // namespace ltweb
