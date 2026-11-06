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

#include "DedicatedServer.h"

#include <atomic>
#include <set>
#include <algorithm>
#include <iostream>
#include <utility>

#include "raknet/RakServer.h"
#include "raknet/PacketEnumerations.h"

#include "NetworkMessage.h"
#include "ThreadSafeRakServer.h"
#include "NetworkGame.h"
#include "GenericIO.h"

#ifndef WIN32
#ifndef __ANDROID__
#ifndef __SWITCH__
#include <sys/syslog.h>
#endif
#endif
#endif

extern std::atomic<int> SWLS_PacketCount;
extern std::atomic<int> SWLS_Connections;
extern std::atomic<int> SWLS_Games;

namespace
{
	// time a refused client gets to receive the reason before the connection closes
	const std::chrono::milliseconds REFUSED_CLOSE_DELAY(1000);
}

#ifdef __ANDROID__
extern "C"
#endif
void syslog(int pri, const char* format, ...);

DedicatedServer::DedicatedServer(ServerInfo info,
								const std::vector<std::string>& rulefiles,
								const std::vector<float>& gamespeeds,
								int max_clients, bool local_server)
: mServer(new ThreadSafeRakServer())
, mAcceptNewPlayers(true)
, mPlayerHosted( local_server )
, mServerInfo(std::move(info))
{
	if (!mServer->access([&](RakServer& srv){ return srv.Start(max_clients, 1, mServerInfo.port);}))
	{
		syslog(LOG_ERR, "Couldn't bind to port %i, exiting", mServerInfo.port);
		throw(2);
	}

	/// \todo this code should be places in ServerInfo
	mMatchMaker.setSendFunction([&](const RakNet::BitStream& stream, PlayerID target){ mServer->Send(stream, LOW_PRIORITY, RELIABLE_ORDERED, target); });
	mMatchMaker.setCreateGame([&](NetworkPlayer& left, NetworkPlayer& right,
								PlayerSide switchSide, const std::string& rules, int stw, float sp){
							createGame(left, right, switchSide, rules, stw, sp); });

	// add gamespeeds
	for( auto& s : gamespeeds )
		mMatchMaker.addGameSpeedOption( s );

	// add rules
	for( const auto& f : rulefiles )
		mMatchMaker.addRuleOption( f );

	mServer->access([&](RakServer& srv){
		srv.setUpdateCallback([this](){ queuePackets(); }
	);} );
}

DedicatedServer::~DedicatedServer()
{
	mServer->access( [](RakServer& srv){ srv.Disconnect(50); } );
}

void DedicatedServer::queuePackets()
{
	packet_ptr packet;
	while ((packet = mServer->access( [](RakServer& srv){ return srv.Receive(); })))
	{
		SWLS_PacketCount++;

		switch(packet->data[0])
		{
			// connection status changes
			case ID_NEW_INCOMING_CONNECTION:
			case ID_CONNECTION_LOST:
			case ID_DISCONNECTION_NOTIFICATION:
			case ID_ENTER_SERVER:
			case ID_LOBBY:
			case ID_BLOBBY_SERVER_PRESENT:
			{
				std::lock_guard<std::mutex> lock( mPacketQueueMutex );
				mPacketQueue.push_back( packet );
				break;
			}
			// game progress packets
			case ID_INPUT_UPDATE:
			case ID_PAUSE:
			case ID_UNPAUSE:
			case ID_CHAT_MESSAGE:
			case ID_REPLAY:
			case ID_RULES:
			{
				// disallow player map changes while we sort out packets!
				std::lock_guard<std::mutex> lock( mPlayerMapMutex );

				auto player = mPlayerMap.find(packet->playerId);
				// delete the disconnectiong player
				if( player != mPlayerMap.end() && player->second->getGame() )
				{
					player->second->getGame() ->injectPacket( packet );
				} else {
					syslog(LOG_ERR, "received packet from player not in playerlist!");
				}

				break;
			}
			default:
				syslog(LOG_DEBUG, "Unknown packet %d received\n", int(packet->data[0]));
		}
	}
}

/*!
 * \details This function handles packets responsible for connections itself, but delegates the other message
 * types to `processSingleMessage()`. This is both for readability, and also because we want to ensure that errors
 * (in the sense of exceptions) raised by the other message processing does not crash the entire server.
 */
