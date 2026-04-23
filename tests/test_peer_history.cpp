/*

Copyright (c) 2026, Muhammad Hassan Raza
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define BOOST_TEST_MODULE peer_history
#include <boost/test/included/unit_test.hpp>

#include "peer_history.hpp"

#include <libtorrent/address.hpp>
#include <libtorrent/peer_info.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace {

std::uint64_t const full_mask = (std::uint64_t(1) << ltweb::peer_history_entry::num_fields) - 1;

lt::peer_info make_peer(int const tag)
{
	lt::peer_info pi;
	pi.flags = lt::peer_info::interesting | lt::peer_info::supports_extensions;
	pi.source = lt::peer_info::tracker;
	pi.read_state = lt::peer_info::bw_network;
	pi.write_state = lt::peer_info::bw_idle;
	pi.client = "client-" + std::to_string(tag);
	pi.num_pieces = 10 + tag;
	pi.pending_disk_bytes = 1000 + tag;
	pi.pending_disk_read_bytes = 2000 + tag;
	pi.num_hashfails = tag;
	pi.payload_down_speed = 3000 + tag;
	pi.payload_up_speed = 4000 + tag;
	pi.download_queue_length = 5 + tag;
	pi.upload_queue_length = 6 + tag;
	pi.timed_out_requests = 7 + tag;
	pi.progress_ppm = 10000 + tag;
	pi.total_download = 50000 + tag;
	pi.total_upload = 60000 + tag;

	std::memset(pi.pid.data(), tag, static_cast<std::size_t>(lt::peer_id::size()));
	pi.pieces = lt::typed_bitfield<lt::piece_index_t>(16);
	pi.pieces.set_bit(lt::piece_index_t(tag % 8));
	pi.pieces.set_bit(lt::piece_index_t((tag % 8) + 8));

	pi.set_endpoints(
		lt::tcp::endpoint(lt::make_address_v4("192.0.2." + std::to_string(tag + 1)), 5000 + tag)
		, lt::tcp::endpoint(lt::make_address_v4("198.51.100." + std::to_string(tag + 1)), 6000 + tag));

	return pi;
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(snapshot_returns_all_requested_fields)
{
	ltweb::peer_history ph(lt::sha1_hash{});

	std::vector<lt::peer_info> peers;
	peers.push_back(make_peer(1));
	peers.push_back(make_peer(2));

	ph.update(peers);
	auto const r = ph.query(0, full_mask);

	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(r.updated.size() == 2u);
	BOOST_TEST(r.removed.empty());
	for (auto const& u : r.updated)
		BOOST_TEST(u.field_mask == full_mask);
}

BOOST_AUTO_TEST_CASE(delta_returns_only_requested_changed_fields)
{
	ltweb::peer_history ph(lt::sha1_hash{});

	std::vector<lt::peer_info> peers1;
	peers1.push_back(make_peer(1));
	auto const f1 = ph.update(peers1);

	std::vector<lt::peer_info> peers2 = peers1;
	peers2[0].client = "different-client";
	peers2[0].payload_down_speed += 33;
	peers2[0].num_pieces += 1;
	ph.update(peers2);

	std::uint64_t const requested_mask =
		(std::uint64_t(1) << ltweb::peer_history_entry::client)
		| (std::uint64_t(1) << ltweb::peer_history_entry::down_rate)
		| (std::uint64_t(1) << ltweb::peer_history_entry::num_pieces)
		| (std::uint64_t(1) << ltweb::peer_history_entry::source);

	auto const r = ph.query(f1, requested_mask);

	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.updated.size() == 1u);
	BOOST_TEST(r.removed.empty());
	if (!r.updated.empty())
	{
		std::uint64_t const expected =
			(std::uint64_t(1) << ltweb::peer_history_entry::client)
			| (std::uint64_t(1) << ltweb::peer_history_entry::down_rate)
			| (std::uint64_t(1) << ltweb::peer_history_entry::num_pieces);
		BOOST_TEST(r.updated[0].field_mask == expected);
		BOOST_TEST(r.updated[0].entry->info.client == "different-client");
	}
}

BOOST_AUTO_TEST_CASE(new_peer_is_sent_as_full_update)
{
	ltweb::peer_history ph(lt::sha1_hash{});

	std::vector<lt::peer_info> peers1;
	peers1.push_back(make_peer(1));
	auto const f1 = ph.update(peers1);

	std::vector<lt::peer_info> peers2 = peers1;
	peers2.push_back(make_peer(2));

	std::uint64_t const requested_mask =
		(std::uint64_t(1) << ltweb::peer_history_entry::flags)
		| (std::uint64_t(1) << ltweb::peer_history_entry::client);

	ph.update(peers2);
	auto const r = ph.query(f1, requested_mask);

	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.updated.size() == 1u);
	BOOST_TEST(r.removed.empty());
	if (!r.updated.empty())
	{
		BOOST_TEST(r.updated[0].field_mask == requested_mask);
		BOOST_TEST(r.updated[0].entry->info.client == "client-2");
	}
}

BOOST_AUTO_TEST_CASE(removed_peer_not_reported_if_client_never_saw_add)
{
	ltweb::peer_history ph(lt::sha1_hash{});

	std::vector<lt::peer_info> empty;
	std::vector<lt::peer_info> peers;
	peers.push_back(make_peer(3));

	auto const f0 = ph.update(empty);
	auto const f1 = ph.update(peers);
	ph.update(empty);

	auto const r0 = ph.query(f0, full_mask);
	BOOST_TEST(!r0.is_snapshot);
	BOOST_TEST(r0.removed.empty());

	auto const r1 = ph.query(f1, full_mask);
	BOOST_TEST(!r1.is_snapshot);
	BOOST_TEST(r1.removed.size() == 1u);
}

BOOST_AUTO_TEST_CASE(peer_reappears_after_removal)
{
	ltweb::peer_history ph(lt::sha1_hash{});

	std::vector<lt::peer_info> peers1;
	peers1.push_back(make_peer(4));
	auto const f1 = ph.update(peers1);

	std::vector<lt::peer_info> empty;
	ph.update(empty);

	std::vector<lt::peer_info> peers3;
	peers3.push_back(make_peer(4));
	peers3[0].client = "client-4-reconnected";
	ph.update(peers3);

	auto const r = ph.query(f1, full_mask);
	BOOST_TEST(!r.is_snapshot);
	BOOST_TEST(r.removed.empty());
	BOOST_TEST(r.updated.size() == 1u);
	if (!r.updated.empty())
	{
		BOOST_TEST(r.updated[0].field_mask == full_mask);
		BOOST_TEST(r.updated[0].entry->info.client == "client-4-reconnected");
	}
}

BOOST_AUTO_TEST_CASE(horizon_after_tombstone_eviction)
{
	ltweb::peer_history ph(lt::sha1_hash{}, 2);

	std::vector<lt::peer_info> peers1;
	peers1.push_back(make_peer(1));
	peers1.push_back(make_peer(2));
	peers1.push_back(make_peer(3));
	auto const f_client = ph.update(peers1);

	std::vector<lt::peer_info> empty;
	ph.update(empty);

	BOOST_TEST(ph.horizon() > 0u);

	std::vector<lt::peer_info> peers3;
	peers3.push_back(make_peer(9));
	ph.update(peers3);

	auto const r = ph.query(f_client, full_mask);
	BOOST_TEST(r.is_snapshot);
	BOOST_TEST(r.removed.empty());
	BOOST_TEST(r.updated.size() == 1u);
	if (!r.updated.empty())
		BOOST_TEST(r.updated[0].entry->info.client == "client-9");
}
