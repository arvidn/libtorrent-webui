/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_WEBSOCKET_CONN_HPP
#define LTWEB_WEBSOCKET_CONN_HPP

#include <memory> // enable_shared_from_this
#include <functional>
#include <deque>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

namespace ltweb {

namespace ws = boost::beast::websocket;
namespace http = boost::beast::http;
namespace beast = boost::beast;

struct permissions_interface;

// TODO: make this an interface
struct libtorrent_webui;

struct websocket_conn : std::enable_shared_from_this<websocket_conn> {
	websocket_conn(
		libtorrent_webui* parent,
		permissions_interface const* perms,
		ws::stream<beast::ssl_stream<beast::tcp_stream>>&& conn,
		std::function<void(bool)>&& done
	);
	~websocket_conn();

	// TODO: this should take a span
	bool send_packet(char const* buffer, int len);
	void start_accept(http::request<http::string_body> const& request);
	void close();

	permissions_interface const* perms() const { return m_perms; }

private:
	void on_accept(beast::error_code const& ec);
	void do_send();
	void on_send(beast::error_code const& ec, std::size_t);
	void do_read();
	void on_read(beast::error_code const& ec, std::size_t num_bytes);
	void do_close();
	void on_close(beast::error_code const& ec);
	void on_shutdown(beast::error_code const& ec);

	using socket_type = ws::stream<beast::ssl_stream<beast::tcp_stream>>;
	socket_type m_conn;
	std::deque<std::vector<char>> m_send_buffer;
	std::function<void(bool)> m_done;
	beast::flat_buffer m_read_buffer;
	libtorrent_webui* m_parent;
	permissions_interface const* m_perms;
	bool m_stopping = false;
};

} // namespace ltweb
#endif
