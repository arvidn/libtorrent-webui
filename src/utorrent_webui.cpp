/*

Copyright (c) 2012-2015, 2017-2018, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "utorrent_webui.hpp"
#include "disk_space.hpp"
#include "base64.hpp"
#include "auth.hpp"
#include "no_auth.hpp"
#include "hex.hpp"
#include "utils.hpp" // for str()

#include <cstring> // for strcmp()
#include <cstdio>
#include <cinttypes>
#include <vector>
#include <map>
#include <cstdint>

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/magnet_uri.hpp" // for make_magnet_uri
#include "libtorrent/span.hpp"
#include "libtorrent/hasher.hpp"
#include "response_buffer.hpp" // for appendf
#include "torrent_post.hpp"
#include "escape_json.hpp"
#include "auto_load.hpp"
#include "save_settings.hpp"
#include "torrent_history.hpp"

#include "url_decode.hpp"

namespace ltweb
{

utorrent_webui::utorrent_webui(lt::session& s, save_settings_interface* sett
	, auto_load* al, torrent_history* hist
	, auth_interface const* auth)
	: m_ses(s)
	, m_al(al)
	, m_auth(auth)
	, m_settings(sett)
//	, m_rss_filter(rss_filter)
	, m_hist(hist)
{
	if (m_auth == nullptr)
	{
		const static no_auth n;
		m_auth = &n;
	}

	m_start_time = time(nullptr);
	m_version = 1;

	std::uint64_t seed = lt::clock_type::now().time_since_epoch().count();
	auto hash = lt::hasher(reinterpret_cast<char const*>(&seed), sizeof(seed)).final();
	m_token = to_hex(hash);

	m_webui_cookie = "{}";

	if (m_settings)
	{
		m_webui_cookie = m_settings->get_str("ut_webui_cookie", "{}");
		int port = m_settings->get_int("listen_port", -1);
		if (port != -1)
		{
			lt::settings_pack pack;
			std::string const listen_interface = "0.0.0.0:" + std::to_string(port);
			pack.set_str(lt::settings_pack::listen_interfaces, listen_interface.c_str());
			m_ses.apply_settings(pack);
		}
	}

}

utorrent_webui::~utorrent_webui() {}

struct method_handler
{
	char const* action_name;
	void (utorrent_webui::*fun)(std::vector<char>&, char const* args, permissions_interface const* p);
};

static const method_handler handlers[] =
{
	{ "start", &utorrent_webui::start },
	{ "forcestart", &utorrent_webui::force_start },
	{ "stop", &utorrent_webui::stop },
	{ "pause", &utorrent_webui::stop },
	{ "unpause", &utorrent_webui::start },

	{ "queueup", &utorrent_webui::queue_up },
	{ "queuedown", &utorrent_webui::queue_down },
	{ "queuetop", &utorrent_webui::queue_top },
	{ "queuebottom", &utorrent_webui::queue_bottom },

	{ "getfiles", &utorrent_webui::send_file_list },
	{ "getpeers", &utorrent_webui::send_peer_list },
	{ "getprops", &utorrent_webui::get_properties },
	{ "recheck", &utorrent_webui::recheck },
	{ "remove", &utorrent_webui::remove_torrent },
	{ "setprio", &utorrent_webui::set_file_priority },
	{ "getsettings", &utorrent_webui::get_settings },
	{ "setsetting", &utorrent_webui::set_settings },
	{ "add-url", &utorrent_webui::add_url },
//	{ "setprops", &utorrent_webui:: },
	{ "removedata", &utorrent_webui::remove_torrent_and_data },
	{ "list-dirs", &utorrent_webui::list_dirs },
//	{ "rss-update", &utorrent_webui::rss_update },
//	{ "rss-remove", &utorrent_webui::rss_remove },
//	{ "filter-update", &utorrent_webui::rss_filter_update },
//	{ "filter-remove", &utorrent_webui::rss_filter_remove },
	{ "removetorrent", &utorrent_webui::remove_torrent },
	{ "removedatatorrent", &utorrent_webui::remove_torrent_and_data },
	{ "getversion", &utorrent_webui::get_version },
//	{ "add-peer", &utorrent_webui:: },
};

// URL-decode a percent-encoded string (+ decoded as space).
static std::string url_decode(std::string_view s)
{
	std::string result;
	result.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '+') { result += ' '; }
		else if (s[i] == '%' && i + 2 < s.size())
		{
			char hex[3] = { s[i+1], s[i+2], '\0' };
			result += char(strtol(hex, nullptr, 16));
			i += 2;
		}
		else { result += s[i]; }
	}
	return result;
}

// Return the raw (URL-encoded) value for var_name in query, or nullopt if not found.
static std::optional<std::string_view> get_query_var(std::string_view query, std::string_view var_name)
{
	while (!query.empty())
	{
		auto const eq = query.find('=');
		if (eq == std::string_view::npos) break;
		auto const amp = query.find('&', eq + 1);
		std::string_view const key = query.substr(0, eq);
		std::string_view const val = query.substr(eq + 1
			, amp == std::string_view::npos ? std::string_view::npos : amp - eq - 1);
		if (key == var_name) return val;
		if (amp == std::string_view::npos) break;
		query.remove_prefix(amp + 1);
	}
	return std::nullopt;
}

void utorrent_webui::handle_http(http::request<http::string_body> request
	, beast::ssl_stream<beast::tcp_stream>& socket
	, std::function<void(bool)> done)
{
	auto const target = std::string(request.target());
	auto const qpos = target.find('?');
	std::string const uri = qpos != std::string::npos ? target.substr(0, qpos) : target;
	std::string const query_string = qpos != std::string::npos ? target.substr(qpos + 1) : std::string{};

	// redirect /gui and /gui/ (no query) to /gui/index.html
	if (uri == "/gui" || (uri == "/gui/" && query_string.empty()))
	{
		http::response<http::empty_body> res{http::status::moved_permanently, request.version()};
		res.set(http::field::location, "/gui/index.html");
		res.keep_alive(request.keep_alive());
		send_http(socket, done, std::move(res));
		return;
	}

	permissions_interface const* perms = parse_http_auth(request, m_auth);
	if (!perms)
	{
		http::response<http::empty_body> res{http::status::unauthorized, request.version()};
		res.set(http::field::www_authenticate, "Basic realm=\"BitTorrent\"");
		res.keep_alive(request.keep_alive());
		send_http(socket, done, std::move(res));
		return;
	}

	std::vector<char> response;

	// Auth token handling
	if (uri == "/gui/token.html")
	{
		// note, the uTorrent webUI actually requires the xml attributes
		// to use single quotes here.
		appendf(response, "<html><div id='token' style='display:none;'>%s</div></html>"
			, m_token.c_str());
		TORRENT_ASSERT(response.back() != '\0');
		response.push_back('\0');

		http::response<http::string_body> res{http::status::ok, request.version()};
		res.set(http::field::content_type, "text/html");
		res.body() = std::string(response.data(), response.size() - 1);
		res.keep_alive(request.keep_alive());
		send_http(socket, done, std::move(res));
		return;
	}

	if (uri != "/gui/")
	{
		send_http(socket, done, http_error(request, http::status::not_found));
		return;
	}

	if (query_string.empty())
	{
		send_http(socket, done, http_error(request, http::status::bad_request));
		return;
	}

	appendf(response, "{\"build\":%d", LIBTORRENT_VERSION_NUM);

	if (auto const action = get_query_var(query_string, "action"))
	{
		// add-file is special, since it posts the torrent
		if (*action == "add-file")
		{
			if (!perms->allow_add())
			{
				http::response<http::empty_body> res{http::status::unauthorized, request.version()};
				res.set(http::field::www_authenticate, "Basic realm=\"BitTorrent\"");
				res.keep_alive(request.keep_alive());
				send_http(socket, done, std::move(res));
				return;
			}
			lt::error_code ec;
			auto params = parse_torrent_post(request, ec);
			if (ec)
			{
				http::response<http::string_body> res{http::status::bad_request, request.version()};
				res.body() = ec.message();
				res.keep_alive(request.keep_alive());
				send_http(socket, done, std::move(res));
				return;
			}

			std::string const save_path = m_settings
				? m_settings->get_str("save_path", "./downloads") : "./downloads";
			bool const start_paused = m_settings && m_settings->get_int("start_paused", 0);
			for (auto& p : params)
			{
				p.save_path = save_path;
				if (start_paused)
					p.flags = (p.flags & ~lt::torrent_flags::auto_managed) | lt::torrent_flags::paused;
				else
					p.flags = (p.flags & ~lt::torrent_flags::paused) | lt::torrent_flags::auto_managed;
				m_ses.async_add_torrent(std::move(p));
			}
		}
		else
		{
			for (auto const& e : handlers)
			{
				if (*action != e.action_name) continue;
				(this->*e.fun)(response, query_string.c_str(), perms);
				break;
			}
		}
	}

	if (auto const list = get_query_var(query_string, "list"))
	{
		if (atoi(url_decode(*list).c_str()) > 0)
		{
			send_torrent_list(response, query_string.c_str(), perms);
			send_rss_list(response, query_string.c_str(), perms);
		}
	}

	appendf(response, "}");
	response.push_back('\0');

	http::response<http::string_body> res{http::status::ok, request.version()};
	res.set(http::field::content_type, "text/json");
	res.body() = std::string(response.data(), response.size() - 1);
	res.keep_alive(request.keep_alive());
	send_http(socket, done, std::move(res));
}

template <typename Fun>
void utorrent_webui::apply_fun(char const* args, Fun const& f)
{
	std::vector<lt::torrent_status> t = parse_torrents(args);
	for (lt::torrent_status const& st : t)
		f(st);
}

void utorrent_webui::start(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_start()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.clear_error();
		st.handle.unset_flags(lt::torrent_flags::upload_mode);
		st.handle.set_flags(lt::torrent_flags::auto_managed);
		st.handle.resume();
	});
}

void utorrent_webui::stop(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_stop()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.unset_flags(lt::torrent_flags::auto_managed);
		st.handle.pause();
	});
}

void utorrent_webui::force_start(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_start()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.unset_flags(lt::torrent_flags::auto_managed);
		st.handle.resume();
	});
}

void utorrent_webui::recheck(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_recheck()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.force_recheck();
	});
}

void utorrent_webui::queue_up(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.queue_position_up();
	});
}

void utorrent_webui::queue_down(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.queue_position_down();
	});
}

void utorrent_webui::queue_top(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.queue_position_top();
	});
}

void utorrent_webui::queue_bottom(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	apply_fun(args, [](lt::torrent_status const& st) {
		st.handle.queue_position_bottom();
	});
}

void utorrent_webui::remove_torrent(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_remove()) return;

	apply_fun(args, [this](lt::torrent_status const& st) {
		m_ses.remove_torrent(st.handle);
	});
}

void utorrent_webui::set_file_priority(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_set_file_prio()) return;

	auto const prio_val = get_query_var(args, "p");
	if (!prio_val) return;
	int prio = atoi(url_decode(*prio_val).c_str());
	prio *= 2;

	std::vector<lt::file_index_t> files;
	for (char const* f = strstr(args, "&f="); f; f = strstr(f, "&f="))
	{
		f += 3;
		char* end;
		int idx = strtol(f, &end, 10);
		if (*end == '&' || *end == '\0')
		{
			files.emplace_back(idx);
			f = end;
		}
	}

	apply_fun(args, [&](lt::torrent_status const& st) {
		for (lt::file_index_t const j : files)
			st.handle.file_priority(j, lt::download_priority_t(prio));
	});
}

void utorrent_webui::remove_torrent_and_data(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_remove() || !p->allow_remove_data()) return;

	apply_fun(args, [this](lt::torrent_status const& st) {
		m_ses.remove_torrent(st.handle, lt::session::delete_files);
	});
}

void utorrent_webui::list_dirs(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	std::string const save_path = m_settings ? m_settings->get_str("save_path", "./downloads") : "./downloads";
	appendf(response, ", \"download-dirs\": [{\"path\":\"%s\",\"available\":%" PRId64 "}]"
		, escape_json(save_path).c_str()
		, free_disk_space(save_path) / 1024 / 1024);
}

char const* settings_name(int s)
{
	return lt::name_for_setting(s);
}

void utorrent_webui::get_settings(std::vector<char>& response, char const* args
	, permissions_interface const* p)
{
	appendf(response, ", \"settings\": [");

	lt::settings_pack sett = m_ses.get_settings();

	// type: 0 = int, 1= bool, 2=string
	bool first = true;
	for (int i = 0; i < lt::settings_pack::num_string_settings; ++i)
	{
		if (!p->allow_get_settings(lt::settings_pack::string_type_base + i)) continue;

		int s = lt::settings_pack::string_type_base + i;
		if (!first) response.push_back(',');
		first = false;
		appendf(response, "[\"%s\",2,\"%s\",{\"access\":\"%c\"}]\n"
			, settings_name(s), escape_json(sett.get_str(s)).c_str()
			, p->allow_set_settings(s) ? 'Y' : 'R');
	}

	for (int i = 0; i < lt::settings_pack::num_bool_settings; ++i)
	{
		int s = lt::settings_pack::bool_type_base + i;
		if (!p->allow_get_settings(s)) continue;

		char const* sname;
		bool value;
		if (s == lt::settings_pack::enable_dht)
		{
			sname = "dht";
			value = sett.get_bool(s);
		}
		else if (s == lt::settings_pack::enable_lsd)
		{
			sname = "lsd";
			value = sett.get_bool(s);
		}
		else if (s == lt::settings_pack::enable_natpmp)
		{
			sname = "natpmp";
			value = sett.get_bool(s);
		}
		else if (s == lt::settings_pack::enable_upnp)
		{
			sname = "upnp";
			value = sett.get_bool(s);
		}
		else if (s == lt::settings_pack::auto_manage_prefer_seeds)
		{
			sname = "seeds_prioritized";
			value = sett.get_bool(s);
		}
		else
		{
			sname = settings_name(s);
			value = sett.get_bool(s);
		}
		if (!first) response.push_back(',');
		first = false;
		appendf(response, "[\"%s\",1,\"%s\",{\"access\":\"%c\"}]\n"
			, sname, value ? "true" : "false"
			, p->allow_set_settings(s) ? 'Y' : 'R');
	}

	for (int i = 0; i < lt::settings_pack::num_int_settings; ++i)
	{
		int s = lt::settings_pack::int_type_base + i;
		if (!p->allow_get_settings(s)) continue;

		char const* sname;
		std::int64_t value;
/*		if (s == lt::settings_pack::cache_size)
		{
			sname = "cache.override_size";
			value = std::int64_t(sett.get_int(s)) * 16 / 1024;
		}
		else
*/
		if (s == lt::settings_pack::upload_rate_limit)
		{
			sname = "max_ul_rate";
			value = std::int64_t(sett.get_int(s)) / 1024;
		}
		else if (s == lt::settings_pack::download_rate_limit)
		{
			sname = "max_dl_rate";
			value = std::int64_t(sett.get_int(s)) / 1024;
		}
		else if (s == lt::settings_pack::connections_limit)
		{
			sname = "conns_globally";
			value = sett.get_int(s);
		}
		else if (s == lt::settings_pack::active_downloads)
		{
			sname = "max_active_downloads";
			value = sett.get_int(s);
		}
		else if (s == lt::settings_pack::active_limit)
		{
			sname = "max_active_torrent";
			value = (std::min)(sett.get_int(s), sett.get_int(lt::settings_pack::active_seeds));
		}
		else
		{
			sname = settings_name(s);
			value = sett.get_int(s);
		}

		if (!first) response.push_back(',');
		first = false;
		appendf(response, "[\"%s\",0,\"%" PRId64 "\",{\"access\":\"%c\"}]\n"
			, sname, value, p->allow_set_settings(s) ? 'Y' : 'R');
	}

	if (!first) response.push_back(',');
	first = false;
	appendf(response, "[\"torrents_start_stopped\",1,\"%s\",{\"access\":\"%c\"}]\n"
		, (m_settings && m_settings->get_int("start_paused", 0)) ? "true" : "false"
		, p->allow_stop() ? 'Y' : 'R');

	if (m_al)
	{
		if (!first) response.push_back(',');
		first = false;
		appendf(response,
			"[\"dir_autoload\",2,\"%s\",{\"access\":\"%c\"}]\n"
			",[\"dir_autoload_flag\",1,\"%s\",{\"access\":\"%c\"}]"
			, escape_json(m_al->auto_load_dir()).c_str()
			, p->allow_set_settings(-1) ? 'Y' : 'R'
			, m_al->scan_interval().count() != 0 ? "true" : "false"
			, p->allow_set_settings(-1) ? 'Y' : 'R');
	}

	if (p->allow_get_settings(lt::settings_pack::enable_outgoing_tcp)
		&& p->allow_get_settings(lt::settings_pack::enable_outgoing_utp)
		&& p->allow_get_settings(lt::settings_pack::enable_incoming_tcp)
		&& p->allow_get_settings(lt::settings_pack::enable_incoming_utp))
	{
		if (!first) response.push_back(',');
		first = false;
		appendf(response,
			"[\"bt.transp_disposition\",0,\"%d\",{\"access\":\"%c\"}]\n"
			, (sett.get_bool(lt::settings_pack::enable_outgoing_tcp) ? 1 : 0)
				+ (sett.get_bool(lt::settings_pack::enable_outgoing_utp) ? 2 : 0)
				+ (sett.get_bool(lt::settings_pack::enable_incoming_tcp) ? 4 : 0)
				+ (sett.get_bool(lt::settings_pack::enable_incoming_utp) ? 8 : 0)
				, (p->allow_set_settings(lt::settings_pack::enable_outgoing_tcp)
					&& p->allow_set_settings(lt::settings_pack::enable_outgoing_utp)
					&& p->allow_set_settings(lt::settings_pack::enable_incoming_tcp)
					&& p->allow_set_settings(lt::settings_pack::enable_incoming_utp))
				? 'Y' : 'R');
	}

	if (p->allow_get_settings(-1))
	{
		if (!first) response.push_back(',');
		first = false;
		appendf(response,
			"[\"dir_active_download\",2,\"%s\",{\"access\":\"%c\"}]\n"
			",[\"bind_port\",0,\"%d\",{\"access\":\"%c\"}]\n"
			, escape_json(m_settings ? m_settings->get_str("save_path", "./downloads") : "./downloads").c_str()
			, p->allow_set_settings(-1) ? 'Y' : 'R'
			, m_ses.listen_port()
			, p->allow_set_settings(-1) ? 'Y' : 'R');
	}

	if (m_settings)
	{
		appendf(response,
			",[\"gui.default_del_action\",0,\"%d\",{\"access\":\"%c\"}]\n"
			, m_settings->get_int("default_del_action", 0)
			, p->allow_set_settings(-1) ? 'Y' : 'R');
	}

	if (!first) response.push_back(',');
	appendf(response,
		"[\"webui.cookie\",2,\"%s\",{\"access\":\"Y\"}]\n"
		",[\"language\",0,\"0\",{\"access\":\"Y\"}]\n"
		",[\"webui.enable_listen\",1,\"true\",{\"access\":\"R\"}]\n"
		",[\"webui.enable_guest\",1,\"false\",{\"access\":\"R\"}]\n"
		",[\"webui.port\",0,\"%d\",{\"access\":\"R\"}]\n"
		",[\"cache.override\",1,\"true\",{\"access\":\"R\"}]\n"
		// the webUI uses the existence of this setting as an
		// indication of supporting the getpeers action, so we
		// need to define it in order to support peers
		",[\"webui.uconnect_enable\",1,\"false\",{\"access\":\"R\"}]\n"
		"]"
		, escape_json(m_webui_cookie).c_str(), 0);
}
bool to_bool(std::string const& s)
{
	return s != "false" && s != "0";
}