void DedicatedServer::processPackets()
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
		SWLS_PacketCount++;

		int packet_id = packet->data[0];
		if(packet_id != ID_NEW_INCOMING_CONNECTION && !isConnected(packet->playerId)) {
			syslog(LOG_NOTICE, "Incoming packet (%d) from %s, which is not connected. Ignoring packet.",
				   packet_id, packet->playerId.toString().c_str());
			continue;
		}

		switch(packet_id)
		{
			// connection status changes
			case ID_NEW_INCOMING_CONNECTION:
				SWLS_Connections++;
				if ( !mAcceptNewPlayers )
				{
					RakNet::BitStream stream;
					stream.Write( (char)ID_NO_FREE_INCOMING_CONNECTIONS );
					mServer->Send( stream, HIGH_PRIORITY, RELIABLE_ORDERED, packet->playerId);
					mServer->access( [&](RakServer& srv){ srv.CloseConnection( packet->playerId, true ); } );
					syslog(LOG_DEBUG, "Connection not accepted (%d) from %s.",
						   packet_id, packet->playerId.toString().c_str());
				} else if( addConnection(packet->playerId) ) {
					syslog(LOG_DEBUG, "Connection incoming (%d) from %s, %d clients connected now",
						   packet_id, packet->playerId.toString().c_str(), getConnectedClients());
				} else {
					syslog(LOG_NOTICE, "Connection incoming (%d) from %s, but is already connected.",
						   packet_id, packet->playerId.toString().c_str());
				}
				break;
			case ID_CONNECTION_LOST:
			case ID_DISCONNECTION_NOTIFICATION:
			{
				mActiveConnections.erase(packet->playerId);
				mRefusedConnections.erase(packet->playerId);
				if( mJoinCodes )
					mJoinCodes->release(packet->playerId);

				auto player = mPlayerMap.find(packet->playerId);
				// delete the disconnecting player.
				if( player != mPlayerMap.end() )
				{
					const std::string playerName = player->second->getName();
					if( player->second->getGame() )
					{
						player->second->getGame()->injectPacket( packet );
					}

					// no longer count this player as connected. protect this change with a mutex
					{
						std::lock_guard<std::mutex> lock( mPlayerMapMutex );
						mPlayerMap.erase( player );
						// player iterator is invalid after erase
					}
					mMatchMaker.removePlayer(packet->playerId);
					syslog(LOG_DEBUG, "Player removed (%d) from %s (%s), %d players available",
						   packet_id, packet->playerId.toString().c_str(),
						   playerName.c_str(), (int)mPlayerMap.size());
				}

				syslog(LOG_DEBUG, "Connection closed (%d) from %s, %d clients connected now",
					   packet_id, packet->playerId.toString().c_str(), getConnectedClients());
				break;
			}
			case ID_ENTER_SERVER:
			case ID_LOBBY:
			case ID_BLOBBY_SERVER_PRESENT:
				try {
					RakNet::BitStream stream(packet->data, packet->length, false);
					stream.IgnoreBytes(1);	// ignore the message id
					processSingleMessage(static_cast<MessageType>(packet_id), packet->playerId, stream);
				} catch (const std::exception& error) {
					syslog(LOG_ERR, "An error occurred while processing packet (%d) from %s: %s",
						   packet_id, packet->playerId.toString().c_str(), error.what());
				}
				break;
			default:
				syslog(LOG_DEBUG, "Unknown packet %d received\n", packet_id);
		}
	}
}


void DedicatedServer::updateGames()
{
	if( mJoinCodes )
	{
		auto now = std::chrono::steady_clock::now().time_since_epoch();
		mJoinCodes->poll( std::chrono::duration_cast<std::chrono::milliseconds>(now).count() );
		closeRefusedConnections();
	}

	// update new game creation for locally hosted games.
	if( mPlayerHosted )
	{
		mMatchMaker.setAllowNewGames(mMatchMaker.getOpenGamesCount() == 0);
	}

	// this loop ensures that all games that have finished (e.g. because one
	// player left) still process network packets, to let the other player
	// finalize its interactions (sending replays etc).
	for(auto& it : mPlayerMap)
	{
		auto game = it.second->getGame();
		if(game && !game->isGameValid())
		{
			game->processPackets();
		}
	}

	// remove dead games from gamelist
	for (auto iter = mGameList.begin(); iter != mGameList.end();  )
	{
		if (!(*iter)->isGameValid())
		{
			syslog( LOG_DEBUG, "Removed game %s vs %s from gamelist",
					(*iter)->getPlayerID(LEFT_PLAYER).toString().c_str(),
					(*iter)->getPlayerID(RIGHT_PLAYER).toString().c_str()
					);
			iter = mGameList.erase(iter);
		}
		else
		{
			++iter;
		}
	}
}

