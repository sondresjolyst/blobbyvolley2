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
#include "NetworkMessage.h"

/* includes */
#include <cstring>
#include <ostream>
#include <cassert>

#include "UserConfig.h"
#include "PlayerIdentity.h"
#include "GenericIO.h"
#include "DuelMatchState.h"
#include "Global.h"
#include "server/MatchAssignments.h"

/* implementation */
ServerInfo::ServerInfo(RakNet::BitStream& stream, const char* ip, uint16_t p)
{
	activegames = 0;
	waitingplayers = 0;
	memset(hostname, 0, sizeof(hostname));
	memset(name, 0, sizeof(name));
	memset(description, 0, sizeof(description));

	strncpy(hostname, ip, sizeof(hostname));
	hostname[sizeof(hostname) - 1] = 0;

	port = p;

	stream.Read(activegames);
	stream.Read(name, sizeof(name));
	stream.Read(waitingplayers);
	stream.Read(description, sizeof(description));

	// ensure the strings are null terminated
	name[sizeof(name) - 1] = 0;
	description[sizeof(description) - 1] = 0;
}

ServerInfo::ServerInfo(const IUserConfigReader& config)
{
	/// \todo we only need a config reader here!

	// default values
	std::string n = "Blobby Volley 2 Server";
	std::string d = "no description available";

	activegames = 0;
	memset(hostname, 0, sizeof(hostname));
	memset(name, 0, sizeof(name));
	memset(description, 0, sizeof(description));
	waitingplayers = 0;

	std::string tmp;
	tmp = config.getString("name", n);
	strncpy(name, tmp.c_str(), sizeof(name) - 1);
	tmp = config.getString("description", d);
	strncpy(description, tmp.c_str(), sizeof(description) - 1);
	port = config.getInteger("port", BLOBBY_PORT);
}

ServerInfo::ServerInfo(const std::string& playername)
{
	activegames = 0;
	memset(hostname, 0, sizeof(hostname));
	memset(name, 0, sizeof(name));
	memset(description, 0, sizeof(description));
	waitingplayers = 0;

	std::strncpy(hostname, "localhost", sizeof(hostname));
	port = BLOBBY_PORT;

	std::strncpy(name, std::string(playername + "'s game").c_str(), sizeof(name) - 1);
	std::strncpy(description, "client hosted game", sizeof(description) - 1);
}

void ServerInfo::writeToBitstream(RakNet::BitStream& stream)
{
	stream.Write(activegames);
	stream.Write(name, sizeof(name));
	stream.Write(waitingplayers);
	stream.Write(description, sizeof(description));
	assert( stream.GetNumberOfBytesUsed() == static_cast<int>(BLOBBY_SERVER_PRESENT_PACKET_SIZE) );
}

const size_t ServerInfo::BLOBBY_SERVER_PRESENT_PACKET_SIZE = sizeof((unsigned char)ID_BLOBBY_SERVER_PRESENT)
		+ 2 * sizeof(int) 	// activegames &  waiting players
		+ 32				// name
		+ 192;			// description



void makeEnterServerPacket(RakNet::BitStream& stream, const PlayerIdentity& player)
{
	stream.Write((unsigned char)ID_ENTER_SERVER);

	// Send preferred side
	stream.Write( player.getPreferredSide() );

	// Send playername
	char myname[16];
	strncpy(myname, player.getName().c_str(), sizeof(myname));
	stream.Write(myname, sizeof(myname));

	// send color settings
	stream.Write(player.getStaticColor().toInt());

	// tell the server what this client can do
	stream.Write(CLIENT_FEATURES_MARKER);
	stream.Write((uint32_t)CLIENT_FEATURE_TOURNAMENT);
}

uint32_t readClientFeatures(RakNet::BitStream& stream)
{
	uint32_t marker = 0;
	uint32_t features = 0;
	if (stream.GetNumberOfUnreadBits() < 64)
		return 0;
	if (!stream.Read(marker) || marker != CLIENT_FEATURES_MARKER)
		return 0;
	if (!stream.Read(features))
		return 0;
	return features;
}

bool readRulesChecksum(RakNet::BitStream& stream, RulesChecksumData& data)
{
	stream.IgnoreBytes(1);	// ID_RULES_CHECKSUM
	if (!stream.Read(data.checksum) || !stream.Read(data.scoreToWin))
		return false;
	return isValidScoreToWin(data.scoreToWin);
}

void makeRulesAnswerPacket(RakNet::BitStream& stream, bool needRules)
{
	stream.Write((unsigned char)ID_RULES);
	stream.Write(needRules);
}

GameReadyData readGameReady(RakNet::BitStream& stream)
{
	GameReadyData data;
	stream.IgnoreBytes(1);	// ID_GAME_READY
	stream.Read(data.speed);

	char name[16] = {0};
	stream.Read(name, sizeof(name));
	// ensures that the name is null terminated
	name[sizeof(name) - 1] = '\0';
	data.opponent = name;

	stream.Read(data.color);
	return data;
}

void readGameUpdate(RakNet::BitStream& stream, unsigned& inputTime, DuelMatchState& state)
{
	stream.IgnoreBytes(1);	// ID_GAME_UPDATE
	inputTime = 0;
	stream.Read(inputTime);
	auto in = createGenericReader(&stream);
	in->generic<DuelMatchState>(state);
}

bool readWinNotification(RakNet::BitStream& stream, int& winner)
{
	stream.IgnoreBytes(1);	// ID_WIN_NOTIFICATION
	winner = NO_PLAYER;
	return stream.Read(winner) && isPlayerSide((PlayerSide)winner);
}



bool operator == (const ServerInfo& lval, const ServerInfo& rval)
{
	// check if ip and port are identical!
	/// \todo maybe we should user raknets pladerId directly?
	return lval.port == rval.port && !strncmp(lval.hostname, rval.hostname,
			sizeof(lval.hostname));
}

std::ostream& operator<<(std::ostream& stream, const ServerInfo& val)
{
	return stream << val.name << " (" << val.hostname << ":" << val.port << ")";
}

void ServerStatusLists::read(GenericIn& in)
{
	in.generic<std::vector<unsigned int>>( speeds );
	in.generic<std::vector<std::string>>( rules );
	in.generic<std::vector<std::string>>( rulesAuthors );
	in.generic<std::vector<unsigned int>>( gameIds );
	in.generic<std::vector<std::string>>( gameNames );
	in.generic<std::vector<unsigned char>>( gameSpeeds );
	in.generic<std::vector<unsigned char>>( gameRules );
	in.generic<std::vector<unsigned char>>( gameScores );
	in.generic<std::deque<bool>>( gamePasswords );
}

bool ServerStatusLists::consistent() const
{
	if( speeds.empty() || rules.empty() || rulesAuthors.size() != rules.size() )
		return false;

	const std::size_t games = gameIds.size();
	if( gameNames.size() != games || gameSpeeds.size() != games || gameRules.size() != games ||
		gameScores.size() != games || gamePasswords.size() != games )
		return false;

	for( std::size_t i = 0; i < games; ++i )
	{
		if( gameSpeeds[i] >= speeds.size() || gameRules[i] >= rules.size() )
			return false;
	}
	return true;
}