void utorrent_webui::set_settings(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	lt::settings_pack pack;

	std::set<std::string> duplicates;
	for (char const* s = strstr(args, "&s="); s; s = strstr(s, "&s="))
	{
		s += 3;
		char const* key_end = strchr(s, '&');
		if (key_end == nullptr) continue;
		if (strncmp(key_end, "&v=", 3) != 0) continue;
		char const* v_end = strchr(key_end + 3, '&');
		if (v_end == nullptr) v_end = s + strlen(s);

		std::string key(s, key_end - s);
		std::string value(key_end + 3, v_end - key_end - 3);
		lt::error_code ec;
		value = url_decode(value, ec);

		// ignore duplicate settings
		if (duplicates.count(key)) continue;
		duplicates.insert(key);

		s = v_end;

		if (ec) continue;

		if (key == "webui.cookie")
		{
			// TODO: store this in some lt::session-specific store, so multiple
			// users don't clobber each other
			m_webui_cookie = value;
			if (m_settings) m_settings->set_str("ut_webui_cookie", value);
		}
		else if (key == "bind_port")
		{
			if (!p->allow_set_settings(-1)) continue;
			int port = atoi(value.c_str());
			std::string const listen_interface = "0.0.0.0:" + std::to_string(port);
			pack.set_str(lt::settings_pack::listen_interfaces, listen_interface.c_str());
			if (m_settings) m_settings->set_int("listen_port", port);
		}
		else if (key == "bt.transp_disposition")
		{
			if (!p->allow_set_settings(lt::settings_pack::enable_outgoing_tcp)
			|| !p->allow_set_settings(lt::settings_pack::enable_outgoing_utp)
			|| !p->allow_set_settings(lt::settings_pack::enable_incoming_tcp)
			|| !p->allow_set_settings(lt::settings_pack::enable_incoming_utp))
				continue;

			int mask = atoi(value.c_str());
			pack.set_bool(lt::settings_pack::enable_outgoing_tcp, mask & 1);
			pack.set_bool(lt::settings_pack::enable_outgoing_utp, mask & 2);
			pack.set_bool(lt::settings_pack::enable_incoming_tcp, mask & 4);
			pack.set_bool(lt::settings_pack::enable_incoming_utp, mask & 8);
		}
		else if (key == "conns_globally")
		{
			if (!p->allow_set_settings(lt::settings_pack::connections_limit)) continue;
			pack.set_int(lt::settings_pack::connections_limit, atoi(value.c_str()));
		}
		else if (key == "max_active_downloads")
		{
			if (!p->allow_set_settings(lt::settings_pack::active_downloads)) continue;
			pack.set_int(lt::settings_pack::active_downloads, atoi(value.c_str()));
		}
		else if (key == "max_active_torrent")
		{
			if (!p->allow_set_settings(lt::settings_pack::active_limit)) continue;
			if (!p->allow_set_settings(lt::settings_pack::active_seeds)) continue;
			pack.set_int(lt::settings_pack::active_limit, atoi(value.c_str()));
			pack.set_int(lt::settings_pack::active_seeds, atoi(value.c_str()));
		}
		else if (key == "seeds_prioritized")
		{
			if (!p->allow_set_settings(lt::settings_pack::auto_manage_prefer_seeds)) continue;
			pack.set_bool(lt::settings_pack::auto_manage_prefer_seeds, to_bool(value));
		}
		else if (key == "torrents_start_stopped")
		{
			if (!p->allow_stop()) continue;
			if (m_settings) m_settings->set_int("start_paused", to_bool(value));
		}
		else if (key == "dir_autoload" && m_al)
		{
			if (!p->allow_set_settings(-1)) continue;
			m_al->set_auto_load_dir(value);
		}
		else if (key == "dir_autoload_flag" && m_al)
		{
			if (!p->allow_set_settings(-1)) continue;
			m_al->set_scan_interval(std::chrono::seconds(to_bool(value) ? 0 : 20));
		}
		else if (key == "dir_active_download")
		{
			if (!p->allow_set_settings(-1)) continue;
			if (m_settings) m_settings->set_str("save_path", value);
		}
/*
		else if (key == "cache.override_size")
		{
			if (!p->allow_set_settings(lt::settings_pack::cache_size)) continue;

			int size = atoi(value.c_str()) * 1024 / 16;
			pack.set_int(lt::settings_pack::cache_size, size);
		}
*/
		else if (key == "max_ul_rate")
		{
			if (!p->allow_set_settings(lt::settings_pack::upload_rate_limit)) continue;
			pack.set_int(lt::settings_pack::upload_rate_limit, atoi(value.c_str()) * 1024);
		}
		else if (key == "max_dl_rate")
		{
			if (!p->allow_set_settings(lt::settings_pack::download_rate_limit)) continue;
			pack.set_int(lt::settings_pack::download_rate_limit, atoi(value.c_str()) * 1024);
		}
		else if (key == "dht")
		{
			if (!p->allow_set_settings(lt::settings_pack::enable_dht)) continue;
			pack.set_bool(lt::settings_pack::enable_dht, to_bool(value));
		}
		else if (key == "natpmp")
		{
			if (!p->allow_set_settings(lt::settings_pack::enable_natpmp)) continue;
			pack.set_bool(lt::settings_pack::enable_natpmp, to_bool(value));
		}
		else if (key == "upnp")
		{
			if (!p->allow_set_settings(lt::settings_pack::enable_upnp)) continue;
			pack.set_bool(lt::settings_pack::enable_upnp, to_bool(value));
		}
		else if (key == "lsd")
		{
			if (!p->allow_set_settings(lt::settings_pack::enable_lsd)) continue;
			pack.set_bool(lt::settings_pack::enable_lsd, to_bool(value));
		}
		else if (key == "gui.default_del_action" && m_settings)
		{
			m_settings->set_int("default_del_action", atoi(value.c_str()));
		}
		else
		{
			int field = lt::setting_by_name(key.c_str());
			if (field < 0)
			{
				fprintf(stderr, "unknown setting: %s\n", key.c_str());
				continue;
			}
			switch (field & lt::settings_pack::type_mask)
			{
				case lt::settings_pack::string_type_base:
					if (!p->allow_set_settings(field)) continue;
					pack.set_str(field, value.c_str());
					break;
				case lt::settings_pack::int_type_base:
					if (!p->allow_set_settings(field)) continue;
					pack.set_int(field, atoi(value.c_str()));
					break;
				case lt::settings_pack::bool_type_base:
					if (!p->allow_set_settings(field)) continue;
					pack.set_bool(field, to_bool(value));
					break;
			}
		}
	}
	m_ses.apply_settings(pack);

	lt::error_code ec;
	if (m_settings) m_settings->save(ec);
}

