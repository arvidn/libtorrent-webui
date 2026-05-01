/*

Copyright (c) 2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_NO_AUTH_HPP
#define LTWEB_NO_AUTH_HPP

#include <string>

#include "auth_interface.hpp"

namespace ltweb {

struct no_auth : auth_interface {
	no_auth() {}
	virtual permissions_interface const*
	find_user(std::string username, std::string password) const;
};

} // namespace ltweb

#endif // LTWEB_NO_AUTH_HPP
