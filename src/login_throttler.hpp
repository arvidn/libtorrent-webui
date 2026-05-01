/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_LOGIN_THROTTLER_HPP
#define LTWEB_LOGIN_THROTTLER_HPP

#include <boost/asio/ip/address.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <variant>

namespace ltweb {

// Per-network rate limiter for the login endpoint. Tracks failed
// login attempts and reports when an IP's enclosing network has
// exceeded the failure budget. Used by the login http_handler to
// short-circuit POST /login by redirecting back to the login form
// with the remaining wait time, before the expensive PBKDF2 verify.
//
// IPv4 traffic is grouped by /24 and IPv6 by /64. Per-network keys
// defeat trivial IP rotation, which is essentially free on IPv6
// where one host holds 2^64+ addresses. IPv4-mapped IPv6
// (::ffff:1.2.3.4) is unwrapped and tracked as the underlying v4 so
// a dual-stack listener does not give one host two budgets.
//
// Memory is bounded per table by max_entries. When full, a new
// network is admitted with 50% probability by evicting a random
// existing entry; otherwise the new attempt is untracked. The
// randomized policy means an attacker flooding fresh networks
// doubles their cost without being able to deterministically clobber
// a specific legitimate entry.
//
// Thread-safe; one instance is shared across the io_context thread
// pool. Lazy expiration on lookup keeps the tables churning without
// a background timer.
struct login_throttler {
	// /24 for v4 -> 3-byte prefix; /64 for v6 -> 8-byte prefix.
	using v4_key = std::array<unsigned char, 3>;
	using v6_key = std::array<unsigned char, 8>;

	// max_failures: failures within the window required to trigger
	//   a block.
	// window: sliding window over which failures are counted.
	// block_duration: how long a triggered block lasts.
	// max_entries: hard cap per table (v4 and v6 are bounded
	//   independently).
	explicit login_throttler(
		std::size_t max_failures = 5,
		std::chrono::milliseconds window = std::chrono::seconds(60),
		std::chrono::milliseconds block_duration = std::chrono::minutes(5),
		std::size_t max_entries = 10000
	);

	login_throttler(login_throttler const&) = delete;
	login_throttler& operator=(login_throttler const&) = delete;

	// If the IP's enclosing network is blocked, returns the
	// remaining block duration. Otherwise returns zero. Lazily
	// expires the entry.
	std::chrono::seconds blocked_for(boost::asio::ip::address const& ip) const;

	// Record the outcome of a login attempt. success=true clears
	// the network's record; success=false appends a failure
	// timestamp and may push the network into the blocked state.
	void record(boost::asio::ip::address const& ip, bool success);

	// Drop expired entries from both tables. Lazy cleanup happens
	// on every lookup, so this is only useful as an explicit
	// reclaim. No scheduler is provided.
	void prune_expired();

	// Number of currently stored entries (including any expired
	// ones not yet pruned). For tests and diagnostics.
	std::size_t size_v4() const;
	std::size_t size_v6() const;

	// Test-only: replace the RNG seed for reproducible eviction
	// behavior. Production code does not need to call this.
	void seed_for_testing(std::uint_fast32_t seed);

private:
	struct entry {
		std::deque<std::chrono::steady_clock::time_point> failures;
		std::chrono::steady_clock::time_point blocked_until{};
	};

	// Normalize an address to a canonical v4 or v6 prefix key. Any
	// IPv4-mapped IPv6 address is unwrapped to its v4 form first.
	static std::variant<v4_key, v6_key> normalize(boost::asio::ip::address const& ip);

	// Drop failures outside the window and clear the block if it
	// has expired (also clears residual failures, giving a clean
	// slate after serving the block).
	static void
	expire(entry& e, std::chrono::steady_clock::time_point now, std::chrono::milliseconds window);

	// Per-table operations. Templated so the v4 and v6 paths share
	// one definition. Definitions live in login_throttler.cpp; all
	// callers are in the same TU so explicit instantiation is not
	// required.
	template <class Map>
	std::chrono::seconds blocked_for_in(Map& table, typename Map::key_type const& key) const;

	template <class Map>
	void record_failure_in(Map& table, typename Map::key_type const& key);

	// Walk the table, drop entries whose state has fully aged out.
	// Caller must hold m_mutex. Used both by the public prune_expired
	// and by the eviction path so we can reclaim cheap space before
	// resorting to random eviction.
	template <class Map>
	static void prune_table_locked(
		Map& table, std::chrono::steady_clock::time_point now, std::chrono::milliseconds window
	);

	std::size_t m_max_failures;
	std::chrono::milliseconds m_window;
	std::chrono::milliseconds m_block_duration;
	std::size_t m_max_entries;
	mutable std::mutex m_mutex;
	mutable std::map<v4_key, entry> m_v4_table;
	mutable std::map<v6_key, entry> m_v6_table;
	mutable std::mt19937 m_rng;
};

} // namespace ltweb

#endif
