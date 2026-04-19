/*

Copyright (c) 2012-2013, 2020, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_WEBUI_HPP
#define LTWEB_WEBUI_HPP

#include <vector>
#include <string>
#include <thread>
#include <map>
#include <functional>

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
	// TODO: this should return a std::string_view
	virtual std::string path_prefix() const = 0;

	// called for each HTTP request. Once the response has been sent, the done()
	// function must be called, to read another request from the client.
	virtual void handle_http(http::request<http::string_body> request
		, beast::ssl_stream<beast::tcp_stream>& socket
		, std::function<void(bool)> done) = 0;

	// called when the webui is destructing. The handler must close any
	// connections it's still keeping alive. The webui destructor will join the
	// threads which wait for all io contexts to finish all their work and
	// exit.
	virtual void shutdown() {}
};

struct listener;

namespace ltweb
{
	template<typename Body, typename Fields>
	void send_http(beast::ssl_stream<beast::tcp_stream>& socket
		, std::function<void(bool)> done
		, http::response<Body, Fields>&& msg)
	{
		msg.prepare_payload();
		auto sp = std::make_shared<http::response<Body, Fields>>(std::move(msg));
		auto& req = *sp;

		http::async_write(socket, req, [response = std::move(sp), d = std::move(done)]
			(beast::error_code const& ec, std::size_t) {
				d(bool(ec));
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

