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
#include <array>
#include "libtorrent/fwd.hpp"
#include "libtorrent/alert.hpp" // for alert_category_t
#include "libtorrent/alert_types.hpp" // for num_alert_types
#include "libtorrent/span.hpp"

namespace ltweb {

struct alert_observer;

// TODO: rename to alert_dispatcher
struct TORRENT_EXPORT alert_handler {
	alert_handler(lt::session& ses);

	// subscribes `o` to a set of alert types, given as a list of alert
	// classes (each must expose the `alert_type` and `static_category`
	// static constants the libtorrent alert hierarchy provides). The OR of
	// every subscriber's `static_category` is pushed to the session as
	// `settings_pack::alert_mask`, so callers no longer have to enable
	// alert categories manually.
	template <typename... Alerts>
	void subscribe(alert_observer* o, int flags = 0)
	{
		constexpr std::array<int, sizeof...(Alerts)> types{Alerts::alert_type...};
		constexpr lt::alert_category_t cats =
			(lt::alert_category_t{} | ... | Alerts::static_category);
		subscribe_impl(lt::span<int const>{types}, o, flags, cats);
	}

	void dispatch_alerts(std::vector<lt::alert*>& alerts) const;
	void dispatch_alerts(lt::time_duration max_wait) const;
	void unsubscribe(alert_observer* o);

	void abort();

private:
	void subscribe_impl(
		lt::span<int const> types, alert_observer* o, int flags, lt::alert_category_t cats
	);

	std::array<std::vector<alert_observer*>, lt::num_alert_types> m_observers;

	// running OR of every subscriber's category bits. pushed to the
	// session via apply_settings() whenever it grows.
	lt::alert_category_t m_subscribed_categories{};

	// when set to true, all outstanding (std::future-based) subscriptions
	// are cancelled, and new such subscriptions are disabled, by failing
	// immediately
	bool m_abort;

	lt::session& m_ses;
};

} // namespace ltweb

#endif // LTWEB_ALERT_HANDLER_HPP_INCLUDED
