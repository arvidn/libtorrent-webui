/*

Copyright (c) 2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "auth_interface.hpp"
#include "no_auth.hpp"

namespace ltweb {

permissions_interface const* no_auth::find_user(std::string username, std::string password) const
{
	const static full_permissions full_perms;
	return &full_perms;
}

} // namespace ltweb
