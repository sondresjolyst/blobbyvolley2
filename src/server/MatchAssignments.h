/*=============================================================================
Blobby Volley 2
Copyright (C) 2026 Sjølyst Innovation AS

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

#include "TournamentLimits.h"

namespace tinyxml2
{
	class XMLDocument;
	class XMLElement;
}

/// A match that the tournament site assigned to two players.
/// The fields have the names and meanings of a match in the sync of the site.
struct MatchAssignment
{
	int matchId = 0;
	/// Player id of player 1, who plays on the left.
	int player1 = 0;
	/// Player id of player 2, who plays on the right. 0 in a training match, where the server bot plays on the right.
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
	/// The bot script of a training match, such as reduced, without .lua. Empty for a match of two players.
	std::string bot;
	/// Strength of the bot, from MIN_BOT_DIFFICULTY to MAX_BOT_DIFFICULTY. 0 is the strongest.
	int botDifficulty = DEFAULT_BOT_DIFFICULTY;

	/// True for a training match of one player against a bot that the server runs.
	bool isTraining() const { return !bot.empty(); }
	bool hasPlayer(int playerId) const { return playerId > 0 && (playerId == player1 || playerId == player2); }
	int opponentOf(int playerId) const { return playerId == player1 ? player2 : player1; }
};

bool operator==(const MatchAssignment& a, const MatchAssignment& b);
bool operator!=(const MatchAssignment& a, const MatchAssignment& b);

/// Brings a pause between two games, in seconds, into the allowed range.
int clampNextGameDelay(int seconds);

/// True if name is a plain rules file name such as default.lua, without a folder.
bool isValidRuleFileName(const std::string& name);

/// True if name is a plain bot script name such as reduced, without .lua and without a folder.
bool isValidBotName(const std::string& name);

/// The name the human player sees for the bot of a training match. It fits the name field of the game.
std::string trainingBotName(const std::string& bot);

/// Reads an optional whole number attribute of an XML element. Returns false when it is there
/// but not a number from min to max. value only changes when the attribute is read.
bool readIntAttribute(const tinyxml2::XMLElement* element, const char* name, int min, int max, int& value);

/// Parses the text of a players file into doc and returns its <tournament> root element.
/// Returns nullptr and sets error when the text is not a players file.
const tinyxml2::XMLElement* parsePlayersFile(tinyxml2::XMLDocument& doc, const std::string& xml, std::string& error);

/*! Reads the match elements of a players file.
	\details The file looks like this:
	\code
	<tournament>
		<player id="12" nickname="Ola" code="K7QM2XPR4T"/>
		<player id="17" nickname="Kari" code="B9HNZ3WD6F"/>
		<match id="7" player1="12" player2="17" ruleFile="default.lua" scoreToWin="15" speed="75" bestOf="3" wins1="1" wins2="0"/>
		<match id="8" player1="17" bot="reduced" botDifficulty="13" scoreToWin="15"/>
	</tournament>
	\endcode
	Only id, player1 and player2 are required. A match with a bot instead of player2 is a training match
	against a bot that the server runs. It has no player2 and is always one game. A match with a bad value, or with a player
	that is not in knownPlayers, is skipped. So is a match whose wins already decide the series,
	since a decided match should have left the file. A match id that appears more than once is
	dropped for every match that uses it.
	Returns false and sets error when the text is not a players file.
*/
bool parseMatchAssignments(const std::string& xml, const std::set<int>& knownPlayers,
						std::vector<MatchAssignment>& matches, std::vector<std::string>& warnings,
						std::string& error);

/// The same for the root element of a players file that is already parsed.
void parseMatchAssignments(const tinyxml2::XMLElement* root, const std::set<int>& knownPlayers,
						std::vector<MatchAssignment>& matches, std::vector<std::string>& warnings);
