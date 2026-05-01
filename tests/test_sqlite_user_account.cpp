/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE sqlite_user_account
#include <boost/test/included/unit_test.hpp>

#include "sqlite_user_account.hpp"

#include <sqlite3.h>
#include <openssl/rand.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace ltweb;

namespace {

// RAII tempfile that creates a unique path under the system temp dir
// and removes the file on destruction. Each sqlite3_open(":memory:")
// produces a fresh DB, so seeding and verifying must share a real
// filesystem path.
struct tempfile {
	tempfile()
	{
		auto p = std::filesystem::temp_directory_path()
			/ ("ltweb_sqlite_user_test_" + std::to_string(std::rand()) + ".sqlite");
		path = p.string();
	}
	~tempfile()
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
	std::string path;
};

void seed_user(
	std::string const& db_path,
	std::string const& username,
	std::string const& password,
	int group_id,
	int iterations = 1000
)
{
	sqlite3* db = nullptr;
	BOOST_TEST_REQUIRE(sqlite3_open(db_path.c_str(), &db) == SQLITE_OK);

	BOOST_TEST_REQUIRE(
		sqlite3_exec(
			db,
			"CREATE TABLE IF NOT EXISTS users("
			"username TEXT PRIMARY KEY NOT NULL,"
			"password BLOB NOT NULL,"
			"salt BLOB NOT NULL,"
			"iterations INTEGER NOT NULL,"
			"group_id INTEGER NOT NULL);",
			nullptr,
			nullptr,
			nullptr
		)
		== SQLITE_OK
	);

	std::array<unsigned char, 16> salt;
	BOOST_TEST_REQUIRE(RAND_bytes(salt.data(), int(salt.size())) == 1);

	std::array<unsigned char, 32> hash;
	BOOST_TEST_REQUIRE(pbkdf2_hmac_sha256(password, salt, iterations, hash));

	sqlite3_stmt* stmt = nullptr;
	BOOST_TEST_REQUIRE(
		sqlite3_prepare_v2(
			db, "INSERT OR REPLACE INTO users VALUES (?, ?, ?, ?, ?);", -1, &stmt, nullptr
		)
		== SQLITE_OK
	);
	sqlite3_bind_text(stmt, 1, username.c_str(), int(username.size()), SQLITE_TRANSIENT);
	sqlite3_bind_blob(stmt, 2, hash.data(), int(hash.size()), SQLITE_TRANSIENT);
	sqlite3_bind_blob(stmt, 3, salt.data(), int(salt.size()), SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, iterations);
	sqlite3_bind_int(stmt, 5, group_id);
	BOOST_TEST_REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
	sqlite3_finalize(stmt);

	sqlite3_close(db);
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(pbkdf2_round_trip_is_deterministic)
{
	std::array<unsigned char, 16> salt{};
	for (std::size_t i = 0; i < salt.size(); ++i)
		salt[i] = static_cast<unsigned char>(i);

	std::array<unsigned char, 32> a{}, b{}, c{};
	BOOST_TEST(pbkdf2_hmac_sha256("password", salt, 1000, a));
	BOOST_TEST(pbkdf2_hmac_sha256("password", salt, 1000, b));
	BOOST_TEST(a == b);

	BOOST_TEST(pbkdf2_hmac_sha256("Password", salt, 1000, c));
	BOOST_TEST(a != c);
}

BOOST_AUTO_TEST_CASE(verify_unknown_user_returns_nullopt)
{
	tempfile db;
	sqlite_user_account accounts(db.path);
	BOOST_TEST(!accounts.verify("mallory", "anything").has_value());
}

BOOST_AUTO_TEST_CASE(verify_correct_password_returns_group)
{
	tempfile db;
	seed_user(db.path, "alice", "secret", 0);
	seed_user(db.path, "bob", "readonly", 1);

	sqlite_user_account accounts(db.path);

	auto a = accounts.verify("alice", "secret");
	BOOST_TEST(a.has_value());
	BOOST_TEST(*a == 0);

	auto b = accounts.verify("bob", "readonly");
	BOOST_TEST(b.has_value());
	BOOST_TEST(*b == 1);
}

BOOST_AUTO_TEST_CASE(verify_wrong_password_returns_nullopt)
{
	tempfile db;
	seed_user(db.path, "alice", "secret", 0);

	sqlite_user_account accounts(db.path);
	BOOST_TEST(!accounts.verify("alice", "wrong").has_value());
}

BOOST_AUTO_TEST_CASE(verify_case_sensitive_username)
{
	tempfile db;
	seed_user(db.path, "alice", "secret", 0);

	sqlite_user_account accounts(db.path);
	// "Alice" and "ALICE" should not match the seeded "alice" - the
	// schema uses the SQLite default (case-sensitive) collation.
	BOOST_TEST(!accounts.verify("Alice", "secret").has_value());
	BOOST_TEST(!accounts.verify("ALICE", "secret").has_value());
}

BOOST_AUTO_TEST_CASE(verify_returns_stored_group_id)
{
	tempfile db;
	seed_user(db.path, "ghost", "boo", 99);

	sqlite_user_account accounts(db.path);
	auto g = accounts.verify("ghost", "boo");
	BOOST_TEST(g.has_value());
	BOOST_TEST(*g == 99);
	// Note: the login class is responsible for rejecting out-of-range
	// group_ids; sqlite_user_account just returns whatever is stored.
}
