/*

Copyright (c) 2012-2013, 2017, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_AUTO_LOAD_HPP
#define LTWEB_AUTO_LOAD_HPP

#include "alert_observer.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/io_context.hpp"
#include "boost/asio/high_resolution_timer.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>

namespace ltweb {
struct alert_handler;
struct save_settings_interface;

struct auto_load : alert_observer {
	auto_load(lt::session& s, alert_handler* h, save_settings_interface* sett = nullptr);
	~auto_load();

	void set_auto_load_dir(std::string const& dir);
	std::string const& auto_load_dir() const { return m_dir; }

	std::chrono::seconds scan_interval() const { return m_scan_interval; }
	void set_scan_interval(std::chrono::seconds s);

	void set_remove_files(bool r);
	bool remove_files() const;

private:
	void handle_alert(lt::alert const* a) override;
	void on_scan(lt::error_code const& ec);

	void thread_fun();

	lt::session& m_ses;
	alert_handler* m_alerts;
	boost::asio::io_context m_ios;
	boost::asio::high_resolution_timer m_timer;
	save_settings_interface* m_settings;

	// whether or not to remove .torrent files
	// as they are loaded
	bool m_remove_files;

	// filenames already submitted to the session, to avoid re-adding on rescan
	std::set<std::string> m_already_loaded;

	// when remove_files is set, maps a per-add ID to the .torrent file path
	// pending deletion once the add_torrent_alert confirms success
	std::map<std::uintptr_t, std::filesystem::path> m_pending_files;
	std::uintptr_t m_next_pending_id = 0;

	std::string m_dir;
	std::chrono::seconds m_scan_interval;
	bool m_abort;

	// protects m_abort, m_scan_interval, m_dir, m_remove_files,
	// m_already_loaded, and m_pending_files
	mutable std::mutex m_mutex;

	// this needs to be last in order to be initialized
	// last in the constructor. This way the object is
	// guaranteed to be completely constructed by the time
	// the thread function is started
	std::thread m_thread;
};
} // namespace ltweb

#endif
