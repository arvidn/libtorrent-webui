// Copyright (c) 2026, Arvid Norberg
// All rights reserved.
//
// You may use, distribute and modify this code under the terms of the BSD license,
// see LICENSE file.

#define BOOST_TEST_MODULE file_response
#include <boost/test/included/unit_test.hpp>

#include "file_response.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace ltweb::aux;
using namespace std::string_view_literals;

// ---------------------------------------------------------------------------
// strip_query
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_SUITE(strip_query_suite)

BOOST_AUTO_TEST_CASE(no_query_returns_input)
{
	BOOST_TEST(strip_query("/foo") == "/foo"sv);
	BOOST_TEST(strip_query("") == ""sv);
}

BOOST_AUTO_TEST_CASE(strips_query_string)
{
	BOOST_TEST(strip_query("/foo?bar=baz") == "/foo"sv);
	BOOST_TEST(strip_query("/a/b/c?x=1&y=2") == "/a/b/c"sv);
}

BOOST_AUTO_TEST_CASE(empty_query_still_strips) { BOOST_TEST(strip_query("/foo?") == "/foo"sv); }

BOOST_AUTO_TEST_CASE(leading_question_mark) { BOOST_TEST(strip_query("?bar") == ""sv); }

BOOST_AUTO_TEST_CASE(only_first_question_mark_splits)
{
	// We do not interpret nested ? characters; everything from the
	// first ? on is the "query", per RFC 3986.
	BOOST_TEST(strip_query("/a?b?c") == "/a"sv);
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// etag_for_mtime
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_SUITE(etag_for_mtime_suite)

BOOST_AUTO_TEST_CASE(stable_for_same_mtime)
{
	auto const t = fs::file_time_type::clock::now();
	BOOST_TEST(etag_for_mtime(t) == etag_for_mtime(t));
}

BOOST_AUTO_TEST_CASE(differs_for_different_mtimes)
{
	auto const t1 = fs::file_time_type::clock::now();
	auto const t2 = t1 + std::chrono::hours(1);
	BOOST_TEST(etag_for_mtime(t1) != etag_for_mtime(t2));
}

BOOST_AUTO_TEST_CASE(quoted_form)
{
	auto const t = fs::file_time_type::clock::now();
	std::string const tag = etag_for_mtime(t);
	BOOST_TEST(tag.size() >= 2u);
	BOOST_TEST(tag.front() == '"');
	BOOST_TEST(tag.back() == '"');
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// etag_matches
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_SUITE(etag_matches_suite)

BOOST_AUTO_TEST_CASE(exact_match) { BOOST_TEST(etag_matches("\"abc\"", "\"abc\"")); }

BOOST_AUTO_TEST_CASE(empty_header_does_not_match) { BOOST_TEST(!etag_matches("", "\"abc\"")); }

BOOST_AUTO_TEST_CASE(empty_etag_does_not_match)
{
	// Defensive: an empty etag must not silently match an empty header.
	BOOST_TEST(!etag_matches("", ""));
	BOOST_TEST(!etag_matches("\"abc\"", ""));
}

BOOST_AUTO_TEST_CASE(matches_in_comma_list)
{
	BOOST_TEST(etag_matches("\"abc\", \"def\"", "\"abc\""));
	BOOST_TEST(etag_matches("\"abc\", \"def\"", "\"def\""));
}

BOOST_AUTO_TEST_CASE(no_match_for_unrelated_tag)
{
	BOOST_TEST(!etag_matches("\"xyz\"", "\"abc\""));
}

BOOST_AUTO_TEST_CASE(wildcard_is_not_implemented)
{
	// "*" is the conventional If-None-Match wildcard, but the
	// substring-based matcher does not recognise it. This test pins
	// that behaviour so a future change is deliberate.
	BOOST_TEST(!etag_matches("*", "\"abc\""));
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// resolve_gzip_alternate
//
// Touches the real filesystem. Each test creates files inside a unique
// temp directory and removes them on teardown.
// ---------------------------------------------------------------------------

namespace {

struct tmp_root {
	fs::path path;
	tmp_root()
		: path(
			  fs::weakly_canonical(fs::temp_directory_path())
			  / ("ltweb_file_response_" + std::to_string(::getpid()) + "_"
				 + std::to_string(reinterpret_cast<std::uintptr_t>(this)))
		  )
	{
		fs::create_directories(path);
	}
	~tmp_root()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
	tmp_root(tmp_root const&) = delete;
	tmp_root& operator=(tmp_root const&) = delete;

	fs::path touch(std::string_view name) const
	{
		fs::path const p = path / std::string(name);
		std::ofstream(p) << "x";
		return p;
	}
};

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(resolve_gzip_alternate_suite)

BOOST_AUTO_TEST_CASE(prefers_gz_when_accept_encoding_allows)
{
	tmp_root td;
	auto const plain = td.touch("both.txt");
	auto const gz = td.touch("both.txt.gz");

	auto const resolved = resolve_gzip_alternate(plain, "gzip");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->gzip_encoded == true);
	BOOST_TEST(resolved->path == gz);
}

BOOST_AUTO_TEST_CASE(falls_back_to_plain_when_no_gzip_in_accept_encoding)
{
	tmp_root td;
	auto const plain = td.touch("both.txt");
	td.touch("both.txt.gz");

	auto const resolved = resolve_gzip_alternate(plain, "");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->gzip_encoded == false);
	BOOST_TEST(resolved->path == plain);
}

BOOST_AUTO_TEST_CASE(falls_back_to_plain_when_gz_sibling_missing)
{
	// Client says it accepts gzip, but no .gz file exists. We should
	// still serve the plain file uncompressed.
	tmp_root td;
	auto const plain = td.touch("only_plain.txt");

	auto const resolved = resolve_gzip_alternate(plain, "gzip, deflate");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->gzip_encoded == false);
	BOOST_TEST(resolved->path == plain);
}

BOOST_AUTO_TEST_CASE(serves_gz_even_when_plain_missing)
{
	// The .gz lookup happens before the plain stat, so a request for
	// a path that only exists in compressed form still works as long
	// as the client accepts gzip.
	tmp_root td;
	fs::path const requested = td.path / "only_gz.txt";
	auto const gz = td.touch("only_gz.txt.gz");

	auto const resolved = resolve_gzip_alternate(requested, "gzip");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->gzip_encoded == true);
	BOOST_TEST(resolved->path == gz);
}