bool DedicatedServer::hasActiveGame() const
{
	return !mGameList.empty();
}

int DedicatedServer::getActiveGamesCount() const
{
	return mGameList.size();
}

int DedicatedServer::getWaitingPlayers() const
{
	return mPlayerMap.size() - 2 * mGameList.size();
}

const ServerInfo& DedicatedServer::getServerInfo() const
{
	return mServerInfo;
}

int DedicatedServer::getConnectedClients() const
{
	return mActiveConnections.size();
}

void DedicatedServer::allowNewPlayers( bool allow )
{
	mAcceptNewPlayers = allow;
}

void DedicatedServer::setJoinCodeGate( std::unique_ptr<JoinCodeGate> gate )
{
	mJoinCodes = std::move(gate);
	if( mJoinCodes )
	{
		mJoinCodes->setLogFunction([](JoinCodeGate::LogLevel level, const std::string& message){
			syslog(level == JoinCodeGate::LogLevel::PROBLEM ? LOG_ERR : LOG_NOTICE, "%s", message.c_str());
		});
	}
}

// debug
void DedicatedServer::printAllPlayers(std::ostream& stream) const
{
	for(const auto & it : mPlayerMap)
	{
		stream << it.second->getID().toString() << " \"" << it.second->getName() << "\" status: ";
		if( it.second->getGame() )
		{
			stream << "playing\n";
		} else
		{
			stream << "waiting\n";
		}
	}
}

void DedicatedServer::printAllGames(std::ostream& stream) const
{
	for(const auto & it : mGameList)
	{
		stream << it->getPlayerID(LEFT_PLAYER).toString() << " vs " << it->getPlayerID(RIGHT_PLAYER).toString() << "\n";
	}
}

// special packet processing
void DedicatedServer::processBlobbyServerPresent( PlayerID source, RakNet::BitStream& stream )
{
	// If the client knows nothing about versioning, the version is 0.0
	int major = 0;
	int minor = 0;
	bool wrongPackageSize = true;

	// current client has bitSize 72
	if( stream.GetNumberOfBitsUsed() == 72)
	{
		stream.Read(major);
		stream.Read(minor);
		wrongPackageSize = false;
	}

	RakNet::BitStream stream2;

	if (wrongPackageSize)
	{
		syslog(LOG_NOTICE, "Outdated client tried to connect! "
						   "Unable to determine client version due to packet size mismatch: %d",
						   stream.GetNumberOfBitsUsed());
		stream2.Write((unsigned char)ID_VERSION_MISMATCH);
		stream2.Write((int)BLOBBY_VERSION_MAJOR);
		stream2.Write((int)BLOBBY_VERSION_MINOR);
		mServer->Send(stream2, LOW_PRIORITY, RELIABLE_ORDERED, source);
	}
	else if (major < BLOBBY_VERSION_MAJOR
		|| (major == BLOBBY_VERSION_MAJOR && minor < BLOBBY_VERSION_MINOR))
	// Check if the packet contains matching version numbers
	{
		stream2.Write((unsigned char)ID_VERSION_MISMATCH);
		stream2.Write((int)BLOBBY_VERSION_MAJOR);
		stream2.Write((int)BLOBBY_VERSION_MINOR);
		mServer->Send(stream2, LOW_PRIORITY, RELIABLE_ORDERED, source);
	}
	else
	{
		mServerInfo.activegames = mGameList.size();
		mServerInfo.waitingplayers = mPlayerMap.size() - 2 * mServerInfo.activegames;

		stream2.Write((unsigned char)ID_BLOBBY_SERVER_PRESENT);
		mServerInfo.writeToBitstream(stream2);

		mServer->Send(stream2, HIGH_PRIORITY, RELIABLE_ORDERED, source);
	}
}

