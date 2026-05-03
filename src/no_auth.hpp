/*

Copyright (c) 2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_NO_AUTH_HPP
#define LTWEB_NO_AUTH_HPP

#include <optional>
#include <string_view>

#include "auth_interface.hpp"

namespace ltweb {

// user_account implementation that accepts any username and password
// and returns group 0. Useful as a development-mode default when no
// real user directory is configured. Do not use in production.
struct no_auth : user_account {
	std::optional<int> verify(std::string_view username, std::string_view password) const override;
};

} // namespace ltweb

#endif // LTWEB_NO_AUTH_HPP
