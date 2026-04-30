// Copyright (c) 2026, Arvid Norberg
// All rights reserved.
//
// You may use, distribute and modify this code under the terms of the BSD license,
// see LICENSE file.

#ifndef LTWEB_FILE_RESPONSE_HPP
#define LTWEB_FILE_RESPONSE_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "webui.hpp" // for beast/http aliases

namespace ltweb {
namespace aux {

// Strip an optional ?query and return the bare URL path.
std::string_view strip_query(std::string_view target);

// Result of selecting which file to serve. .path is the file to
// open. .mtime is its last-modified time. .gzip_encoded is true when
// .path is a .gz sibling and the response should declare
// content-encoding: gzip. .content_type_extension is the extension
// (including the leading dot, e.g. ".css") whose mime_type() lookup
// should drive the response's Content-Type header. It is taken from
// the originally-requested path, NEVER from .path -- when
// .gzip_encoded is true, .path's extension is ".gz", and using it
// would mislabel a gzipped CSS file as application/gzip. Per RFC
// 9110 sec. 8.4: Content-Type describes the underlying media type;
// the gzip wrapper is conveyed separately via Content-Encoding.
struct gzip_resolution {
	std::filesystem::path path;
	std::filesystem::file_time_type mtime;
	bool gzip_encoded;
	std::string content_type_extension;
};

// Decide which on-disk file should satisfy a request for `requested`,
// honouring the client's Accept-Encoding header. If accept_encoding
// contains "gzip" and a sibling <requested>.gz exists, returns that
// file with gzip_encoded=true. Otherwise returns the original path
// with gzip_encoded=false. Returns nullopt if neither file exists.
// The check against accept_encoding is a literal substring match
// (e.g. "gzip, deflate" matches; "gzip;q=0" also matches, preserving
// the previous behaviour).
std::optional<gzip_resolution>
resolve_gzip_alternate(std::filesystem::path const& requested, std::string_view accept_encoding);

// Construct an ETag value (including the surrounding double-quotes)
// derived from a file's last-modified time. Stable for a given mtime:
// the same mtime always produces the same ETag. Not collision-resistant;
// we only need stability for cache validation.
std::string etag_for_mtime(std::filesystem::file_time_type mtime);

// Returns true when an If-None-Match request header value mentions
// the given etag. Both arguments must be the quoted form
// (e.g. "\"abc\""); etag must be non-empty. Treats the header value as
// a plain substring search, so "\"foo\", \"bar\"" matches either entry.
// Wildcards ("*") are not implemented.
bool etag_matches(std::string_view if_none_match, std::string_view etag);

// Apply the headers shared by every successful and revalidation
// response from serve_local_file: Content-Type, ETag, Content-Length,
// keep-alive, Vary: Accept-Encoding, and (when gzip_encoded is true)
// Content-Encoding: gzip.
//
// Vary is required so HTTP caches treat the gzip-encoded and
// identity variants of a URL as separate cache entries -- without
// it, a cache populated by a gzip-capable client could replay the
// compressed bytes to a client that did not advertise gzip support.
//
// gzip_encoded must be true whenever the response body is (or, for
// HEAD, would be) the .gz variant: a HEAD response's headers are
// required to match what a GET would return, and a GET serving the
// .gz file must declare its encoding. Pass false from the 304
// branch -- per RFC 9110 sec. 15.4.5, Content-Encoding is not part
// of the headers required on a 304, and the matching ETag already
// uniquely identifies the variant the cache has stored.
template <typename Response>
void apply_static_response_headers(
	Response& res,
	std::string_view content_type,
	std::string_view etag,
	std::uint64_t content_length,
	bool keep_alive,
	bool gzip_encoded
)
{
	res.set(http::field::content_type, content_type);
	res.set(http::field::etag, etag);
	res.set(http::field::vary, "Accept-Encoding");
	if (gzip_encoded) res.set(http::field::content_encoding, "gzip");
	res.content_length(content_length);
	res.keep_alive(keep_alive);
}

// Send the file at full_path as the response. Owns the entire
// file-serving pipeline: method validation (only GET and HEAD are
// allowed; others get 405), gzip-sibling negotiation, ETag generation,
// conditional-request handling (If-None-Match -> 304), HEAD-vs-GET
// selection, and Content-Length.
//
// The caller is responsible for routing, authentication, and computing
// full_path. full_path is taken as-is: this function does not prepend
// any root or strip any prefix. done() is invoked exactly once when
// the response has been written.
void serve_local_file(
	http::request<http::string_body> const& request,
	std::filesystem::path const& full_path,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
);

} // namespace aux
} // namespace ltweb

#endif
