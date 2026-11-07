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

#include <cstddef>
#include <string>

#include "GenericIOFwd.h"
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "TournamentLimits.h"

// What the game and a tournament server both need to know about tournaments.

/// Number of characters in a join code.
constexpr std::size_t JOIN_CODE_LENGTH = 10;
/// Longest nickname in bytes. The name field on the wire holds 15 bytes and a terminating zero.
constexpr std::size_t MAX_NICKNAME_BYTES = PLAYER_NAME_BUFFER - 1;

/// Trims spaces and turns lower case letters into upper case.
/// Returns an empty string when the result is not a valid join code.
std::string normalizeJoinCode(const std::string& raw);

/// True if code has the right length and only uses the join code alphabet.
bool isValidJoinCode(const std::string& code);

/// True if name is 1 to MAX_NICKNAME_BYTES bytes of UTF-8 without control characters.
bool isValidNickname(const std::string& name);

/// A short part of a name or code that is safe to log. It never holds the whole code.
std::string joinCodeLogPrefix(const std::string& raw);

/// What a tournament server tells a player in a TOURNAMENT_STATUS packet.
struct TournamentStatusData
{
	TournamentStatus status = TournamentStatus::NO_MATCH;
	std::string opponent;
	unsigned game = 0;
	unsigned bestOf = 0;
	unsigned ownWins = 0;
	unsigned opponentWins = 0;
	/// The nickname of the player who gets the packet, empty when the server did not send one.
	std::string ownName;
	/// Milliseconds until the next game starts, as the server counted when it sent the packet.
	unsigned nextGameInMs = 0;
};

/// Reads the body of a TOURNAMENT_STATUS packet, after the packet type.
/// A name that is not a valid nickname is read as empty.
TournamentStatusData readTournamentStatus(GenericIn& in);