BOOST_AUTO_TEST_CASE(returns_nullopt_when_neither_exists)
{
	tmp_root td;
	fs::path const requested = td.path / "missing.txt";

	BOOST_TEST(!resolve_gzip_alternate(requested, "gzip").has_value());
	BOOST_TEST(!resolve_gzip_alternate(requested, "").has_value());
}

BOOST_AUTO_TEST_CASE(only_gz_exists_but_client_does_not_accept)
{
	// Without "gzip" in Accept-Encoding we are not allowed to serve
	// the .gz file, so this resolves to nullopt.
	tmp_root td;
	fs::path const requested = td.path / "only_gz.txt";
	td.touch("only_gz.txt.gz");

	BOOST_TEST(!resolve_gzip_alternate(requested, "").has_value());
	BOOST_TEST(!resolve_gzip_alternate(requested, "deflate").has_value());
}

BOOST_AUTO_TEST_CASE(content_type_extension_uses_requested_not_resolved_when_gzip)
{
	// Regression guard: when gzip negotiation routes us to a .gz
	// sibling, the resolved file's extension is ".gz" -- but the
	// response's Content-Type must describe the UNDERLYING media
	// type (e.g. text/css for a styles.css request). Using
	// resolved->path.extension() would yield Content-Type:
	// application/gzip, which is wrong. Per RFC 9110 sec. 8.4:
	// Content-Type names the underlying media type; Content-Encoding
	// (set separately) names the gzip wrapper.
	tmp_root td;
	auto const plain = td.touch("styles.css");
	td.touch("styles.css.gz");

	auto const resolved = resolve_gzip_alternate(plain, "gzip");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_REQUIRE(resolved->gzip_encoded);

	// The trap: resolved->path.extension() is ".gz" -- the WRONG
	// thing to feed to mime_type() lookup.
	BOOST_TEST(resolved->path.extension().string() == ".gz");

	// The fix: content_type_extension is the requested path's
	// extension, regardless of which sibling we resolved.
	BOOST_TEST(resolved->content_type_extension == ".css");
}

BOOST_AUTO_TEST_CASE(content_type_extension_for_identity_response)
{
	// Non-gzip branch must also set content_type_extension so
	// callers can use a single field unconditionally.
	tmp_root td;
	auto const plain = td.touch("page.html");

	auto const resolved = resolve_gzip_alternate(plain, "");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(!resolved->gzip_encoded);
	BOOST_TEST(resolved->content_type_extension == ".html");
}

