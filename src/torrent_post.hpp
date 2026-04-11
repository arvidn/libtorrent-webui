/*

Copyright (c) 2012-2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_TORRENT_POST_HPP
#define LTWEB_TORRENT_POST_HPP

#include "webui.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/add_torrent_params.hpp"

lt::add_torrent_params parse_torrent_post(http::request<http::string_body> const& req, lt::error_code& ec);

#endif

