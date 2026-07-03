// Copyright (c) 2026, Arvid Norberg
// All rights reserved.
//
// You may use, distribute and modify this code under the terms of the BSD license,
// see LICENSE file.

#include "file_response.hpp"
#include "mime_type.hpp"

#include <sstream>
#include <system_error>

#include <boost/algorithm/string/predicate.hpp>

#include "libtorrent/hasher.hpp"

namespace fs = std::filesystem;

namespace ltweb {
namespace aux {

std::string_view strip_query(std::string_view target)
{
	auto q = target.find('?');
	if (q != std::string_view::npos) return target.substr(0, q);
	return target;
}

namespace {

// Returns whether a q-value token is exactly zero, per the RFC 9110
// sec. 12.4.2 grammar: ("0" ["." 0*3DIGIT]) / ("1" ["." 0*3("0")]).
// Whitespace (RFC 9110 sec. 5.6.3 OWS: space or tab) and '.' are
// ignored wherever they appear, so surrounding or embedded whitespace
// never needs trimming separately -- a value is zero iff every
// non-ignored character in it is '0'. Malformed input (a digit other
// than '0', or no digit at all) is treated as the default weight
// (q=1, i.e. not zero) since we only need to distinguish "explicitly
// refused" from everything else.
bool is_zero_qvalue(std::string_view value)
{
	bool seen_zero = false;
	for (char const c : value) {
		if (c == ' ' || c == '\t' || c == '.') continue;
		if (c != '0') return false;
		seen_zero = true;
	}
	return seen_zero;
}

// Scans accept_encoding once for an entry whose name matches `token`
// (case-insensitively) and returns whether that entry's q-value is
// nonzero, or nullopt if `token` is not mentioned at all. Returns the
// first matching entry's weight; RFC 9110 does not define which one
// wins when a token is listed more than once.
std::optional<bool> entry_accept(std::string_view accept_encoding, std::string_view token)
{
	std::string_view remaining = accept_encoding;
	while (!remaining.empty()) {
		auto const comma = remaining.find(',');
		std::string_view entry = remaining.substr(0, comma);
		remaining =
			(comma == std::string_view::npos) ? std::string_view{} : remaining.substr(comma + 1);

		auto const semi = entry.find(';');
		std::string_view name = entry.substr(0, semi);
		while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
			name.remove_prefix(1);
		while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
			name.remove_suffix(1);
		if (name.empty() || !boost::algorithm::iequals(name, token)) continue;

		bool accept = true;
		if (semi != std::string_view::npos) {
			std::string_view params = entry.substr(semi + 1);
			while (!params.empty()) {
				auto const semi2 = params.find(';');
				std::string_view param = params.substr(0, semi2);
				params = (semi2 == std::string_view::npos) ? std::string_view{}
														   : params.substr(semi2 + 1);

				auto const eq = param.find('=');
				if (eq == std::string_view::npos) continue;
				std::string_view pname = param.substr(0, eq);
				while (!pname.empty() && (pname.front() == ' ' || pname.front() == '\t'))
					pname.remove_prefix(1);
				while (!pname.empty() && (pname.back() == ' ' || pname.back() == '\t'))
					pname.remove_suffix(1);
				if (boost::algorithm::iequals(pname, "q")) {
					accept = !is_zero_qvalue(param.substr(eq + 1));
					break;
				}
			}
		}
		return accept;
	}
	return std::nullopt;
}

// Returns whether `accept_encoding` (RFC 9110 sec. 12.5.3) permits
// `coding`. An explicit "<coding>;q=..." entry always wins over a "*"
// entry -- including an explicit q=0 refusal, per the spec's
// precedence rule. A coding mentioned nowhere is not acceptable:
// unlike identity, gzip/zstd are never implied.
bool encoding_acceptable(std::string_view accept_encoding, std::string_view coding)
{
	if (auto const accept = entry_accept(accept_encoding, coding)) return *accept;
	if (auto const accept = entry_accept(accept_encoding, "*")) return *accept;
	return false;
}

} // namespace

std::optional<encoding_resolution>
resolve_encoded_alternate(fs::path const& requested, std::string_view accept_encoding)
{
	// content_type_extension is always taken from `requested`, never from
	// the compressed sibling's path -- the sibling's extension is ".zst"
	// or ".gz", which would mislabel the Content-Type. See comment on
	// encoding_resolution.
	std::string const ext = requested.extension().string();

	if (encoding_acceptable(accept_encoding, "zstd")) {
		std::error_code ec;
		fs::path zst_path = requested;
		zst_path += ".zst";
		auto const mtime = fs::last_write_time(zst_path, ec);
		if (!ec) return encoding_resolution{std::move(zst_path), mtime, "zstd", ext};
	}

	if (encoding_acceptable(accept_encoding, "gzip")) {
		std::error_code ec;
		fs::path gz_path = requested;
		gz_path += ".gz";
		auto const mtime = fs::last_write_time(gz_path, ec);
		if (!ec) return encoding_resolution{std::move(gz_path), mtime, "gzip", ext};
	}

	std::error_code ec;
	auto const mtime = fs::last_write_time(requested, ec);
	if (ec) return std::nullopt;
	return encoding_resolution{requested, mtime, {}, ext};
}

std::string etag_for_mtime(fs::file_time_type mtime)
{
	auto const mtime_int = mtime.time_since_epoch().count();
	lt::sha1_hash const mtime_h =
		lt::hasher(reinterpret_cast<char const*>(&mtime_int), int(sizeof(mtime_int))).final();

	std::stringstream str;
	str << '"' << mtime_h << '"';
	return str.str();
}

static std::string_view cache_control_for_extension(std::string_view ext)
{
	if (ext == ".html") return "no-cache";
	return "max-age=3600";
}

bool etag_matches(std::string_view if_none_match, std::string_view etag)
{
	if (etag.empty()) return false;
	return boost::algorithm::contains(if_none_match, etag);
}

void serve_local_file(
	http::request<http::string_body> const& request,
	fs::path const& full_path,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	if (request.method() != http::verb::get && request.method() != http::verb::head) {
		return send_http(
			socket, std::move(done), http_error(request, http::status::method_not_allowed)
		);
	}

	auto const enc_it = request.find(http::field::accept_encoding);
	std::string_view const accept_enc = (enc_it != request.end())
		? std::string_view(enc_it->value().data(), enc_it->value().size())
		: std::string_view{};

	auto const resolved = resolve_encoded_alternate(full_path, accept_enc);
	if (!resolved)
		return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	beast::error_code ec;
	http::file_body::value_type body;
	body.open(resolved->path.c_str(), beast::file_mode::scan, ec);
	if (ec) return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	auto const size = body.size();
	std::string const etag = etag_for_mtime(resolved->mtime);
	// Use resolved->content_type_extension, NOT resolved->path.extension():
	// when a compressed sibling is selected, resolved->path's extension
	// is ".gz" or ".zst", which would yield the wrong Content-Type for a
	// .css/.html/.js etc. request.
	std::string const& extension = resolved->content_type_extension;
	std::string_view const cache_ctrl = cache_control_for_extension(extension);

	auto const inm_it = request.find(http::field::if_none_match);
	std::string_view const if_none_match = (inm_it != request.end())
		? std::string_view(inm_it->value().data(), inm_it->value().size())
		: std::string_view{};

	if (etag_matches(if_none_match, etag)) {
		// 304 deliberately omits Content-Encoding (empty string):
		// per RFC 9110 sec. 15.4.5 it is not in the required header set
		// for a 304, and the matching ETag already pins the variant.
		http::response<http::empty_body> res{http::status::not_modified, request.version()};
		apply_static_response_headers(
			res, mime_type(extension), etag, cache_ctrl, size, request.keep_alive(), {}
		);
		return send_http(socket, std::move(done), std::move(res));
	}

	if (request.method() == http::verb::head) {
		// HEAD must mirror GET's headers, so forward content_encoding.
		http::response<http::empty_body> res{http::status::ok, request.version()};
		apply_static_response_headers(
			res,
			mime_type(extension),
			etag,
			cache_ctrl,
			size,
			request.keep_alive(),
			resolved->content_encoding
		);
		return send_http(socket, std::move(done), std::move(res));
	}

	http::response<http::file_body> res{
		std::piecewise_construct,
		std::make_tuple(std::move(body)),
		std::make_tuple(http::status::ok, request.version())
	};
	apply_static_response_headers(
		res,
		mime_type(extension),
		etag,
		cache_ctrl,
		size,
		request.keep_alive(),
		resolved->content_encoding
	);
	send_http(socket, std::move(done), std::move(res));
}

} // namespace aux
} // namespace ltweb
