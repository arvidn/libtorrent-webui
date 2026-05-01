/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_SESSION_AUTHENTICATOR_HPP
#define LTWEB_SESSION_AUTHENTICATOR_HPP

#include "auth_interface.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ltweb {

// auth_interface implementation backed by an in-memory session table.
// authenticate() resolves a "session=<id>" cookie value against the
// table. Use a user_account (pam_auth, sqlite_user_account, ...) to
// verify a username/password pair on the login form, then call
// create() on a successful login to mint a session ID suitable for
// the Set-Cookie response header.
//
// Sessions have an idle timeout: each successful authenticate() slides
// the expiry forward. There is no absolute timeout - if you need one,
// store it in the entry and check on lookup.
//
// Thread-safe; protected handlers may share a single instance across
// the io_context thread pool.
struct session_authenticator : auth_interface {
	// idle_timeout: a session expires this long after its last
	// successful authenticate() call.
	explicit session_authenticator(std::chrono::seconds idle_timeout = std::chrono::hours(1));

	// auth_interface: returns the session's permissions, or nullptr if
	// the cookie is unknown or expired.
	permissions_interface const* authenticate(std::string_view session_cookie) const override;

	// Mint a new session bound to perms. Returns the cookie value to
	// place in a Set-Cookie header (typically:
	//   Set-Cookie: session=<value>; HttpOnly; Secure; SameSite=Strict
	// ). The caller is responsible for keeping perms alive for the
	// lifetime of the session - in practice perms is one of the
	// long-lived singleton permissions_interface implementations.
	std::string create(permissions_interface const* perms);

	// Invalidate a session (logout). No-op if unknown.
	void destroy(std::string_view session_id);

	// Drop all expired entries. Lazy cleanup happens on every lookup,
	// so this is only useful if you want to bound memory for sessions
	// that are never looked up again (eg after a server restart of a
	// peer process). Optional; no scheduler is provided.
	void prune_expired();

	// Number of currently stored entries (including any expired ones
	// that have not yet been pruned). Primarily for tests and
	// diagnostics.
	std::size_t size() const;

private:
	struct entry {
		permissions_interface const* perms;
		std::chrono::steady_clock::time_point expires;
	};

	// Transparent hasher: enables heterogeneous lookup so
	// m_sessions.find(string_view) and erase(string_view) avoid the
	// per-call std::string allocation that the default hasher forces.
	// Requires std::equal_to<> as the key_equal (also transparent).
	struct string_hash {
		using is_transparent = void;
		std::size_t operator()(std::string_view s) const noexcept
		{
			return std::hash<std::string_view>{}(s);
		}
		std::size_t operator()(std::string const& s) const noexcept
		{
			return std::hash<std::string_view>{}(s);
		}
		std::size_t operator()(char const* s) const noexcept
		{
			return std::hash<std::string_view>{}(s);
		}
	};

	std::chrono::seconds m_idle_timeout;
	mutable std::mutex m_mutex;
	mutable std::unordered_map<std::string, entry, string_hash, std::equal_to<>> m_sessions;
};

} // namespace ltweb

#endif
