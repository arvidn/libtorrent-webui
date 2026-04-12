#include "libtorrent_webui.hpp"
#include "file_downloader.hpp"
#include "save_settings.hpp"
#include "save_resume.hpp"
#include "torrent_history.hpp"
#include "auth.hpp"
#include "pam_auth.hpp"
#include "serve_files.hpp"
#include "webui.hpp"
#include "no_auth.hpp"

#include "libtorrent/session.hpp"
#include "alert_handler.hpp"
#include "stats_logging.hpp"

#include <signal.h>
#include <iostream>
#include <thread>
#include <chrono>

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
	no_auth authorizer;
//	auth authorizer;
//	ec.clear();
//	authorizer.load_accounts("users.conf", ec);
//	if (ec)
//		authorizer.add_account("admin", "test", 0);
//	ec.clear();
//	pam_auth authorizer("bittorrent");

	save_resume resume(ses, "resume.dat", &alerts);
	resume.load(ec);
	// TODO: log error if ec is set

	stats_logging log(ses, &alerts);

	webui_base webport(8090, "server.pem");

	// this serves static files from directory "bt" exposed at HTTP path /bt/
	serve_files static_files("/bt/", "bt");
	webport.add_handler(&static_files);

	// websocket access to controlling the bittorrent client exposed at HTTP
	// path /bt/control
	libtorrent_webui lt_handler(ses, &hist, &authorizer, &alerts, &sett);
	webport.add_handler(&lt_handler);

	// allows requesting files from within torrents exposed at HTTP path
	// /download/<info-hash>/<file-index>
	// supports range requests
	file_downloader file_handler(ses, &alerts, &authorizer);
	file_handler.set_disposition(false);
	webport.add_handler(&file_handler);

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
	// and cancel the ones in flught now, otherwise the webport
	// may dead-lock. Some of its threads may be blocked waiting
	// for alerts. Those alerts aren't likely to ever arrive at
	// this point.
	alerts.abort();

	fprintf(stderr, "saving settings\n");
	sett.save(ec);

	fprintf(stderr, "destructing session\n");
	return 0;
}

