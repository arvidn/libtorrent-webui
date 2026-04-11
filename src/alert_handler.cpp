/*

Copyright (c) 2014, 2017, 2019, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"

#include <algorithm>
#include <mutex>
#include <cstdarg>
#include <memory>
#include <condition_variable>

#include "alert_handler.hpp"
#include "alert_observer.hpp"

namespace ltweb
{

	alert_handler::alert_handler(lt::session& ses)
		: m_abort(false)
		, m_ses(ses)
	{}

	void alert_handler::subscribe(alert_observer* o, int const flags, ...)
	{
		std::array<int, 64> types;
		types.fill(0);
		va_list l;
		va_start(l, flags);
		int t = va_arg(l, int);
		int i = 0;
		while (t != 0 && i < 64)
		{
			types[i] = t;
			++i;
			t = va_arg(l, int);
		}
		va_end(l);
		subscribe_impl(types.data(), i, o, flags);
	}

	void alert_handler::dispatch_alerts(std::vector<lt::alert*>& alerts) const
	{
		for (lt::alert* a : alerts)
		{
			int const type = a->type();

			// copy this vector since handlers may unsubscribe while we're looping
			std::vector<alert_observer*> alert_dispatchers = m_observers[type];
			for (auto* h : alert_dispatchers)
				h->handle_alert(a);
		}
		alerts.clear();
	}

	void alert_handler::dispatch_alerts() const
	{
		std::vector<lt::alert*> alert_queue;
		m_ses.pop_alerts(&alert_queue);
		dispatch_alerts(alert_queue);
	}

	void alert_handler::unsubscribe(alert_observer* o)
	{
		for (int i = 0; i < o->num_types; ++i)
		{
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

	// TODO: use span<int const>
	void alert_handler::subscribe_impl(int const* type_list, int const num_types
		, alert_observer* o, int const flags)
	{
		o->types.fill(0);
		o->flags = flags;
		for (int i = 0; i < num_types; ++i)
		{
			int const type = type_list[i];
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
	}

	void alert_handler::abort()
	{
		m_abort = true;
	}

}

