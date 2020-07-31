/*

Copyright (c) 2020, Arvid Norberg
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

#include "mime_type.hpp"

namespace libtorrent {

std::string_view mime_type(fs::path const& ext)
{
	if (ext == ".htm")  return "text/html";
	if (ext == ".html") return "text/html";
	if (ext == ".css")  return "text/css";
	if (ext == ".txt")  return "text/plain";
	if (ext == ".js")   return "text/javascript";
	if (ext == ".json") return "application/json";
	if (ext == ".xml")  return "application/xml";
	if (ext == ".png")  return "image/png";
	if (ext == ".jpeg") return "image/jpeg";
	if (ext == ".jpg")  return "image/jpeg";
	if (ext == ".gif")  return "image/gif";
	if (ext == ".bmp")  return "image/bmp";
	if (ext == ".ico")  return "image/vnd.microsoft.icon";
	if (ext == ".tiff") return "image/tiff";
	if (ext == ".tif")  return "image/tiff";
	if (ext == ".svg")  return "image/svg+xml";
	if (ext == ".svgz") return "image/svg+xml";
	return "application/octet-stream";
}

}
