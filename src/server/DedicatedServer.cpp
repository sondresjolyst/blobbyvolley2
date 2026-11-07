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
#include "FileExceptions.h"

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

	// wait before an assigned game whose player could not be found is tried again
	const unsigned TOURNAMENT_RETRY_DELAY_MS = 1000;

	unsigned long long steadyMilliseconds()
	{
		auto now = std::chrono::steady_clock::now().time_since_epoch();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
	}
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
	// this runs on the update thread of the server. When another thread holds the server, the packets
	// are taken on the next update, so the destructor can stop that thread while it holds the server.
	packet_ptr packet;
	while (mServer->tryAccess( [&](RakServer& srv){ packet = srv.Receive(); } ) && packet)
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
			case ID_SPONSORS:
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
					mEntryDeadlines.connected( packet->playerId, std::chrono::steady_clock::now() );
					syslog(LOG_DEBUG, "Connection incoming (%d) from %s, %d clients connected now",
						   packet_id, packet->playerId.toString().c_str(), getConnectedClients());
				} else {
					syslog(LOG_NOTICE, "Connection incoming (%d) from %s, but is already connected.",
						   packet_id, packet->playerId.toString().c_str());
				}
				break;
			case ID_CONNECTION_LOST:
			case ID_DISCONNECTION_NOTIFICATION:
				removeConnection( packet );
				break;
			case ID_ENTER_SERVER:
			case ID_LOBBY:
			case ID_BLOBBY_SERVER_PRESENT:
			case ID_SPONSORS:
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
		mJoinCodes->poll( steadyMilliseconds() );
	closeConnectionsThatDidNotEnter();
	closeRefusedConnections();

	if( isTournament() )
	{
		updateTournament();
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

void DedicatedServer::setTournamentDirector( std::unique_ptr<TournamentDirector> director )
{
	mTournament = std::move(director);
	mTournamentGeneration = 0;
	// no casual games on a tournament server
	mMatchMaker.setAllowNewGames( !mTournament );
}

void DedicatedServer::setSponsorSource( SponsorSource source, unsigned interval_ms )
{
	mSponsorSource = std::move(source);
	mSponsorInterval = interval_ms;
	mNextSponsorPoll = 0;
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
	mEntryDeadlines.remove(source);

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
	const uint32_t features = readClientFeatures(data);

	if( mJoinCodes )
	{
		// an older client would get stuck at the second game of a series
		if( (features & CLIENT_FEATURE_TOURNAMENT) == 0 )
		{
			refuseEntry( source, EntryRefusedReason::OUTDATED_CLIENT );
			return;
		}

		// a known code entered again moves to the new connection, the old one is closed
		PlayerID holder;
		if( mJoinCodes->findHolder( newplayer->getName(), holder ) && holder != source )
			handOverJoinCode( holder );

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

	if( isTournament() )
	{
		mTournament->playerConnected( mJoinCodes->playerIdOf(source) );
		// only a game that knows tournaments gets the sponsor board
		if( (features & CLIENT_FEATURE_TOURNAMENT) != 0 )
			mSponsorPlayers.insert( source );
	}

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

void DedicatedServer::removeConnection(const packet_ptr& packet)
{
	const int packet_id = packet->data[0];
	mActiveConnections.erase(packet->playerId);
	mRefusedConnections.erase(packet->playerId);
	mEntryDeadlines.remove(packet->playerId);
	mSentStatus.erase(packet->playerId);
	mSponsorPlayers.erase(packet->playerId);
	mSentSponsorVersion.erase(packet->playerId);
	mSentLogos.erase(packet->playerId);
	mHeldLogos.erase(packet->playerId);
	if( mJoinCodes )
	{
		if( mTournament )
			mTournament->playerDisconnected( mJoinCodes->playerIdOf(packet->playerId) );
		mJoinCodes->release(packet->playerId);
	}

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
}

void DedicatedServer::handOverJoinCode(PlayerID holder)
{
	// the old holder learns why it is closed
	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_LOBBY);
	stream.Write((unsigned char)LobbyPacketType::ENTRY_REFUSED);
	stream.Write((unsigned char)EntryRefusedReason::CODE_IN_USE);
	mServer->Send(stream, HIGH_PRIORITY, RELIABLE_ORDERED, holder);

	// then it leaves like a lost connection, so lobby, game and tournament forget it
	packet_ptr lost(new Packet(), [](Packet* p){ delete[] p->data; delete p; });
	lost->playerIndex = UNASSIGNED_PLAYER_INDEX;
	lost->playerId = holder;
	lost->data = new unsigned char[1]{ (unsigned char)ID_CONNECTION_LOST };
	lost->length = 1;
	lost->bitSize = 8;
	removeConnection( lost );

	// the connection closes once the reason had time to arrive
	mRefusedConnections[holder] = std::chrono::steady_clock::now() + REFUSED_CLOSE_DELAY;
	syslog(LOG_NOTICE, "Join code handed over, closing the connection from %s", holder.toString().c_str());
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

void DedicatedServer::closeConnectionsThatDidNotEnter()
{
	auto now = std::chrono::steady_clock::now();
	for( PlayerID target : mEntryDeadlines.takeExpired(now) )
	{
		if( isConnected(target) && mRefusedConnections.count(target) == 0 )
		{
			mRefusedConnections[target] = now;
			syslog(LOG_NOTICE, "Connection from %s did not enter in time", target.toString().c_str());
		}
	}
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

			// in tournament mode the server picks the matches
			if( isTournament() )
			{
				syslog(LOG_DEBUG, "Ignored lobby request from %s in tournament mode", source.toString().c_str());
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
		case ID_SPONSORS:
		{
			processSponsorRequest( source, data );
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

// - - - - - - - - - - - - - - - - - -
// 			tournament matches
// - - - - - - - - - - - - - - - - - -

bool DedicatedServer::isTournament() const
{
	return mJoinCodes && mTournament;
}

void DedicatedServer::updateTournament()
{
	const unsigned long long now = steadyMilliseconds();

	// a new players file brings new matches and maybe new player ids
	if( mJoinCodes->getGeneration() != mTournamentGeneration )
	{
		mTournamentGeneration = mJoinCodes->getGeneration();
		mTournament->setAssignments( mJoinCodes->getMatches() );

		std::set<int> connected;
		for( const auto& p : mPlayerMap )
		{
			int id = mJoinCodes->playerIdOf( p.first );
			if( id > 0 )
				connected.insert( id );
		}
		mTournament->setConnectedPlayers( connected );
	}

	checkTournamentGames( now );

	for( const auto& order : mTournament->takeGamesToStart( now ) )
	{
		startTournamentGame( order );
	}

	sendTournamentStatus( now );
	updateSponsors( now );
}

void DedicatedServer::checkTournamentGames(unsigned long long now_ms)
{
	for( auto it = mTournamentGames.begin(); it != mTournamentGames.end(); )
	{
		TournamentGame& entry = *it;
		if( !entry.reported && entry.game->getWinner() == NO_PLAYER && entry.game->isGameValid() )
		{
			// the site shows the score of running games
			const int points1 = entry.game->getScore(LEFT_PLAYER);
			const int points2 = entry.game->getScore(RIGHT_PLAYER);
			if( points1 != entry.livePoints1 || points2 != entry.livePoints2 )
			{
				entry.livePoints1 = points1;
				entry.livePoints2 = points2;
				mTournament->reportLiveScore( entry.matchId, points1, points2 );
			}
		}
		if( !entry.reported )
		{
			// the winner is checked first, since a player may leave right after the win
			PlayerSide winner = entry.game->getWinner();
			if( winner != NO_PLAYER )
			{
				entry.reported = true;
				mTournament->gameFinished( entry.matchId, winner == LEFT_PLAYER ? 1 : 2,
							entry.game->getScore(LEFT_PLAYER), entry.game->getScore(RIGHT_PLAYER),
							entry.startedAt, std::chrono::system_clock::now(), now_ms );
			}
			else if( !entry.game->isGameValid() )
			{
				entry.reported = true;
				// the player who left is no longer in the player map. Player 1 plays on the left.
				int left = 0;
				const bool leftGone = mPlayerMap.count( entry.game->getPlayerID(LEFT_PLAYER) ) == 0;
				const bool rightGone = mPlayerMap.count( entry.game->getPlayerID(RIGHT_PLAYER) ) == 0;
				if( leftGone != rightGone )
					left = leftGone ? entry.player1 : entry.player2;
				mTournament->gameAbandoned( entry.matchId,
							entry.game->getScore(LEFT_PLAYER), entry.game->getScore(RIGHT_PLAYER),
							entry.startedAt, std::chrono::system_clock::now(), left );
			}
		}

		// a finished game stays until a player leaves or the next game replaces it
		if( entry.reported && !entry.game->isGameValid() )
			it = mTournamentGames.erase( it );
		else
			++it;
	}
}

void DedicatedServer::endGameOf(NetworkPlayer& player)
{
	std::shared_ptr<NetworkGame> old = player.getGame();
	if( !old )
		return;

	// detach every player, so the old game cannot send anything to them anymore
	{
		std::lock_guard<std::mutex> lock( mPlayerMapMutex );
		for( auto& p : mPlayerMap )
		{
			if( p.second->getGame() == old )
				p.second->setGame( nullptr );
		}
	}

	mGameList.remove( old );
	for( auto it = mTournamentGames.begin(); it != mTournamentGames.end(); )
	{
		if( it->game == old )
			it = mTournamentGames.erase( it );
		else
			++it;
	}
	// the game thread stops when the last reference goes away
}

std::vector<unsigned char> DedicatedServer::finishForNextGame(const std::shared_ptr<NetworkGame>& game)
{
	if( !game )
		return std::vector<unsigned char>();
	game->finish();
	return game->makeReplayPacket();
}

void DedicatedServer::startTournamentGame(const TournamentDirector::GameOrder& order)
{
	const MatchAssignment& match = order.match;

	PlayerID id1, id2;
	auto player1 = mPlayerMap.end();
	auto player2 = mPlayerMap.end();
	if( mJoinCodes->findConnection( match.player1, id1 ) )
		player1 = mPlayerMap.find( id1 );
	if( mJoinCodes->findConnection( match.player2, id2 ) )
		player2 = mPlayerMap.find( id2 );
	if( player1 == mPlayerMap.end() || player2 == mPlayerMap.end() )
	{
		// log once and try again a little later, not on every tick
		if( mMissingPlayerLogged.insert( match.matchId ).second )
			syslog(LOG_ERR, "Match %d: a player is not connected after all, the game waits", match.matchId);
		mTournament->gameNotStarted( match.matchId, true, steadyMilliseconds() + TOURNAMENT_RETRY_DELAY_MS );
		return;
	}
	mMissingPlayerLogged.erase( match.matchId );

	NetworkPlayer& left = *player1->second;
	NetworkPlayer& right = *player2->second;

	// an earlier game of these players is over, the director only starts free players.
	// Its queued packets are handled first, so a replay asked for in time still arrives.
	std::vector<unsigned char> replayLeft = finishForNextGame( left.getGame() );
	std::vector<unsigned char> replayRight = left.getGame() == right.getGame() ? replayLeft : finishForNextGame( right.getGame() );
	endGameOf( left );
	endGameOf( right );

	mMatchMaker.removePlayer( id1 );
	mMatchMaker.removePlayer( id2 );

	// player 1 plays on the left. Each player still sees itself on its own side.
	// As in the casual lobby, a player without a side is never mirrored.
	bool switchLeft = left.getDesiredSide() == RIGHT_PLAYER;
	bool switchRight = right.getDesiredSide() == LEFT_PLAYER;

	std::shared_ptr<NetworkGame> game;
	try
	{
		game = std::make_shared<NetworkGame>( mServer.get(), left, right, switchLeft, switchRight,
							match.ruleFile, match.scoreToWin, (float)match.speed );
	}
	catch( const std::exception& error )
	{
		syslog(LOG_ERR, "Match %d: could not create game %d with rules %s: %s",
			   match.matchId, order.game, match.ruleFile.c_str(), error.what());
		mTournament->gameNotStarted( match.matchId );
		return;
	}
	catch( ... )
	{
		syslog(LOG_ERR, "Match %d: could not create game %d with rules %s",
			   match.matchId, order.game, match.ruleFile.c_str());
		mTournament->gameNotStarted( match.matchId );
		return;
	}

	// a tournament game cannot be paused, so one player cannot freeze a match
	game->setPauseAllowed( false );

	// a late replay request reaches the new game, which answers with the old replay
	game->setPreviousReplay( LEFT_PLAYER, std::move(replayLeft) );
	game->setPreviousReplay( RIGHT_PLAYER, std::move(replayRight) );

	{
		std::lock_guard<std::mutex> lock( mPlayerMapMutex );
		left.setGame( game );
		right.setGame( game );
	}

	SWLS_Games++;
	mGameList.push_back( game );
	mTournamentGames.push_back( TournamentGame{ match.matchId, game, std::chrono::system_clock::now(), false,
												match.player1, match.player2, -1, -1 } );

	syslog(LOG_NOTICE, "Match %d game %d started: '%s' vs. '%s', rules %s, %d points, speed %d, best of %d",
		   match.matchId, order.game, left.getName().c_str(), right.getName().c_str(),
		   match.ruleFile.c_str(), match.scoreToWin, match.speed, match.bestOf);
}

void DedicatedServer::sendTournamentStatus(unsigned long long now_ms)
{
	for( const auto& p : mPlayerMap )
	{
		int id = mJoinCodes->playerIdOf( p.first );
		PlayerTournamentStatus status = mTournament->statusOf( id, now_ms );

		auto sent = mSentStatus.find( p.first );
		if( sent != mSentStatus.end() && sent->second == status )
			continue;
		mSentStatus[p.first] = status;

		std::string opponent;
		if( const JoinCodeEntry* entry = mJoinCodes->getTable().findById( status.opponentId ) )
			opponent = entry->nickname;

		// the player's name is the nickname from the players file since the join code was accepted
		RakNet::BitStream stream;
		unsigned nextGameIn = status.nextGameAt > now_ms ? (unsigned)(status.nextGameAt - now_ms) : 0;
		writeTournamentStatus( stream, status, opponent, p.second->getName(), nextGameIn );
		mServer->Send( stream, LOW_PRIORITY, RELIABLE_ORDERED, p.first );
	}
}

// - - - - - - - - - - - - - - - - - -
// 			sponsor board
// - - - - - - - - - - - - - - - - - -

void DedicatedServer::updateSponsors(unsigned long long now_ms)
{
	if( mSponsorSource && now_ms >= mNextSponsorPoll )
	{
		mNextSponsorPoll = now_ms + mSponsorInterval;
		sponsors::Board board;
		if( mSponsorSource( board ) )
		{
			// held logos point into the old board
			mHeldLogos.clear();
			mSponsors.setBoard( std::move(board) );
			syslog(LOG_NOTICE, "Sponsor board %s with %d logos is sent to the players",
				   sponsors::boardStyleName(mSponsors.board().style), (int)mSponsors.board().logos.size());
		}
	}

	if( !mSponsors.hasBoard() )
		return;

	// each player gets the announcement once per board version
	for( const auto& player : mSponsorPlayers )
	{
		// players accept the board only after their tournament status
		if( mSentStatus.count(player) == 0 )
			continue;
		std::string& sent = mSentSponsorVersion[player];
		if( sent == mSponsors.version() )
			continue;
		sent = mSponsors.version();
		mSentLogos[player].clear();

		RakNet::BitStream stream;
		mSponsors.writeAnnouncement( stream );
		mServer->SendOnChannel( stream, LOW_PRIORITY, RELIABLE_ORDERED, sponsors::NETWORK_CHANNEL, player );
	}

	// logos asked for during a game go out once the game is over
	for( auto it = mHeldLogos.begin(); it != mHeldLogos.end(); )
	{
		if( inRunningGame( it->first ) )
		{
			++it;
			continue;
		}
		for( const sponsors::LogoFile* logo : it->second )
		{
			RakNet::BitStream answer;
			sponsors::writeLogo( answer, *logo );
			mServer->SendOnChannel( answer, LOW_PRIORITY, RELIABLE_ORDERED, sponsors::NETWORK_CHANNEL, it->first );
		}
		it = mHeldLogos.erase( it );
	}
}

bool DedicatedServer::inRunningGame(PlayerID player) const
{
	auto found = mPlayerMap.find( player );
	if( found == mPlayerMap.end() )
		return false;
	auto game = found->second->getGame();
	return game && game->isGameValid() && game->getWinner() == NO_PLAYER;
}

void DedicatedServer::processSponsorRequest(PlayerID source, RakNet::BitStream& stream)
{
	if( !isTournament() || mSponsorPlayers.count(source) == 0 )
		return;

	sponsors::SponsorPacketType type;
	if( !sponsors::readPacketType( stream, type ) || type != sponsors::SponsorPacketType::REQUEST )
		return;

	// a player gets each logo once, so a request cannot make the server send the same data again and again
	std::vector<const sponsors::LogoFile*> logos = mSponsors.answer( stream, mSentLogos[source] );
	if( inRunningGame( source ) )
	{
		// the logos wait until the game is over, so they do not compete with it
		auto& held = mHeldLogos[source];
		held.insert( held.end(), logos.begin(), logos.end() );
		return;
	}
	for( const sponsors::LogoFile* logo : logos )
	{
		RakNet::BitStream answer;
		sponsors::writeLogo( answer, *logo );
		mServer->SendOnChannel( answer, LOW_PRIORITY, RELIABLE_ORDERED, sponsors::NETWORK_CHANNEL, source );
	}
}