void utorrent_webui::send_file_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	std::vector<lt::torrent_status> t = parse_torrents(args);
	appendf(response, ",\"files\":[");
	bool first = true;
	std::vector<std::int64_t> progress;
	std::vector<lt::download_priority_t> file_prio;
	for (std::vector<lt::torrent_status>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->handle.file_progress(progress);
		file_prio = i->handle.get_file_priorities();
		std::shared_ptr<const lt::torrent_info> ti = i->torrent_file.lock();
		if (!ti || !ti->is_valid()) continue;
		lt::file_storage const& files = ti->layout();

		// TODO: get_renamed_files() is synchronous, we should use async functions only
		lt::renamed_files const renames = i->handle.get_renamed_files();

		if (!first) response.push_back(',');
		first = false;
		appendf(response, "\"%s\",[", to_hex(ti->info_hashes().get_best()).c_str());
		bool first_file = true;
		for (lt::file_index_t i : files.file_range())
		{
			int first_piece = files.file_offset(i) / files.piece_length();
			int last_piece = (files.file_offset(i) + files.file_size(i)) / files.piece_length();
			// don't round 1 down to 0. 0 is special (do-not-download)
			if (file_prio[static_cast<int>(i)] == lt::low_priority)
				file_prio[static_cast<int>(i)] = lt::download_priority_t{2};
			if (!first_file) response.push_back(',');
			first_file = false;
			appendf(response, "[\"%s\", %" PRId64 ", %" PRId64 ", %d"
				, escape_json(renames.file_name(files, i)).c_str()
				, files.file_size(i)
				, progress[static_cast<int>(i)]
				// uTorrent's web UI uses 4 priority levels, libtorrent uses 8
				, static_cast<std::uint8_t>(file_prio[static_cast<int>(i)]) / 2
				);

			if (m_version > 0)
			{
				appendf(response, ", %d, %d]"
					, first_piece
					, last_piece - first_piece);
			}
			else
			{
				response.push_back(']');
			}
		}

		response.push_back(']');
	}
	response.push_back(']');
}

