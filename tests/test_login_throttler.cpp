/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE login_throttler
#include <boost/test/included/unit_test.hpp>

#include "login_throttler.hpp"

#include <boost/asio/ip/address.hpp>

#include <chrono>
#include <thread>

using namespace ltweb;
using boost::asio::ip::make_address;
using namespace std::chrono_literals;

namespace {

// All time-based tests use millisecond windows so the suite runs in
// well under a second. Production defaults are seconds-scale.
constexpr auto default_window = 200ms;
constexpr auto default_block = 200ms;

login_throttler make_throttler(
	std::size_t max_failures = 3,
	std::chrono::milliseconds window = default_window,
	std::chrono::milliseconds block = default_block,
	std::size_t max_entries = 1000
)
{
	return login_throttler(max_failures, window, block, max_entries);
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(single_failure_does_not_block)
{
	auto t = make_throttler();
	auto const ip = make_address("10.0.0.1");
	t.record(ip, false);
	BOOST_TEST(t.blocked_for(ip).count() == 0);
}

BOOST_AUTO_TEST_CASE(threshold_failures_block)
{
	auto t = make_throttler(3);
	auto const ip = make_address("10.0.0.1");
	t.record(ip, false);
	t.record(ip, false);
	t.record(ip, false);
	BOOST_TEST(t.blocked_for(ip).count() > 0);
}

BOOST_AUTO_TEST_CASE(failures_outside_window_do_not_block)
{
	auto t = make_throttler(3, 50ms);
	auto const ip = make_address("10.0.0.1");
	t.record(ip, false);
	t.record(ip, false);
	std::this_thread::sleep_for(100ms);
	t.record(ip, false);
	// only the most recent failure is within the window so we are
	// well below the threshold of 3.
	BOOST_TEST(t.blocked_for(ip).count() == 0);
}

BOOST_AUTO_TEST_CASE(success_clears_state)
{
	auto t = make_throttler(3);
	auto const ip = make_address("10.0.0.1");
	t.record(ip, false);
	t.record(ip, false);
	t.record(ip, true);
	BOOST_TEST(t.size_v4() == std::size_t(0));

	// after a clear, two more failures must not trigger a block
	// (the budget reset)
	t.record(ip, false);
	t.record(ip, false);
	BOOST_TEST(t.blocked_for(ip).count() == 0);
}

BOOST_AUTO_TEST_CASE(block_expires_after_block_duration)
{
	auto t = make_throttler(3, 1s, 50ms);
	auto const ip = make_address("10.0.0.1");
	t.record(ip, false);
	t.record(ip, false);
	t.record(ip, false);
	BOOST_TEST(t.blocked_for(ip).count() > 0);

	std::this_thread::sleep_for(150ms);
	BOOST_TEST(t.blocked_for(ip).count() == 0);
}

BOOST_AUTO_TEST_CASE(same_v4_24_shares_entry)
{
	// /24 prefix is hardcoded
	auto t = make_throttler(3);
	auto const a = make_address("10.0.0.1");
	auto const b = make_address("10.0.0.5");
	auto const c = make_address("10.0.0.250");
	t.record(a, false);
	t.record(b, false);
	t.record(c, false);
	// any IP in the /24 should report blocked
	BOOST_TEST(t.blocked_for(make_address("10.0.0.99")).count() > 0);
	BOOST_TEST(t.size_v4() == std::size_t(1));
}

BOOST_AUTO_TEST_CASE(different_v4_24_do_not_share)
{
	auto t = make_throttler(3);
	t.record(make_address("10.0.0.1"), false);
	t.record(make_address("10.0.1.1"), false);
	t.record(make_address("10.0.2.1"), false);
	// each in its own /24 -> three distinct entries, none blocked
	BOOST_TEST(t.size_v4() == std::size_t(3));
	BOOST_TEST(t.blocked_for(make_address("10.0.0.1")).count() == 0);
	BOOST_TEST(t.blocked_for(make_address("10.0.1.1")).count() == 0);
	BOOST_TEST(t.blocked_for(make_address("10.0.2.1")).count() == 0);
}

BOOST_AUTO_TEST_CASE(same_v6_64_shares_entry)
{
	auto t = make_throttler(3);
	auto const a = make_address("2001:db8::1");
	auto const b = make_address("2001:db8::ffff:ffff:ffff:ffff");
	t.record(a, false);
	t.record(a, false);
	t.record(b, false);
	BOOST_TEST(t.blocked_for(make_address("2001:db8::42")).count() > 0);
	BOOST_TEST(t.size_v6() == std::size_t(1));
}

BOOST_AUTO_TEST_CASE(v4_mapped_v6_normalizes_to_v4)
{
	auto t = make_throttler(3);
	t.record(make_address("1.2.3.4"), false);
	t.record(make_address("1.2.3.5"), false);
	// IPv4-mapped IPv6 of the same /24 should land in the same v4
	// entry, not a separate v6 entry.
	t.record(make_address("::ffff:1.2.3.6"), false);
	BOOST_TEST(t.size_v4() == std::size_t(1));
	BOOST_TEST(t.size_v6() == std::size_t(0));
	BOOST_TEST(t.blocked_for(make_address("1.2.3.99")).count() > 0);
}

BOOST_AUTO_TEST_CASE(v4_and_v6_tables_are_independent)
{
	auto t = make_throttler(3);
	t.record(make_address("10.0.0.1"), false);
	t.record(make_address("10.0.0.1"), false);
	t.record(make_address("10.0.0.1"), false);
	BOOST_TEST(t.blocked_for(make_address("10.0.0.1")).count() > 0);

	// a v6 IP with no recorded failures must not be blocked just
	// because some unrelated v4 network is.
	BOOST_TEST(t.blocked_for(make_address("2001:db8::1")).count() == 0);
	BOOST_TEST(t.size_v4() == std::size_t(1));
	BOOST_TEST(t.size_v6() == std::size_t(0));
}

BOOST_AUTO_TEST_CASE(table_cap_enforced)
{
	// max_entries=2: insert many distinct /24s with one failure
	// each. The table must never exceed 2.
	login_throttler t(3, default_window, default_block, 2);
	t.seed_for_testing(42);
	for (int i = 0; i < 100; ++i) {
		std::string ip = "10.0." + std::to_string(i) + ".1";
		t.record(make_address(ip), false);
		BOOST_TEST(t.size_v4() <= std::size_t(2));
	}
}

BOOST_AUTO_TEST_CASE(cap_hit_prunes_expired_before_random_eviction)
{
	// max_entries=2, short window. Fill the table with two entries
	// then let them age out. A new insert at the cap must reclaim
	// the expired entries via prune rather than randomly evicting
	// (which would only succeed 50% of the time).
	login_throttler t(3, 50ms, default_block, 2);
	t.record(make_address("10.0.0.1"), false);
	t.record(make_address("10.0.1.1"), false);
	BOOST_TEST(t.size_v4() == std::size_t(2));

	std::this_thread::sleep_for(100ms);

	// At this point the two entries are stale but still occupy
	// slots. A new insert should succeed deterministically by
	// pruning, regardless of the RNG.
	t.record(make_address("10.0.2.1"), false);
	BOOST_TEST(t.size_v4() == std::size_t(1));
}

BOOST_AUTO_TEST_CASE(prune_expired_drops_aged_entries)
{
	auto t = make_throttler(3, 50ms, default_block);
	t.record(make_address("10.0.0.1"), false);
	t.record(make_address("10.0.1.1"), false);
	t.record(make_address("2001:db8::1"), false);
	BOOST_TEST(t.size_v4() == std::size_t(2));
	BOOST_TEST(t.size_v6() == std::size_t(1));

	std::this_thread::sleep_for(100ms);
	t.prune_expired();
	BOOST_TEST(t.size_v4() == std::size_t(0));
	BOOST_TEST(t.size_v6() == std::size_t(0));
}

BOOST_AUTO_TEST_CASE(blocked_for_lazy_drops_expired_entry)
{
	// blocked_for must clean up entries whose only state was a
	// stale failure (no block, failures outside the window).
	auto t = make_throttler(3, 50ms, default_block);
	t.record(make_address("10.0.0.1"), false);
	BOOST_TEST(t.size_v4() == std::size_t(1));
	std::this_thread::sleep_for(100ms);
	BOOST_TEST(t.blocked_for(make_address("10.0.0.1")).count() == 0);
	BOOST_TEST(t.size_v4() == std::size_t(0));
}
