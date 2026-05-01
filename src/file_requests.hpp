/*

Copyright (c) 2014, 2017, 2019, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef FILE_REQUESTS_HPP_
#define FILE_REQUESTS_HPP_

#include <boost/shared_array.hpp>
#include <future>
#include <set>
#include <boost/functional/hash.hpp>

#include <mutex> // for mutex
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/peer_id.hpp" // for sha1_hash
#include "libtorrent/extensions.hpp" // for plugin
#include "libtorrent/time.hpp" // for time_point
#include "libtorrent/info_hash.hpp"

using lt::sha1_hash;
using std::mutex;

namespace ltweb {

struct piece_entry {
	boost::shared_array<char> buffer;
	int size;
	lt::piece_index_t piece;
};

// this is a session plugin which wraps the concept of reading pieces
// from torrents, returning futures for when those pieces are complete
struct file_requests : lt::plugin {
	file_requests();
	void on_alert(lt::alert const* a) override;
	void on_tick() override;
	std::shared_future<piece_entry> read_piece(
		lt::torrent_handle const& h, lt::piece_index_t piece, lt::clock_type::duration timeout_ms
	);

	lt::feature_flags_t implemented_features() override
	{
		return lt::plugin::alert_feature | lt::plugin::tick_feature;
	}

private:
	struct piece_request {
		lt::info_hash_t info_hash;
		lt::piece_index_t piece;
		std::shared_ptr<std::promise<piece_entry>> promise;
		lt::clock_type::time_point timeout;
		bool operator==(piece_request const& rq) const
		{
			return rq.info_hash == info_hash && rq.piece == piece;
		}
		bool operator<(piece_request const& rq) const
		{
			return info_hash == rq.info_hash ? piece < rq.piece : info_hash < rq.info_hash;
		}
	};

	std::size_t hash_value(piece_request const& r) const;

	std::mutex m_mutex;
	typedef std::multiset<piece_request> requests_t;
	requests_t m_requests;
	requests_t::iterator m_next_timeout;

	// TOOD: figure out a way to clear out info-hashes
	std::map<lt::info_hash_t, std::set<lt::piece_index_t>> m_have_pieces;
};

} // namespace ltweb

#endif // FILE_REQUESTS_HPP_
