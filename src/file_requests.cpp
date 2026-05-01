/*

Copyright (c) 2014, 2017-2018, 2023, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <memory>

#include "file_requests.hpp"

#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_handle.hpp"

//#define DLOG printf
#define DLOG                                                                                       \
	if (false) printf

using namespace ltweb;

std::size_t file_requests::hash_value(piece_request const& r) const
{
	sha1_hash const& h = r.info_hash.get_best();
	return (h[0] | (h[1] << 8) | (h[2] << 16) | (h[3] << 24)) ^ static_cast<int>(r.piece);
}

file_requests::file_requests()
	: m_next_timeout(m_requests.begin())
{
}

void file_requests::on_alert(lt::alert const* a)
{
	lt::read_piece_alert const* p = lt::alert_cast<lt::read_piece_alert>(a);
	if (p) {
		piece_request rq;
		rq.info_hash = p->handle.info_hashes();
		rq.piece = p->piece;
		typedef requests_t::iterator iter;

		DLOG(
			"lt::read_piece_alert: %d (%s)\n",
			static_cast<int>(p->piece),
			p->error.message().c_str()
		);
		std::unique_lock<std::mutex> l(m_mutex);
		std::pair<iter, iter> range = m_requests.equal_range(rq);
		if (range.first == m_requests.end()) return;

		piece_entry pe;
		pe.buffer = p->buffer;
		pe.piece = p->piece;
		pe.size = p->size;
		for (iter i = range.first; i != range.second; ++i) {
			i->promise->set_value(pe);
			if (m_next_timeout == i) ++m_next_timeout;
		}
		m_requests.erase(range.first, range.second);

		DLOG("outstanding requests: ");
		for (iter i = m_requests.begin(); i != m_requests.end(); ++i) {
			TORRENT_ASSERT(i->info_hash != rq.info_hash || i->piece != rq.piece);
			DLOG(
				"(%02x%02x, %d) ",
				i->info_hash.get_best()[0],
				i->info_hash.get_best()[1],
				static_cast<int>(i->piece)
			);
		}
		DLOG("\n");
		return;
	}

	lt::piece_finished_alert const* pf = lt::alert_cast<lt::piece_finished_alert>(a);
	if (pf) {
		DLOG("piece_finished: %d\n", static_cast<int>(pf->piece_index));
		piece_request rq;
		rq.info_hash = pf->handle.info_hashes();
		rq.piece = pf->piece_index;
		using iter = requests_t::iterator;
		m_have_pieces[rq.info_hash].insert(pf->piece_index);

		std::unique_lock<std::mutex> l(m_mutex);
		std::pair<iter, iter> range = m_requests.equal_range(rq);
		if (range.first == m_requests.end()) return;
		l.unlock();

		DLOG("read_piece: %d\n", static_cast<int>(pf->piece_index));
		pf->handle.read_piece(pf->piece_index);
		return;
	}

	// if a torrent is stopped or removed, abort any piece requests
	piece_request rq;
	lt::torrent_removed_alert const* tr = lt::alert_cast<lt::torrent_removed_alert>(a);
	lt::torrent_paused_alert const* tp = lt::alert_cast<lt::torrent_paused_alert>(a);
	if (tr) {
		rq.info_hash = tr->info_hashes;
	} else if (tp) {
		rq.info_hash = tp->handle.info_hashes();
	} else
		return;

	// remove all requests for the torrent
	rq.piece = lt::piece_index_t{0};
	typedef requests_t::iterator iter;

	std::unique_lock<std::mutex> l(m_mutex);
	iter first = m_requests.lower_bound(rq);
	rq.piece = lt::piece_index_t{INT_MAX};
	iter last = m_requests.upper_bound(rq);
	if (first == last) return;

	for (iter i = first; i != last; ++i) {
		if (i != m_next_timeout) continue;
		m_next_timeout = last;
		break;
	}

	m_requests.erase(first, last);
}

void file_requests::on_tick()
{
	std::unique_lock<std::mutex> l(m_mutex);

	if (m_next_timeout == m_requests.end()) m_next_timeout = m_requests.begin();

	auto const now = lt::clock_type::now();

	if (m_next_timeout != m_requests.end()) {
		if (m_next_timeout->timeout < now) {
			auto to_remove = m_next_timeout;
			++m_next_timeout;
			m_requests.erase(to_remove);
		} else {
			++m_next_timeout;
		}
	}
}

std::shared_future<piece_entry> file_requests::read_piece(
	lt::torrent_handle const& h,
	lt::piece_index_t const piece,
	lt::clock_type::duration const timeout_ms
)
{
	TORRENT_ASSERT(piece >= lt::piece_index_t{0});
	TORRENT_ASSERT(piece < h.torrent_file()->end_piece());

	piece_request rq;
	rq.info_hash = h.info_hashes();
	rq.piece = piece;
	rq.promise.reset(new std::promise<piece_entry>());
	rq.timeout = lt::clock_type::now() + timeout_ms;

	std::unique_lock<std::mutex> l(m_mutex);
	m_requests.insert(rq);
	l.unlock();

	DLOG("piece_priority: %d <- 7\n", static_cast<int>(piece));
	h.piece_priority(piece, lt::top_priority);
	//	h.set_piece_deadline(piece, 0, torrent_handle::alert_when_available);
	if (m_have_pieces[rq.info_hash].count(piece)) {
		DLOG("read_piece: %d\n", static_cast<int>(piece));
		h.read_piece(piece);
	}
	return std::shared_future<piece_entry>(rq.promise->get_future());
}
