/*

Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_WEBSOCKET_CONN_HPP
#define TORRENT_WEBSOCKET_CONN_HPP

#include <memory> // enable_shared_from_this
#include <functional>
#include <deque>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

namespace libtorrent {

namespace ws = boost::beast::websocket;
namespace http = boost::beast::http;
namespace beast = boost::beast;

struct permissions_interface;

// TODO: make this an interface
struct libtorrent_webui;

struct websocket_conn : std::enable_shared_from_this<websocket_conn>
{
	websocket_conn(libtorrent_webui* parent, permissions_interface const* perms
		, ws::stream<beast::ssl_stream<beast::tcp_stream>>&& conn
		, std::function<void(bool)>&& done);
	~websocket_conn();

	// TODO: this should take a span
	bool send_packet(char const* buffer, int len);
	void start_accept(http::request<http::string_body> const& request);

	permissions_interface const* perms() const { return m_perms; }

private:

	void on_accept(beast::error_code const& ec);
	void do_send();;
	void on_send(beast::error_code const& ec, std::size_t);
	void do_read();
	void on_read(beast::error_code const& ec, std::size_t num_bytes);
	void close();
	void do_close();
	void on_close(beast::error_code const& ec);
	void on_shutdown(beast::error_code const& ec);

	ws::stream<beast::ssl_stream<beast::tcp_stream>> m_conn;
	std::deque<std::vector<char>> m_send_buffer;
	std::function<void(bool)> m_done;
	beast::flat_buffer m_read_buffer;
	libtorrent_webui* m_parent;
	permissions_interface const* m_perms;
	bool m_stopping = false;
};

}
#endif
