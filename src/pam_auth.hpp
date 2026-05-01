/*

Copyright (c) 2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PAM_AUTH_HPP
#define LTWEB_PAM_AUTH_HPP

#include "auth_interface.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ltweb {
// user_account implementation backed by Linux PAM. verify() invokes
// the configured PAM service to authenticate (username, password).
// On a successful PAM authentication the user's group number is
// looked up in the per-user override map; if no override is set,
// the default group is returned.
struct pam_auth : user_account {
	explicit pam_auth(std::string service_name, int default_group = 0);
	~pam_auth();

	// Group returned for any successfully authenticated user that
	// does not have a per-user override.
	void set_default_group(int g) { default_group = g; }

	// Per-user group override; takes precedence over the default.
	void set_user_group(std::string username, int g) { users[std::move(username)] = g; }

	std::optional<int> verify(std::string_view username, std::string_view password) const override;

private:
	int default_group;
	std::string service_name;
	// Users not in this map who successfully authenticate get
	// default_group.
	std::map<std::string, int> users;
};
} // namespace ltweb

#endif