std::string trackers_as_string(lt::torrent_handle h)
{
	std::string ret;
	std::vector<lt::announce_entry> trackers = h.trackers();
	int last_tier = 0;
	for (std::vector<lt::announce_entry>::iterator i = trackers.begin()
		, end(trackers.end()); i != end; ++i)
	{
		if (last_tier != i->tier) ret += "\\r\\n";
		last_tier = i->tier;
		ret += i->url;
		ret += "\\r\\n";
	}
	return ret;
}

void utorrent_webui::add_url(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_add()) return;

	auto const url_val = get_query_var(args, "url")
		? get_query_var(args, "url") : get_query_var(args, "s");
	if (!url_val) return;
	std::string const url_str = url_decode(*url_val);

	lt::add_torrent_params atp = lt::parse_magnet_uri(url_str);
	atp.save_path = m_settings ? m_settings->get_str("save_path", "./downloads") : "./downloads";
	if (m_settings && m_settings->get_int("start_paused", 0))
		atp.flags = (atp.flags & ~lt::torrent_flags::auto_managed) | lt::torrent_flags::paused;
	else
		atp.flags = (atp.flags & ~lt::torrent_flags::paused) | lt::torrent_flags::auto_managed;

	m_ses.async_add_torrent(std::move(atp));
}

