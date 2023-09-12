/*

Copyright (c) 2013, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include <memory>

#include "file_requests.hpp"

#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/torrent.hpp"

//#define DLOG printf
#define DLOG if (false) printf

using namespace libtorrent;

std::size_t file_requests::hash_value(piece_request const& r) const
{
	sha1_hash const& h = r.info_hash.get_best();
	return (h[0] | (h[1] << 8) | (h[2] << 16) | (h[3] << 24)) ^ static_cast<int>(r.piece);
}

file_requests::file_requests()
	: m_next_timeout(m_requests.begin())
{
}

void file_requests::on_alert(alert const* a)
{
	read_piece_alert const* p = alert_cast<read_piece_alert>(a);
	if (p)
	{
		std::shared_ptr<aux::torrent> t = p->handle.native_handle();

		piece_request rq;
		rq.info_hash = t->info_hash();
		rq.piece = p->piece;
		typedef requests_t::iterator iter;

		DLOG("read_piece_alert: %d (%s)\n", static_cast<int>(p->piece), p->error.message().c_str());
		std::unique_lock<std::mutex> l(m_mutex);
		std::pair<iter, iter> range = m_requests.equal_range(rq);
		if (range.first == m_requests.end()) return;

		piece_entry pe;
		pe.buffer = p->buffer;
		pe.piece = p->piece;
		pe.size = p->size;
		for (iter i = range.first; i != range.second; ++i)
		{
			i->promise->set_value(pe);
			if (m_next_timeout == i)
				++m_next_timeout;
		}
		m_requests.erase(range.first, range.second);

		DLOG("outstanding requests: ");
		for (iter i = m_requests.begin(); i != m_requests.end(); ++i)
		{
			TORRENT_ASSERT(i->info_hash != rq.info_hash || i->piece != rq.piece);
			DLOG("(%02x%02x, %d) ", i->info_hash.get_best()[0], i->info_hash.get_best()[1], static_cast<int>(i->piece));
		}
		DLOG("\n");
		return;
	}

	piece_finished_alert const* pf = alert_cast<piece_finished_alert>(a);
	if (pf)
	{
		DLOG("piece_finished: %d\n", static_cast<int>(pf->piece_index));
		std::shared_ptr<aux::torrent> t = pf->handle.native_handle();
		piece_request rq;
		rq.info_hash = t->info_hash();
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
	torrent_removed_alert const* tr = alert_cast<torrent_removed_alert>(a);
	torrent_paused_alert const* tp = alert_cast<torrent_paused_alert>(a);
	if (tr)
	{
		rq.info_hash = tr->info_hashes;
	}
	else if (tp)
	{
		rq.info_hash = tp->handle.info_hashes();
	}
	else return;

	// remove all requests for the torrent
	rq.piece = piece_index_t{0};
	typedef requests_t::iterator iter;

	std::unique_lock<std::mutex> l(m_mutex);
	iter first = m_requests.lower_bound(rq);
	rq.piece = piece_index_t{INT_MAX};
	iter last = m_requests.upper_bound(rq);
	if (first == last) return;

	for (iter i = first; i != last; ++i)
	{
		if (i != m_next_timeout) continue;
		m_next_timeout = last;
		break;
	}

	m_requests.erase(first, last);

}

void file_requests::on_tick()
{
	std::unique_lock<std::mutex> l(m_mutex);

	if (m_next_timeout == m_requests.end())
		m_next_timeout = m_requests.begin();

	auto const now = lt::clock_type::now();

	if (m_next_timeout != m_requests.end())
	{
		if (m_next_timeout->timeout < now)
		{
			auto to_remove = m_next_timeout;
			++m_next_timeout;
			m_requests.erase(to_remove);
		}
		else
		{
			++m_next_timeout;
		}
	}
}

std::shared_future<piece_entry> file_requests::read_piece(
	torrent_handle const& h
	, lt::piece_index_t const piece
	, lt::clock_type::duration const timeout_ms)
{
	TORRENT_ASSERT(piece >= piece_index_t{0});
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
	if (m_have_pieces[rq.info_hash].count(piece))
	{
		DLOG("read_piece: %d\n", static_cast<int>(piece));
		h.read_piece(piece);
	}
	return std::shared_future<piece_entry>(rq.promise->get_future());
}

