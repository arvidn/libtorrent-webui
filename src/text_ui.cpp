/*

Copyright (c) 2014, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "text_ui.hpp"
#include "libtorrent/alert_types.hpp"

#include "alert_handler.hpp"

extern "C" {
#include <ncurses.h>
#include <cdk/cdk.h>
}

namespace ltweb
{

screen::screen()
{
	WINDOW* scr = initscr();
	m_screen = initCDKScreen(scr);
	initCDKColor();
}

screen::~screen()
{
	eraseCDKScreen(m_screen);
	endCDK();
}

int screen::width() const
{
	int h, w;
	getmaxyx(m_screen->window, h, w);
	return w;
}

int screen::height() const
{
	int h, w;
	getmaxyx(m_screen->window, h, w);
	return h;
}

void screen::refresh()
{
	refreshCDKScreen(m_screen);
}

log_window::log_window(screen& scr, int x, int y, int w, int h)
	: m_win(newCDKSwindow(scr.native_handle(), x, y, h, w, "", 100, 1, 0))
{
}

log_window::~log_window()
{
	destroyCDKSwindow(m_win);
}

int log_window::width() const
{
	int h, w;
	getmaxyx(m_win->win, h, w);
	return w;
}

int log_window::height() const
{
	int h, w;
	getmaxyx(m_win->win, h, w);
	return h;
}

void log_window::set_pos(int x, int y, int w, int h)
{
	wresize(m_win->win, w, h);
	moveCDKSwindow(m_win, x, y, 1, 0);
}

void log_window::log_line(std::string l)
{
	addCDKSwindow(m_win, (char*)l.c_str(), BOTTOM);
}

error_log::error_log(screen& scr, int x, int y, int w, int h
	, alert_handler* alerts)
	: log_window(scr, x, y, w, h)
	, m_alerts(alerts)
{
	m_alerts->subscribe(this, 0
		, add_torrent_alert::alert_type
		, rss_alert::alert_type
		, read_piece_alert::alert_type
		, mmap_cache_alert::alert_type
		, dht_error_alert::alert_type
		, torrent_need_cert_alert::alert_type
		, file_rename_failed_alert::alert_type
		, tracker_error_alert::alert_type
		, scrape_failed_alert::alert_type
		, storage_moved_failed_alert::alert_type
		, torrent_delete_failed_alert::alert_type
		, save_resume_data_failed_alert::alert_type
		, url_seed_alert::alert_type
		, file_error_alert::alert_type
		, metadata_failed_alert::alert_type
		, udp_error_alert::alert_type
		, listen_failed_alert::alert_type
		, portmap_error_alert::alert_type
		, fastresume_rejected_alert::alert_type
		, torrent_error_alert::alert_type
		, 0);
}

void error_log::handle_alert(alert const* a)
{
	// we're just interested in errors
	read_piece_alert const* rpa = alert_cast<const read_piece_alert>(a);
	if (rpa && !rpa->ec) return;

	add_torrent_alert const* ata = alert_cast<const add_torrent_alert>(a);
	if (ata && !ata->error) return;

	rss_alert const* ra = alert_cast<const rss_alert>(a);
	if (ra && !ra->error) return;

	log_line(a->message());
}

error_log::~error_log()
{
	m_alerts->unsubscribe(this);
}

}

