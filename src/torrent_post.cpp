/*

Copyright (c) 2012, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "torrent_post.hpp"
#include "mime_part.hpp"
#include "libtorrent/load_torrent.hpp"

#include <string_view>

using namespace ltweb;

lt::add_torrent_params parse_torrent_post(http::request<http::string_body> const& req
	, lt::error_code& ec)
{
	auto const invalid = [&]() -> lt::add_torrent_params {
		ec = lt::error_code(boost::system::errc::invalid_argument, boost::system::generic_category());
		return {};
	};

	std::string_view const body = req.body();

	if (body.empty()) return invalid();

	if (body.size() > 10 * 1024 * 1024)
	{
		ec = lt::error_code(boost::system::errc::file_too_large, boost::system::generic_category());
		return {};
	}

	// expect a multipart message here
	std::string_view const ct{req[http::field::content_type]};
	if (ct.find("multipart/form-data") == std::string_view::npos) return invalid();

	auto const boundary_pos = ct.find("boundary=");
	if (boundary_pos == std::string_view::npos) return invalid();
	std::string_view const boundary = ct.substr(boundary_pos + 9);

	// find the first boundary marker in the body
	auto const first = body.find(boundary);
	if (first == std::string_view::npos) return invalid();

	std::string_view remaining = body.substr(first + boundary.size());

	// loop through all parts
	while (!remaining.empty())
	{
		auto const next = remaining.find(boundary);
		std::string_view const part = (next == std::string_view::npos)
			? remaining : remaining.substr(0, next);

		std::string content_type;
		std::string_view torrent_body = parse_mime_part(part, content_type);

		if (torrent_body.data() != nullptr
			&& (content_type == "application/octet-stream"
				|| content_type == "application/x-bittorrent"))
		{
			if (torrent_body.size() >= 4
				&& torrent_body.substr(torrent_body.size() - 4) == "\r\n--")
				torrent_body.remove_suffix(4);

			return lt::load_torrent_buffer(
				lt::span<char const>{torrent_body.data(), int(torrent_body.size())},
				ec, lt::load_torrent_limits{});
		}

		if (next == std::string_view::npos) break;
		remaining = remaining.substr(next + boundary.size());
	}

	return invalid();
}
