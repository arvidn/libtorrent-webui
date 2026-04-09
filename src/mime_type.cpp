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
#include <unordered_map>
#include <string_view>

namespace ltweb {

using namespace std::literals::string_view_literals;

namespace {
std::unordered_map<std::string_view, std::string_view> const g_mime_types{
	{".torrent"sv, "application/x-bittorrent"sv},
	{".json"sv, "application/json"sv},
	{".xslt"sv, "application/xml"sv},
	{".xsl"sv, "application/xml"sv},
	{".doc"sv, "application/msword"sv},
	{".exe"sv, "application/octet-stream"sv},
	{".zip"sv, "application/zip"sv},
	{".xls"sv, "application/excel"sv},
	{".tgz"sv, "application/x-tar-gz"sv},
	{".tar"sv, "application/x-tar"sv},
	{".gz"sv, "application/gzip"sv},
	{".arj"sv, "application/x-arj-compressed"sv},
	{".rar"sv, "application/x-arj-compressed"sv},
	{".rtf"sv, "application/rtf"sv},
	{".pdf"sv, "application/pdf"sv},
	{".swf"sv, "application/x-shockwave-flash"sv},

// text
	{".html"sv, "text/html"sv},
	{".htm"sv, "text/html"sv},
	{".shtm"sv, "text/html"sv},
	{".shtml"sv, "text/html"sv},
	{".css"sv, "text/css"sv},
	{".js"sv, "text/javascript"sv},
	{".txt"sv, "text/plain"sv},
	{".xml"sv, "text/xml"sv},

// audio
	{".mp3"sv, "audio/mpeg"sv},
	{".mid"sv, "audio/mid"sv},
	{".m3u"sv, "audio/x-mpegurl"sv},
	{".ram"sv, "audio/x-pn-realaudio"sv},
	{".ra"sv, "audio/x-pn-realaudio"sv},
	{".ogg"sv, "audio/ogg"sv},
	{".wav"sv, "audio/wav"sv},

// video
	{".mpg"sv, "video/mpeg"sv},
	{".mpeg"sv, "video/mpeg"sv},
	{".webm"sv, "video/webm"sv},
	{".mov"sv, "video/quicktime"sv},
	{".mp4"sv, "video/mp4"sv},
	{".m4v"sv, "video/x-m4v"sv},
	{".asf"sv, "video/x-ms-asf"sv},
	{".avi"sv, "video/x-msvideo"sv},

// image
	{".webp"sv, "image/webp"sv},
	{".bmp"sv, "image/bmp"sv},
	{".ico"sv, "image/vnd.microsoft.icon"sv},
	{".gif"sv, "image/gif"sv},
	{".jpg"sv, "image/jpeg"sv},
	{".jpeg"sv, "image/jpeg"sv},
	{".png"sv, "image/png"sv},
	{".svg"sv, "image/svg+xml"sv},
	{".svgz"sv, "image/svg+xml"sv},
	{".tiff"sv, "image/tiff"sv},
	{".tif"sv, "image/tiff"sv},

// fonts
	{".ttf"sv, "application/x-font-ttf"sv},
	{".woff"sv, "font/woff"sv},
	{".woff2"sv, "font/woff2"sv},
	{".otf"sv, "font/otf"sv},
};

}

std::string_view mime_type(std::string_view ext)
{
	auto it = g_mime_types.find(ext);
	if (it == g_mime_types.end()) return "application/octet-stream";
	return it->second;
}

}
