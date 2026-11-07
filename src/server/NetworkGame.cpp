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

/* header include */
#include "NetworkGame.h"

/* includes */
#include <atomic>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cassert>

#include "ThreadSafeRakServer.h"
#include "raknet/BitStream.h"
#include "raknet/GetTime.h"


#include "NetworkMessage.h"
#include "replays/ReplayRecorder.h"
#include "FileRead.h"
#include "FileSystem.h"
#include "GenericIO.h"
#include "MatchEvents.h"
#include "PhysicWorld.h"
#include "NetworkPlayer.h"
#include "InputSource.h"

extern std::atomic<int> SWLS_GameSteps;

namespace
{
	// the checksum goes out again to a player who has not answered after this long
	const std::chrono::milliseconds RULES_CHECKSUM_REPEAT(3000);
}

/* implementation */

NetworkGame::NetworkGame(ThreadSafeRakServer* server, NetworkPlayer& leftPlayer,
			NetworkPlayer& rightPlayer, PlayerSide switchedSide,
			std::string rules, int scoreToWin, float speed) :
	NetworkGame(server, leftPlayer, rightPlayer, switchedSide == LEFT_PLAYER, switchedSide == RIGHT_PLAYER,
				std::move(rules), scoreToWin, speed)
{
}

NetworkGame::NetworkGame(ThreadSafeRakServer* server, NetworkPlayer& leftPlayer,
			NetworkPlayer& rightPlayer, bool switchLeft, bool switchRight,
			std::string rules, int scoreToWin, float speed) :
	mServer(server),
	mMatch(new DuelMatch(false, rules, scoreToWin)),
	mSpeedController(speed),
	mLeftInput (new InputSource()),
	mRightInput(new InputSource()),
	mLeftLastTime(-1),
	mRightLastTime(-1),
	mRecorder(new ReplayRecorder()),
	mGameValid(true),
	mWinner(NO_PLAYER)
{
	mScore[LEFT_PLAYER] = 0;
	mScore[RIGHT_PLAYER] = 0;

	// check that both players don't have an active game
	if(leftPlayer.getGame())
	{
		BOOST_THROW_EXCEPTION( std::runtime_error("Trying to start a game with player already in another game!") );
	}

	if(rightPlayer.getGame())
	{
		BOOST_THROW_EXCEPTION( std::runtime_error("Trying to start a game with player already in another game!") );
	}

	mMatch->setPlayers( leftPlayer.getIdentity(), rightPlayer.getIdentity() );
	mMatch->setInputSources(mLeftInput, mRightInput);

	mLeftPlayer = leftPlayer.getID();
	mRightPlayer = rightPlayer.getID();
	mSwitchLeft = switchLeft;
	mSwitchRight = switchRight;

	mRecorder->setPlayerNames(leftPlayer.getName(), rightPlayer.getName());
	mRecorder->setPlayerColors(leftPlayer.getColor(), rightPlayer.getColor());
	mRecorder->setGameSpeed(mSpeedController.getGameSpeed());
	mRecorder->setGameRules(rules);

	// read rulesfile into a string
	mRulesSent[0] = false;
	mRulesSent[1] = false;

	rules = FileRead::makeLuaFilename( rules );
	FileRead file(std::string("rules/") + rules);
	mRulesChecksum = file.calcChecksum(0);
	mRulesString = file.readRawBytes(file.length());
}

void NetworkGame::start()
{
	sendRulesChecksum();

	// game loop
	mGameThread = std::thread(
		[this]()
		{
			while(mGameValid)
			{
				processPackets();
				// a player who did not answer the checksum is asked again
				if (!isGameStarted() && std::chrono::steady_clock::now() - mRulesChecksumSentAt >= RULES_CHECKSUM_REPEAT)
					sendRulesChecksum();
				step();
				SWLS_GameSteps++;
				mSpeedController.update();
			}
		}					);
}

void NetworkGame::sendRulesChecksum()
{
	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_RULES_CHECKSUM);
	stream.Write(mRulesChecksum);
	stream.Write(mMatch->getScoreToWin());
	/// \todo write file author and title, too; maybe add a version number in scripts, too.
	if (!mRulesSent[LEFT_PLAYER])
		mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, mLeftPlayer);
	if (!mRulesSent[RIGHT_PLAYER])
		mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, mRightPlayer);
	mRulesChecksumSentAt = std::chrono::steady_clock::now();
}

