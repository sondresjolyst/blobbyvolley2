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
#include <iosfwd>
#include <cstdint>
#include <deque>
#include <vector>

#include "raknet/PacketEnumerations.h"
#include "raknet/NetworkTypes.h"
#include "raknet/BitStream.h"
#include "BlobbyDebug.h"
#include "GenericIOFwd.h"

enum MessageType
{
	ID_GENERIC_MESSAGE = ID_RESERVED9 + 1,
	ID_INPUT_UPDATE,	// send input data from client to server
	ID_GAME_UPDATE,		// send game status from server to client [unreliable]
	ID_GAME_EVENTS,		// send game events from server to client [reliable]
	ID_OPPONENT_DISCONNECTED,
	ID_GAME_READY,
	ID_WIN_NOTIFICATION,
	ID_ENTER_SERVER = ID_RESERVED9 + 10,
	ID_PAUSE,
	ID_UNPAUSE,
	ID_BLOBBY_SERVER_PRESENT = ID_RESERVED9 + 13,
	ID_VERSION_MISMATCH = ID_RESERVED9 + 14,
	ID_REPLAY,
	ID_CHAT_MESSAGE,
	ID_RULES_CHECKSUM,
	ID_RULES,
	ID_SERVER_STATUS,
	ID_LOBBY,
	ID_SPONSORS
};

// General Information:
// 	Because the client may choose their side and the server rotates
// 	everything if necessary, PlayerSide information may not be
// 	correct on all peers. When the server sends player side information
// 	to a client, the information has to be converted into the view
// 	of the client.

