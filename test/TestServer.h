#pragma once

#include <vector>

#include "raknet/NetworkTypes.h"
#include "server/Tournament.h"

// Helpers for the tests of the server and the tournament.

// a loopback address with the given port
inline PlayerID makeID(unsigned short port)
{
	PlayerID id;
	id.binaryAddress = 0x0100007F;
	id.port = port;
	return id;
}

// remembers every tournament event
struct RecordingEvents : public TournamentEvents
{
	struct Connected { int matchId, player1, player2; };
	std::vector<Connected> connected;
	std::vector<GameReport> games;
	std::vector<SeriesResult> series;

	void matchConnected(int matchId, int player1, int player2) override
	{
		connected.push_back(Connected{matchId, player1, player2});
	}
	void gameEnded(const GameReport& report) override { games.push_back(report); }
	void seriesEnded(const SeriesResult& result) override { series.push_back(result); }
};