NetworkGame::~NetworkGame()
{
	mGameValid = false;
	if (mGameThread.joinable())
		mGameThread.join();
}

void NetworkGame::finish()
{
	mGameValid = false;
	if (mGameThread.joinable())
		mGameThread.join();
	processPackets();
}

std::vector<unsigned char> NetworkGame::makeReplayPacket() const
{
	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_REPLAY);
	std::shared_ptr<GenericOut> out = createGenericWriter( &stream );
	mRecorder->send( *out );
	return std::vector<unsigned char>(stream.GetData(), stream.GetData() + stream.GetNumberOfBytesUsed());
}

void NetworkGame::setPreviousReplay( PlayerSide side, std::vector<unsigned char> packet )
{
	if (side != LEFT_PLAYER && side != RIGHT_PLAYER)
		return;
	std::lock_guard<std::mutex> lock(mPreviousReplayMutex);
	mPreviousReplay[side] = std::move(packet);
}

void NetworkGame::injectPacket(const packet_ptr& packet)
{
	std::lock_guard<std::mutex> lock(mPacketQueueMutex);
	mPacketQueue.push_back(packet);
}

void NetworkGame::broadcastBitstream(const RakNet::BitStream& stream, const RakNet::BitStream& switchedstream)
{
	// checks that stream and switchedstream don't have the same content.
	// this is a common mistake that arises from constructs like:
	//		BitStream stream
	//		... fill common data into stream
	//		BitStream switchedstream
	//		.. fill data depending on side in both streams
	//		broadcastBistream(stream, switchedstream)
	//
	//	here, the internal data of switchedstream is the same as stream so all
	//	changes made with switchedstream are done with stream alike. this was not
	//  the intention of this construct so it should be caught by this assertion.
	/// NEVER USE THIS FUNCTION LIKE broadcastBitstream(str, str), use, broadcastBitstream(str) instead
	/// this function is intended for sending two different streams to the two clients

	assert( &stream != &switchedstream );
	assert( stream.GetData() != switchedstream.GetData() );
	const RakNet::BitStream& leftStream = mSwitchLeft ? switchedstream : stream;
	const RakNet::BitStream& rightStream = mSwitchRight ? switchedstream : stream;

	mServer->Send(leftStream, HIGH_PRIORITY, RELIABLE_ORDERED, mLeftPlayer);
	mServer->Send(rightStream, HIGH_PRIORITY, RELIABLE_ORDERED, mRightPlayer);
}

void NetworkGame::broadcastBitstream(const RakNet::BitStream& stream)
{

	mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, mLeftPlayer);
	mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, mRightPlayer);
}

void NetworkGame::processPackets()
{
	while (true)
	{
		packet_ptr packet;
		{
			std::lock_guard<std::mutex> lock(mPacketQueueMutex);
			if (mPacketQueue.empty())
				break;
			packet = mPacketQueue.front();
			mPacketQueue.pop_front();
		}

		processPacket( packet );
	}
}

