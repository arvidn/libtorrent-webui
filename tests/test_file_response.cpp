// Copyright (c) 2026, Arvid Norberg
// All rights reserved.
//
// You may use, distribute and modify this code under the terms of the BSD license,
// see LICENSE file.

#define BOOST_TEST_MODULE file_response
#include <boost/test/included/unit_test.hpp>
#include <boost/test/tools/context.hpp>

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
// resolve_encoded_alternate
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

BOOST_AUTO_TEST_SUITE(resolve_encoded_alternate_suite)

BOOST_AUTO_TEST_CASE(accept_encoding_selects_expected_encoding)
{
	// Every case below shares the same filesystem layout (plain + .gz +
	// .zst all present), which isolates Accept-Encoding parsing (RFC
	// 9110 sec. 12.5.3 -- tokens, q-values, "*", malformed input) from
	// the file-existence fallback behaviour covered by the tests below
	// this one.
	struct case_t {
		std::string_view header;
		std::string_view expected;
	};
	static constexpr case_t cases[] = {
		{"gzip, zstd", "zstd"}, // zstd preferred when both accepted
		{"gzip", "gzip"}, // gzip alone; zstd unmentioned -> not implied
		{"", ""}, // no Accept-Encoding -> identity
		{"zstd;q=0, gzip", "gzip"}, // explicit refusal of zstd
		{"zstd;q=0, gzip;q=0.000", ""}, // both explicitly refused
		{"*", "zstd"}, // wildcard accepts zstd
		{"*, zstd;q=0", "gzip"}, // explicit refusal overrides wildcard
		{"GZIP;Q=0, ZsTd", "zstd"}, // case-insensitive coding/param names
		{"  zstd ; q = 0  ,  gzip  ", "gzip"}, // whitespace around tokens/params
		{"\tzstd\t;\tq=0\t,\tgzip\t", "gzip"}, // tab is OWS too (RFC 9110 sec. 5.6.3)
		{"gzip;foo=bar;q=0", ""}, // q found when not the first param
		{"gzip;q=0.000", ""}, // trailing zero digits still mean zero
		{"gzip;q=0.001", "gzip"}, // nonzero fraction is accepted
		{"gzip;q=banana", "gzip"}, // malformed q defaults to accepted
		{"gzip;q=", "gzip"}, // empty q value defaults to accepted
		{",,gzip,,", "gzip"}, // empty entries between commas skipped
		{" ; ;;, ;q=1 , ", ""}, // garbage/bare separators ignored
		{"br, identity;q=0.5", ""}, // unlisted coding not implied
		{"*;q=0", ""}, // wildcard refusal rejects everything
	};

	for (auto const& c : cases) {
		BOOST_TEST_CONTEXT("Accept-Encoding: " << c.header)
		{
			tmp_root td;
			auto const plain = td.touch("both.txt");
			auto const gz = td.touch("both.txt.gz");
			auto const zst = td.touch("both.txt.zst");

			auto const resolved = resolve_encoded_alternate(plain, c.header);
			BOOST_REQUIRE(resolved.has_value());
			BOOST_TEST(resolved->content_encoding == c.expected);

			fs::path const expected_path = c.expected == "zstd" ? zst
				: c.expected == "gzip"							? gz
																: plain;
			BOOST_TEST(resolved->path == expected_path);
		}
	}
}

BOOST_AUTO_TEST_CASE(falls_back_to_gzip_when_no_zst_sibling)
{
	// No .zst exists; falls back to .gz when client accepts gzip.
	tmp_root td;
	auto const plain = td.touch("both.txt");
	auto const gz = td.touch("both.txt.gz");

	auto const resolved = resolve_encoded_alternate(plain, "gzip, zstd");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->content_encoding == "gzip");
	BOOST_TEST(resolved->path == gz);
}

BOOST_AUTO_TEST_CASE(falls_back_to_plain_when_compressed_siblings_missing)
{
	// Client says it accepts both encodings, but no siblings exist.
	tmp_root td;
	auto const plain = td.touch("only_plain.txt");

	auto const resolved = resolve_encoded_alternate(plain, "gzip, zstd");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->content_encoding.empty());
	BOOST_TEST(resolved->path == plain);
}

