/*

Copyright (c) 2012-2013, 2017-2018, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_AUTH_HPP
#define LTWEB_AUTH_HPP

#include "auth_interface.hpp"

#include "libtorrent/peer_id.hpp" // lt::sha1_hash
#include "libtorrent/error_code.hpp"

#include <mutex> // for mutex
#include <string>
#include <map>
#include <vector>
#include <array>

#include <boost/beast/http.hpp>

namespace http = boost::beast::http;

namespace ltweb
{
	permissions_interface const* parse_http_auth(http::request<http::string_body> const& request
		, auth_interface const* auth);

	/**
		Implements simple access control. Users can be added, removed, saved and load from
		a plain text file. Access permissions are controlled via groups. There are two default
		groups. 0=full access, 1=read-only access. Groups can be re-definied using the set_group()
		function. Any user belonging to an undefined group will be denied any access.

		This object is thread safe. It is synchronized internally.
	*/
	struct auth : auth_interface
	{
		auth();
		void add_account(std::string const& user, std::string const& pwd, int group);
		void remove_account(std::string const& user);
		std::vector<std::string> users() const;

		void save_accounts(std::string const& filename, lt::error_code& ec) const;
		void load_accounts(std::string const& filename, lt::error_code& ec);

		void set_group(int g, permissions_interface const* perms);

		permissions_interface const* find_user(std::string username, std::string password) const;

	private:

		struct account_t
		{
			lt::sha1_hash password_hash(std::string const& pwd) const;

			lt::sha1_hash hash;
			std::array<char, 10> salt;
			int group;
		};

		mutable std::mutex m_mutex;
		std::map<std::string, account_t> m_accounts;

		// the permissions for each group
		std::vector<permissions_interface const*> m_groups;
	};
}

#endif