/// this function processes a single packet received for this network game
void NetworkGame::processPacket( const packet_ptr& packet )
{
	switch(packet->data[0])
	{
		case ID_CONNECTION_LOST:
		case ID_DISCONNECTION_NOTIFICATION:
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_OPPONENT_DISCONNECTED);
			// only the player who stays is told, the other one is gone
			if (packet->playerId == mLeftPlayer)
				mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, mRightPlayer);
			else if (packet->playerId == mRightPlayer)
				mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, mLeftPlayer);
			else
				broadcastBitstream(stream);
			mMatch->pause();
			mGameValid = false;
			break;
		}

		case ID_INPUT_UPDATE:
		{

			unsigned time = 0;
			RakNet::BitStream stream(packet->data, packet->length, false);

			// ignore ID_INPUT_UPDATE
			stream.IgnoreBytes(1);
			stream.Read(time);
			PlayerInputAbs newInput(stream);

			if (packet->playerId == mLeftPlayer)
			{
				if (mSwitchLeft)
					newInput.swapSides();
				mLeftInput->setInput(newInput);
				mLeftLastTime = time;
			}
			if (packet->playerId == mRightPlayer)
			{
				if (mSwitchRight)
					newInput.swapSides();
				mRightInput->setInput(newInput);
				mRightLastTime = time;
			}
			break;
		}

		case ID_PAUSE:
		{
			// the client only pauses when the server sends this back, so ignoring it keeps both playing
			if (!mPauseAllowed)
				break;
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_PAUSE);
			broadcastBitstream(stream);
			mMatch->pause();
			break;
		}

		case ID_UNPAUSE:
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_UNPAUSE);
			broadcastBitstream(stream);
			mMatch->unpause();
			break;
		}

		case ID_CHAT_MESSAGE:
		{
			RakNet::BitStream stream(packet->data, packet->length, false);

			stream.IgnoreBytes(1); // ID_CHAT_MESSAGE
			char message[31] = {0};
			// drop messages that are too short
			if(!stream.Read(message, sizeof(message)))
				break;

			RakNet::BitStream stream2;
			stream2.Write((unsigned char)ID_CHAT_MESSAGE);
			stream2.Write(message, sizeof(message));
			if (mLeftPlayer == packet->playerId)
				mServer->Send(stream2, LOW_PRIORITY, RELIABLE_ORDERED, mRightPlayer);
			else
				mServer->Send(stream2, LOW_PRIORITY, RELIABLE_ORDERED, mLeftPlayer);
			break;
		}

		case ID_REPLAY:
		{
			// before this game starts, a player asking for a replay means the game before
			if (!isGameStarted())
			{
				std::lock_guard<std::mutex> lock(mPreviousReplayMutex);
				const auto& previous = mPreviousReplay[packet->playerId == mLeftPlayer ? LEFT_PLAYER : RIGHT_PLAYER];
				if (!previous.empty())
				{
					RakNet::BitStream old(const_cast<unsigned char*>(previous.data()), previous.size(), true);
					mServer->Send(old, LOW_PRIORITY, RELIABLE_ORDERED, packet->playerId);
					break;
				}
			}

			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_REPLAY);
			std::shared_ptr<GenericOut> out = createGenericWriter( &stream );
			mRecorder->send( *out );
			assert( stream.GetData()[0] == ID_REPLAY );

			mServer->Send(stream, LOW_PRIORITY, RELIABLE_ORDERED, packet->playerId);

			break;
		}

		case ID_RULES:
		{
			std::shared_ptr<RakNet::BitStream> stream = std::make_shared<RakNet::BitStream>(packet->data,
					packet->length, false);
			bool needRules;
			stream->IgnoreBytes(1);
			stream->Read(needRules);
			// a player answers once, a second answer to a repeated checksum is ignored
			const PlayerSide side = mLeftPlayer == packet->playerId ? LEFT_PLAYER : RIGHT_PLAYER;
			if (mRulesSent[side])
				break;
			mRulesSent[side] = true;

			if (needRules)
			{
				stream = std::make_shared<RakNet::BitStream>();
				stream->Write((unsigned char)ID_RULES);
				stream->Write( (int)mRulesString.size() );
				stream->Write( mRulesString.data(), mRulesString.size());
				assert( stream->GetData()[0] == ID_RULES );

				mServer->Send(*stream, HIGH_PRIORITY, RELIABLE_ORDERED, packet->playerId);
			}

			if (isGameStarted())
			{
				// buffer for playernames
				char name[16];

				// writing data into leftStream
				RakNet::BitStream leftStream;
				leftStream.Write((unsigned char)ID_GAME_READY);
				leftStream.Write((int)mSpeedController.getGameSpeed());
				strncpy(name, mMatch->getPlayer(RIGHT_PLAYER).getName().c_str(), sizeof(name));
				leftStream.Write(name, sizeof(name));
				leftStream.Write(mMatch->getPlayer(RIGHT_PLAYER).getStaticColor().toInt());

				// writing data into rightStream
				RakNet::BitStream rightStream;
				rightStream.Write((unsigned char)ID_GAME_READY);
				rightStream.Write((int)mSpeedController.getGameSpeed());
				strncpy(name, mMatch->getPlayer(LEFT_PLAYER).getName().c_str(), sizeof(name));
				rightStream.Write(name, sizeof(name));
				rightStream.Write(mMatch->getPlayer(LEFT_PLAYER).getStaticColor().toInt());

				mServer->Send(leftStream, HIGH_PRIORITY, RELIABLE_ORDERED, mLeftPlayer);
				mServer->Send(rightStream, HIGH_PRIORITY, RELIABLE_ORDERED, mRightPlayer);
			}

			break;
		}

		default:
			printf("unknown packet %d received\n",
				int(packet->data[0]));
			break;
	}
}

bool NetworkGame::isGameValid() const
{
	return mGameValid;
}


