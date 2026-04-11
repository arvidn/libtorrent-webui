/*

Copyright (c) 2014, 2017, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_ALERT_HANDLER_HPP_INCLUDED
#define LTWEB_ALERT_HANDLER_HPP_INCLUDED

#include <vector>
#include <memory>
#include <mutex>
#include <deque>
#include <future>
#include "libtorrent/fwd.hpp"
#include "libtorrent/alert_types.hpp" // for num_alert_types

namespace ltweb
{

struct alert_observer;

// TODO: rename to alert_dispatcher
struct TORRENT_EXPORT alert_handler
{
	alert_handler(lt::session& ses);

	// TODO 2: move the responsibility of picking which
	// alert types to subscribe to to the observer
	// TODO 3: make subscriptions automatically enable
	// the corresponding category of alerts in the session somehow
	// TODO: 3 make this a variadic template
	void subscribe(alert_observer* o, int flags = 0, ...);
	void dispatch_alerts(std::vector<lt::alert*>& alerts) const;
	void dispatch_alerts(lt::time_duration max_wait) const;
	void unsubscribe(alert_observer* o);

	void abort();

private:

	void subscribe_impl(int const* type_list, int num_types, alert_observer* o, int flags);

	std::array<std::vector<alert_observer*>, lt::num_alert_types> m_observers;

	// when set to true, all outstanding (std::future-based) subscriptions
	// are cancelled, and new such subscriptions are disabled, by failing
	// immediately
	bool m_abort;

	lt::session& m_ses;
};

}

#endif // LTWEB_ALERT_HANDLER_HPP_INCLUDED