BOOST_AUTO_TEST_CASE(mtime_is_for_the_chosen_file)
{
	// When we serve the .gz, the returned mtime must be the .gz file's
	// mtime, not the plain file's. This matters for ETag consistency.
	tmp_root td;
	auto const plain = td.touch("both.txt");
	auto const gz = td.touch("both.txt.gz");

	// Stamp the .gz a known interval in the past so its mtime
	// differs from the plain file's by more than filesystem
	// resolution would normally allow.
	auto const plain_mtime = fs::last_write_time(plain);
	auto const expected_gz_mtime = plain_mtime - std::chrono::hours(2);
	std::error_code ec;
	fs::last_write_time(gz, expected_gz_mtime, ec);
	BOOST_REQUIRE(!ec);

	auto const resolved = resolve_gzip_alternate(plain, "gzip");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->gzip_encoded == true);
	BOOST_TEST((resolved->mtime == expected_gz_mtime));
	BOOST_TEST((resolved->mtime != plain_mtime));
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// apply_static_response_headers
//
// Verifies the headers set on every success/revalidation response from
// serve_local_file. The most important guarantee here is that Vary:
// Accept-Encoding is always present, because without it a shared HTTP
// cache could serve a gzip-encoded response to a client that does not
// support gzip.
// ---------------------------------------------------------------------------

namespace {

http::response<http::empty_body> make_blank_response()
{
	// HTTP/1.1 (version 11): keep-alive is the default, so toggling
	// res.keep_alive(false) is what writes Connection: close.
	return http::response<http::empty_body>{http::status::ok, 11};
}

std::string header_value(http::response<http::empty_body> const& res, http::field f)
{
	auto const it = res.find(f);
	if (it == res.end()) return {};
	return std::string(it->value());
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(apply_static_response_headers_suite)

BOOST_AUTO_TEST_CASE(sets_vary_accept_encoding)
{
	// Vary must be set so caches keep gzip-encoded and identity
	// variants in separate cache entries -- regardless of whether
	// THIS particular response is the gzip-encoded one.
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/html", "\"abc\"", 1234u, true, false);
	BOOST_TEST(header_value(res, http::field::vary) == "Accept-Encoding");
}

BOOST_AUTO_TEST_CASE(sets_vary_on_gzip_response_too)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/html", "\"abc\"", 1234u, true, true);
	BOOST_TEST(header_value(res, http::field::vary) == "Accept-Encoding");
}

BOOST_AUTO_TEST_CASE(sets_content_type)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "image/png", "\"abc\"", 0u, true, false);
	BOOST_TEST(header_value(res, http::field::content_type) == "image/png");
}

BOOST_AUTO_TEST_CASE(sets_etag)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"deadbeef\"", 0u, true, false);
	BOOST_TEST(header_value(res, http::field::etag) == "\"deadbeef\"");
}

BOOST_AUTO_TEST_CASE(sets_content_length)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", 4096u, true, false);
	BOOST_TEST(header_value(res, http::field::content_length) == "4096");
}

BOOST_AUTO_TEST_CASE(keep_alive_true_on_http11)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", 0u, true, false);
	BOOST_TEST(res.keep_alive() == true);
}

BOOST_AUTO_TEST_CASE(keep_alive_false_on_http11)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", 0u, false, false);
	BOOST_TEST(res.keep_alive() == false);
}

BOOST_AUTO_TEST_CASE(sets_content_encoding_when_gzip_encoded_true)
{
	// HEAD and GET both forward gzip_encoded=true when serving the
	// .gz variant. This guarantees both branches advertise the
	// encoding identically: the original bug was that HEAD did not.
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", 0u, true, true);
	BOOST_TEST(header_value(res, http::field::content_encoding) == "gzip");
}

BOOST_AUTO_TEST_CASE(does_not_set_content_encoding_when_gzip_encoded_false)
{
	// 304 Not Modified passes false here: per RFC 9110 sec. 15.4.5
	// Content-Encoding is not in the required header set for a 304,
	// and the matching ETag already pins the variant the cache has.
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", 0u, true, false);
	BOOST_TEST(header_value(res, http::field::content_encoding) == "");
}

BOOST_AUTO_TEST_CASE(works_with_other_body_types)
{
	// The helper is templated; sanity-check that it compiles and
	// applies headers on a different body type too. serve_local_file
	// uses both http::empty_body (304/HEAD) and http::file_body (GET).
	http::response<http::string_body> res{http::status::ok, 11};
	apply_static_response_headers(res, "text/plain", "\"y\"", 7u, true, true);
	auto const vary = res.find(http::field::vary);
	BOOST_REQUIRE(vary != res.end());
	BOOST_TEST(vary->value() == "Accept-Encoding");
	auto const ce = res.find(http::field::content_encoding);
	BOOST_REQUIRE(ce != res.end());
	BOOST_TEST(ce->value() == "gzip");
}

BOOST_AUTO_TEST_SUITE_END()
