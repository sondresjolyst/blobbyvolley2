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

#include <string>
#include <map>
#include <list>
#include <mutex>
#include <deque>
#include <iosfwd>
#include <memory>
#include <set>
#include <chrono>

#include "NetworkPlayer.h"
#include "NetworkMessage.h"
#include "server/MatchMaker.h"
#include "server/JoinCodes.h"
#include "server/Tournament.h"

class ThreadSafeRakServer;

// function for logging to replacing syslog
enum {
	LOG_ERR,
	LOG_NOTICE,
	LOG_DEBUG
};

/*! \class DedicatedServer
	\brief manages a blobby server.
	\details Lets the players connect, matches games and processes them.
*/
class DedicatedServer
{
	public:
		/// create a dedicated server with the data specified in info, using the rules from rulesfile, allowing max_clients
		/// simultaneous connections
		/// \todo Maybe two classes for server info: local server info for a server, and remote for data sent to client
		/// \param is_local: Set to true, to indicate a locally hosted server intended for a single game.
		DedicatedServer(ServerInfo info, const std::vector<std::string>& rulefile,
						const std::vector<float>& speeds, int max_clients, bool is_local = false);
		~DedicatedServer();

		// server processing
		void queuePackets(); // sort incoming packets into queues. called from raknet thread!
		///! \brief This function handles the packet processing loop for any non-game packet.
		void processPackets();
		void updateGames();

		// status queries
		bool hasActiveGame() const;
		int getActiveGamesCount() const;
		int getWaitingPlayers() const;
		int getConnectedClients() const;

		const ServerInfo& getServerInfo() const;

		// debug functions
		void printAllPlayers(std::ostream& stream) const;
		void printAllGames(std::ostream& stream) const;


		// server settings
		void allowNewPlayers( bool allow );

		/// Turns on tournament mode. Every player then has to send a join code as name.
		/// Without a gate the server behaves as a casual server.
		void setJoinCodeGate( std::unique_ptr<JoinCodeGate> gate );

		/// Starts the matches of the players file in tournament mode. Needs a join code gate.
		/// Casual games are not possible then.
		void setTournamentDirector( std::unique_ptr<TournamentDirector> director );

	private:
		// creates a new game with those players
		// does not add the game to the active game list
		void createGame(NetworkPlayer& left, NetworkPlayer& right,
						PlayerSide switchSide, const std::string& rules, int scoreToWin, float gamespeed);

		// packet handling functions / utility functions
		/// This function encapsulates processing our custom packets from the server main loop.
		void processSingleMessage(MessageType message_id, PlayerID source, RakNet::BitStream& data);
		void processEnterServer(PlayerID source, RakNet::BitStream& stream);
		void processBlobbyServerPresent(PlayerID source, RakNet::BitStream& stream);
		void refuseEntry(PlayerID source, EntryRefusedReason reason);
		void closeRefusedConnections();

		// tournament matches
		bool isTournament() const;
		void updateTournament();
		void checkTournamentGames(unsigned long long now_ms);
		void startTournamentGame(const TournamentDirector::GameOrder& order);
		void endGameOf(NetworkPlayer& player);
		void sendTournamentStatus(unsigned long long now_ms);

		// raknet server used
		const std::unique_ptr<ThreadSafeRakServer> mServer;

		// true, if new players should be accepted
		bool mAcceptNewPlayers;
		// true, if this is a player hosted local server
		bool mPlayerHosted;
		// server info with server config
		ServerInfo mServerInfo;

		// containers for all games and mapping players to their games
		std::list< std::shared_ptr<NetworkGame> > mGameList;
		std::map< PlayerID, std::shared_ptr<NetworkPlayer>> mPlayerMap;
		std::mutex mPlayerMapMutex;

		// Keeps track of all open connections, so we can discard packets that arrive
		// for non-connected players
		std::set<PlayerID> mActiveConnections;

		bool isConnected(PlayerID player) const;
		bool addConnection(PlayerID player);

		// packet queue
		std::deque<packet_ptr> mPacketQueue;
		std::mutex mPacketQueueMutex;

		MatchMaker mMatchMaker;

		// tournament mode, empty on a casual server
		std::unique_ptr<JoinCodeGate> mJoinCodes;
		// refused connections and when to close them
		std::map<PlayerID, std::chrono::steady_clock::time_point> mRefusedConnections;

		// starts assigned matches, empty on a casual server
		std::unique_ptr<TournamentDirector> mTournament;
		// players file generation the director knows
		unsigned mTournamentGeneration = 0;
		struct TournamentGame
		{
			int matchId;
			std::shared_ptr<NetworkGame> game;
			WallTime startedAt;
			bool reported;
		};
		std::vector<TournamentGame> mTournamentGames;
		// status each player got last
		std::map<PlayerID, PlayerTournamentStatus> mSentStatus;
};
