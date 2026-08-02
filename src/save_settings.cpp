/*

Copyright (c) 2012-2013, 2015, 2017-2018, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "save_settings.hpp"

#include <functional>
#include <fstream>
#include <boost/tuple/tuple.hpp> // for boost::tie

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include <filesystem>

namespace ltweb {


std::vector<char> load_file(char const* filename)
{
	std::ifstream ifs(filename, std::ios_base::binary);
	ifs.unsetf(std::ios_base::skipws);
	return {std::istream_iterator<char>(ifs), std::istream_iterator<char>()};
}

int save_file(std::string const& filename, std::vector<char> const& v)
{
	std::fstream f(filename, std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);
	f.write(v.data(), v.size());
	return !f.fail();
}

save_settings::save_settings(
	lt::session& s, lt::settings_pack const&, std::string const& settings_file
)
	: m_ses(s)
	, m_settings_file(settings_file)
{
}

save_settings::~save_settings() {}

void save_settings::save(lt::error_code& ec) const
{
	// snapshot the maps before doing any I/O
	std::map<std::string, int> ints;
	std::map<std::string, std::string> strings;
	{
		std::lock_guard<std::mutex> l(m_mutex);
		ints = m_ints;
		strings = m_strings;
	}

	// back-up current settings file as .bak before saving the new one
	std::string backup = m_settings_file + ".bak";
	std::error_code fec;
	bool const has_settings = std::filesystem::exists(m_settings_file, fec);
	bool const has_backup = std::filesystem::exists(backup, fec);

	if (has_settings && has_backup) std::filesystem::remove(backup, fec);

	if (has_settings) std::filesystem::rename(m_settings_file, backup, fec);

	ec.clear();

	lt::entry sett = lt::write_session_params(m_ses.session_state());

	for (auto const& i : ints)
		sett[i.first] = i.second;

	for (auto const& i : strings)
		sett[i.first] = i.second;

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), sett);
	save_file(m_settings_file, buf);
}

namespace {
void load_settings_impl(
	lt::session_params& params,
	std::string const& filename,
	lt::error_code& ec,
	std::map<std::string, int>& custom_ints,
	std::map<std::string, std::string>& custom_strings
)
{
	ec.clear();
	custom_ints.clear();
	custom_strings.clear();
	std::vector<char> buf = load_file(filename.c_str());

	lt::bdecode_node sett = lt::bdecode(buf, ec);
	if (ec) return;

	if (sett.type() != lt::bdecode_node::dict_t) return;

	// read_session_params() understands the on-disk layout libtorrent
	// itself writes (write_session_params()): real settings and DHT
	// state live nested under "settings" / "dht state", not as flat
	// top-level keys.
	{
		lt::session_params sp = lt::read_session_params(sett);
		params.dht_state = std::move(sp.dht_state);
		params.settings = std::move(sp.settings);
	}

	// anything else at the top level is one of the webui's own custom
	// keys (eg. save_path), merged in by save_settings::save().
	int num_items = sett.dict_size();
	for (int i = 0; i < num_items; ++i) {
		lt::bdecode_node item;
		lt::string_view key;
		boost::tie(key, item) = sett.dict_at(i);

		if (item.type() == lt::bdecode_node::int_t) {
			custom_ints[std::string(key)] = int(item.int_value());
		} else if (item.type() == lt::bdecode_node::string_t) {
			custom_strings[std::string(key)] = std::string(item.string_value());
		}
	}
}
} // namespace

void load_settings(
	lt::session_params& params,
	std::string const& filename,
	lt::error_code& ec,
	std::map<std::string, int>& custom_ints,
	std::map<std::string, std::string>& custom_strings
)
{
	ec.clear();
	load_settings_impl(params, filename, ec, custom_ints, custom_strings);
	if (!ec) return;
	std::string const backup = filename + ".bak";
	load_settings_impl(params, backup, ec, custom_ints, custom_strings);
}

void save_settings::set_int(char const* key, int val)
{
	std::lock_guard<std::mutex> l(m_mutex);
	m_ints[key] = val;
}

void save_settings::set_str(char const* key, std::string val)
{
	std::lock_guard<std::mutex> l(m_mutex);
	m_strings[key] = std::move(val);
}

int save_settings::get_int(char const* key, int def) const
{
	std::lock_guard<std::mutex> l(m_mutex);
	auto const i = m_ints.find(key);
	if (i == m_ints.end()) return def;
	return i->second;
}

std::string save_settings::get_str(char const* key, char const* def) const
{
	std::lock_guard<std::mutex> l(m_mutex);
	auto const i = m_strings.find(key);
	if (i == m_strings.end()) return def;
	return i->second;
}

} // namespace ltweb
