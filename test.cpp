#include "libtorrent_webui.hpp"
#include "utorrent_webui.hpp"
#include "file_downloader.hpp"
#include "save_settings.hpp"
#include "save_resume.hpp"
#include "torrent_history.hpp"
#include "serve_files.hpp"
#include "public_file.hpp"
#include "webui.hpp"
#include "login.hpp"
#include "login_throttler.hpp"
#include "session_authenticator.hpp"

#include "sqlite_user_account.hpp"
#include "no_auth.hpp"
#include "pam_auth.hpp"

#include "libtorrent/session.hpp"
#include "alert_handler.hpp"
#include "stats_logging.hpp"
#include "torrent_post.hpp"

#include <signal.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

using namespace std::literals::chrono_literals;

bool quit = false;
bool force_quit = false;

void sighandler(int s)
{
	quit = true;
}

void sighandler_forcequit(int s)
{
	force_quit = true;
}

using namespace libtorrent;
using namespace ltweb;

int main(int argc, char *const argv[])
{
	// skip executable name
	++argv;
	--argc;

	session_params s;
	error_code ec;
	s.settings.set_str(settings_pack::listen_interfaces, "0.0.0.0:6881");
	load_settings(s, "settings.dat", ec);
	if (ec) std::cout << "Failed to load settings: " << ec.message() << '\n';

	if (argc > 0)
	{
		s.settings.set_str(settings_pack::listen_interfaces, *argv);
		std::cout << "listening on: " << *argv << std::endl;
		++argv;
		--argc;
	}
	s.settings.set_int(settings_pack::alert_mask, 0xffffffff);

	lt::session ses(s);

	alert_handler alerts(ses);

	save_settings sett(ses, s.settings, "settings.dat");

	torrent_history hist(&alerts);

	save_resume resume(ses, "resume.dat", &alerts);
	resume.load(ec);
	// TODO: log error if ec is set

	stats_logging log(&alerts);

	// It's important that all http handlers outlive the webui_base object. So
	// they have to be constructed first

	// in-memory session table for cookie-based authentication. Login
	// mints into this; libtorrent_webui and utorrent_webui resolve
	// session cookies through it. Must outlive every handler that
	// references it.
	session_authenticator sessions;

	// permission singletons for the login handler's group_id mapping.
	// group_id 0 -> full access, group_id 1 -> read-only.
	full_permissions full_perms;
	read_only_permissions ro_perms;

	// this serves static files from directory "bt" exposed at HTTP path /bt/.
	// Authenticates via session cookie. Other paths redirect to login on
	// auth failure. The login form is served by the login handler at
	// /login, not from here.
	serve_files static_files("/bt/", "bt", sessions, "/login");

	// a small set of files that must be reachable without authentication
	// (eg /favicon.ico, the public stylesheet). Each handler serves a
	// single file at an exact server path; no directory traversal, no auth.
	public_file favicon("/favicon.ico", "bt/favicon.ico");
	public_file public_styles("/styles.css", "bt/styles.css");

	// websocket access to controlling the bittorrent client exposed at HTTP
	// path /bt/control. Authenticates via session cookie; redirects to the
	// login page when the cookie is missing or expired.
	libtorrent_webui lt_handler(ses, hist, sessions, alerts, sett, "/login");

	// uTorrent-compatible HTTP API exposed at /gui. Authenticates via
	// session cookie; redirects to the login page on auth failure.
	utorrent_webui ut_handler(ses, sett, hist, sessions, "/login");

	// adds torrents posted to /bt/add. Authenticates via session cookie.
	torrent_post_handler post(ses, sessions, &sett);

	// allows requesting files from within torrents exposed at HTTP path
	// /download/<info-hash>/<file-index>
	// supports range requests. Authenticates via session cookie.
	file_downloader file_handler(ses, &alerts, sessions);
	file_handler.set_disposition(false);

	// sqlite_user_account accounts("users.db");
	// pam_auth accounts("bittorrent");
	no_auth accounts;

	// rate-limit POST /login attempts per /24 (v4) and /64 (v6)
	// network. Defaults: 5 failures in 60s -> 5min block.
	login_throttler throttler;

	// GET /login serves the login form with a CSRF token; POST /login
	// validates credentials + CSRF and mints a session cookie. The
	// HTML template is read once at startup; parse_login_template
	// throws if the template is missing or malformed (eg the
	// __CSRF_TOKEN__ placeholder is not in the blessed hidden input).
	std::ifstream login_html_file("bt/login.html");
	if (!login_html_file)
	{
		std::cerr << "Failed to open bt/login.html\n";
		return 1;
	}
	std::stringstream login_html_buf;
	login_html_buf << login_html_file.rdbuf();

	login login_handler("/login", login_html_buf.str()
		, accounts, sessions, throttler, "/bt/test.html"
		, {&full_perms, &ro_perms});

	webui_base webport(8090, "server.pem");

	webport.add_handler(&static_files);
	webport.add_handler(&favicon);
	webport.add_handler(&public_styles);
	webport.add_handler(&lt_handler);
	webport.add_handler(&ut_handler);
	webport.add_handler(&post);
	webport.add_handler(&file_handler);
	webport.add_handler(&login_handler);

	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	bool shutting_down = false;
	lt::time_point last_update = lt::clock_type::now();
	while (!quit || !resume.ok_to_quit())
	{
		alerts.dispatch_alerts(500ms);
		if (quit && !shutting_down)
		{
			resume.save_all();
			shutting_down = true;
			fprintf(stderr, "saving resume data\n");
			signal(SIGTERM, &sighandler_forcequit);
			signal(SIGINT, &sighandler_forcequit);
		}
		lt::time_point const now = lt::clock_type::now();
		if (!quit && now - last_update > 500ms)
		{
			resume.tick();
			ses.post_torrent_updates();
			last_update = now;
		}
		if (force_quit)
		{
			fprintf(stderr, "force quitting\n");
			break;
		}
	}

	fprintf(stderr, "abort alerts\n");
	// it's important to disable any more alert subscriptions
	// and cancel the ones in flight now, otherwise the webport
	// may dead-lock. Some of its threads may be blocked waiting
	// for alerts. Those alerts aren't likely to ever arrive at
	// this point.
	alerts.abort();

	fprintf(stderr, "saving settings\n");
	sett.save(ec);

	fprintf(stderr, "destructing session\n");
	return 0;
}

