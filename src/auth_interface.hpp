/*

Copyright (c) 2013, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PERM_INTERFACE_HPP
#define LTWEB_PERM_INTERFACE_HPP

#include <optional>
#include <string_view>

#include "perms.hpp"

namespace ltweb {

/**
	The interface to an authentication module. This plugs into web
	interfaces to authenticate requests and determine their access
	permissions.

	The header string is passed in already extracted, so this
	interface stays transport-agnostic and avoids depending on Beast.

	session_cookie: value of the session cookie

	Returns the permissions for the authenticated request, or nullptr
	if authentication failed.
*/
struct auth_interface {
	virtual permissions_interface const* authenticate(std::string_view session_cookie) const = 0;
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

} // namespace ltweb

#endif // LTWEB_PERM_INTERFACE_HPP