// ID_INPUT_UPDATE = 63:
// 	Description:
// 		This packet is sent from client to server every frame.
// 		It contains the current input state as three booleans.
// 	Structure:
// 		ID_INPUT_UPDATE
// 		ID_TIMESTAMP
// 		timestamp (int)
// 		left keypress (bool)
// 		right keypress (bool)
// 		up keypress (bool)
//
// ID_PHYSIC_UPDATE:
// 	Description:
// 		The server sends this information of the current physics state
// 		to all clients every frame. Local physic states will always
// 		be overwritten.
// 	Structure:
// 		ID_PHYSIC_UPDATE
// 		ID_TIMESTAMP
// 		timestamp (int)
//		packet_number (unsigned char)
// 		Physic data (analysed by PhysicWorld)
//
// ID_GAME_READY
// 	Description:
// 		Message sent from server to client when all clients are
// 		ready. The input is enabled after this message on the client.
// 		The attribute opponent name carry's the name of the connected
// 		opponent.
// 	Structure:
// 		ID_GAME_READY
//		gamespeed (int)
// 		opponent name (char[16])
//		opponent color (int)
//
// ID_ENTER_SERVER
// 	Description:
// 		Message sent from client to server after connecting to it.
// 		The side attribute tells the server on which side the client
// 		wants to play. The name attribute reports to players name,
// 		truncated to 16 characters. Color is the network color.
// 	Structure:
// 		ID_ENTER_SERVER
// 		side (PlayerSide)
// 		name (char[16])
//		color (int)
//		features marker (uint32, CLIENT_FEATURES_MARKER), only from newer clients
//		features (uint32, ClientFeature bits), only from newer clients
//	Servers read only the fields they know, so the two trailing fields do not
//	bother older servers. A tournament server refuses a client without the
//	tournament feature, since that client cannot play a best-of series.
//
// ID_PAUSE
// 	Description:
// 		Sent from client to server, this message can be seen as a request
// 		to pause the game. From server to client it is an acknowledgement
// 		of the pause and request demand to display an appropriate dialog.
// 	Structure:
// 		ID_PAUSE
//
// ID_UNPAUSE
// 	Description:
// 		As ID_PAUSE, this packets is an acknowledgement if sent from a client
// 		and a request if sent from the server.
// 	Structure:
// 		ID_UNPAUSE
//
// ID_OPPONENTED_DISCONNECTED
// 	Description:
// 		Sent from server to client when an opponent left the game
// 	Structure:
// 		ID_OPPONENT_DISCONNECTED
//
// ID_BLOBBY_SERVER_PRESENT
// 	Description:
// 		Sent from client to probe a server and from server to client
// 		as answer to the same packet.
// 		Sent with version number since alpha 7 in the first case.
// 	Structure:
// 		ID_BLOBBY_SERVER_PRESENT
// 		major (int)
// 		minor (int)
//
// ID_VERSION_MISMATCH
// 	Description:
// 		Sent from server to client if the version number
// 		differs from the one of the server.
// 	Structure:
// 		ID_VERSION_MISMATCH
//		server_major (int)
//		server_minor (int)
//
// ID_REPLAY
// 	Description:
// 		Sent from client to server to request a replay
// 		Sent from server to client to transmit the replay
// 	Structure:
// 		ID_REPLAY
//		size (int)
//		data
//
// ID_RULES_CHECKSUM
// 	Description:
// 		Sent from server to client to tell rules file checksum
// 		Client should send ID_RULES after receiving ID_RULES_CHECKSUM
// 			to tell server if he needs rules file transmitting
// 	Structure:
// 		ID_RULES_CHECKSUM
//		checksum (int)
//
// ID_RULES
// 	Description:
// 		Sent from client to server to request a rules file
// 		Sent from server to client to transmit the rules file
// 		Game is starting only after transmitting a rules file
// 	Structure (from client to server):
// 		ID_RULES
//		needRules (bool)
// 	Structure (from server to client):
// 		ID_RULES
//		size (int)
//		data
//
// ID_LOBBY
// 	Description:
//		This packet is used for matchmaking messages in the lobby.
//		ID_CHALLENGE
//		(unsigned char) TYPE
//
//		ENTRY_REFUSED is only sent by a server in tournament mode. It answers an
//		ID_ENTER_SERVER whose name is not an accepted join code. The server closes
//		the connection shortly after.
//		ID_LOBBY
//		ENTRY_REFUSED
//		reason (EntryRefusedReason)
//
//		TOURNAMENT_STATUS is only sent by a server in tournament mode. It tells a
//		player what the server waits for. Casual lobby requests are ignored there.
//		ID_LOBBY
//		TOURNAMENT_STATUS
//		status (TournamentStatus as unsigned char)
//		opponent nickname (string, empty when there is no match)
//		game number (uint32)
//		best of (uint32)
//		games won by the player (uint32)
//		games won by the opponent (uint32)
//		own nickname (string), shown instead of the name in the options
//		milliseconds until the next game of the series starts (uint32), 0 when none waits
//
// ID_RULES_CHECKSUM
//	In tournament mode the server also sends this to a player in a finished game,
//	when the next game of a best-of series starts.
//
// ID_SPONSORS
//	Description:
//		Only a server in tournament mode sends this, and only to players with the
//		tournament feature. It brings the sponsor board, see SponsorSync.h.
//		The packets use ordering channel 1, so logos do not hold up the game.
//		The server announces the board when a player enters and when it changes:
//		ID_SPONSORS
//		BOARD (SponsorPacketType as unsigned char)
//		version (64 hex characters)
//		style name length (unsigned char), style name
//		logo count (unsigned char, at most 32)
//		per logo: hash (64 hex characters), size (uint32)
//	The player asks for the logos it has not cached:
//		ID_SPONSORS
//		REQUEST
//		version (64 hex characters)
//		count (unsigned char), hashes (64 hex characters each)
//	The server sends each asked logo once:
//		ID_SPONSORS
//		LOGO
//		hash (64 hex characters)
//		size (uint32)
//		data
//

enum class LobbyPacketType : unsigned char
{
	SERVER_STATUS,
	REMOVED_FROM_GAME,
	OPEN_GAME,
	JOIN_GAME,
	LEAVE_GAME,
	GAME_STATUS,
	START_GAME,
	ENTRY_REFUSED,
	TOURNAMENT_STATUS
};

enum class EntryRefusedReason : unsigned char
{
	UNKNOWN_CODE,
	CODE_IN_USE,
	OUTDATED_CLIENT
};