void DedicatedServer::processEnterServer(PlayerID source, RakNet::BitStream& data) {
	// a connection enters the server only once
	if( mPlayerMap.find(source) != mPlayerMap.end() )
	{
		syslog(LOG_NOTICE, "Repeated entry (%d) from %s ignored.",
			   ID_ENTER_SERVER, source.toString().c_str());
		return;
	}

	// a refused connection only waits to be closed
	if( mRefusedConnections.count(source) != 0 )
	{
		return;
	}

	auto newplayer = std::make_shared<NetworkPlayer>(source, data);

	if( mJoinCodes )
	{
		JoinCodeGate::Result result = mJoinCodes->admit( *newplayer );
		if( result != JoinCodeGate::Result::ACCEPTED )
		{
			refuseEntry( source, result == JoinCodeGate::Result::CODE_IN_USE ?
							EntryRefusedReason::CODE_IN_USE : EntryRefusedReason::UNKNOWN_CODE );
			return;
		}
	}

	// add to player map. protect with mutex
	{
		std::lock_guard<std::mutex> lock( mPlayerMapMutex );
		mPlayerMap[source] = newplayer;
	}

	mMatchMaker.addPlayer(source, newplayer);

	syslog(LOG_DEBUG, "Player added (%d) from %s (%s), %d players available",
		   ID_ENTER_SERVER, source.toString().c_str(),
		   newplayer->getName().c_str(), (int)mPlayerMap.size());

	// if this is a locally hosted server, any player that connects automatically joins an
	// open game
	if (mPlayerHosted &&
		mMatchMaker.getOpenGamesCount() != 0)
	{
		RakNet::BitStream stream;
		stream.Write((unsigned char)ID_LOBBY);
		stream.Write((unsigned char)LobbyPacketType::JOIN_GAME);
		auto writer = createGenericWriter(&stream);
		writer->uint32(mMatchMaker.getOpenGameIDs().front());
		writer->generic<std::string>("");
		mMatchMaker.receiveLobbyPacket( source, stream );
	}
}


void DedicatedServer::refuseEntry(PlayerID source, EntryRefusedReason reason)
{
	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_LOBBY);
	stream.Write((unsigned char)LobbyPacketType::ENTRY_REFUSED);
	stream.Write((unsigned char)reason);
	mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, source);

	mRefusedConnections[source] = std::chrono::steady_clock::now() + REFUSED_CLOSE_DELAY;
	syslog(LOG_NOTICE, "Entry refused for %s", source.toString().c_str());
}

void DedicatedServer::closeRefusedConnections()
{
	auto now = std::chrono::steady_clock::now();
	for( auto it = mRefusedConnections.begin(); it != mRefusedConnections.end(); )
	{
		if( it->second <= now )
		{
			PlayerID target = it->first;
			mServer->access( [&](RakServer& srv){ srv.CloseConnection( target, true ); } );
			mActiveConnections.erase( target );
			it = mRefusedConnections.erase( it );
			syslog(LOG_DEBUG, "Closed refused connection from %s, %d clients connected now",
				   target.toString().c_str(), getConnectedClients());
		}
		else
		{
			++it;
		}
	}
}

void DedicatedServer::processSingleMessage(MessageType message_id, PlayerID source, RakNet::BitStream& data)
{
	switch ( message_id )
	{
		// player connects to server
		case ID_ENTER_SERVER:
		{
			processEnterServer(source, data);
			break;
		}
		case ID_LOBBY:
		{
			if( !mMatchMaker.hasPlayer(source) )
			{
				syslog(LOG_NOTICE, "Received Lobby packet (%d) from %s, who is not in the lobby. Ignoring.",
					   message_id, source.toString().c_str());
				break;
			}

			// which player is wanted as opponent
			mMatchMaker.receiveLobbyPacket( source, data );
			break;
		}
		case ID_BLOBBY_SERVER_PRESENT:
		{
			processBlobbyServerPresent( source, data );
			break;
		}
		default:
			syslog(LOG_ERR, "Invalid message type (%d) passed into `processSingleMessage` function.", message_id);
			throw std::logic_error("Invalid packet type passed into `processSingleMessage`");
	}
}


void DedicatedServer::createGame(NetworkPlayer& left,
								NetworkPlayer& right,
								PlayerSide switchSide,
								const std::string& rules,
								int scoreToWin, float gamespeed)
{
	auto newgame = std::make_shared<NetworkGame>(mServer.get(), left, right,
								switchSide, rules, scoreToWin, gamespeed);
	left.setGame( newgame );
	right.setGame( newgame );

	SWLS_Games++;

	/// \todo add some logging?
	syslog(LOG_DEBUG, "Created game '%s' vs. '%s', rules: '%s'",
		   left.getName().c_str(), right.getName().c_str(), rules.c_str());
	mGameList.push_back(newgame);
}


bool DedicatedServer::isConnected(PlayerID player) const {
	return mActiveConnections.count(player) != 0;
}

bool DedicatedServer::addConnection(PlayerID player) {
	auto result = mActiveConnections.insert(player);
	return result.second;
}
