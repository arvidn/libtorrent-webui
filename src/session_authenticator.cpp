/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "session_authenticator.hpp"
#include "hex.hpp"

#include <openssl/rand.h>

#include <array>
#include <stdexcept>

namespace ltweb {

namespace {

// 32 bytes -> 256 bits of entropy, encoded as 64 hex chars.
constexpr std::size_t session_id_bytes = 32;

std::string generate_session_id()
{
	std::array<char, session_id_bytes> buf;
	if (RAND_bytes(reinterpret_cast<unsigned char*>(buf.data()), int(buf.size())) != 1)
		throw std::runtime_error("RAND_bytes failed");
	return to_hex(lt::span<char const>(buf.data(), buf.size()));
}

} // anonymous namespace

session_authenticator::session_authenticator(std::chrono::seconds idle_timeout)
	: m_idle_timeout(idle_timeout)
{
}

permissions_interface const* session_authenticator::
	authenticate(std::string_view session_cookie, std::string_view /*authorization*/) const
{
	if (session_cookie.empty()) return nullptr;

	std::lock_guard<std::mutex> l(m_mutex);

	// Heterogeneous lookup: the map's hasher and key_equal are both
	// transparent so we can find by string_view without allocating.
	auto i = m_sessions.find(session_cookie);
	if (i == m_sessions.end()) return nullptr;

	auto const now = std::chrono::steady_clock::now();
	if (now >= i->second.expires) {
		m_sessions.erase(i);
		return nullptr;
	}

	// Slide the expiry on a successful hit (idle timeout).
	i->second.expires = now + m_idle_timeout;
	return i->second.perms;
}

std::string session_authenticator::create(permissions_interface const* perms)
{
	std::string id = generate_session_id();
	auto const expires = std::chrono::steady_clock::now() + m_idle_timeout;

	std::lock_guard<std::mutex> l(m_mutex);
	m_sessions[id] = entry{perms, expires};
	return id;
}

void session_authenticator::destroy(std::string_view session_id)
{
	std::lock_guard<std::mutex> l(m_mutex);
	// unordered_map::erase did not get a heterogeneous overload until
	// C++23; the find -> erase(iterator) two-step is allocation-free
	// under C++20 thanks to the transparent hasher.
	auto i = m_sessions.find(session_id);
	if (i != m_sessions.end()) m_sessions.erase(i);
}

void session_authenticator::prune_expired()
{
	auto const now = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> l(m_mutex);
	for (auto i = m_sessions.begin(); i != m_sessions.end();) {
		if (now >= i->second.expires)
			i = m_sessions.erase(i);
		else
			++i;
	}
}

std::size_t session_authenticator::size() const
{
	std::lock_guard<std::mutex> l(m_mutex);
	return m_sessions.size();
}

} // namespace ltweb
