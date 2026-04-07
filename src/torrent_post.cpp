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
#include "libtorrent/aux_/http_parser.hpp" // for http_parser
#include "libtorrent/torrent_info.hpp"

using namespace libtorrent;

bool parse_torrent_post(http::request<http::string_body> const& req
	, add_torrent_params& params, error_code& ec)
{
	std::string const& body = req.body();
	int content_length = static_cast<int>(body.size());

	if (content_length <= 0)
	{
		ec = error_code(boost::system::errc::invalid_argument, boost::system::generic_category());
		return false;
	}

	if (content_length > 10 * 1024 * 1024)
	{
		ec = error_code(boost::system::errc::file_too_large, boost::system::generic_category());
		return false;
	}

	char const* body_start = body.c_str();
	char const* body_end = body_start + content_length;

	// expect a multipart message here
	std::string const content_type_str = std::string(req[http::field::content_type]);
	char const* content_type = content_type_str.c_str();
	if (strstr(content_type, "multipart/form-data") == nullptr) return false;

	char const* boundary = strstr(content_type, "boundary=");
	if (boundary == nullptr) return false;

	boundary += 9;

	char const* part_start = strstr(body_start, boundary);
	if (part_start == nullptr) return false;

	part_start += strlen(boundary);
	char const* part_end = nullptr;

	// loop through all parts
	for (; part_start < body_end; part_start = (std::min)(body_end, part_end + strlen(boundary)))
	{
		part_end = strstr(part_start, boundary);
		if (part_end == nullptr) part_end = body_end;

		aux::http_parser part;
		bool error = false;
		part.incoming(span<char const>(part_start, part_end - part_start), error);

		std::string const& disposition = part.header("content-type");
		if (disposition != "application/octet-stream"
			&& disposition != "application/x-bittorrent") continue;

		char const* torrent_start = part.get_body().data();
		params.ti = std::make_shared<torrent_info>(span<char const>{torrent_start, part_end - torrent_start}, ec, from_span);
		if (ec) return false;
		return true;
	}

	return false;
}