void utorrent_webui::get_properties(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	std::vector<lt::torrent_status> t = parse_torrents(args);
	appendf(response, ",\"props\":[");
	bool first = true;
	for (std::vector<lt::torrent_status>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		lt::torrent_status const& st = *i;
		std::shared_ptr<const lt::torrent_info> ti = st.torrent_file.lock();
		if (!first) response.push_back(',');
		first = false;
		appendf(response, "{\"hash\":\"%s\","
			"\"trackers\":\"%s\","
			"\"ulrate\":%d,"
			"\"dlrate\":%d,"
			"\"superseed\":%d,"
			"\"dht\":%d,"
			"\"pex\":%d,"
			"\"seed_override\":%d,"
			"\"seed_ratio\": %f,"
			"\"seed_time\": %d,"
			"\"ulslots\": %d,"
			"\"seed_num\": %d}"
			, ti ? to_hex(ti->info_hash()).c_str() : ""
			, trackers_as_string(i->handle).c_str()
			, st.handle.download_limit()
			, st.handle.upload_limit()
			, bool(st.flags & lt::torrent_flags::super_seeding)
			, ti && ti->priv() ? 0 : m_ses.is_dht_running()
			, ti && ti->priv() ? 0 : 1
			, 0
			, 0
			, 0
			, 0
			, 0
			);
	}
	response.push_back(']');
}

