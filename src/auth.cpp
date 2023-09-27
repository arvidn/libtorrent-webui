/*

Copyright (c) 2012, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "auth.hpp"

#include "libtorrent/hasher.hpp"
#include "save_settings.hpp"
#include "base64.hpp"
#include "hex.hpp"

extern "C" {
#include "local_mongoose.h"
}

#include <vector>
#include <string.h> // for strcmp() 
#include <stdio.h>
#include <sys/time.h>

namespace libtorrent
{

const static read_only_permissions read_perms;
const static full_permissions full_perms;

auth::auth()
{
	// default groups are:
	// 0: full permissions
	// 1: read-only permissions
	// this is configurable via the set_group() function
	m_groups.push_back(&full_perms);
	m_groups.push_back(&read_perms);
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);
	srand(tv.tv_usec);
}

/**
	Queries the object for users it currently recognizes.
	\return a vector of usernames of all users currently in the account list
*/
std::vector<std::string> auth::users() const
{
	std::unique_lock<std::mutex> l(m_mutex);

	std::vector<std::string> users;
	for (auto const& a : m_accounts)
	{
		users.push_back(a.first);
	}
	return users;
}

/**
	Adds an account to the account list. To determine the access permissions for
	this user, user set_group() with the same group number to associate a permissions_interface
	object.
	\param user The user name of the new account. If the user already exists, its
	            password and group number will be updated to the ones passed in.
	\param pwd The password for this account.
	\param group The group number for this account. Group numbers may not be negative,
	             they should also be relatively small.
*/
void auth::add_account(std::string const& user, std::string const& pwd, int group)
{
	std::unique_lock<std::mutex> l(m_mutex);

	auto const i = m_accounts.find(user);
	if (i == m_accounts.end())
	{
		account_t acct;
		for (char& c : acct.salt) c = rand();
		acct.group = group;
		acct.hash = acct.password_hash(pwd);
		m_accounts.insert(std::make_pair(user, acct));
	}
	else
	{
		i->second.hash = i->second.password_hash(pwd);
		i->second.group = group;
	}
}

/**
	Remove an account from the account list.
	\param user the username of the account to remove. If there is not
	            account with this name, nothing is done.
*/
void auth::remove_account(std::string const& user)
{
	std::unique_lock<std::mutex> l(m_mutex);

	auto const i = m_accounts.find(user);
	if (i == m_accounts.end()) return;
	m_accounts.erase(i);
}

/**
	Set permissions for a group.
	\param g The group number to update permissions for.
	\param perms A pointer to an object implementing the permissions_interface.
	             It is the callers responsibility to make sure this object
	             stays alive for as long as it is in use. It may be a good idea
	             to allocate permission objects statically.
*/
void auth::set_group(int g, permissions_interface const* perms)
{
	if (g < 0) return;

	std::unique_lock<std::mutex> l(m_mutex);

	if (g >= int(m_groups.size()))
		m_groups.resize(g+1, nullptr);
	m_groups[g] = perms;
}

/**
	Finds appropriate permissions for the given user. If authentication fails, or the user
	doesn't exist, nullptr is returned, which is interpreted as authentication failure.
	\param username The username to authenticate
	\param password The password for this user
	\return The permissions_interface appropriate for this user's access permissions, or nullptr
	        if authentication failed.
*/
permissions_interface const* auth::find_user(std::string username, std::string password) const
{
	std::unique_lock<std::mutex> l(m_mutex);

	std::map<std::string, account_t>::const_iterator i = m_accounts.find(username);
	if (i == m_accounts.end()) return nullptr;

	sha1_hash ph = i->second.password_hash(password);
	if (ph != i->second.hash) return nullptr;

	if (i->second.group < 0 || i->second.group >= int(m_groups.size()))
		return nullptr;

	return m_groups[i->second.group];
}

sha1_hash auth::account_t::password_hash(std::string const& pwd) const
{
	hasher h;
	h.update(salt);
	if (pwd.size()) h.update(pwd);
	sha1_hash ret = h.final();

	return ret;
}

/**
  Save the accounts in the account list to disk.
  \param filename The file to save the accounts to. If the file exists, it will be overwritten.
  \param ec The error code descibing the error, if the function fails
*/
void auth::save_accounts(std::string const& filename, error_code& ec) const
{
	FILE* f = fopen(filename.c_str(), "w+");
	if (f == nullptr)
	{
		ec = error_code(errno, system_category());
		return;
	}
	ec.clear();

	std::unique_lock<std::mutex> l(m_mutex);

	for (auto i = m_accounts.begin(), end(m_accounts.end()); i != end; ++i)
	{
		account_t const& a = i->second;
		fprintf(f, "%s\t%s\t%s\t%d\n", i->first.c_str()
			, to_hex(a.hash).c_str()
			, to_hex(a.salt).c_str()
			, i->second.group);
	}

	fclose(f);
}

/**
	Load accounts from disk.
	\param filename The filename of the file to load accounts from.
	\param ec The error code describing the error if the function fail.
*/
void auth::load_accounts(std::string const& filename, error_code& ec)
{
	FILE* f = fopen(filename.c_str(), "r");
	if (f == nullptr)
	{
		ec = error_code(errno, system_category());
		return;
	}
	ec.clear();

	std::unique_lock<std::mutex> l(m_mutex);

	m_accounts.clear();

	std::array<char, 512> username;
	std::array<char, 41> pwdhash;
	std::array<char, 21> salt;
	int group;

	while (fscanf(f, "%511s\t%40s\t%20s\t%d\n"
		, username.data()
		, pwdhash.data()
		, salt.data()
		, &group) == 4)
	{
		account_t a;
		if (!from_hex({pwdhash.data(), 40}, a.hash.data())) continue;
		if (!from_hex({salt.data(), 20}, a.salt.data())) continue;
		if (group < 0) continue;
		a.group = group;
		m_accounts[std::string(username.data())] = a;
	}

	fclose(f);
}

/**
	Parses the basic authorization header from a mongoose connection and queries the
	provided auth_interface for a permissions object.
	\param conn the mongoos connection object
	\param auth the auth_interface object
	\return the permission object appropriate for the user, or nullptr in case authentication failed.
*/
permissions_interface const* parse_http_auth(mg_connection* conn, auth_interface const* auth)
{
	std::string user;
	std::string pwd;
	char const* authorization = mg_get_header(conn, "authorization");
	if (authorization)
	{
		authorization = strcasestr(authorization, "basic ");
		if (authorization)
		{
			authorization += 6;
			// skip whiltespace
			while (*authorization == ' '
				|| *authorization == '\t')
				++authorization;

			std::string cred = base64decode(authorization);
			user = cred.substr(0, cred.find_first_of(':'));
			pwd = cred.substr(user.size()+1);
		}
	}

	permissions_interface const* perms = auth->find_user(user, pwd);
	if (perms == nullptr) return NULL;
	return perms;
}

}