BOOST_AUTO_TEST_CASE(serves_zst_even_when_plain_missing)
{
	// The .zst lookup happens before the plain stat, so a request for a
	// path that only exists compressed still works.
	tmp_root td;
	fs::path const requested = td.path / "only_zst.txt";
	auto const zst = td.touch("only_zst.txt.zst");

	auto const resolved = resolve_encoded_alternate(requested, "zstd");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->content_encoding == "zstd");
	BOOST_TEST(resolved->path == zst);
}

BOOST_AUTO_TEST_CASE(serves_gz_even_when_plain_missing)
{
	tmp_root td;
	fs::path const requested = td.path / "only_gz.txt";
	auto const gz = td.touch("only_gz.txt.gz");

	auto const resolved = resolve_encoded_alternate(requested, "gzip");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->content_encoding == "gzip");
	BOOST_TEST(resolved->path == gz);
}

BOOST_AUTO_TEST_CASE(returns_nullopt_when_nothing_exists)
{
	tmp_root td;
	fs::path const requested = td.path / "missing.txt";

	BOOST_TEST(!resolve_encoded_alternate(requested, "gzip, zstd").has_value());
	BOOST_TEST(!resolve_encoded_alternate(requested, "").has_value());
}

BOOST_AUTO_TEST_CASE(only_compressed_exists_but_client_does_not_accept)
{
	// Without matching tokens in Accept-Encoding we must not serve the
	// compressed sibling; only the plain file is acceptable, and it does
	// not exist, so the result is nullopt.
	tmp_root td;
	fs::path const requested = td.path / "only_gz.txt";
	td.touch("only_gz.txt.gz");

	BOOST_TEST(!resolve_encoded_alternate(requested, "").has_value());
	BOOST_TEST(!resolve_encoded_alternate(requested, "deflate").has_value());
}

BOOST_AUTO_TEST_CASE(content_type_extension_uses_requested_not_resolved)
{
	// Regression guard: when a compressed sibling is selected, the
	// resolved file's extension is ".gz" or ".zst" -- but the response's
	// Content-Type must describe the UNDERLYING media type (e.g. text/css
	// for a styles.css request). Per RFC 9110 sec. 8.4: Content-Type
	// names the underlying media type; Content-Encoding names the wrapper.
	tmp_root td;
	auto const plain = td.touch("styles.css");
	td.touch("styles.css.gz");
	td.touch("styles.css.zst");

	auto const resolved_gz = resolve_encoded_alternate(plain, "gzip");
	BOOST_REQUIRE(resolved_gz.has_value());
	BOOST_TEST(resolved_gz->path.extension().string() == ".gz");
	BOOST_TEST(resolved_gz->content_type_extension == ".css");

	auto const resolved_zst = resolve_encoded_alternate(plain, "zstd");
	BOOST_REQUIRE(resolved_zst.has_value());
	BOOST_TEST(resolved_zst->path.extension().string() == ".zst");
	BOOST_TEST(resolved_zst->content_type_extension == ".css");
}

BOOST_AUTO_TEST_CASE(content_type_extension_for_identity_response)
{
	// Identity branch must also set content_type_extension so
	// callers can use a single field unconditionally.
	tmp_root td;
	auto const plain = td.touch("page.html");

	auto const resolved = resolve_encoded_alternate(plain, "");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->content_encoding.empty());
	BOOST_TEST(resolved->content_type_extension == ".html");
}

BOOST_AUTO_TEST_CASE(mtime_is_for_the_chosen_file)
{
	// When we serve the compressed sibling, the returned mtime must be
	// that sibling's mtime, not the plain file's.
	tmp_root td;
	auto const plain = td.touch("both.txt");
	auto const gz = td.touch("both.txt.gz");

	auto const plain_mtime = fs::last_write_time(plain);
	auto const expected_gz_mtime = plain_mtime - std::chrono::hours(2);
	std::error_code ec;
	fs::last_write_time(gz, expected_gz_mtime, ec);
	BOOST_REQUIRE(!ec);

	auto const resolved = resolve_encoded_alternate(plain, "gzip");
	BOOST_REQUIRE(resolved.has_value());
	BOOST_TEST(resolved->content_encoding == "gzip");
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
// cache could serve a compressed response to a client that does not
// support that encoding.
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
	// Vary must be set so caches keep encoded and identity variants in
	// separate cache entries -- regardless of whether this particular
	// response is the compressed one.
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/html", "\"abc\"", "no-cache", 1234u, true, {});
	BOOST_TEST(header_value(res, http::field::vary) == "Accept-Encoding");
}

