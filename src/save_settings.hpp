/*

Copyright (c) 2012-2013, 2017-2018, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_SAVE_SETTINGS_HPP
#define LTWEB_SAVE_SETTINGS_HPP

#include "libtorrent/session.hpp"
#include <mutex>
#include "libtorrent/error_code.hpp"

#include <string>
#include <map>

namespace ltweb
{
	struct save_settings_interface
	{
		virtual void save(lt::error_code& ec) const = 0;
		virtual void set_int(char const* key, int val) = 0;
		virtual void set_str(char const* key, std::string val) = 0;
		virtual int get_int(char const* key, int def = 0) const = 0;
		virtual std::string get_str(char const* key, char const* def = "") const = 0;
	};

	struct save_settings : save_settings_interface
	{
		save_settings(lt::session& s, lt::settings_pack const& sp
			, std::string const& settings_file);
		~save_settings();

		void save(lt::error_code& ec) const;

		void set_int(char const* key, int val);
		void set_str(char const* key, std::string val);

		int get_int(char const* key, int def) const;
		std::string get_str(char const* key, char const* def = "") const;

	private:

		lt::session& m_ses;
		std::string m_settings_file;
		mutable std::mutex m_mutex;
		std::map<std::string, int> m_ints;
		std::map<std::string, std::string> m_strings;
	};

	void load_settings(lt::session_params& params, std::string const& filename
		, lt::error_code& ec);

	std::vector<char> load_file(char const* filename);
	int save_file(std::string const& filename, std::vector<char> const& v);

}

#endif

