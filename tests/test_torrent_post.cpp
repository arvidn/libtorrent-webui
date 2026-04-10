/*

Copyright (c) 2026, Arvid Norberg
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
#include "test.hpp"

#include <boost/beast/http.hpp>
#include <boost/system/errc.hpp>
#include <string>
#include <string_view>

namespace http = boost::beast::http;

int main_ret = 0;

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

} // anonymous namespace

int main()
{
	// --- Rejection before load_torrent_buffer ---

	// Empty body
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=X");
		req.body() = "";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// Body too large (>10 MiB): distinct file_too_large error, not invalid_argument
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=X");
		req.body() = std::string(10 * 1024 * 1024 + 1, 'x');
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(ec.value() == (int)boost::system::errc::file_too_large);
		TEST_CHECK(ec.category() == boost::system::generic_category());
	}

	// Content-Type is not multipart/form-data
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "application/octet-stream");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// Media type contains multipart/form-data as a substring but is not exactly that
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "text/multipart/form-data; boundary=X");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// Parameter name has "boundary" as a suffix: notboundary=X
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; notboundary=X");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// Parameter name has "boundary" as a prefix: xboundary=X
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; xboundary=X");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// multipart/form-data with no boundary parameter
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data");
		req.body() = "--X\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--X--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// Empty boundary (unquoted): boundary= with nothing after it
	// body.find("") always succeeds, so without a guard this would hang.
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=");
		req.body() = "--\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n----\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// Malformed quoted boundary: parse_quoted_string rejects it, propagated as invalid
	{
		http::request<http::string_body> req{http::verb::post, "/", 11};
		req.set(http::field::content_type, "multipart/form-data; boundary=\"unterminated");
		req.body() = "--unterminated\r\nContent-Type: application/x-bittorrent\r\n\r\ndata\r\n--unterminated--\r\n";
		req.prepare_payload();
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// No part with an accepted content type
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=X", "X",
			"text/plain", "hello");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(is_parse_error(ec));
	}

	// --- Boundary parsing: parsing should succeed, load_torrent_buffer reached.
	//     We pass garbage torrent bytes and verify the error is NOT a parse error.

	// Standard unquoted boundary
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=simple-boundary",
			"simple-boundary",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	// Quoted boundary: boundary="----WebKitFormBoundaryABC"
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=\"----WebKitFormBoundaryABC\"",
			"----WebKitFormBoundaryABC",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	// Boundary followed by additional parameters: boundary=foo; charset=utf-8
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=foo; charset=utf-8",
			"foo",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	// Case-insensitive content type: Multipart/Form-Data
	{
		auto req = make_multipart(
			"Multipart/Form-Data; boundary=X",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	// Case-insensitive boundary parameter name: BOUNDARY=
	{
		auto req = make_multipart(
			"multipart/form-data; BOUNDARY=X",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	// OWS (space) between boundary= and value: boundary= foo
	{
		auto req = make_multipart(
			"multipart/form-data; boundary= X",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	// OWS (tab) between boundary= and value: boundary=\tX
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=\tX",
			"X",
			"application/x-bittorrent", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	// application/octet-stream is also accepted as part content type
	{
		auto req = make_multipart(
			"multipart/form-data; boundary=X",
			"X",
			"application/octet-stream", "not-a-torrent");
		lt::error_code ec;
		parse_torrent_post(req, ec);
		TEST_CHECK(!is_parse_error(ec));
	}

	return main_ret;
}
