/*

Copyright (c) 2013, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PERM_INTERFACE_HPP
#define LTWEB_PERM_INTERFACE_HPP

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base64.hpp"

namespace ltweb {

/**
	This is the interface an object need to implement in order
	to specify custom access permissions.
*/
struct permissions_interface {
	/// If returning true, the user may start torrents
	virtual bool allow_start() const = 0;

	/// If returning true, the user may stop torrents
	virtual bool allow_stop() const = 0;

	/// If returning true, the user may re-check torrents
	virtual bool allow_recheck() const = 0;

	/// If returning true, the user may modifiy the priority of files
	virtual bool allow_set_file_prio() const = 0;

	/// If returning true, the user may list torrents
	// TODO: separate this out to listing torrents and listing files
	virtual bool allow_list() const = 0;

	/// If returning true, the user may add torrents
	virtual bool allow_add() const = 0;

	/// If returning true, the user may remove torrents
	virtual bool allow_remove() const = 0;

	/// If returning true, the user may remove torrents
	/// and delete their data from disk
	virtual bool allow_remove_data() const = 0;

	/// If returning true, the user may queue-up or -down torrents torrents
	virtual bool allow_queue_change() const = 0;

	/// If returning true, the user may GET the specified setting
	/// name is the constant used in lt::settings_pack or -1 for
	/// settings that don't fit a libtorrent setting
	virtual bool allow_get_settings(int name) const = 0;

	/// If returning true, the user may SET the specified setting
	/// name is the constant used in lt::settings_pack or -1 for
	/// settings that don't fit a libtorrent setting
	virtual bool allow_set_settings(int name) const = 0;

	/// If returning true, the user may download the content of torrents
	virtual bool allow_get_data() const = 0;

	// TODO: separate permissions to alter torrent state. separate different categories of settings?
	/// If returning true, the user is allowed to query session status,
	/// like global upload and download rates
	virtual bool allow_session_status() const = 0;
};

/**
  The interface to an authentication module. This plugs into web
  interfaces to authenticate requests and determine their access
  permissions. Implementations decide which schemes are honored
  (session cookies, HTTP Basic, etc) by inspecting the two header
  values passed in.

  The header strings are passed in already extracted, so this
  interface stays transport-agnostic and avoids depending on Beast.

	session_cookie: value of the session cookie, or empty if absent.
	authorization:  value of the Authorization header, or empty.

	Returns the permissions for the authenticated request, or nullptr
	if authentication failed.
*/
struct auth_interface {
	virtual permissions_interface const*
	authenticate(std::string_view session_cookie, std::string_view authorization) const = 0;
};

// A directory of user accounts. Implementations look up the username,
// verify the password (typically against a salted hash), and return
// the user's group number on success. std::nullopt means the account
// does not exist or the password is wrong - callers should not
// distinguish between the two, to avoid leaking whether a username
// exists.
struct user_account {
	virtual std::optional<int>
	verify(std::string_view username, std::string_view password) const = 0;
};

// Helper for implementations of auth_interface that support HTTP
// Basic Auth. Parses "Basic <base64(user:pass)>" from an
// Authorization header value and returns the decoded (user, pwd)
// pair. Returns ("", "") when the header is empty or uses any
// other scheme - implementations typically treat that as anonymous
// and dispatch on whether such access is allowed.
inline std::pair<std::string, std::string> parse_basic_auth(std::string_view authorization)
{
	// Strip leading whitespace; HTTP parsers usually do this but we
	// cannot assume it for an arbitrary string_view.
	while (!authorization.empty() && (authorization.front() == ' ' || authorization.front() == '\t')
	)
		authorization.remove_prefix(1);

	// Case-insensitive check for "basic" followed by whitespace.
	if (authorization.size() < 6) return {};
	static constexpr char tag[] = "basic";
	for (int i = 0; i < 5; ++i)
		if (std::tolower(static_cast<unsigned char>(authorization[i])) != tag[i]) return {};
	if (authorization[5] != ' ' && authorization[5] != '\t') return {};

	authorization.remove_prefix(5);
	while (!authorization.empty() && (authorization.front() == ' ' || authorization.front() == '\t')
	)
		authorization.remove_prefix(1);

	std::string cred = base64decode(std::string(authorization));
	auto const colon = cred.find(':');
	if (colon == std::string::npos) return {std::move(cred), {}};
	return {cred.substr(0, colon), cred.substr(colon + 1)};
}

/// an implementation of permissions_interface that reject all access
struct no_permissions : permissions_interface {
	no_permissions() {}
	bool allow_start() const override { return false; }
	bool allow_stop() const override { return false; }
	bool allow_recheck() const override { return false; }
	bool allow_set_file_prio() const override { return false; }
	bool allow_list() const override { return false; }
	bool allow_add() const override { return false; }
	bool allow_remove() const override { return false; }
	bool allow_remove_data() const override { return false; }
	bool allow_queue_change() const override { return false; }
	bool allow_get_settings(int) const override { return false; }
	bool allow_set_settings(int) const override { return false; }
	bool allow_get_data() const override { return false; }
	bool allow_session_status() const override { return false; }
};

/// an implementation of permissions_interface that only allow inspecting
/// the stat of the torrent client, not altering it in any way. No modification
/// of settings, no adding/removing/rechecking of torrents.
struct read_only_permissions : permissions_interface {
	read_only_permissions() {}
	bool allow_start() const override { return false; }
	bool allow_stop() const override { return false; }
	bool allow_recheck() const override { return false; }
	bool allow_set_file_prio() const override { return false; }
	bool allow_list() const override { return true; }
	bool allow_add() const override { return false; }
	bool allow_remove() const override { return false; }
	bool allow_remove_data() const override { return false; }
	bool allow_queue_change() const override { return false; }
	bool allow_get_settings(int) const override { return true; }
	bool allow_set_settings(int) const override { return false; }
	bool allow_get_data() const override { return true; }
	bool allow_session_status() const override { return true; }
};

/// an implementation of permissions_interface that permit all access.
struct full_permissions : permissions_interface {
	full_permissions() {}
	bool allow_start() const override { return true; }
	bool allow_stop() const override { return true; }
	bool allow_recheck() const override { return true; }
	bool allow_set_file_prio() const override { return true; }
	bool allow_list() const override { return true; }
	bool allow_add() const override { return true; }
	bool allow_remove() const override { return true; }
	bool allow_remove_data() const override { return true; }
	bool allow_queue_change() const override { return true; }
	bool allow_get_settings(int) const override { return true; }
	bool allow_set_settings(int) const override { return true; }
	bool allow_get_data() const override { return true; }
	bool allow_session_status() const override { return true; }
};

} // namespace ltweb

#endif // LTWEB_PERM_INTERFACE_HPP
