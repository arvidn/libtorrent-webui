/*

Copyright (c) 2012-2015, 2017-2018, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_UT_WEBUI_HPP
#define LTWEB_UT_WEBUI_HPP

#include "webui.hpp"
#include "libtorrent/torrent_handle.hpp"
#include <cstdint>
#include <functional>
#include <vector>
#include <set>
#include <deque>

namespace ltweb {
struct auto_load;
struct save_settings_interface;
struct torrent_history;
struct permissions_interface;
struct auth_interface;

struct utorrent_webui : http_handler {
	utorrent_webui(
		lt::session& s,
		save_settings_interface& sett,
		torrent_history& hist,
		auth_interface const& auth,
		auto_load* al = nullptr
	);
	~utorrent_webui();

	virtual std::string path_prefix() const override { return "/gui"; }
	virtual void handle_http(
		http::request<http::string_body> request,
		beast::ssl_stream<beast::tcp_stream>& socket,
		std::function<void(bool)> done
	) override;

	void start(std::vector<char>&, char const* args, permissions_interface const* p);
	void stop(std::vector<char>&, char const* args, permissions_interface const* p);
	void force_start(std::vector<char>&, char const* args, permissions_interface const* p);
	void recheck(std::vector<char>&, char const* args, permissions_interface const* p);
	void remove_torrent(std::vector<char>&, char const* args, permissions_interface const* p);
	void
	remove_torrent_and_data(std::vector<char>&, char const* args, permissions_interface const* p);
	void list_dirs(std::vector<char>&, char const* args, permissions_interface const* p);
	void set_file_priority(std::vector<char>&, char const* args, permissions_interface const* p);

	void queue_up(std::vector<char>&, char const* args, permissions_interface const* p);
	void queue_down(std::vector<char>&, char const* args, permissions_interface const* p);
	void queue_top(std::vector<char>&, char const* args, permissions_interface const* p);
	void queue_bottom(std::vector<char>&, char const* args, permissions_interface const* p);

	void get_settings(std::vector<char>&, char const* args, permissions_interface const* p);
	void set_settings(std::vector<char>&, char const* args, permissions_interface const* p);

	void get_properties(std::vector<char>&, char const* args, permissions_interface const* p);
	void add_url(std::vector<char>&, char const* args, permissions_interface const* p);

	void send_file_list(std::vector<char>&, char const* args, permissions_interface const* p);
	void send_torrent_list(std::vector<char>&, char const* args, permissions_interface const* p);
	void
	send_peer_list(std::vector<char>& response, char const* args, permissions_interface const* p);

	void get_version(std::vector<char>& response, char const* args, permissions_interface const* p);

	void send_rss_list(std::vector<char>&, char const* args, permissions_interface const* p);
	/*
		void rss_update(std::vector<char>& response, char const* args, permissions_interface const* p);
		void rss_remove(std::vector<char>& response, char const* args, permissions_interface const* p);
		void rss_filter_update(std::vector<char>& response, char const* args, permissions_interface const* p);
		void rss_filter_remove(std::vector<char>& response, char const* args, permissions_interface const* p);
*/
private:
	std::vector<lt::torrent_status> parse_torrents(char const* args) const;
	template <typename Fun>
	void apply_fun(char const* args, Fun const& f);

	time_t m_start_time;
	lt::session& m_ses;
	std::string m_webui_cookie;

	auth_interface const& m_auth;

	save_settings_interface& m_settings;

	// a list of the most recent rss filter rules that were
	// removed. first = cid, second rss_ident.
	std::deque<std::pair<int, int>> m_removed_rss_filters;

	// used to detect which torrents have been updated
	// since last time
	torrent_history& m_hist;

	// optional auto loader, controllable
	// via webui settings
	auto_load* m_al;

	int m_version;
	std::string m_token;
};
} // namespace ltweb

#endif