void NetworkGame::step()
{
	if (!isGameStarted())
		return;

	// don't record the pauses
	if(!mMatch->isPaused())
	{
		mRecorder->record(mMatch->getState());

		mMatch->step();
		mScore[LEFT_PLAYER] = mMatch->getScore(LEFT_PLAYER);
		mScore[RIGHT_PLAYER] = mMatch->getScore(RIGHT_PLAYER);

		broadcastGameEvents();

		PlayerSide winning = mMatch->winningPlayer();
		if (winning != NO_PLAYER)
		{
			// remember the first winner before the clients hear of it, so a player who
			// leaves right after the win does not turn the game into an abandoned one
			int nobody = NO_PLAYER;
			mWinner.compare_exchange_strong(nobody, winning);

			// if someone has won, the game is paused
			mMatch->pause();
			mRecorder->record(mMatch->getState());
			mRecorder->finalize( mMatch->getScore(LEFT_PLAYER), mMatch->getScore(RIGHT_PLAYER) );

			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_WIN_NOTIFICATION);
			stream.Write(winning);

			RakNet::BitStream switchStream;
			switchStream.Write((unsigned char)ID_WIN_NOTIFICATION);
			switchStream.Write(winning == LEFT_PLAYER ? RIGHT_PLAYER : LEFT_PLAYER);

			broadcastBitstream(stream, switchStream);
		}

		broadcastPhysicState(mMatch->getState());
	}
}

void NetworkGame::broadcastPhysicState(const DuelMatchState& state) const
{
	DuelMatchState ms = state;	// modifiable copy

	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_GAME_UPDATE);
	stream.Write( mLeftLastTime );

	/// \todo this required dynamic memory allocation! not good!
	std::shared_ptr<GenericOut> out = createGenericWriter( &stream );

	if (mSwitchLeft)
		ms.swapSides();

	out->generic<DuelMatchState> (ms);
	mServer->Send(stream, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, mLeftPlayer);

	// reset state and stream
	stream.Reset();
	stream.Write((unsigned char)ID_GAME_UPDATE);
	stream.Write( mRightLastTime );

	out = createGenericWriter( &stream );

	// either switch back, or perform switching for right side
	if (mSwitchLeft != mSwitchRight)
		ms.swapSides();

	out->generic<DuelMatchState> (ms);

	mServer->Send(stream, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, mRightPlayer);
}

// helper function that writes a single event to bit stream in a space efficient way.
void NetworkGame::writeEventToStream(RakNet::BitStream& stream, MatchEvent e, bool switchSides ) const
{
	stream.Write((unsigned char)e.event);
	if( switchSides )
		stream.Write((unsigned char)(e.side == LEFT_PLAYER ? RIGHT_PLAYER : LEFT_PLAYER ) );
	else
		stream.Write((unsigned char)e.side);
	if( e.event == MatchEvent::BALL_HIT_BLOB )
		stream.Write( e.intensity );
}

void NetworkGame::broadcastGameEvents() const
{
	RakNet::BitStream stream;

	auto events = mMatch->getEvents();
	// send the events
	if( events.empty() )
		return;
	// add all the events to the stream
	stream.Write( (unsigned char)ID_GAME_EVENTS );
	for(auto& e : events)
		writeEventToStream(stream, e, mSwitchLeft );
	stream.Write((char)0);
	mServer->Send( stream, HIGH_PRIORITY, RELIABLE_ORDERED, mLeftPlayer);

	stream.Reset();
	stream.Write( (unsigned char)ID_GAME_EVENTS );
	for(auto& e : events)
		writeEventToStream(stream, e, mSwitchRight );
	stream.Write((char)0);
	mServer->Send( stream, HIGH_PRIORITY, RELIABLE_ORDERED, mRightPlayer);
}

PlayerSide NetworkGame::getWinner() const
{
	return (PlayerSide)mWinner.load();
}

int NetworkGame::getScore( PlayerSide side ) const
{
	if( side == LEFT_PLAYER || side == RIGHT_PLAYER )
		return mScore[side];
	return 0;
}

PlayerID NetworkGame::getPlayerID( PlayerSide side ) const
{
	if( side == LEFT_PLAYER )
	{
		return mLeftPlayer;
	}
	else if(side == RIGHT_PLAYER)
	{
		return mRightPlayer;
	}

	assert(0);
	return UNASSIGNED_PLAYER_ID;
}

