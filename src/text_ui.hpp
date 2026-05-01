/*

Copyright (c) 2014, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_TEXT_UI_HPP
#define LTWEB_TEXT_UI_HPP

#include <string>
#include "alert_observer.hpp"

extern "C" {
#include <ncurses.h>
#include <cdk/cdk.h>
}

namespace ltweb {
struct alert_handler;

struct screen {
	screen();
	~screen();

	CDKSCREEN* native_handle() { return m_screen; }

	int width() const;
	int height() const;

	void refresh();

private:
	CDKSCREEN* m_screen;
};

struct window {
	virtual int width() const = 0;
	virtual int height() const = 0;
	virtual void set_pos(int x, int y, int width, int height) = 0;
	virtual ~window() {}
};

struct log_window : window {
	log_window(screen& scr, int x, int y, int width, int height);
	~log_window();

	void log_line(std::string l);

	CDKSWINDOW* native_handle() { return m_win; }

	virtual int width() const;
	virtual int height() const;
	virtual void set_pos(int x, int y, int width, int height);

private:
	CDKSWINDOW* m_win;
};

struct error_log
	: log_window
	, alert_observer {
	error_log(screen& scr, int x, int y, int width, int height, alert_handler* alerts);
	~error_log();

private:
	virtual void handle_alert(alert const* a);
	alert_handler* m_alerts;
};

struct torrent_list
	: window
	, alert_observer {

private:
	virtual void handle_alert(alert const* a);
	alert_handler* m_alerts;
};
} // namespace ltweb

#endif
