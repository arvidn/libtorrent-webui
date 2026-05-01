/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PARSE_HTTP_AUTH_HPP
#define LTWEB_PARSE_HTTP_AUTH_HPP

#include "auth_interface.hpp"

#include <string_view>

#include <boost/beast/http.hpp>

namespace http = boost::beast::http;

namespace ltweb {

// Extract the session cookie value and Authorization header from a
// Beast request and forward to auth.authenticate(). All scheme-
// specific parsing (Basic, sessions, ...) lives inside the
// auth_interface implementation.
permissions_interface const*
parse_http_auth(http::request<http::string_body> const& request, auth_interface const& auth);

// Extract the value of a named cookie from an HTTP Cookie header
// value, ignoring other cookies. name must NOT contain a trailing
// "=" - just the cookie name. Returns an empty view if the cookie
// is not present. Cookies are separated by "; " per RFC 6265.
std::string_view extract_cookie(std::string_view cookie_header, std::string_view name);

} // namespace ltweb

#endif
