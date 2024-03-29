/*

Copyright (c) 2013, Arvid Norberg
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

#include "http_whitelist.hpp"
#include "libtorrent/aux_/path.hpp"

extern "C" {
#include "local_mongoose.h"
}

namespace libtorrent
{
	// TODO: get rid of these dependencies
	using lt::lsplit_path;

	http_whitelist::http_whitelist() {}
	http_whitelist::~http_whitelist() {}

	void http_whitelist::add_allowed_prefix(std::string const& prefix)
	{
		m_whitelist.insert(prefix);
	}

	bool http_whitelist::handle_http(mg_connection* conn
		, mg_request_info const* request_info)
	{
		std::string request = request_info->uri;
		const auto split = lsplit_path(request);
		std::string first_element(split.first);

		if (m_whitelist.count(first_element) == 0)
		{
			mg_printf(conn, "HTTP/1.1 404 Not Found\r\n"
				"Content-Length: 0\r\n\r\n");
			return true;
		}

		// forward in the handler chain
		return false;
	}

}

