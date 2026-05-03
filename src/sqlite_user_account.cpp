/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "sqlite_user_account.hpp"

#include <sqlite3.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace ltweb {

bool pbkdf2_hmac_sha256(
	std::string_view pwd,
	std::span<unsigned char const> salt,
	int iterations,
	std::span<unsigned char> out
)
{
	return PKCS5_PBKDF2_HMAC(
			   pwd.data(),
			   int(pwd.size()),
			   salt.data(),
			   int(salt.size()),
			   iterations,
			   EVP_sha256(),
			   int(out.size()),
			   out.data()
		   )
		== 1;
}

sqlite_user_account::sqlite_user_account(std::string sqlite_path)
{
	int rc = sqlite3_open(sqlite_path.c_str(), &m_db);
	if (rc != SQLITE_OK) {
		std::fprintf(
			stderr,
			"sqlite_user_account: failed to open user database [%s]: %s\n",
			sqlite_path.c_str(),
			m_db ? sqlite3_errmsg(m_db) : "open failed"
		);
		if (m_db) {
			sqlite3_close(m_db);
			m_db = nullptr;
		}
		return;
	}

	char* err = nullptr;
	rc = sqlite3_exec(
		m_db,
		"CREATE TABLE IF NOT EXISTS users("
		"username TEXT PRIMARY KEY NOT NULL,"
		"password BLOB NOT NULL,"
		"salt BLOB NOT NULL,"
		"iterations INTEGER NOT NULL,"
		"group_id INTEGER NOT NULL);",
		nullptr,
		nullptr,
		&err
	);
	if (rc != SQLITE_OK) {
		std::fprintf(
			stderr,
			"sqlite_user_account: failed to ensure users table: %s\n",
			err ? err : sqlite3_errmsg(m_db)
		);
		if (err) sqlite3_free(err);
		if (m_db) {
			sqlite3_close(m_db);
			m_db = nullptr;
		}
	}
}

sqlite_user_account::~sqlite_user_account()
{
	if (m_db) sqlite3_close(m_db);
}

std::optional<int>
sqlite_user_account::verify(std::string_view username, std::string_view password) const
{
	if (m_db == nullptr) return std::nullopt;

	std::array<unsigned char, 32> stored_hash;
	std::array<unsigned char, 16> salt;
	int iterations = 0;
	int group_id = -1;

	{
		std::lock_guard<std::mutex> l(m_mutex);

		sqlite3_stmt* stmt = nullptr;
		int rc = sqlite3_prepare_v2(
			m_db,
			"SELECT password, salt, iterations, group_id "
			"FROM users WHERE username = ?;",
			-1,
			&stmt,
			nullptr
		);
		if (rc != SQLITE_OK) {
			std::fprintf(stderr, "sqlite_user_account: prepare failed: %s\n", sqlite3_errmsg(m_db));
			return std::nullopt;
		}

		sqlite3_bind_text(stmt, 1, username.data(), int(username.size()), SQLITE_TRANSIENT);

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return std::nullopt;
		}

		// Validate blob sizes against the schema before copying. A row
		// with a different shape is treated as malformed (auth failure),
		// not a server error.
		if (sqlite3_column_bytes(stmt, 0) != int(stored_hash.size())
			|| sqlite3_column_bytes(stmt, 1) != int(salt.size())) {
			sqlite3_finalize(stmt);
			return std::nullopt;
		}

		std::memcpy(stored_hash.data(), sqlite3_column_blob(stmt, 0), stored_hash.size());
		std::memcpy(salt.data(), sqlite3_column_blob(stmt, 1), salt.size());
		iterations = sqlite3_column_int(stmt, 2);
		group_id = sqlite3_column_int(stmt, 3);

		sqlite3_finalize(stmt);
	}

	if (iterations <= 0) return std::nullopt;

	std::array<unsigned char, 32> derived;
	if (!pbkdf2_hmac_sha256(password, salt, iterations, derived)) {
		return std::nullopt;
	}

	// Constant-time compare; plain memcmp is a timing leak.
	if (CRYPTO_memcmp(derived.data(), stored_hash.data(), stored_hash.size()) != 0)
		return std::nullopt;

	return group_id;
}

} // namespace ltweb
