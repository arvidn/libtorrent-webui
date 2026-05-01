/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_SQLITE_USER_ACCOUNT_HPP
#define LTWEB_SQLITE_USER_ACCOUNT_HPP

#include "auth_interface.hpp"

#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct sqlite3;

namespace ltweb {

// SQLite-backed user_account. The database file is opened on
// construction; the schema is created if missing:
//
//   username   TEXT PRIMARY KEY
//   password   BLOB    -- 32-byte PBKDF2-HMAC-SHA256 output
//   salt       BLOB    -- 16-byte CSPRNG salt
//   iterations INTEGER -- iteration count used at hash time
//   group_id   INTEGER
//
struct sqlite_user_account : user_account {
	explicit sqlite_user_account(std::string sqlite_path);
	~sqlite_user_account();

	sqlite_user_account(sqlite_user_account const&) = delete;
	sqlite_user_account& operator=(sqlite_user_account const&) = delete;

	std::optional<int> verify(std::string_view username, std::string_view password) const override;

private:
	mutable std::mutex m_mutex;
	sqlite3* m_db = nullptr;
};

// Computes PBKDF2-HMAC-SHA256(pwd, salt, iterations) into out.
// Returns false on OpenSSL failure (effectively never). Exposed for
// provisioning tools that need to insert pre-hashed rows.
bool pbkdf2_hmac_sha256(
	std::string_view pwd,
	std::span<unsigned char const> salt,
	int iterations,
	std::span<unsigned char> out
);

} // namespace ltweb

#endif
