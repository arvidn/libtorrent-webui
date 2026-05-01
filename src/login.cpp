/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "login.hpp"
#include "hex.hpp"
#include "login_throttler.hpp"
#include "parse_http_auth.hpp"
#include "session_authenticator.hpp"
#include "url_decode.hpp"

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <boost/optional.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ltweb {

namespace {

// Bound the request body. A login form has at most a few hundred
// bytes; anything bigger is misuse.
constexpr std::size_t max_body_size = 4096;

// 32 bytes -> 256 bits of entropy, encoded as 64 hex chars. Same
// recipe as the session id.
constexpr std::size_t csrf_token_bytes = 32;

// The placeholder is pinned to one exact wrapping: a hidden input
// whose entire opening tag is matched on both sides. This keeps the
// token from landing as raw text, a URL component (Referer leak),
// or inside <script> (XSS-readable), and also rejects subtler
// drift like attribute reordering or extra attributes. Any edit
// that moves the placeholder or changes the surrounding tag fails
// at construction.
constexpr std::string_view csrf_token_marker = "__CSRF_TOKEN__";
constexpr std::string_view csrf_prefix = "<input type=\"hidden\" name=\"csrf\" value=\"";
constexpr std::string_view csrf_suffix = "\" />";

// CSRF cookie lifetime. The form is meant to be filled out in
// seconds; 10 minutes is generous and keeps stale tokens from
// piling up in the user's cookie jar.
constexpr int csrf_cookie_max_age = 600;

// Map a try_login() failure status to a user-facing message. Pre
// percent-encoded so it can be spliced into the redirect URL without
// runtime encoding; the inline script in login.html decodes it via
// URLSearchParams. Spaces become %20; '.', letters, and digits are
// unreserved (RFC 3986) and stay literal. The "sv" literals carry
// the length at compile time so downstream concatenation does not
// strlen them.
std::string_view message_for(http::status s)
{
	using namespace std::literals;
	switch (s) {
		case http::status::unauthorized:
			return "Invalid%20username%20or%20password."sv;
		case http::status::forbidden:
			return "Your%20session%20expired.%20Please%20try%20again."sv;
		case http::status::bad_request:
			return "Please%20fill%20in%20both%20fields."sv;
		default:
			return "Login%20failed."sv;
	}
}

std::string generate_csrf_token()
{
	std::array<char, csrf_token_bytes> buf;
	if (RAND_bytes(reinterpret_cast<unsigned char*>(buf.data()), int(buf.size())) != 1)
		throw std::runtime_error("RAND_bytes failed");
	return to_hex(lt::span<char const>(buf.data(), buf.size()));
}

// Beast Body type that emits three back-to-back fragments without
// pre-concatenating them. The serializer hands a 3-element
// const_buffer array to async_write, which on Linux turns into a
// single writev(2) syscall with a 3-iovec gather - no temporary
// allocation, no memcpy.
//
// The before/after fragments are string_view-only; their backing
// storage is owned by the long-lived login handler. The middle
// fragment is owned by value (the per-request CSRF token).
struct three_part_body {
	struct value_type {
		std::string_view before;
		std::string token;
		std::string_view after;
	};

	static std::uint64_t size(value_type const& body)
	{
		return body.before.size() + body.token.size() + body.after.size();
	}

	class writer {
	public:
		using const_buffers_type = std::array<boost::asio::const_buffer, 3>;

		template <bool isRequest, class Fields>
		writer(http::header<isRequest, Fields> const&, value_type const& body)
			: m_body(body)
		{
		}

		void init(beast::error_code& ec) { ec = {}; }

		boost::optional<std::pair<const_buffers_type, bool>> get(beast::error_code& ec)
		{
			ec = {};
			if (m_sent) return boost::none;
			m_sent = true;
			// "false" = no more after this batch. The serializer will
			// not call get() again. async_write delivers all three
			// buffers in one writev(2).
			return std::make_pair(
				const_buffers_type{
					boost::asio::const_buffer(m_body.before.data(), m_body.before.size()),
					boost::asio::const_buffer(m_body.token.data(), m_body.token.size()),
					boost::asio::const_buffer(m_body.after.data(), m_body.after.size())
				},
				false
			);
		}

	private:
		value_type const& m_body;
		bool m_sent = false;
	};
};

} // anonymous namespace

login_template_parts parse_login_template(std::string html)
{
	auto const first = html.find(csrf_token_marker);
	if (first == std::string::npos) {
		throw std::runtime_error("login template: missing CSRF placeholder __CSRF_TOKEN__");
	}

	auto const second = html.find(csrf_token_marker, first + csrf_token_marker.size());
	if (second != std::string::npos) {
		throw std::runtime_error("login template: __CSRF_TOKEN__ appears more than once");
	}

	bool const lhs_ok = first >= csrf_prefix.size()
		&& std::string_view(html.data() + first - csrf_prefix.size(), csrf_prefix.size())
			== csrf_prefix;
	if (!lhs_ok) {
		throw std::runtime_error(
			"login template: __CSRF_TOKEN__ must be preceded by " + std::string(csrf_prefix)
		);
	}

	auto const after_marker = first + csrf_token_marker.size();
	bool const rhs_ok = html.size() - after_marker >= csrf_suffix.size()
		&& std::string_view(html.data() + after_marker, csrf_suffix.size()) == csrf_suffix;
	if (!rhs_ok) {
		throw std::runtime_error(
			"login template: __CSRF_TOKEN__ must be followed by " + std::string(csrf_suffix)
		);
	}

	login_template_parts out;
	out.before = html.substr(0, first);
	out.after = html.substr(first + csrf_token_marker.size());
	return out;
}

std::map<std::string, std::string> parse_form(std::string_view body)
{
	std::map<std::string, std::string> out;
	while (!body.empty()) {
		auto const amp = body.find('&');
		std::string_view const pair = body.substr(0, amp);
		body.remove_prefix(amp == std::string_view::npos ? body.size() : amp + 1);

		auto const eq = pair.find('=');
		if (eq == std::string_view::npos) continue;

		boost::system::error_code ec;
		std::string key = url_decode(std::string(pair.substr(0, eq)), ec);
		if (ec) continue;
		std::string val = url_decode(std::string(pair.substr(eq + 1)), ec);
		if (ec) continue;
		out.emplace(std::move(key), std::move(val));
	}
	return out;
}

login::login(
	std::string path_prefix_,
	std::string template_html,
	user_account const& accounts,
	session_authenticator& sessions,
	login_throttler& throttler,
	std::string welcome_url,
	std::vector<permissions_interface const*> groups
)
	: m_path_prefix(std::move(path_prefix_))
	, m_accounts(accounts)
	, m_sessions(sessions)
	, m_throttler(throttler)
	, m_welcome_url(std::move(welcome_url))
	, m_groups(std::move(groups))
{
	auto parts = parse_login_template(std::move(template_html));
	m_html_before = std::move(parts.before);
	m_html_after = std::move(parts.after);
}

std::string login::path_prefix() const { return m_path_prefix; }

login::attempt_result login::try_login(http::request<http::string_body> const& req) const
{
	if (req.method() != http::verb::post) return http::status::method_not_allowed;

	if (req.body().size() > max_body_size) return http::status::payload_too_large;

	auto form = parse_form(req.body());

	// Double-submit cookie: the cookie's csrf= value MUST equal the
	// form's csrf field. Either missing fails. SameSite=Strict on the
	// cookie means a cross-site POST will not carry it - the cookie
	// side will be empty and the comparison rejects.
	std::string_view csrf_cookie;
	auto const cookie_it = req.find(http::field::cookie);
	if (cookie_it != req.end()) {
		csrf_cookie = extract_cookie(cookie_it->value(), "csrf");
	}
	auto const csrf_it = form.find("csrf");
	if (csrf_cookie.empty() || csrf_it == form.end() || csrf_it->second.empty()
		|| csrf_cookie.size() != csrf_it->second.size()
		|| CRYPTO_memcmp(csrf_cookie.data(), csrf_it->second.data(), csrf_cookie.size()) != 0) {
		return http::status::forbidden;
	}

	auto const u_it = form.find("username");
	auto const p_it = form.find("password");
	if (u_it == form.end() || p_it == form.end()) return http::status::bad_request;

	auto group = m_accounts.verify(u_it->second, p_it->second);
	if (!group) return http::status::unauthorized;

	int const group_id = *group;
	if (group_id < 0 || std::size_t(group_id) >= m_groups.size() || m_groups[group_id] == nullptr) {
		return http::status::unauthorized;
	}

	return group_id;
}

void login::handle_http(
	http::request<http::string_body> req,
	beast::ssl_stream<beast::tcp_stream>& socket,
	std::function<void(bool)> done
)
{
	if (req.method() == http::verb::get) {
		// Only the exact path serves the form. Sub-paths under the
		// same prefix are not part of the login flow. A query string
		// (eg ?error=...) is allowed because the failure path
		// redirects back here with one.
		std::string_view const target = req.target();
		auto const q = target.find('?');
		std::string_view const path = (q == std::string_view::npos) ? target : target.substr(0, q);
		if (path != m_path_prefix) {
			send_http(socket, std::move(done), http_error(req, http::status::not_found));
			return;
		}

		http::response<three_part_body> res{http::status::ok, req.version()};
		res.set(http::field::content_type, "text/html; charset=utf-8");
		// no-store: prevent the page (and the embedded token) from
		// being cached anywhere along the path.
		res.set(http::field::cache_control, "no-store");
		// Build cookie value first so the token can move into the body.
		std::string token = generate_csrf_token();
		res.set(
			http::field::set_cookie,
			"csrf=" + token + "; HttpOnly; Secure; SameSite=Strict; Path=" + m_path_prefix
				+ "; Max-Age=" + std::to_string(csrf_cookie_max_age)
		);
		res.body() = three_part_body::value_type{m_html_before, std::move(token), m_html_after};
		res.keep_alive(req.keep_alive());
		send_http(socket, std::move(done), std::move(res));
		return;
	}

	// Throttle POST attempts per /24 (v4) or /64 (v6) network. The
	// check happens before try_login so we do not pay PBKDF2 cost on
	// requests we are about to reject anyway.
	auto const remote_ip = beast::get_lowest_layer(socket).socket().remote_endpoint().address();
	auto const block = m_throttler.blocked_for(remote_ip);
	if (block.count() > 0) {
		// Digits are unreserved (RFC 3986) so std::to_string output
		// needs no encoding; the surrounding text is pre-encoded.
		http::response<http::empty_body> res{http::status::see_other, req.version()};
		res.set(
			http::field::location,
			m_path_prefix + "?error=Too%20many%20failed%20attempts.%20Try%20again%20in%20"
				+ std::to_string(block.count()) + "%20seconds."
		);
		res.keep_alive(req.keep_alive());
		send_http(socket, std::move(done), std::move(res));
		return;
	}

	auto const r = try_login(req);
	auto const* group = std::get_if<int>(&r);

	// Record the result so future attempts from this network are
	// throttled. Success clears the network's record.
	m_throttler.record(remote_ip, group != nullptr);

	if (!group) {
		auto const status = std::get<http::status>(r);
		// 405 (non-POST) and 413 (oversized body) are not normal-user
		// typos - keep them as hard errors. 401/403/400 redirect back
		// to the login page with the message in the query string, so
		// the user lands on the styled form with a modal dialog rather
		// than a default browser error page.
		if (status == http::status::method_not_allowed
			|| status == http::status::payload_too_large) {
			send_http(socket, std::move(done), http_error(req, status));
			return;
		}
		http::response<http::empty_body> res{http::status::see_other, req.version()};
		std::string location = m_path_prefix + "?error=";
		location += message_for(status);
		res.set(http::field::location, std::move(location));
		res.keep_alive(req.keep_alive());
		send_http(socket, std::move(done), std::move(res));
		return;
	}

	// Session-fixation defense: if the client already presents a
	// session cookie, drop the old session before minting a new one.
	auto const cookie_it = req.find(http::field::cookie);
	if (cookie_it != req.end()) {
		std::string_view const old_sid = extract_cookie(cookie_it->value(), "session");
		if (!old_sid.empty()) m_sessions.destroy(old_sid);
	}

	std::string const sid = m_sessions.create(m_groups[*group]);

	http::response<http::empty_body> res{http::status::see_other, req.version()};
	res.set(http::field::location, m_welcome_url);
	res.set(
		http::field::set_cookie, "session=" + sid + "; HttpOnly; Secure; SameSite=Strict; Path=/"
	);
	// Clear the spent CSRF cookie. It has served its purpose.
	res.insert(
		http::field::set_cookie,
		"csrf=; HttpOnly; Secure; SameSite=Strict; Path=" + m_path_prefix + "; Max-Age=0"
	);
	res.keep_alive(req.keep_alive());
	send_http(socket, std::move(done), std::move(res));
}

} // namespace ltweb
