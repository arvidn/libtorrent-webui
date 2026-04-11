/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SERVE_FILES_HPP
#define TORRENT_SERVE_FILES_HPP

#include <string_view>
#include <filesystem>
#include <cinttypes>

#include <boost/algorithm/string/predicate.hpp>

#include "libtorrent/hasher.hpp"

#include "serve_files.hpp"
#include "mime_type.hpp"

using boost::algorithm::contains;
namespace fs = std::filesystem;

namespace ltweb
{

serve_files::serve_files(std::string_view prefix
	, std::string_view root_directory)
	: m_root(root_directory)
	, m_prefix(prefix)
{
	if (m_prefix.empty() || m_prefix.back() != '/')
		m_prefix += '/';

	if (m_prefix.front() != '/')
		m_prefix.insert(0, "/");

	if (m_root.empty())
		m_root = ".";

	if (m_root.back() != '/')
		m_root += '/';
}

// this must return the same string every time. This determines which
// request paths are routed to this handler
std::string serve_files::path_prefix() const { return m_prefix; }

// called for each HTTP request. Once the response has been sent, the done()
// function must be called, to read another request from the client.
void serve_files::handle_http(http::request<http::string_body> request
	, beast::ssl_stream<beast::tcp_stream>& socket
	, std::function<void(bool)> done)
{
	if (request.method() != http::verb::get
		&& request.method() != http::verb::head)
	{
		return send_http(socket, std::move(done), http_error(request, http::status::method_not_allowed));
	}

	if (request.target().size() < m_prefix.size())
		return send_http(socket, std::move(done), http_error(request, http::status::internal_server_error));

	if (request.target().substr(m_prefix.size()).find("..") != beast::string_view::npos)
		return send_http(socket, std::move(done), http_error(request, http::status::bad_request));

	assert(m_root.back() == '/' && request.target().front() == '/');

	fs::path const relative_path(std::string(request.target().substr(m_prefix.size())));
	fs::path full_path = m_root / relative_path;

	if (relative_path.empty())
		full_path += "index.html";

	std::string const extension = full_path.extension().string();

	bool use_gzip = false;
	fs::file_time_type mtime{};

	auto const it = request.find(http::field::accept_encoding);
	if (it != request.end() && contains(it->value(), "gzip"))
	{
		// if the client supports gzip content encoding, and there is a file
		// with the same name but with a .gz extension, send it encoded
		std::error_code ec;
		fs::path gz_path = full_path;
		gz_path += ".gz";
		mtime = fs::last_write_time(gz_path, ec);
		if (!ec)
		{
			use_gzip = true;
			full_path = gz_path;
		}
	}

	if (!use_gzip)
	{
		std::error_code ec;
		mtime = fs::last_write_time(full_path, ec);
		if (ec)
			return send_http(socket, std::move(done), http_error(request, http::status::not_found));
	}

	beast::error_code ec;
	http::file_body::value_type body;
	body.open(full_path.c_str(), beast::file_mode::scan, ec);

	if (ec)
		return send_http(socket, std::move(done), http_error(request, http::status::not_found));

	auto const size = body.size();

	auto const mtime_int = mtime.time_since_epoch().count();
	lt::sha1_hash const mtime_h
		= lt::hasher(reinterpret_cast<char const*>(&mtime_int), int(sizeof(mtime_int))).final();

	// TODO: this could be done a bit more efficiently by converting to hex into
	// a stack-allocated fixed-size buffer
	std::stringstream str;
	str << '"' << mtime_h << '"';
	std::string const etag = str.str();

	auto const match_it = request.find(http::field::if_none_match);
	if (match_it != request.end()
		&& contains(match_it->value(), etag))
	{
		http::response<http::empty_body> res{http::status::not_modified, request.version()};
		res.set(http::field::content_type, mime_type(extension));
		res.set(http::field::etag, etag);
		res.content_length(size);
		res.keep_alive(request.keep_alive());
		return send_http(socket, std::move(done), std::move(res));
	}

	if (request.method() == http::verb::head)
	{
		http::response<http::empty_body> res{http::status::ok, request.version()};
		res.set(http::field::content_type, mime_type(extension));
		res.set(http::field::etag, etag);
		res.content_length(size);
		res.keep_alive(request.keep_alive());
		return send_http(socket, std::move(done), std::move(res));
	}

	// Respond to GET request
	http::response<http::file_body> res{
		std::piecewise_construct,
			std::make_tuple(std::move(body)),
			std::make_tuple(http::status::ok, request.version())};
	res.set(http::field::content_type, mime_type(extension));
	res.set(http::field::etag, etag);
	if (use_gzip) res.set(http::field::content_encoding, "gzip");
	res.content_length(size);
	res.keep_alive(request.keep_alive());
	send_http(socket, std::move(done), std::move(res));
}

}

#endif
