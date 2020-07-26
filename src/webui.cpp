/*

Copyright (c) 2012, Arvid Norberg
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

#include <getopt.h> // for getopt_long
#include <stdlib.h> // for daemon()
#include <syslog.h>

#include <memory> // for shared_ptr
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <chrono>
#include <string_view>

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "webui.hpp"

using namespace libtorrent;
using namespace std::literals::chrono_literals;

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <boost/algorithm/string/predicate.hpp>

using boost::algorithm::starts_with;
namespace ssl = boost::asio::ssl;

template <typename Container, typename StringView>
auto find_longest_prefix(Container const& c, StringView const path)
{
	auto best = c.end();
	int length = -1;
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		if (int(it->first.size()) <= length || !starts_with(path, it->first))
			continue;
		
		best = it;
		length = int(it->first.size());
	}
	return best;
}

// Report a failure
void fail(beast::error_code ec, char const* what)
{
	// ssl::error::stream_truncated, also known as an SSL "short read",
	// indicates the peer closed the connection without performing the
	// required closing handshake (for example, Google does this to
	// improve performance). Generally this can be a security issue,
	// but if your communication protocol is self-terminated (as
	// it is with both HTTP and WebSocket) then you may simply
	// ignore the lack of close_notify.
	//
	// https://github.com/boostorg/beast/issues/38
	//
	// https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
	//
	// When a short read would cut off the end of an HTTP message,
	// Beast returns the error beast::http::error::partial_message.
	// Therefore, if we see a short read here, it has occurred
	// after the message has been completed, so it is safe to ignore it.

	if (ec == boost::asio::ssl::error::stream_truncated)
		return;

	std::cerr << what << ": " << ec.message() << "\n";
}

struct http_connection : std::enable_shared_from_this<http_connection>
{
	explicit http_connection(tcp::socket&& socket, ssl::context& ctx
		, std::vector<std::pair<std::string, http_handler*>>& handlers)
		: m_stream(std::move(socket), ctx)
		, m_handlers(handlers)
	{}

	// Start the asynchronous operation
	void run()
	{
		boost::asio::dispatch(m_stream.get_executor(),
			beast::bind_front_handler(&http_connection::on_run, shared_from_this()));
	}

private:

	void on_run()
	{
		// Set the timeout.
		beast::get_lowest_layer(m_stream).expires_after(30s);

		// Perform the SSL handshake
		m_stream.async_handshake(ssl::stream_base::server
			, beast::bind_front_handler(&http_connection::on_handshake, shared_from_this()));
	}

	void on_handshake(beast::error_code ec)
	{
		if (ec) return fail(ec, "handshake");
		do_read();
	}

	void do_read()
	{
		// Make the request empty before reading,
		// otherwise the operation behavior is undefined.
		m_req = {};

		// Set the timeout.
		beast::get_lowest_layer(m_stream).expires_after(30s);

		// Read a request
		http::async_read(m_stream, m_buffer, m_req,
			beast::bind_front_handler(&http_connection::on_read, shared_from_this()));
	}

	void on_read(beast::error_code ec, std::size_t)
	{
		// This means they closed the connection
		if (ec == http::error::end_of_stream)
			return do_close();

		if (ec) return fail(ec, "read");

		auto const req_path = m_req.target();

		auto it = find_longest_prefix(m_handlers, req_path);
		if (it == m_handlers.end())
			return send_http(m_stream, done_function{*this}, http_error(m_req, http::status::not_found));

		it->second->handle_http(std::move(m_req), m_stream, done_function{*this});
	}

	void on_write(bool close, beast::error_code ec, std::size_t)
	{
		if (ec) return fail(ec, "write");

		if (close)
		{
			// This means we should close the connection, usually because
			// the response indicated the "Connection: close" semantic.
			return do_close();
		}
	}

	void do_close()
	{
		// Set the timeout.
		beast::get_lowest_layer(m_stream).expires_after(30s);

		// Perform the SSL shutdown
		m_stream.async_shutdown(
			beast::bind_front_handler(&http_connection::on_shutdown, shared_from_this()));
	}

	void on_shutdown(beast::error_code ec)
	{
		if (ec) return fail(ec, "shutdown");
	}

	struct done_function
	{
		std::shared_ptr<http_connection> m_self;
		explicit done_function(http_connection& self) : m_self(self.shared_from_this()) {}
		void operator()(bool close) const
		{
			if (close) m_self->do_close();
			else m_self->do_read();
		}
	};

	beast::ssl_stream<beast::tcp_stream> m_stream;
	beast::flat_buffer m_buffer;
	http::request<http::string_body> m_req;
	std::vector<std::pair<std::string, http_handler*>>& m_handlers;
};

struct listener : std::enable_shared_from_this<listener>
{
	listener(boost::asio::io_context& ioc
		, ssl::context& ctx
		, tcp::endpoint endpoint
		, std::vector<std::pair<std::string, http_handler*>>& handlers)
		: m_ioc(ioc)
		, m_ctx(ctx)
		, m_acceptor(ioc)
		, m_handlers(handlers)
	{
		m_acceptor.open(endpoint.protocol());
		m_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
		m_acceptor.bind(endpoint);
		m_acceptor.listen();
	}

	void run() { do_accept(); }
	void stop()
	{
		m_stopped = true;
		m_acceptor.close();
	}

private:
	void do_accept()
	{
		if (m_stopped) return;

		// The new connection gets its own strand
		m_acceptor.async_accept(
			boost::asio::make_strand(m_ioc)
			, beast::bind_front_handler(&listener::on_accept, shared_from_this()));
	}

	void on_accept(beast::error_code ec, tcp::socket socket)
	{
		if (ec)
		{
			fail(ec, "accept");
		}
		else
		{
			std::make_shared<http_connection>(std::move(socket), m_ctx, m_handlers)->run();
		}

		// Accept another connection
		do_accept();
	}

	boost::asio::io_context& m_ioc;
	ssl::context& m_ctx;
	tcp::acceptor m_acceptor;
	std::vector<std::pair<std::string, http_handler*>>& m_handlers;
	bool m_stopped = false;
};

webui_base::~webui_base()
{
	m_listener->stop();
//	m_ioc.run_for(2s);
//	m_ioc.stop();

	// TODO: long lived websocket connections aren't closed here

	for (auto& t : m_threads)
		t.join();
}

void webui_base::remove_handler(http_handler* h)
{
	auto const i = std::find_if(m_handlers.begin(), m_handlers.end()
		, [h](std::pair<std::string, http_handler*> v)
		{ return v.second == h; });
	if (i != m_handlers.end()) m_handlers.erase(i);
}

void webui_base::add_handler(http_handler* h)
{
	std::string prefix = h->path_prefix();
	m_handlers.emplace_back(std::move(prefix), h);
}

webui_base::webui_base(int const port, char const* cert_path, int const num_threads)
	: m_ioc(num_threads)
	, m_ctx(ssl::context::tls)
{
	m_ctx.use_certificate_file(cert_path, ssl::context::pem);
	m_ctx.set_password_callback([] (std::size_t max_length, ssl::context::password_purpose p)
		{ return "test"; });
	m_ctx.use_private_key_file("key.pem", ssl::context::pem);

//#error set password callback

	// Create and launch a listening port
	m_listener = std::make_shared<listener>(m_ioc, m_ctx
		, tcp::endpoint{address{}, std::uint16_t(port)}, m_handlers);
	m_listener->run();

	m_threads.reserve(num_threads);
	for(auto i = num_threads; i > 0; --i)
	{
		m_threads.emplace_back([this] {
			m_ioc.run();
		});
	}

}

