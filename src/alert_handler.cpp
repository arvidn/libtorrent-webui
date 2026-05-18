/*

Copyright (c) 2014, 2017, 2019, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"

#include <algorithm>
#include <mutex>
#include <memory>
#include <condition_variable>

#include "alert_handler.hpp"
#include "alert_observer.hpp"

namespace ltweb {

alert_handler::alert_handler(lt::session& ses)
	: m_abort(false)
	, m_ses(ses)
{
}

void alert_handler::dispatch_alerts(std::vector<lt::alert*>& alerts) const
{
	for (lt::alert* a : alerts) {
		int const type = a->type();

		// copy this vector since handlers may unsubscribe while we're looping
		std::vector<alert_observer*> alert_dispatchers = m_observers[type];
		for (auto* h : alert_dispatchers)
			h->handle_alert(a);
	}
	alerts.clear();
}

void alert_handler::dispatch_alerts(lt::time_duration const max_wait) const
{
	std::vector<lt::alert*> alert_queue;
	if (m_ses.wait_for_alert(max_wait) == nullptr) return;

	m_ses.pop_alerts(&alert_queue);
	dispatch_alerts(alert_queue);
}

void alert_handler::unsubscribe(alert_observer* o)
{
	for (int i = 0; i < o->num_types; ++i) {
		int const type = o->types[i];
		if (type == 0) continue;
		TORRENT_ASSERT(type >= 0);
		TORRENT_ASSERT(type < int(m_observers.size()));
		if (type < 0 || type >= int(m_observers.size())) continue;
		auto& alert_observers = m_observers[type];
		auto j = std::find(alert_observers.begin(), alert_observers.end(), o);
		if (j != alert_observers.end()) alert_observers.erase(j);
	}
	o->num_types = 0;
}

void alert_handler::subscribe_impl(
	lt::span<int const> const types,
	alert_observer* o,
	int const flags,
	lt::alert_category_t const cats
)
{
	o->types.fill(0);
	o->flags = flags;
	for (int const type : types) {
		if (type == 0) break;

		// only subscribe once per observer per type
		if (std::count(o->types.data(), o->types.data() + o->num_types, type) > 0) continue;

		TORRENT_ASSERT(type >= 0);
		TORRENT_ASSERT(type < int(m_observers.size()));
		if (type < 0 || type >= int(m_observers.size())) continue;

		o->types[o->num_types++] = type;
		m_observers[type].push_back(o);
		TORRENT_ASSERT(o->num_types < 64);
	}

	lt::alert_category_t const new_mask = m_subscribed_categories | cats;
	if (new_mask != m_subscribed_categories) {
		m_subscribed_categories = new_mask;
		lt::settings_pack p;
		p.set_int(lt::settings_pack::alert_mask, new_mask);
		m_ses.apply_settings(std::move(p));
	}
}

void alert_handler::abort() { m_abort = true; }

} // namespace ltweb
