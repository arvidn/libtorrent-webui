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

#ifndef TORRENT_WEBUI_HPP
#define TORRENT_WEBUI_HPP

#include <vector>
#include <string>
#include <thread>
#include <map>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include "libtorrent/fwd.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

struct http_handler
{
	// this must return the same string every time. This determines which
	// request paths are routed to this handler
	virtual std::string path_prefix() = 0;

	// called for each HTTP request. Once the response has been sent, the done()
	// function must be called, to read another request from the client.
	virtual void handle_http(http::request<http::string_body> request
		, beast::ssl_stream<beast::tcp_stream>& socket
		, std::function<void(bool)> done) = 0;
};

struct listener;

namespace libtorrent
{
	template<typename Body, typename Fields>
	void send_http(beast::ssl_stream<beast::tcp_stream>& socket
		, std::function<void(bool)> done
		, http::response<Body, Fields>&& msg)
	{
		msg.prepare_payload();
		auto sp = std::make_shared<http::message<false, Body, Fields>>(std::move(msg));
		auto& req = *sp;

		http::async_write(socket, req
			, [response = std::move(sp), d = std::move(done)]
			(beast::error_code const& ec, std::size_t)
			{
				d(ec || response->need_eof());
			});
	}

	inline http::response<http::empty_body> http_error(http::request<http::string_body> const& req
		, http::status status)
	{
		http::response<http::empty_body> res{status, req.version()};
		res.keep_alive(req.keep_alive());
		return res;
	};

	struct webui_base
	{
		webui_base(int port, char const* cert_path = nullptr, int num_threads = 4);
		webui_base(webui_base const&) = delete;
		webui_base(webui_base&&) = delete;
		webui_base& operator=(webui_base const&) = delete;
		webui_base& operator=(webui_base&&) = delete;
		~webui_base();

		void add_handler(http_handler* h);
		void remove_handler(http_handler* h);

	private:

		std::vector<std::pair<std::string, http_handler*>> m_handlers;
		std::vector<std::thread> m_threads;
		std::shared_ptr<listener> m_listener;

		boost::asio::io_context m_ioc;
		ssl::context m_ctx;
	};

}

#endif

