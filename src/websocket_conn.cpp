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

#include <memory> // enable_shared_from_this

#include "websocket_conn.hpp"
#include "auth.hpp"

// TODO: drop this dependency
#include "libtorrent_webui.hpp"

using namespace std::literals::chrono_literals;

namespace ltweb {

websocket_conn::websocket_conn(libtorrent_webui* parent, permissions_interface const* perms
	, ws::stream<beast::ssl_stream<beast::tcp_stream>>&& conn
	, std::function<void(bool)>&& done)
	: m_conn(std::move(conn))
	, m_done(std::move(done))
	, m_parent(parent)
	, m_perms(perms)
{}

websocket_conn::~websocket_conn()
{
	assert(m_stopping);
}

bool websocket_conn::send_packet(char const* buffer, int len)
{
	if (m_stopping) return false;

	m_send_buffer.emplace_back(buffer, buffer + len);
	if (m_send_buffer.size() == 1) do_send();
	return true;
}

void websocket_conn::start_accept(http::request<http::string_body> const& request)
{
	m_conn.async_accept(request, beast::bind_front_handler(
		&websocket_conn::on_accept, shared_from_this()));
}

void websocket_conn::on_accept(beast::error_code const& ec)
{
	if (ec) return close();
	if (m_stopping) return;
	do_read();
}

void websocket_conn::do_send()
{
	m_conn.async_write(boost::asio::buffer(m_send_buffer.front())
		, beast::bind_front_handler(&websocket_conn::on_send, shared_from_this()));
}

void websocket_conn::on_send(beast::error_code const& ec, std::size_t)
{
	if (ec)
	{
		m_send_buffer.clear();
		return close();
	}
	assert(m_send_buffer.size() >= 1);
	m_send_buffer.pop_front();

	if (!m_send_buffer.empty()) do_send();
	else if (m_stopping) do_close();
}

void websocket_conn::do_read()
{
	m_read_buffer.clear();
	m_conn.async_read(m_read_buffer, beast::bind_front_handler(
		&websocket_conn::on_read, shared_from_this()));
}

void websocket_conn::on_read(beast::error_code const& ec, std::size_t num_bytes)
{
	if (ec) return close();
	if (m_stopping) return;

	beast::get_lowest_layer(m_conn).expires_after(60s);

	if (!m_parent->on_websocket_read(this
		, { static_cast<char const*>(m_read_buffer.cdata().data())
		, int(m_read_buffer.cdata().size())}))
	{
		return close();
	}

	do_read();
}

void websocket_conn::close()
{
	if (m_stopping) return;
	m_stopping = true;

	if (!m_send_buffer.empty()) return;
	do_close();
}

void websocket_conn::do_close()
{
	// we can't actually call done here, because we've stolen the
	// socket from the http connection. We have to shut it down
	// ourselves

	beast::get_lowest_layer(m_conn).expires_after(30s);

	m_conn.async_close(ws::close_reason{}
		, beast::bind_front_handler(&websocket_conn::on_close, shared_from_this()));
}

void websocket_conn::on_close(beast::error_code const& ec)
{
	m_conn.next_layer().async_shutdown(
		beast::bind_front_handler(&websocket_conn::on_shutdown, shared_from_this()));
}

void websocket_conn::on_shutdown(beast::error_code const& ec)
{
	beast::get_lowest_layer(m_conn).close();
	m_done = nullptr;
}

} // namespace ltweb
