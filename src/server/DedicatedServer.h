/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)
Changed by Sjølyst Innovation AS, 2026

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
#include <atomic>
#include <chrono>
#include <functional>

#include "NetworkPlayer.h"
#include "NetworkMessage.h"
#include "server/MatchMaker.h"
#include "server/JoinCodes.h"
#include "server/EntryDeadlines.h"
#include "server/Tournament.h"
#include "SponsorSync.h"
#include "ClientVersion.h"

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

		/// Gives the sponsor board to send to the players in tournament mode. It returns true and the board
		/// when the board changed. It is called on the main loop, at most once per interval.
		typedef std::function<bool(sponsors::Board&)> SponsorSource;
		void setSponsorSource( SponsorSource source, unsigned interval_ms = 1000 );

		/// Sets the oldest client version and the download page that the server tells the clients about.
		void setVersionPolicy( const VersionPolicy& policy );

	private:
		// creates a new game with those players
		// does not add the game to the active game list
		void createGame(NetworkPlayer& left, NetworkPlayer& right,
						PlayerSide switchSide, const std::string& rules, int scoreToWin, float gamespeed);

		// packet handling functions / utility functions
		/// This function encapsulates processing our custom packets from the server main loop.
		void processSingleMessage(MessageType message_id, PlayerID source, RakNet::BitStream& data);
		/// since is when the entry arrived, an entry that waits for a silent player is tried again
		void processEnterServer(PlayerID source, RakNet::BitStream& stream, std::chrono::steady_clock::time_point since);
		void retryWaitingEntries();
		void processBlobbyServerPresent(PlayerID source, RakNet::BitStream& stream);
		void refuseEntry(PlayerID source, EntryRefusedReason reason);
		// tells a client why it cannot enter or why its connection closes
		void sendEntryRefused(PlayerID target, EntryRefusedReason reason);
		// tells a client the version of the server and whether it should update
		void sendServerVersion(PlayerID target, VersionNotice notice);
		void closeRefusedConnections();
		void closeConnectionsThatDidNotEnter();
		// forgets a connection that closed: lobby, game, tournament and every per connection map
		void removeConnection(const packet_ptr& packet);
		// closes the connection that held a join code that another connection entered
		void handOverJoinCode(PlayerID holder);

		// tournament matches
		bool isTournament() const;
		void updateTournament();
		void checkTournamentGames(unsigned long long now_ms);
		void startTournamentGame(const TournamentDirector::GameOrder& order);
		// starts a training match of one player against a bot script that the server runs
		void startTrainingGame(const TournamentDirector::GameOrder& order);
		void endGameOf(NetworkPlayer& player);
		// stops a finished game and returns its replay packet, empty without a game
		std::vector<unsigned char> finishForNextGame(const std::shared_ptr<NetworkGame>& game);
		void sendTournamentStatus(unsigned long long now_ms);

		// sponsor board
		void updateSponsors(unsigned long long now_ms);
		void processSponsorRequest(PlayerID source, RakNet::BitStream& stream);
		// true while the player plays a game that has no winner yet
		/// true while a player who was just told about the sponsor board may still ask for its logos
		bool waitsForSponsors(PlayerID player, unsigned long long now_ms) const;
		bool inRunningGame(PlayerID player) const;
		// true while the running game of the player is paused because that player went silent
		bool isMissingFromGame(PlayerID player) const;

		// raknet server used
		const std::unique_ptr<ThreadSafeRakServer> mServer;

		// true, if new players should be accepted
		bool mAcceptNewPlayers;
		// true, if this is a player hosted local server
		bool mPlayerHosted;
		// oldest client version and download page
		VersionPolicy mVersionPolicy;
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
		// updates of the network thread that found the server busy, and when that was last logged
		std::atomic<unsigned> mBusyUpdates{0};
		unsigned long long mBusyLoggedAt = 0;

		// connections to close and when each was refused
		std::map<PlayerID, std::chrono::steady_clock::time_point> mRefusedConnections;
		// a new connection has to send ID_ENTER_SERVER within ten seconds
		EntryDeadlines mEntryDeadlines{ std::chrono::seconds(10) };

		// entries with the code of a player in a running game who went quiet, they wait until it is
		// clear whether that player is gone
		struct WaitingEntry
		{
			PlayerID source;
			std::vector<unsigned char> packet;
			std::chrono::steady_clock::time_point since;
		};
		std::vector<WaitingEntry> mWaitingEntries;
		bool mRetryingEntries = false;

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
			// player ids on the left and the right
			int player1;
			int player2;
			// score that was sent as live score last, -1 before the first
			int livePoints1;
			int livePoints2;
			// a training match against the server bot, whose player 2 is 0
			bool training;
			// the bot stats of a training game were logged
			bool botLogged;
			// the game was paused for a silent player when it was checked last
			bool waiting;
		};
		// logs once how the bot of a finished training game did
		void logTrainingBot(TournamentGame& entry);
		std::vector<TournamentGame> mTournamentGames;
		// status each player got last
		std::map<PlayerID, PlayerTournamentStatus> mSentStatus;
		// matches whose missing player was logged already
		std::set<int> mMissingPlayerLogged;

		// sponsor board of a tournament server and what each player got
		SponsorSource mSponsorSource;
		unsigned mSponsorInterval = 1000;
		unsigned long long mNextSponsorPoll = 0;
		sponsors::BoardSender mSponsors;
		// players that announced tournament support, only they get the board
		std::set<PlayerID> mSponsorPlayers;
		std::map<PlayerID, std::string> mSentSponsorVersion;
		std::map<PlayerID, std::set<std::string>> mSentLogos;
		// logos a player asked for during a game, sent when the game is over
		std::map<PlayerID, std::vector<const sponsors::LogoFile*>> mHeldLogos;
		// until when a game of a player waits for its request for logos
		std::map<PlayerID, unsigned long long> mSponsorWait;
};