/// Marks the features field at the end of ID_ENTER_SERVER. The bytes spell BTRN.
constexpr uint32_t CLIENT_FEATURES_MARKER = 0x4E525442;

enum ClientFeature : uint32_t
{
	/// The client shows the tournament status and plays the next game of a series.
	CLIENT_FEATURE_TOURNAMENT = 1
};

enum class TournamentStatus : unsigned char
{
	NO_MATCH,
	WAITING_FOR_OPPONENT,
	WAITING_FOR_SLOT,
	WAITING_FOR_ORGANIZER,
	PLAYING,
	NEXT_GAME,
	MATCH_OVER
};

class IUserConfigReader;

struct ServerInfo : public ObjectCounter<ServerInfo>
{
	// read server info from a bit stream, additionally, the server address and port are needed
	ServerInfo(RakNet::BitStream& stream, const char* ip, uint16_t port);
	// read server info from a user config object
	explicit ServerInfo(const IUserConfigReader& config);
	explicit ServerInfo(const std::string& playername);

	void writeToBitstream(RakNet::BitStream& stream);

	/// \todo maybe we should define ServerInfo a little bit more
	///			as e.g., hostname can be left uninitialised on server
	/// we combine to functionalities here: server information and server addresses.
	int activegames;
	uint16_t port;
	char hostname[64];
	char name[32];
	int waitingplayers;
	char description[192];

	static const size_t BLOBBY_SERVER_PRESENT_PACKET_SIZE;
};

// convenience functions for building packets
class PlayerIdentity;
void makeEnterServerPacket(RakNet::BitStream& stream, const PlayerIdentity& player );
/// Reads the features at the end of an ID_ENTER_SERVER packet whose other fields were read. 0 when there are none.
uint32_t readClientFeatures(RakNet::BitStream& stream);

// Readers of the packets a client gets during a game. Each takes the whole packet, from the packet type on.
struct DuelMatchState;

/// The fields of an ID_RULES_CHECKSUM packet
struct RulesChecksumData
{
	int checksum = 0;
	int scoreToWin = 0;
};
/// False when a field is missing or the points to win are not valid.
bool readRulesChecksum(RakNet::BitStream& stream, RulesChecksumData& data);
/// The answer of a client to ID_RULES_CHECKSUM. needRules asks the server to send the rules file.
void makeRulesAnswerPacket(RakNet::BitStream& stream, bool needRules);

/// The fields of an ID_GAME_READY packet. A missing field stays at its default.
struct GameReadyData
{
	int speed = 0;
	std::string opponent;
	int color = 0;
};
GameReadyData readGameReady(RakNet::BitStream& stream);

/// Reads the start of an ID_GAME_UPDATE packet: the time of the last input the server got from this client.
unsigned readGameUpdateTime(RakNet::BitStream& stream);
/// Reads the state of the game that follows the time in an ID_GAME_UPDATE packet.
void readGameUpdateState(RakNet::BitStream& stream, DuelMatchState& state);

/// Reads the winning side of an ID_WIN_NOTIFICATION packet. False when it is not a side.
bool readWinNotification(RakNet::BitStream& stream, int& winner);

/// The lists of a SERVER_STATUS lobby packet, after the waiting player count
struct ServerStatusLists
{
	std::vector<unsigned int> speeds;
	std::vector<std::string> rules;
	std::vector<std::string> rulesAuthors;
	std::vector<unsigned int> gameIds;
	std::vector<std::string> gameNames;
	std::vector<unsigned char> gameSpeeds;
	std::vector<unsigned char> gameRules;
	std::vector<unsigned char> gameScores;
	std::deque<bool> gamePasswords;

	void read(GenericIn& in);
	/// True if there are speeds and rules, every rules file has an author, every game has
	/// an entry in each game list, and the speed and rules of every game are in the lists.
	bool consistent() const;
};

bool operator == (const ServerInfo& lval, const ServerInfo& rval);
std::ostream& operator<<(std::ostream& stream, const ServerInfo& val);
