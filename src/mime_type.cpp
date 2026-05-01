/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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

} // namespace ltweb
