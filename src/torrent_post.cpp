/*

Copyright (c) 2012-2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "torrent_post.hpp"
#include "mime_part.hpp"
#include "utils.hpp"
#include "parse_http_auth.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_flags.hpp"

#include <string_view>
#include <vector>

using namespace ltweb;

std::vector<lt::add_torrent_params> parse_torrent_post(
	http::request<http::string_body> const& req,
	lt::error_code& ec,
	torrent_post_limits const& limits
)
{
	auto const invalid = [&]() -> std::vector<lt::add_torrent_params> {
		ec = lt::error_code(
			boost::system::errc::invalid_argument, boost::system::generic_category()
		);
		return {};
	};

	std::string_view const body = req.body();

	if (body.empty()) return invalid();

	if (std::int64_t(body.size()) > limits.max_body_bytes) {
		ec = lt::error_code(boost::system::errc::file_too_large, boost::system::generic_category());
		return {};
	}

	// expect a multipart message here
	std::string_view const ct{req[http::field::content_type]};

	// extract the media-type token (everything before the first ;), trim OWS,
	// then compare exactly -- a substring search would accept e.g. text/multipart/form-data
	auto const first_semi = ct.find(';');
	std::string_view const media_type =
		trim((first_semi == std::string_view::npos) ? ct : ct.substr(0, first_semi));
	if (!iequals(media_type, "multipart/form-data")) return invalid();

	// scan Content-Type parameters (split on ;) for one whose name is exactly "boundary"
	std::string boundary;
	std::string_view params =
		(first_semi == std::string_view::npos) ? std::string_view{} : ct.substr(first_semi + 1);
	while (!params.empty()) {
		auto const semi = params.find(';');
		std::string_view const param =
			trim((semi == std::string_view::npos) ? params : params.substr(0, semi));

		auto const eq = param.find('=');
		if (eq != std::string_view::npos && iequals(trim(param.substr(0, eq)), "boundary")) {
			std::string_view value = trim(param.substr(eq + 1));
			if (!value.empty() && value.front() == '"') {
				auto unquoted = parse_quoted_string(value);
				if (!unquoted) return invalid();
				boundary = std::move(*unquoted);
			} else {
				boundary = std::string(value);
			}
			break;
		}

		if (semi == std::string_view::npos) break;
		params = params.substr(semi + 1);
	}

	if (boundary.empty()) return invalid();

	// find the first boundary marker in the body
	auto const first = body.find(boundary);
	if (first == std::string_view::npos) return invalid();

	std::string_view remaining = body.substr(first + boundary.size());

	std::vector<lt::add_torrent_params> result;
	bool found_matching_part = false;
	lt::error_code last_part_ec;
	std::int64_t payload_so_far = 0;

	// loop through all parts, collecting every accepted .torrent part
	while (!remaining.empty()) {
		auto const next = remaining.find(boundary);
		std::string_view const part =
			(next == std::string_view::npos) ? remaining : remaining.substr(0, next);

		std::string content_type;
		std::string_view torrent_body = parse_mime_part(part, content_type);

		if (torrent_body.data() != nullptr
			&& (content_type == "application/octet-stream"
				|| content_type == "application/x-bittorrent")) {
			found_matching_part = true;

			if (static_cast<int>(result.size()) >= limits.max_torrent_count
				|| payload_so_far + std::int64_t(torrent_body.size()) > limits.max_payload_bytes)
				break;

			payload_so_far += std::int64_t(torrent_body.size());

			if (torrent_body.size() >= 4
				&& torrent_body.substr(torrent_body.size() - 4) == "\r\n--")
				torrent_body.remove_suffix(4);

			lt::error_code part_ec;
			lt::add_torrent_params p = lt::load_torrent_buffer(
				lt::span<char const>{torrent_body.data(), int(torrent_body.size())},
				part_ec,
				lt::load_torrent_limits{}
			);
			if (!part_ec)
				result.push_back(std::move(p));
			else
				last_part_ec = part_ec;
		}

		if (next == std::string_view::npos) break;
		remaining = remaining.substr(next + boundary.size());
	}

	if (result.empty()) {
		// found a matching part but it failed to parse: propagate the torrent
		// error so callers can distinguish this from a request-level rejection
		if (found_matching_part && last_part_ec) {
			ec = last_part_ec;
			return {};
		}
		return invalid();
	}
	return result;
}

namespace ltweb {

torrent_post_handler::torrent_post_handler(
	lt::session& ses, auth_interface const& auth, save_settings_interface* settings
)
	: m_ses(ses)
	, m_auth(auth)
	, m_settings(settings)
{
}

std::string torrent_post_handler::path_prefix() const { return "/bt/add"; }

void torrent_post_handler::handle_http(
	http::request<http::string_body> req,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	if (req.method() != http::verb::post) {
		send_http(socket, std::move(done), http_error(req, http::status::method_not_allowed));
		return;
	}

	permissions_interface const* perms = parse_http_auth(req, m_auth);
	if (!perms) {
		auto res = http_error(req, http::status::unauthorized);
		res.set(http::field::www_authenticate, "Basic realm=\"BitTorrent\"");
		send_http(socket, std::move(done), std::move(res));
		return;
	}

	if (!perms->allow_add()) {
		send_http(socket, std::move(done), http_error(req, http::status::forbidden));
		return;
	}

	lt::error_code ec;
	auto params = parse_torrent_post(req, ec, {100, 100LL * 1024 * 1024, 110LL * 1024 * 1024});

	if (ec) {
		auto const status =
			(ec
			 == lt::error_code(
				 boost::system::errc::file_too_large, boost::system::generic_category()
			 ))
			? http::status::payload_too_large
			: http::status::bad_request;
		send_http(socket, std::move(done), http_error(req, status));
		return;
	}

	std::string const save_path =
		m_settings ? m_settings->get_str("save_path", "./downloads") : "./downloads";

	bool const start_paused = m_settings && m_settings->get_int("start_paused", 0);
	for (auto& p : params) {
		p.save_path = save_path;
		if (start_paused)
			p.flags = (p.flags & ~lt::torrent_flags::auto_managed) | lt::torrent_flags::paused;
		else
			p.flags = (p.flags & ~lt::torrent_flags::paused) | lt::torrent_flags::auto_managed;
		m_ses.async_add_torrent(std::move(p));
	}

	http::response<http::empty_body> res{http::status::no_content, req.version()};
	res.keep_alive(req.keep_alive());
	send_http(socket, std::move(done), std::move(res));
}

} // namespace ltweb
