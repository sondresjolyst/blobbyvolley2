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

	/// True when a system that is not yet connected has waited too long since the attempt began.
	inline bool ConnectAttemptTimedOut( unsigned int connectionTime, unsigned int now )
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
