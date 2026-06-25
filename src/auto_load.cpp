/*

Copyright (c) 2012-2013, 2017, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "auto_load.hpp"
#include "alert_handler.hpp"

#include <functional>

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "save_settings.hpp"

#include <filesystem>

using namespace std::placeholders;

namespace ltweb {

namespace {

std::vector<std::string>
list_dir(std::string const& path, bool (*filter_fun)(lt::string_view), lt::error_code& ec)
{
	std::vector<std::string> ret;
	std::error_code fec;
	std::filesystem::directory_iterator it(
		path, std::filesystem::directory_options::skip_permission_denied, fec
	);
	if (fec) {
		ec.assign(fec.value(), boost::system::system_category());
		return ret;
	}
	for (auto const& entry : it) {
		std::string const name = entry.path().filename().string();
		if (filter_fun(lt::string_view(name.data(), name.size()))) ret.push_back(name);
	}
	return ret;
}
} // namespace

auto_load::auto_load(lt::session& s, alert_handler* h, save_settings_interface* sett)
	: m_ses(s)
	, m_alerts(h)
	, m_timer(m_ios)
	, m_settings(sett)
	, m_remove_files(true)
	, m_dir("./autoload")
	, m_scan_interval(20)
	, m_abort(false)
	, m_thread(std::bind(&auto_load::thread_fun, this))
{
	if (m_alerts) m_alerts->subscribe<lt::add_torrent_alert>(this);

	if (m_settings) {
		int const interval = m_settings->get_int("autoload_interval", -1);
		if (interval != -1) set_scan_interval(std::chrono::seconds(interval));
		std::string path = m_settings->get_str("autoload_dir", "");
		if (!path.empty()) set_auto_load_dir(path);
		int remove_files = m_settings->get_int("autoload_remove", -1);
		if (remove_files != -1) set_remove_files(remove_files);
	}
}

auto_load::~auto_load()
{
	if (m_alerts) m_alerts->unsubscribe(this);

	std::unique_lock<std::mutex> l(m_mutex);
	m_abort = true;
	l.unlock();
	m_timer.cancel();
	m_thread.join();
}

void auto_load::set_remove_files(bool r)
{
	std::unique_lock<std::mutex> l(m_mutex);
	m_remove_files = r;
	if (m_settings) m_settings->set_int("autoload_remove", r);
}

bool auto_load::remove_files() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	return m_remove_files;
}

void auto_load::set_auto_load_dir(std::string const& dir)
{
	std::unique_lock<std::mutex> l(m_mutex);
	m_dir = dir;
	if (m_settings) m_settings->set_str("autoload_dir", dir);
	l.unlock();

	// reset the timeout to use the new interval
	m_timer.expires_after(lt::seconds(0));
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));
}

void auto_load::set_scan_interval(std::chrono::seconds s)
{
	std::unique_lock<std::mutex> l(m_mutex);
	if (m_scan_interval == s) return;
	m_scan_interval = s;
	if (m_settings) m_settings->set_int("autoload_interval", s.count());
	l.unlock();

	// interval of 0 means disabled
	if (m_scan_interval == std::chrono::seconds(0)) {
		m_timer.cancel();
		return;
	}

	// reset the timeout to use the new interval
	m_timer.expires_after(lt::seconds(m_scan_interval));
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));
}

void auto_load::thread_fun()
{
	// the std::mutex must be held while inspecting m_abort
	std::unique_lock<std::mutex> l(m_mutex);

	m_timer.expires_after(lt::seconds(1));
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));

	while (!m_abort) {
		l.unlock();
		m_ios.restart();
		m_ios.run();
		l.lock();
	}
}

void auto_load::on_scan(lt::error_code const& e)
{
	if (e) return;
	std::unique_lock<std::mutex> l(m_mutex);
	if (m_abort) return;

	// interval of 0 means disabled
	if (m_scan_interval == std::chrono::seconds(0)) return;

	std::string path = m_dir;
	bool const remove_files = m_remove_files;
	l.unlock();

	lt::error_code ec;
	std::vector<std::string> ents = list_dir(
		path,
		[](lt::string_view p) { return p.size() > 8 && p.substr(p.size() - 8) == ".torrent"; },
		ec
	);
	for (auto const& file : ents) {
		l.lock();
		bool const already_added = m_already_loaded.count(file) > 0;
		l.unlock();

		if (already_added) continue;

		std::string file_path = (std::filesystem::path(path) / file).string();
		lt::error_code tec;
		lt::add_torrent_params p = lt::load_torrent_file(file_path, tec, {});

		// assume the file isn't fully written yet.
		if (tec) continue;

		p.save_path = m_settings ? m_settings->get_str("save_path", "./downloads") : "./downloads";
		if (m_settings && m_settings->get_int("start_paused", 0))
			p.flags = (p.flags & ~lt::torrent_flags::auto_managed) | lt::torrent_flags::paused;
		else
			p.flags = (p.flags & ~lt::torrent_flags::paused) | lt::torrent_flags::auto_managed;
		// TODO: there should be a configuration option to
		// move the torrent file into a different directory
		if (m_alerts && remove_files) {
			l.lock();
			std::uintptr_t const id = ++m_next_pending_id;
			m_pending_files[id] = file_path;
			m_already_loaded.insert(file);
			l.unlock();
			p.userdata = lt::client_data_t(reinterpret_cast<void*>(id));
		} else if (remove_files) {
			std::error_code fec;
			std::filesystem::remove(file_path, fec);
		} else {
			l.lock();
			m_already_loaded.insert(file);
			l.unlock();
		}

		m_ses.async_add_torrent(std::move(p));
	}

	l.lock();
	std::chrono::seconds interval = m_scan_interval;
	l.unlock();

	// interval of 0 means disabled
	if (interval == std::chrono::seconds(0)) return;

	m_timer.expires_after(interval);
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));
}

void auto_load::handle_alert(lt::alert const* a)
{
	auto const* at = lt::alert_cast<lt::add_torrent_alert>(a);
	if (at == nullptr) return;

	void* const raw = at->params.userdata.get<void>();
	if (raw == nullptr) return;
	std::uintptr_t const id = reinterpret_cast<std::uintptr_t>(raw);

	std::unique_lock<std::mutex> l(m_mutex);
	auto const it = m_pending_files.find(id);
	if (it == m_pending_files.end()) return;
	std::filesystem::path const file_path = std::move(it->second);
	m_pending_files.erase(it);
	std::string const filename = file_path.filename().string();

	if (at->error) {
		m_already_loaded.erase(filename);
		return;
	}

	bool const should_remove = m_remove_files;
	l.unlock();

	if (should_remove) {
		std::error_code fec;
		std::filesystem::remove(file_path, fec);
		if (!fec) {
			l.lock();
			m_already_loaded.erase(filename);
		}
	}
}

} // namespace ltweb