std::string utorrent_peer_flags(lt::peer_info const& pi)
{
	std::string ret;
	if (pi.flags & lt::peer_info::remote_interested)
	{
		ret += (pi.flags & lt::peer_info::choked) ? 'u' : 'U';
	}
	else if (!(pi.flags & lt::peer_info::choked))
	{
		// ERROR: we're unchoking someone that isn't interested
		ret += '?';
	}

	if (pi.flags & lt::peer_info::interesting)
	{
		ret += (pi.flags & lt::peer_info::remote_choked) ? 'd' : 'D';
	}
	else if (!(pi.flags & lt::peer_info::remote_choked))
	{
		// ERROR: we're being unchoked even though we're not interested
		ret += 'K';
	}

	if (pi.flags & lt::peer_info::optimistic_unchoke)
		ret += 'O';

	if (pi.flags & lt::peer_info::snubbed)
		ret += 'S';

	// separate flags from sources with a space
	ret += ' ';

	if (!(pi.source & lt::peer_info::incoming))
		ret += 'I';

	if ((pi.source & lt::peer_info::dht))
		ret += 'H';

	if ((pi.source & lt::peer_info::pex))
		ret += 'X';

	if ((pi.source & lt::peer_info::lsd))
		ret += 'L';

	if ((pi.flags & lt::peer_info::rc4_encrypted))
		ret += 'E';
	else if ((pi.flags & lt::peer_info::plaintext_encrypted))
		ret += 'e';

	if ((pi.flags & lt::peer_info::on_parole))
		ret += 'F';

	if (pi.flags & lt::peer_info::utp_socket)
		ret += 'P';
	return ret;
}

