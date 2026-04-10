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

using namespace ltweb;

lt::add_torrent_params parse_torrent_post(http::request<http::string_body> const& req
	, lt::error_code& ec)
{
	auto const invalid = [&]() -> lt::add_torrent_params {
		ec = lt::error_code(boost::system::errc::invalid_argument, boost::system::generic_category());
		return {};
	};

	std::string const& body = req.body();
	int content_length = static_cast<int>(body.size());

	if (content_length <= 0) return invalid();

	if (content_length > 10 * 1024 * 1024)
	{
		ec = lt::error_code(boost::system::errc::file_too_large, boost::system::generic_category());
		return {};
	}

	char const* body_start = body.c_str();
	char const* body_end = body_start + content_length;

	// expect a multipart message here
	std::string const content_type_str = std::string(req[http::field::content_type]);
	char const* content_type = content_type_str.c_str();
	if (strstr(content_type, "multipart/form-data") == nullptr) return invalid();

	char const* boundary = strstr(content_type, "boundary=");
	if (boundary == nullptr) return invalid();

	boundary += 9;

	char const* part_start = strstr(body_start, boundary);
	if (part_start == nullptr) return invalid();

	part_start += strlen(boundary);
	char const* part_end = nullptr;

	// loop through all parts
	for (; part_start < body_end; part_start = (std::min)(body_end, part_end + strlen(boundary)))
	{
		part_end = strstr(part_start, boundary);
		if (part_end == nullptr) part_end = body_end;

		std::string content_type;
		char const* torrent_start = parse_mime_part(part_start, part_end, content_type);
		if (torrent_start == nullptr) continue;
		if (content_type != "application/octet-stream"
			&& content_type != "application/x-bittorrent") continue;

		return lt::load_torrent_buffer(
			lt::span<char const>{torrent_start, part_end - torrent_start}, ec, lt::load_torrent_limits{});
	}

	return invalid();
}
