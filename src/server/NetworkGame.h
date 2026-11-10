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

#include <atomic>
#include <chrono>
#include <list>
#include <mutex>
#include <thread>
#include <memory>

#include "Global.h"
#include "raknet/NetworkTypes.h"
#include "raknet/BitStream.h"
#include "raknet/PacketPriority.h"
#include "SpeedController.h"
#include "DuelMatch.h"
#include "BlobbyDebug.h"

class ThreadSafeRakServer;
class ReplayRecorder;
class NetworkPlayer;
class ScriptBot;

typedef std::list<packet_ptr> PacketQueue;

class NetworkGame : public ObjectCounter<NetworkGame>
{
	public:
		// The given server is used to send messages to the client, received
		// messages have to bo injected manually in this class.
		// The PlayerID parameters are the IDs of the participating players.
		// The IDs are assumed to be on the same side as they are named.
		// If both players want to be on the same side, switchedSide
		// decides which player is switched.
		/// \exception Throws FileLoadException, if the desired rules file could not be loaded
		///	\exception Throws std::runtime_error, if \p leftPlayer or \p rightPlayer are already assigned to a game.
		NetworkGame(ThreadSafeRakServer* server, NetworkPlayer& leftPlayer,
					NetworkPlayer& rightPlayer, PlayerSide switchedSide,
					std::string rules, int scoreToWin, float speed);

		/// Like the constructor above, but each player can be switched on its own.
		/// A switched player sees the game mirrored. Tournament matches use this to keep
		/// player 1 on the left while both players see themselves on their own side.
		NetworkGame(ThreadSafeRakServer* server, NetworkPlayer& leftPlayer,
					NetworkPlayer& rightPlayer, bool switchLeft, bool switchRight,
					std::string rules, int scoreToWin, float speed);

		~NetworkGame();

		/// Sends the rules checksum to the players and starts the game thread. Call it once the
		/// players know this game, so that their answer reaches it. A second call does nothing.
		void start();

		/// A player who has not answered the checksum gets it again after interval, at most maxSends
		/// times in all. Then the game ends without starting. Call it before start().
		void setRulesChecksumRepeat(std::chrono::milliseconds interval, int maxSends);

		/// True if the game ended because a player never answered the checksum.
		bool didNotStart() const { return mNotStarted; }

		void injectPacket(const packet_ptr& packet);

		/// It returns whether both clients are still connected.
		bool isGameValid() const;

		// This function processes the queued network packets,
		// makes a physic step, checks the rules and broadcasts
		// the current state and outstanding messages to the clients.
		void step();

		/// This function processes all queued network packets.
		void processPackets();

		// game info
		/// gets network IDs of players
		PlayerID getPlayerID( PlayerSide side ) const;

		/// The player who won, or NO_PLAYER while nobody has won. Safe to call from any thread.
		PlayerSide getWinner() const;
		/// The score of a side, as of the last step. Safe to call from any thread.
		int getScore( PlayerSide side ) const;

		/// Stops the game thread and handles the packets that are still queued, such as a replay request.
		void finish();
		/// The ID_REPLAY packet with the replay of this game. Call it after finish().
		std::vector<unsigned char> makeReplayPacket() const;
		/// A replay of the game before this one, sent to a player who asks before this game starts.
		void setPreviousReplay( PlayerSide side, std::vector<unsigned char> packet );

		/// A game where pausing is not allowed ignores pause requests, so one player cannot hold it up.
		void setPauseAllowed( bool allowed ) { mPauseAllowed = allowed; }

		/// A player who sends nothing for limit while the game runs pauses it, so no points are scored against
		/// a blob that cannot move. The game goes on once that player is heard again, or once a connection that
		/// took its place with replacePlayer() answered the rules checksum. The bot of a training match never
		/// pauses the game. Zero, the default, turns this off. Call it before start().
		void setSilenceLimit( std::chrono::milliseconds limit ) { mSilenceLimit = limit; }
		bool pausesForSilence() const { return mSilenceLimit.count() > 0; }

		/// True while the game is paused for a player who went silent. Safe to call from any thread.
		bool isWaitingForPlayer() const { return mWaiting; }
		/// True while the game is paused because the player of this side went silent. Safe to call from any thread.
		bool isMissing( PlayerSide side ) const;
		/// The side of a connection in this game, NO_PLAYER for any other connection. Safe to call from any thread.
		PlayerSide getSide( const PlayerID& player ) const;
		/// How long the player of this side has sent nothing. Safe to call from any thread.
		std::chrono::milliseconds getSilence( PlayerSide side ) const;

		/// Seats a new connection on a side whose player went silent. The new connection gets the rules
		/// checksum and the game goes on once it answered. Safe to call from any thread.
		void replacePlayer( PlayerSide side, const PlayerID& player, bool switched );

