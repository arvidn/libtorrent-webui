/*

Copyright (c) 2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PAM_AUTH_HPP
#define LTWEB_PAM_AUTH_HPP

#include "auth_interface.hpp"
#include <string>
#include <map>

namespace ltweb
{
	struct pam_auth : auth_interface
	{
		pam_auth(std::string service_name);
		~pam_auth();

		// these are the permissions the user receives
		// if successfully authenticated
		void set_permissions(permissions_interface* perms) { m_perms = perms; }

		void set_user_permissions(std::string username, permissions_interface* p)
		{ m_users[username] = p; }

		permissions_interface const* find_user(std::string username, std::string password) const;

	private:

		permissions_interface* m_perms;
		std::string m_service_name;
		// if some users have different permissions than the default
		// users, they have an entry in this map. Users not in this
		// map that successfully authenticate will still get the
		// default permissions in m_perms (which defaults to full permissions)
		std::map<std::string, permissions_interface*> m_users;
	};
}

#endif