BOOST_AUTO_TEST_CASE(sets_vary_on_compressed_response_too)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/html", "\"abc\"", "no-cache", 1234u, true, "gzip");
	BOOST_TEST(header_value(res, http::field::vary) == "Accept-Encoding");
}

BOOST_AUTO_TEST_CASE(sets_content_type)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "image/png", "\"abc\"", "max-age=3600", 0u, true, {});
	BOOST_TEST(header_value(res, http::field::content_type) == "image/png");
}

BOOST_AUTO_TEST_CASE(sets_etag)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"deadbeef\"", "max-age=3600", 0u, true, {});
	BOOST_TEST(header_value(res, http::field::etag) == "\"deadbeef\"");
}

BOOST_AUTO_TEST_CASE(sets_cache_control)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/html", "\"x\"", "no-cache", 0u, true, {});
	BOOST_TEST(header_value(res, http::field::cache_control) == "no-cache");

	auto res2 = make_blank_response();
	apply_static_response_headers(res2, "text/css", "\"x\"", "max-age=3600", 0u, true, {});
	BOOST_TEST(header_value(res2, http::field::cache_control) == "max-age=3600");
}

BOOST_AUTO_TEST_CASE(sets_content_length)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", "max-age=3600", 4096u, true, {});
	BOOST_TEST(header_value(res, http::field::content_length) == "4096");
}

BOOST_AUTO_TEST_CASE(keep_alive_true_on_http11)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", "max-age=3600", 0u, true, {});
	BOOST_TEST(res.keep_alive() == true);
}

BOOST_AUTO_TEST_CASE(keep_alive_false_on_http11)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", "max-age=3600", 0u, false, {});
	BOOST_TEST(res.keep_alive() == false);
}

BOOST_AUTO_TEST_CASE(sets_content_encoding_gzip)
{
	// HEAD and GET both forward the encoding when serving a compressed
	// sibling. This guarantees both branches advertise it identically.
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", "max-age=3600", 0u, true, "gzip");
	BOOST_TEST(header_value(res, http::field::content_encoding) == "gzip");
}

BOOST_AUTO_TEST_CASE(sets_content_encoding_zstd)
{
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", "max-age=3600", 0u, true, "zstd");
	BOOST_TEST(header_value(res, http::field::content_encoding) == "zstd");
}

BOOST_AUTO_TEST_CASE(does_not_set_content_encoding_when_empty)
{
	// 304 Not Modified passes an empty string here: per RFC 9110 sec.
	// 15.4.5 Content-Encoding is not in the required header set for a
	// 304, and the matching ETag already pins the variant the cache has.
	auto res = make_blank_response();
	apply_static_response_headers(res, "text/css", "\"x\"", "no-cache", 0u, true, {});
	BOOST_TEST(header_value(res, http::field::content_encoding) == "");
}

BOOST_AUTO_TEST_CASE(works_with_other_body_types)
{
	// The helper is templated; sanity-check that it compiles and
	// applies headers on a different body type too. serve_local_file
	// uses both http::empty_body (304/HEAD) and http::file_body (GET).
	http::response<http::string_body> res{http::status::ok, 11};
	apply_static_response_headers(res, "text/plain", "\"y\"", "max-age=3600", 7u, true, "gzip");
	auto const vary = res.find(http::field::vary);
	BOOST_REQUIRE(vary != res.end());
	BOOST_TEST(vary->value() == "Accept-Encoding");
	auto const ce = res.find(http::field::content_encoding);
	BOOST_REQUIRE(ce != res.end());
	BOOST_TEST(ce->value() == "gzip");
}

BOOST_AUTO_TEST_SUITE_END()
