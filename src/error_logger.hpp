/*

Copyright (c) 2013, 2017, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_ERROR_LOGGER_HPP
#define LTWEB_ERROR_LOGGER_HPP

#include "alert_observer.hpp"
#include <string>
#include <stdio.h> // for FILE

namespace ltweb {

struct alert_handler;

struct error_logger : alert_observer {
	error_logger(alert_handler* alerts, std::string const& log_file, bool redirect_stderr);
	~error_logger();

	void handle_alert(lt::alert const* a);

private:
	FILE* m_file;
	alert_handler* m_alerts;
};

} // namespace ltweb

#endif
