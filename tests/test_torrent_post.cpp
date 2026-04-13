/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE torrent_post
#include <boost/test/included/unit_test.hpp>

#include "torrent_post.hpp"

#include <boost/beast/http.hpp>
#include <boost/system/errc.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace http = boost::beast::http;

namespace {

// Returns true when the error is the sentinel value that parse_torrent_post
// uses to signal its own rejection (invalid_argument/generic_category).
// A different error means parsing succeeded and load_torrent_buffer was reached.
bool is_parse_error(lt::error_code const& ec)
{
	return ec.value() == (int)boost::system::errc::invalid_argument
		&& ec.category() == boost::system::generic_category();
}

// Build a well-formed multipart/form-data request with a single part.
//
// content_type_header  -- verbatim Content-Type header value (including boundary=...)
// boundary             -- the bare boundary string to embed in the body (no surrounding --)
// part_ct              -- Content-Type of the single part
// part_body            -- raw bytes of the part body
http::request<http::string_body> make_multipart(
	std::string_view content_type_header,
	std::string_view boundary,
	std::string_view part_ct,
	std::string_view part_body)
{
	std::string body;
	body += "--";
	body += boundary;
	body += "\r\n";
	body += "Content-Disposition: form-data; name=\"file\"\r\n";
	body += "Content-Type: ";
	body += part_ct;
	body += "\r\n\r\n";
	body += part_body;
	body += "\r\n--";
	body += boundary;
	body += "--\r\n";

	http::request<http::string_body> req{http::verb::post, "/", 11};
	req.set(http::field::content_type, content_type_header);
	req.body() = std::move(body);
	req.prepare_payload();
	return req;
}

// Build a well-formed multipart/form-data request with multiple parts.
//
// boundary  -- the bare boundary string
// parts     -- sequence of (Content-Type, body) pairs
http::request<http::string_body> make_multipart_n(
	std::string_view boundary,
	std::vector<std::pair<std::string_view, std::string_view>> const& parts)
{
	std::string body;
	body += "--";
	body += boundary;
	for (auto const& [ct, part_body] : parts)
	{
		body += "\r\n";
		body += "Content-Disposition: form-data; name=\"file\"\r\n";
		body += "Content-Type: ";
		body += ct;
		body += "\r\n\r\n";
		body += part_body;
		body += "\r\n--";
		body += boundary;
	}
	body += "--\r\n";

	std::string const ct_header = "multipart/form-data; boundary=" + std::string(boundary);
	http::request<http::string_body> req{http::verb::post, "/", 11};
	req.set(http::field::content_type, ct_header);
	req.body() = std::move(body);
	req.prepare_payload();
	return req;
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(rejection_cases)
{
	// Empty body
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=X");
		req.body() = "";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// Body too large (>10 MiB): distinct file_too_large error, not invalid_argument
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=X");
		req.body() = std::string(10 * 1024 * 1024 + 1, 'x');
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(ec.value() == (int)boost::system::errc::file_too_large);
		BOOST_CHECK(ec.category() == boost::system::generic_category());
	}

	// Content-Type is not multipart/form-data
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "application/octet-stream");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// Media type contains multipart/form-data as a substring but is not exactly that
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "text/multipart/form-data; boundary=X");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// Parameter name has "boundary" as a suffix: notboundary=X
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; notboundary=X");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// Parameter name has "boundary" as a prefix: xboundary=X
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; xboundary=X");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// multipart/form-data with no boundary parameter
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// Empty boundary (unquoted): boundary= with nothing after it
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=");
		req.body() = "--\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n----\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// Malformed quoted boundary: parse_quoted_string rejects it, propagated as invalid
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=\"unterminated");
		req.body() = "--unterminated\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--unterminated--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}

	// No part with an accepted content type
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=X", "X",
			"text/plain", "hello");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(is_parse_error(ec));
	}
}

