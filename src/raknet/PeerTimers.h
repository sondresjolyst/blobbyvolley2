/*=============================================================================
Blobby Volley 2
Copyright (C) 2026 Sjølyst Innovation AS

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

/**
 * @file PeerTimers.h
 *
 * The timer decisions of RakPeer, on the clock of GetTime.
 */
#ifndef __PEER_TIMERS_H
#define __PEER_TIMERS_H

#include "GetTime.h"

namespace RakNet
{
	/// True when a connected system has had no reliable send for long enough to need a keepalive.
	/// After a keepalive the last send is set ahead of now, so this stays false until then.
	inline bool KeepAliveDue( unsigned int lastReliableSend, unsigned int now )
	{
		return TimeElapsed( lastReliableSend, now ) > 5000;
	}

	/// True when a system that is not connected has stayed so for too long. connectionTime is when
	/// the system was added, or the last update while it was connected. So the limit counts from the
	/// start of a connect attempt, or from when a connected system began to close.
	inline bool NotConnectedTimedOut( unsigned int connectionTime, unsigned int now )
	{
		return TimeElapsed( connectionTime, now ) > 10000;
	}

	/// True when it is time to ping a connected system. A next ping time of 0 means ping now.
	inline bool PingDue( unsigned int nextPingTime, unsigned int now )
	{
		return nextPingTime == 0 || TimeIsAfter( now, nextPingTime );
	}
}

#endif