void utorrent_webui::send_peer_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	std::vector<lt::torrent_status> torrents = parse_torrents(args);
	appendf(response, ",\"peers\":[");
	bool first = true;
	for (std::vector<lt::torrent_status>::iterator i = torrents.begin()
		, end(torrents.end()); i != end; ++i)
	{
		std::shared_ptr<const lt::torrent_info> ti = i->torrent_file.lock();
		if (!ti || !ti->is_valid()) continue;

		if (!first) response.push_back(',');
		first = false;
		appendf(response, "\"%s\",["
			, to_hex(i->info_hashes.get_best()).c_str());

		bool first_peer = true;
		std::vector<lt::peer_info> peers;
		i->handle.get_peer_info(peers);
		for (lt::peer_info const& p : peers)
		{
			auto const& addr = p.remote_endpoint().address();
			std::string const ep = addr.is_v6()
				? str('[', addr, "]:", p.remote_endpoint().port())
				: str(addr, ":", p.remote_endpoint().port());
			if (!first_peer) response.push_back(',');
			first_peer = false;
			appendf(response, "[\"  \",\"%s\",\"%s\",%d,%d,\"%s\",\"%s\",%d,%d,%d,%d,%d"
				",%d,%" PRId64 ",%" PRId64 ",%d,%d,%d,%d,%d,%d,%d]"
				, ep.c_str()
				, ""
				, bool(p.flags & lt::peer_info::utp_socket)
				, p.remote_endpoint().port()
				, escape_json(p.client).c_str()
				, utorrent_peer_flags(p).c_str()
				, p.num_pieces * 1000 / ti->num_pieces()
				, p.down_speed
				, p.up_speed
				, p.download_queue_length
				, p.upload_queue_length
				, lt::total_seconds(p.last_request)
				, p.total_upload
				, p.total_download
				, p.num_hashfails
				, 0
				, 0
				, 0
				, p.send_buffer_size
				, lt::total_seconds(p.last_active)
				, 0
				);
		}

		response.push_back(']');
	}
	response.push_back(']');
}

void utorrent_webui::get_version(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	lt::settings_pack const sett = m_ses.get_settings();
	auto const our_peer_id = sett.get_str(lt::settings_pack::peer_fingerprint);
	appendf(response, ",\"version\":{\"engine_version\": \"%s\""
		",\"major_version\": %d"
		",\"minor_version\": %d"
		",\"peer_id\": \"%s\""
		",\"user_agent\": \"%s\""
		",\"product_code\": \"server\""
		"}"
		, LIBTORRENT_REVISION, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR
		, to_hex(our_peer_id).c_str(), m_ses.get_settings().get_str(lt::settings_pack::user_agent).c_str());
}

enum ut_state_t
{
	STARTED = 1,
	CHECKING = 2,
	START_AFTER_CHECK = 4,
	CHECKED = 8,
	ERROR = 16,
	PAUSED = 32,
	AUTO = 64,
	LOADED = 128
};

int utorrent_status(lt::torrent_status const& st)
{
	int ret = 0;
	if (st.has_metadata) ret |= LOADED;
	if (!(st.flags & lt::torrent_flags::paused) && (st.state == lt::torrent_status::downloading
		|| st.state == lt::torrent_status::downloading_metadata
		|| st.state == lt::torrent_status::seeding
		|| st.state == lt::torrent_status::finished))
		ret |= STARTED;

	if (!(st.flags & lt::torrent_flags::paused) && st.state == lt::torrent_status::checking_files)
		ret |= CHECKING;
	else
		ret |= CHECKED;
	if (st.errc) ret |= ERROR;
	if (st.flags & lt::torrent_flags::auto_managed) ret |= AUTO;
	return ret;
}

