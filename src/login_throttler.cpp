/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "login_throttler.hpp"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>

#include <iterator>

namespace ltweb {

login_throttler::login_throttler(
	std::size_t max_failures,
	std::chrono::milliseconds window,
	std::chrono::milliseconds block_duration,
	std::size_t max_entries
)
	: m_max_failures(max_failures)
	, m_window(window)
	, m_block_duration(block_duration)
	, m_max_entries(max_entries)
	, m_rng(std::random_device{}())
{
}

namespace {

bool v6_is_v4_mapped(boost::asio::ip::address_v6::bytes_type const& b)
{
	for (int i = 0; i < 10; ++i)
		if (b[i] != 0) return false;
	return b[10] == 0xff && b[11] == 0xff;
}

login_throttler::v4_key v4_prefix(boost::asio::ip::address_v4::bytes_type const& b)
{
	return login_throttler::v4_key{{b[0], b[1], b[2]}};
}

login_throttler::v4_key v4_prefix_from_mapped(boost::asio::ip::address_v6::bytes_type const& b)
{
	return login_throttler::v4_key{{b[12], b[13], b[14]}};
}

login_throttler::v6_key v6_prefix(boost::asio::ip::address_v6::bytes_type const& b)
{
	return login_throttler::v6_key{{b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]}};
}

} // anonymous namespace

std::variant<login_throttler::v4_key, login_throttler::v6_key>
login_throttler::normalize(boost::asio::ip::address const& ip)
{
	if (ip.is_v6()) {
		auto const v6_bytes = ip.to_v6().to_bytes();
		if (v6_is_v4_mapped(v6_bytes)) return v4_prefix_from_mapped(v6_bytes);
		return v6_prefix(v6_bytes);
	}

	return v4_prefix(ip.to_v4().to_bytes());
}

void login_throttler::expire(
	entry& e, std::chrono::steady_clock::time_point now, std::chrono::milliseconds window
)
{
	while (!e.failures.empty() && now - e.failures.front() >= window)
		e.failures.pop_front();

	if (e.blocked_until != std::chrono::steady_clock::time_point{} && now >= e.blocked_until) {
		e.blocked_until = std::chrono::steady_clock::time_point{};
		e.failures.clear();
	}
}

template <class Map>
std::chrono::seconds
login_throttler::blocked_for_in(Map& table, typename Map::key_type const& key) const
{
	auto it = table.find(key);
	if (it == table.end()) return std::chrono::seconds(0);

	auto const now = std::chrono::steady_clock::now();
	expire(it->second, now, m_window);

	if (it->second.blocked_until == std::chrono::steady_clock::time_point{}) {
		// Not blocked. If the entry is now empty, drop it.
		if (it->second.failures.empty()) table.erase(it);
		return std::chrono::seconds(0);
	}

	// Round up so a 100ms remaining block reports as 1s in the
	// "try again in N seconds" error message rather than 0s.
	return std::chrono::ceil<std::chrono::seconds>(it->second.blocked_until - now);
}

template <class Map>
void login_throttler::prune_table_locked(
	Map& table, std::chrono::steady_clock::time_point now, std::chrono::milliseconds window
)
{
	for (auto i = table.begin(); i != table.end();) {
		expire(i->second, now, window);
		if (i->second.failures.empty()
			&& i->second.blocked_until == std::chrono::steady_clock::time_point{})
			i = table.erase(i);
		else
			++i;
	}
}

template <class Map>
void login_throttler::record_failure_in(Map& table, typename Map::key_type const& key)
{
	auto const now = std::chrono::steady_clock::now();

	auto it = table.find(key);
	if (it == table.end()) {
		// Need a new entry. Enforce the per-table cap.
		if (table.size() >= m_max_entries) {
			// First try to reclaim space cheaply by dropping
			// fully-expired entries. Under flood pressure this
			// finds nothing (all entries are recent) and we fall
			// through to random eviction. Under mixed traffic it
			// frees stale entries so live ones do not get evicted.
			prune_table_locked(table, now, m_window);
		}
		if (table.size() >= m_max_entries) {
			// 50/50: evict a random existing entry, or drop the
			// new attempt without tracking. Random eviction means
			// an attacker flooding fresh networks pays double and
			// has only 1/(2*max_entries) probability per attempt
			// of evicting any specific legitimate entry.
			if ((m_rng() & 1u) == 0u) {
				return;
			}
			std::uniform_int_distribution<std::size_t> dist(0, table.size() - 1);
			auto victim = table.begin();
			std::advance(victim, dist(m_rng));
			table.erase(victim);
		}
		it = table.emplace(key, entry{}).first;
	}

	auto& e = it->second;
	expire(e, now, m_window);

	// Cap deque length at m_max_failures so the table cannot grow
	// per-entry under sustained pressure.
	if (e.failures.size() == m_max_failures) e.failures.pop_front();
	e.failures.push_back(now);

	if (e.failures.size() >= m_max_failures) e.blocked_until = now + m_block_duration;
}

std::chrono::seconds login_throttler::blocked_for(boost::asio::ip::address const& ip) const
{
	auto const key = normalize(ip);

	std::lock_guard<std::mutex> l(m_mutex);
	if (auto const* k4 = std::get_if<v4_key>(&key)) return blocked_for_in(m_v4_table, *k4);
	return blocked_for_in(m_v6_table, std::get<v6_key>(key));
}

void login_throttler::record(boost::asio::ip::address const& ip, bool success)
{
	auto const key = normalize(ip);

	std::lock_guard<std::mutex> l(m_mutex);

	if (success) {
		if (auto const* k4 = std::get_if<v4_key>(&key))
			m_v4_table.erase(*k4);
		else
			m_v6_table.erase(std::get<v6_key>(key));
		return;
	}

	if (auto const* k4 = std::get_if<v4_key>(&key))
		record_failure_in(m_v4_table, *k4);
	else
		record_failure_in(m_v6_table, std::get<v6_key>(key));
}

void login_throttler::prune_expired()
{
	auto const now = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> l(m_mutex);
	prune_table_locked(m_v4_table, now, m_window);
	prune_table_locked(m_v6_table, now, m_window);
}

std::size_t login_throttler::size_v4() const
{
	std::lock_guard<std::mutex> l(m_mutex);
	return m_v4_table.size();
}

std::size_t login_throttler::size_v6() const
{
	std::lock_guard<std::mutex> l(m_mutex);
	return m_v6_table.size();
}

void login_throttler::seed_for_testing(std::uint_fast32_t seed)
{
	std::lock_guard<std::mutex> l(m_mutex);
	m_rng.seed(seed);
}

} // namespace ltweb