BOOST_AUTO_TEST_CASE(boundary_parsing)
{
	// Parsing should succeed, load_torrent_buffer reached.
	// We pass garbage torrent bytes and verify the error is NOT a parse error.

	// Standard unquoted boundary
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=simple-boundary",
			"simple-boundary",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// Quoted boundary: boundary="----WebKitFormBoundaryABC"
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=\"----WebKitFormBoundaryABC\"",
			"----WebKitFormBoundaryABC",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// Boundary followed by additional parameters: boundary=foo; charset=utf-8
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=foo; charset=utf-8",
			"foo",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// Case-insensitive content type: Multipart/Form-Data
	{
		auto req = make_multipart(
			"Multipart/Form-Data; boundary=X",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// Case-insensitive boundary parameter name: BOUNDARY=
	{
		auto req = make_multipart(
			"multipart/form-data; BOUNDARY=X",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// OWS (space) between boundary= and value: boundary= X
	{
		auto req = make_multipart(
			"multipart/form-data; boundary= X",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// OWS (tab) between boundary= and value: boundary=\tX
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=\tX",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// application/octet-stream is also accepted as part content type
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=X",
			"X",
			"application/octet-stream", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}
}

BOOST_AUTO_TEST_CASE(multi_part)
{
	// Two matching parts, both with garbage bytes.
	// Both parts should be attempted; both fail to decode. Because a matching
	// part was found and load_torrent_buffer returned an error, the error
	// propagated back is the torrent decode error -- not a parse error.
	{
		auto req = make_multipart_n("X", {
			{"application/x-bittorrent", "not-a-torrent"},
			{"application/x-bittorrent", "also-not-a-torrent"}
		});
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// Non-matching part (text/plain) before a matching part.
	// The text/plain part must be silently skipped; the bittorrent part
	// is found, attempted, and its decode error propagated.
	{
		auto req = make_multipart_n("X", {
			{"text/plain", "this-is-plain-text"},
			{"application/x-bittorrent", "not-a-torrent"}
		});
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}

	// Matching part before a non-matching part.
	// Non-matching part at the end must be silently skipped; decode error
	// from the bittorrent part is propagated.
	{
		auto req = make_multipart_n("X", {
			{"application/x-bittorrent", "not-a-torrent"},
			{"text/plain", "this-is-plain-text"}
		});
		lt::error_code ec;
		parse_torrent_post(req, ec);
		BOOST_TEST(!is_parse_error(ec));
	}
}

BOOST_AUTO_TEST_CASE(limits)
{
	// max_torrent_count=0: the limit check fires immediately for the first
	// matching part (result.size()=0 >= max_torrent_count=0), so the loop
	// breaks before any decode attempt. last_part_ec is never set, so the
	// function falls through to return invalid().
	{
		torrent_post_limits lim;
		lim.max_torrent_count = 0;
		auto req = make_multipart_n("X", {
			{"application/x-bittorrent", "not-a-torrent"}
		});
		lt::error_code ec;
		parse_torrent_post(req, ec, lim);
		BOOST_TEST(is_parse_error(ec));
	}

	// max_payload_bytes smaller than the first part's body size.
	// The limit check fires before any decode attempt so last_part_ec is
	// never set; the function falls through to return invalid().
	{
		torrent_post_limits lim;
		lim.max_payload_bytes = 5;
		auto req = make_multipart_n("X", {
			{"application/x-bittorrent", "10-byte-body"}  // 12 bytes > 5
		});
		lt::error_code ec;
		parse_torrent_post(req, ec, lim);
		BOOST_TEST(is_parse_error(ec));
	}

	// max_payload_bytes allows the first part to be decoded but cuts off
	// the second. The first part's decode error must be propagated (not a
	// parse error), even though the second part was never attempted.
	{
		torrent_post_limits lim;
		lim.max_payload_bytes = 10;
		// first part is 3 bytes (0+3 <= 10), second is 12 bytes (3+12 > 10)
		auto req = make_multipart_n("X", {
			{"application/x-bittorrent", "abc"},
			{"application/x-bittorrent", "12-byte-body!"}
		});
		lt::error_code ec;
		parse_torrent_post(req, ec, lim);
		BOOST_TEST(!is_parse_error(ec));
	}
}