std::string utorrent_message(lt::torrent_status const& st)
{
	if (st.errc) return "Error: " + st.errc.message();
	if (st.flags & lt::torrent_flags::upload_mode) return "Upload Mode";

	if (st.state == lt::torrent_status::checking_resume_data)
		return "Checking";

	if (st.state == lt::torrent_status::checking_files)
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "Checking (%d.%1d%%)"
			, st.progress_ppm / 10000, st.progress_ppm % 10000);
		return msg;
	}
	if (st.state == lt::torrent_status::downloading)
	{
		if (st.flags & lt::torrent_flags::auto_managed)
		{
			return (st.flags & lt::torrent_flags::paused) ? "Queued" : "Downloading";
		}
		else
		{
			return (st.flags & lt::torrent_flags::paused) ? "Stopped" : "[F] Downloading";
		}
	}

	if (st.state == lt::torrent_status::seeding
		|| st.state == lt::torrent_status::finished)
	{
		if (st.flags & lt::torrent_flags::auto_managed)
		{
			return (st.flags & lt::torrent_flags::paused) ? "Queued Seed" : "Seeding";
		}
		else
		{
			return (st.flags & lt::torrent_flags::paused) ? "Finished" : "[F] Seeding";
		}
	}

	if (st.state == lt::torrent_status::downloading_metadata)
		return "Downloading metadata";

//	if (st.state == lt::torrent_status::allocating)
//		return "Allocating";

	assert(false);
	return "??";
}

void utorrent_webui::send_torrent_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	int cid = 0;
	if (auto const cid_val = get_query_var(args, "cid"))
		cid = atoi(url_decode(*cid_val).c_str());

	appendf(response, cid > 0 ? ",\"torrentp\":[" : ",\"torrents\":[");

	std::vector<lt::torrent_status> torrents;
	m_hist->updated_since(cid, torrents);

	bool first = true;
	for (lt::torrent_status const& t : torrents)
	{
		std::shared_ptr<const lt::torrent_info> ti = t.torrent_file.lock();
		if (!first) response.push_back(',');
		first = false;
		appendf(response, "[\"%s\",%d,\"%s\",%" PRId64 ",%d,%" PRId64 ",%" PRId64 ",%f,%d,%d,%d,\"%s\",%d,%d,%d,%d,%d,%d,%" PRId64 ""
			, to_hex(t.info_hashes.get_best()).c_str()
			, utorrent_status(t)
			, escape_json(t.name).c_str()
			, ti ? ti->total_size() : 0
			, t.progress_ppm / 1000
			, t.all_time_download
			, t.all_time_upload
			, t.all_time_download == 0 ? 0 : float(t.all_time_upload) * 1000.f / t.all_time_download
			, t.upload_payload_rate
			, t.download_payload_rate
			, t.download_payload_rate == 0 ? 0 : (t.total_wanted - t.total_wanted_done) / t.download_payload_rate
			, "" // label
			, t.num_peers - t.num_seeds
			, t.list_peers - t.list_seeds
			, t.num_seeds
			, t.list_seeds
			, t.distributed_full_copies < 0 ? 0
				: int(t.distributed_full_copies << 16) + int(t.distributed_fraction * 65536 / 1000)
			, t.queue_position
			, t.total_wanted - t.total_wanted_done
			);

		if (m_version > 0)
		{
			appendf(response, ",\"%s\",\"%s\",\"%s\",\"%s\",%" PRId64 ",%" PRId64 ",\"%s\",\"%s\",%d,\"%s\"]"
			, "" // url this torrent came from
			, "" // feed URL this torrent belongs to
			, escape_json(utorrent_message(t)).c_str()
			, to_hex(t.info_hashes.get_best()).c_str()
			, t.added_time
			, t.completed_time
			, "" // app
			, escape_json(t.save_path).c_str()
			, 0
			, "");
		}
		else
		{
			response.push_back(']');
		}
	}

	std::vector<lt::sha1_hash> const removed = m_hist->removed_since(cid);

	appendf(response, "], \"torrentm\": [");
	first = true;
	for (lt::sha1_hash const& i : removed)
	{
		if (!first) response.push_back(',');
		first = false;
		appendf(response, "\"%s\"", to_hex(i).c_str());
	}
	// TODO: support labels
	appendf(response, "], \"label\": [], \"torrentc\": \"%d\"", m_hist->frame());
}

void utorrent_webui::send_rss_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;
	appendf(response, ",\"rssfeeds\":[],\"rssfeedm\":[],\"rssfilters\":[],\"rssfilterm\":[]");
}

std::vector<lt::torrent_status> utorrent_webui::parse_torrents(char const* args) const
{
	std::vector<lt::torrent_status> ret;

	for (char const* hash = strstr(args, "&hash="); hash; hash = strstr(hash, "&hash="))
	{
		hash += 6;
		char const* end = strchr(hash, '&');
		if (end != nullptr && end - hash != 40) continue;
		if (end == nullptr && strlen(hash) != 40) continue;
		lt::sha1_hash h;
		bool ok = from_hex({hash, 40}, h.data());
		if (!ok) continue;
		lt::torrent_status ts = m_hist->get_torrent_status(h);
		if (!ts.handle.is_valid()) continue;
		ret.push_back(ts);
	}
	return ret;
}

}

