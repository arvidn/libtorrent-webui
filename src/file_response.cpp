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

std::optional<encoding_resolution>
resolve_encoded_alternate(fs::path const& requested, std::string_view accept_encoding)
{
	// content_type_extension is always taken from `requested`, never from
	// the compressed sibling's path -- the sibling's extension is ".zst"
	// or ".gz", which would mislabel the Content-Type. See comment on
	// encoding_resolution.
	// TODO: parse Accept-Encoding per RFC 9110 sec. 12.5.3 instead of
	// substring-matching. Current checks wrongly serve compressed variants
	// for "zstd;q=0" / "gzip;q=0" (explicit refusal) and ignore "*".
	std::string const ext = requested.extension().string();

	if (boost::algorithm::contains(accept_encoding, "zstd")) {
		std::error_code ec;
		fs::path zst_path = requested;
		zst_path += ".zst";
		auto const mtime = fs::last_write_time(zst_path, ec);
		if (!ec) return encoding_resolution{std::move(zst_path), mtime, "zstd", ext};
	}

	if (boost::algorithm::contains(accept_encoding, "gzip")) {
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
