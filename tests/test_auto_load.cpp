/*

Copyright (c) 2026, Arvid Norberg
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

#define BOOST_TEST_MODULE auto_load
#include <boost/test/included/unit_test.hpp>

#include "auto_load.hpp"

#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

lt::session make_session()
{
	lt::settings_pack sp;
	sp.set_bool(lt::settings_pack::enable_dht, false);
	sp.set_bool(lt::settings_pack::enable_lsd, false);
	sp.set_bool(lt::settings_pack::enable_upnp, false);
	sp.set_bool(lt::settings_pack::enable_natpmp, false);
	sp.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
	return lt::session(sp);
}

// Minimal v1 single-file torrent (1-byte file, one piece, all-zero SHA-1).
// Info-dict keys are in strict lexicographic order as required by bencode.
void write_minimal_torrent(std::filesystem::path const& path)
{
	static constexpr char const data[] =
		"d4:infod6:lengthi1e"
		"4:name8:test.txt"
		"12:piece lengthi16384e"
		"6:pieces20:"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"ee";
	std::ofstream f(path, std::ios::binary);
	f.write(data, sizeof(data) - 1);
}

// Wait up to `timeout` for an add_torrent_alert. Returns true if one arrives.
bool wait_for_add(lt::session& ses, std::chrono::seconds const timeout)
{
	auto const deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline)
	{
		auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now());
		if (remaining.count() <= 0) break;
		ses.wait_for_alert(remaining);
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
			if (a->type() == lt::add_torrent_alert::alert_type)
				return true;
	}
	return false;
}

// RAII temporary directory, cleaned up on destruction.
struct temp_dir
{
	std::filesystem::path const path;
	explicit temp_dir(char const* name)
		: path(std::filesystem::temp_directory_path() / name)
	{
		std::filesystem::create_directories(path);
	}
	~temp_dir() { std::filesystem::remove_all(path); }
};

} // anonymous namespace

BOOST_AUTO_TEST_CASE(basic_pickup)
{
	// A .torrent file placed in the watched directory is picked up and added
	// to the session. set_auto_load_dir() cancels the 1-second startup timer
	// and fires an immediate scan via expires_after(0).
	lt::session ses = make_session();
	temp_dir dir("ltweb_auto_load_1");
	write_minimal_torrent(dir.path / "test.torrent");

	ltweb::auto_load al(ses);
	al.set_remove_files(false);
	al.set_auto_load_dir(dir.path.string());

	BOOST_TEST(wait_for_add(ses, std::chrono::seconds(5)));
}

BOOST_AUTO_TEST_CASE(remove_files_true)
{
	// remove_files=true: the .torrent file is deleted after it is loaded
	lt::session ses = make_session();
	temp_dir dir("ltweb_auto_load_2");
	auto const torrent_path = dir.path / "test.torrent";
	write_minimal_torrent(torrent_path);

	ltweb::auto_load al(ses);
	al.set_remove_files(true);
	al.set_auto_load_dir(dir.path.string());

	BOOST_TEST(wait_for_add(ses, std::chrono::seconds(5)));
	BOOST_TEST(!std::filesystem::exists(torrent_path));
}

BOOST_AUTO_TEST_CASE(remove_files_false)
{
	// remove_files=false: the .torrent file is kept after it is loaded
	lt::session ses = make_session();
	temp_dir dir("ltweb_auto_load_3");
	auto const torrent_path = dir.path / "test.torrent";
	write_minimal_torrent(torrent_path);

	ltweb::auto_load al(ses);
	al.set_remove_files(false);
	al.set_auto_load_dir(dir.path.string());

	BOOST_TEST(wait_for_add(ses, std::chrono::seconds(5)));
	BOOST_TEST(std::filesystem::exists(torrent_path));
}

BOOST_AUTO_TEST_CASE(non_torrent_ignored)
{
	// Non-.torrent files are ignored: place a .txt file alongside a .torrent,
	// wait for the one expected alert, then assert only one torrent was added.
	lt::session ses = make_session();
	temp_dir dir("ltweb_auto_load_4");
	write_minimal_torrent(dir.path / "real.torrent");
	{ std::ofstream f(dir.path / "ignored.txt"); f << "not a torrent\n"; }

	ltweb::auto_load al(ses);
	al.set_remove_files(false);
	al.set_auto_load_dir(dir.path.string());

	BOOST_TEST(wait_for_add(ses, std::chrono::seconds(5)));
	BOOST_TEST(ses.get_torrents().size() == 1);
}
