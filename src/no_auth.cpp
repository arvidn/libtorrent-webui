/*

Copyright (c) 2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "no_auth.hpp"

namespace ltweb {

std::optional<int> no_auth::verify(std::string_view, std::string_view) const { return 0; }

} // namespace ltweb
