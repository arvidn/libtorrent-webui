/*

Copyright (c) 2017-2019, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#ifndef LTWEB_ALERT_OBSERVER_HPP_INCLUDED
#define LTWEB_ALERT_OBSERVER_HPP_INCLUDED

#include <cstdint>
#include <array>

#include "libtorrent/fwd.hpp"

namespace ltweb {

struct alert_observer {
	friend struct alert_handler;

	alert_observer() = default;
	alert_observer(alert_observer const&) = delete;

	virtual void handle_alert(lt::alert const* a) = 0;

private:
	std::array<std::uint8_t, 64> types;
	int num_types = 0;
	int flags = 0;
};

} // namespace ltweb

#endif // LTWEB_ALERT_OBSERVER_HPP_INCLUDED
