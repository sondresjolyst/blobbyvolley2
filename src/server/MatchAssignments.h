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

#include <set>
#include <string>
#include <vector>

#include "TournamentProtocol.h"

/// A match that the tournament site assigned to two players.
/// The fields have the names and meanings of a match in the sync of the site.
struct MatchAssignment
{
	int matchId = 0;
	/// Player id of player 1, who plays on the left.
	int player1 = 0;
	/// Player id of player 2, who plays on the right.
	int player2 = 0;
	std::string ruleFile = "default.lua";
	int scoreToWin = 15;
	/// Game speed in frames per second.
	int speed = 75;
	/// Number of games in the series. A player who wins more than half of them wins the match.
	int bestOf = 1;
	/// Number of the next game to play.
	int nextGame = 1;
	/// True while the organizer has to decide about an abandoned game. No game starts then.
	bool awaitingDecision = false;
	/// Games player 1 and player 2 have won so far, as the site counts them. -1 when the file does not say.
	int wins1 = -1;
	int wins2 = -1;
	/// Seconds between two games of the series. -1 when the file does not say, then the server config decides.
	int nextGameDelay = -1;

	bool hasPlayer(int playerId) const { return playerId == player1 || playerId == player2; }
	int opponentOf(int playerId) const { return playerId == player1 ? player2 : player1; }
};

bool operator==(const MatchAssignment& a, const MatchAssignment& b);
bool operator!=(const MatchAssignment& a, const MatchAssignment& b);

/// Brings a pause between two games, in seconds, into the allowed range.
int clampNextGameDelay(int seconds);

/// True if name is a plain rules file name such as default.lua, without a folder.
bool isValidRuleFileName(const std::string& name);

/*! Reads the match elements of a players file.
	\details The file looks like this:
	\code
	<tournament>
		<player id="12" nickname="Ola" code="K7QM2XPR4T"/>
		<player id="17" nickname="Kari" code="B9HNZ3WD6F"/>
		<match id="7" player1="12" player2="17" ruleFile="default.lua" scoreToWin="15" speed="75" bestOf="3" wins1="1" wins2="0"/>
	</tournament>
	\endcode
	Only id, player1 and player2 are required. A match with a bad value, or with a player
	that is not in knownPlayers, is skipped. So is a match whose wins already decide the series,
	since a decided match should have left the file. A match id that appears more than once is
	dropped for every match that uses it.
	Returns false and sets error when the text is not a players file.
*/
bool parseMatchAssignments(const std::string& xml, const std::set<int>& knownPlayers,
						std::vector<MatchAssignment>& matches, std::vector<std::string>& warnings,
						std::string& error);