		/// Lets a bot script play one side, for a training match against the server. The player of that side
		/// is a stand-in that is not connected, so nothing is sent to it. The bot gets the state of each step
		/// on the game thread and waits at most its budget for the script, so a slow script never stalls the
		/// network. Call it before start().
		void setBot( PlayerSide side, std::unique_ptr<ScriptBot> bot );

		/// What the bot did so far: the longest time the script took for one step, -1 before the first,
		/// and the steps it did not finish within its budget. Safe to call from any thread.
		long getBotMaxMicroseconds() const { return mBotMaxUs; }
		unsigned getBotLateSteps() const { return mBotLateSteps; }
		/// The Lua error that stopped the bot, empty while it plays. Call it after finish().
		const std::string& getBotError() const { return mBotError; }

	private:
		void broadcastBitstream(const RakNet::BitStream& stream, const RakNet::BitStream& switchedstream);
		void broadcastBitstream(const RakNet::BitStream& stream);
		void broadcastPhysicState(const DuelMatchState& state) const;
		void broadcastGameEvents() const;
		void writeEventToStream(RakNet::BitStream& stream, MatchEvent e, bool switchSides ) const;
		bool isGameStarted() { return mRulesSent[LEFT_PLAYER] && mRulesSent[RIGHT_PLAYER]; }

		// process a single packet
		void processPacket( const packet_ptr& packet );

		// the connection of a side, for the game thread
		PlayerID idOf( PlayerSide side ) const { return side == LEFT_PLAYER ? mLeftPlayer : mRightPlayer; }
		PlayerSide sideOf( const PlayerID& player ) const;
		void sendGameReady( PlayerSide side );

		// pauses the game while a player is silent and lets it go on once every player is back
		void checkSilence();
		void startWaiting();
		void stopWaiting();
		// seats a connection that replacePlayer() left for the game thread
		void applyReplacement();
		static long long steadyNow();

		// sends to a player, but never to the stand-in of the bot
		void sendTo(const RakNet::BitStream& stream, PacketPriority priority, PacketReliability reliability, PlayerID target) const;
		// gives the bot the state and takes its input for the next step
		void stepBot();

		ThreadSafeRakServer* mServer;
		// only the game thread changes the connections, and it holds this mutex while it does
		mutable std::mutex mPlayerIdMutex;
		PlayerID mLeftPlayer;
		PlayerID mRightPlayer;
		// a switched player gets mirrored data
		bool mSwitchLeft;
		bool mSwitchRight;
		bool mPauseAllowed = true;

		PacketQueue mPacketQueue;
		std::mutex mPacketQueueMutex;

		const std::unique_ptr<DuelMatch> mMatch;
		SpeedController mSpeedController;
		std::shared_ptr<InputSource> mLeftInput;
		std::shared_ptr<InputSource> mRightInput;
		unsigned mLeftLastTime;
		unsigned mRightLastTime;
		std::thread mGameThread;

		const std::unique_ptr<ReplayRecorder> mRecorder;

		std::atomic<bool> mGameValid;
		std::atomic<int> mWinner;
		std::atomic<int> mScore[MAX_PLAYERS];
		/// The points to win the match was created with, before the rules file changed them
		int mScoreToWin = 0;

		std::mutex mPreviousReplayMutex;
		std::vector<unsigned char> mPreviousReplay[MAX_PLAYERS];

		bool mRulesSent[MAX_PLAYERS];
		int mRulesChecksum = 0;
		std::chrono::steady_clock::time_point mRulesChecksumSentAt;
		// by default about 15 s in all before the game gives up
		std::chrono::milliseconds mRulesChecksumRepeat{3000};
		int mMaxRulesChecksumSends = 5;
		int mRulesChecksumSends = 0;
		bool mStartCalled = false;
		std::atomic<bool> mNotStarted{false};
		void sendRulesChecksum();
		void giveUpStart();
		std::vector<char> mRulesString;
		// true once both players answered the first checksum
		bool mHasStarted = false;

		// a player who sends nothing this long pauses the game, zero for never
		std::chrono::milliseconds mSilenceLimit{0};
		// when each side was heard last, in steady clock nanoseconds
		std::atomic<long long> mLastHeard[MAX_PLAYERS];
		std::atomic<bool> mMissing[MAX_PLAYERS];
		std::atomic<bool> mWaiting{false};
		long long mWaitingSince = 0;
		// a connection that takes the place of a silent player, guarded by mPlayerIdMutex
		bool mReplacing[MAX_PLAYERS] = {false, false};
		PlayerID mReplacement[MAX_PLAYERS];
		bool mReplacementSwitched[MAX_PLAYERS] = {false, false};

		// the bot of a training match, only touched by the game thread once the game started
		std::unique_ptr<ScriptBot> mBot;
		PlayerSide mBotSide = NO_PLAYER;
		std::atomic<long> mBotMaxUs{-1};
		std::atomic<unsigned> mBotLateSteps{0};
		std::string mBotError;
};

