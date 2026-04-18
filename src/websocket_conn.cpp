/*

Copyright (c) 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <memory> // enable_shared_from_this

#include <boost/asio/dispatch.hpp>

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
	TORRENT_ASSERT(m_stopping);
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
	TORRENT_ASSERT(m_send_buffer.size() >= 1);
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
	boost::asio::dispatch(beast::get_lowest_layer(m_conn).get_executor()
		, [self = shared_from_this()]() {
			if (self->m_stopping) return;
			self->m_stopping = true;
			if (!self->m_send_buffer.empty()) return;
			self->do_close();
		});
}

void websocket_conn::do_close()
{
	// we can't actually call done here, because we've stolen the
	// socket from the http connection. We have to shut it down
	// ourselves

	beast::get_lowest_layer(m_conn).expires_after(10s);

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
