/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <vector>

#include "raknet/NetworkTypes.h"

/// Connections that have not entered the server yet, and the time each has to enter by.
class EntryDeadlines
{
	public:
		using Clock = std::chrono::steady_clock;

		explicit EntryDeadlines(Clock::duration limit) : mLimit(limit) {}

		/// A new connection has to enter within the limit.
		void connected(PlayerID id, Clock::time_point now) { mDeadlines[id] = now + mLimit; }

		/// The connection entered or closed, so it has no deadline any more.
		void remove(PlayerID id) { mDeadlines.erase(id); }

		/// Removes and returns the connections whose deadline has passed.
		std::vector<PlayerID> takeExpired(Clock::time_point now)
		{
			std::vector<PlayerID> expired;
			for( auto it = mDeadlines.begin(); it != mDeadlines.end(); )
			{
				if( it->second <= now )
				{
					expired.push_back(it->first);
					it = mDeadlines.erase(it);
				}
				else
				{
					++it;
				}
			}
			return expired;
		}

		std::size_t size() const { return mDeadlines.size(); }

	private:
		Clock::duration mLimit;
		std::map<PlayerID, Clock::time_point> mDeadlines;
};
