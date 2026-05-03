/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "sqlite_user_account.hpp"

#include <sqlite3.h>
#include <openssl/rand.h>

#include <termios.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace ltweb;

namespace {

// Schema-aligned constants. iterations is stored per-row, so this can
// change later without breaking existing rows; 600k matches OWASP's
// 2026 guidance for PBKDF2-SHA256 and is also the default in
// sqlite_user_account.cpp.
constexpr int salt_len = 16;
constexpr int hash_len = 32;
constexpr int iterations = 600000;

// Read a line from stdin with terminal echo disabled. Returns the
// entered string with the trailing newline stripped, or an empty
// string on read failure.
std::string read_password_from_tty()
{
	termios original;
	bool restore = false;
	if (tcgetattr(STDIN_FILENO, &original) == 0) {
		termios silent = original;
		silent.c_lflag &= ~ECHO;
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &silent) == 0) restore = true;
	}

	char buf[1024];
	char* got = std::fgets(buf, sizeof(buf), stdin);

	if (restore) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
	std::fputc('\n', stderr);

	if (got == nullptr) return {};

	std::size_t len = std::strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		--len;
	return std::string(buf, len);
}

void usage()
{
	std::fprintf(
		stderr,
		"usage:\n"
		"  add_user <username> <group-number> [db-path]\n"
		"\n"
		"  Reads the password from stdin (with echo disabled).\n"
		"  db-path defaults to ./users.db\n"
		"  Group numbers may not be negative.\n"
		"  If the user already exists, the row is replaced.\n"
	);
}

} // anonymous namespace

int main(int argc, char* argv[])
{
	if (argc < 3 || argc > 4) {
		usage();
		return 1;
	}

	char const* username = argv[1];
	int const group_id = std::atoi(argv[2]);
	char const* db_path = argc >= 4 ? argv[3] : "./users.db";

	if (group_id < 0) {
		std::fprintf(stderr, "group number may not be negative\n");
		return 1;
	}

	std::fprintf(stderr, "enter password for %s: ", username);
	std::fflush(stderr);
	std::string password = read_password_from_tty();
	if (password.empty()) {
		std::fprintf(stderr, "no password entered\n");
		return 1;
	}

	std::array<unsigned char, salt_len> salt;
	if (RAND_bytes(salt.data(), int(salt.size())) != 1) {
		std::fprintf(stderr, "failed to generate salt\n");
		return 1;
	}

	std::array<unsigned char, hash_len> hash;
	if (!pbkdf2_hmac_sha256(password, salt, iterations, hash)) {
		std::fprintf(stderr, "failed to hash password\n");
		return 1;
	}

	// sqlite3_open creates the file if it does not exist.
	sqlite3* db = nullptr;
	int rc = sqlite3_open(db_path, &db);
	if (rc != SQLITE_OK) {
		std::fprintf(
			stderr, "failed to open %s: %s\n", db_path, db ? sqlite3_errmsg(db) : "open failed"
		);
		if (db) sqlite3_close(db);
		return 1;
	}

	// Same DDL sqlite_user_account uses on first run.
	char* err = nullptr;
	rc = sqlite3_exec(
		db,
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
		std::fprintf(stderr, "failed to create schema: %s\n", err ? err : sqlite3_errmsg(db));
		if (err) sqlite3_free(err);
		sqlite3_close(db);
		return 1;
	}

	// INSERT OR REPLACE handles both create and update via the
	// PRIMARY KEY conflict resolution.
	sqlite3_stmt* stmt = nullptr;
	rc = sqlite3_prepare_v2(
		db, "INSERT OR REPLACE INTO users VALUES (?, ?, ?, ?, ?);", -1, &stmt, nullptr
	);
	if (rc != SQLITE_OK) {
		std::fprintf(stderr, "prepare failed: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 1;
	}

	sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
	sqlite3_bind_blob(stmt, 2, hash.data(), int(hash.size()), SQLITE_TRANSIENT);
	sqlite3_bind_blob(stmt, 3, salt.data(), int(salt.size()), SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, iterations);
	sqlite3_bind_int(stmt, 5, group_id);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		std::fprintf(stderr, "step failed: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return 1;
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	std::fprintf(stderr, "user '%s' added to %s in group %d\n", username, db_path, group_id);
	return 0;
}
