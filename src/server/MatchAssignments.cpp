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

#include "MatchAssignments.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "tinyxml2.h"

namespace
{
	// longest rules file name
	const std::size_t MAX_RULE_FILE_NAME = 64;
	// longest bot script name
	const std::size_t MAX_BOT_NAME = 40;
	// the name field of the game holds 15 characters and the terminating zero
	const std::size_t MAX_SHOWN_NAME = 15;

	bool isPlainNameChar(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
	}
}

bool readIntAttribute(const tinyxml2::XMLElement* element, const char* name, int min, int max, int& value)
{
	if (!element->Attribute(name))
		return true;
	int read = 0;
	if (element->QueryIntAttribute(name, &read) != tinyxml2::XML_SUCCESS || read < min || read > max)
		return false;
	value = read;
	return true;
}

const tinyxml2::XMLElement* parsePlayersFile(tinyxml2::XMLDocument& doc, const std::string& xml, std::string& error)
{
	if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
	{
		error = std::string("the players file is not valid XML: ") + doc.ErrorName();
		return nullptr;
	}

	const tinyxml2::XMLElement* root = doc.RootElement();
	if (!root || std::strcmp(root->Name(), "tournament") != 0)
	{
		error = "the players file has no <tournament> root element";
		return nullptr;
	}
	return root;
}

bool operator==(const MatchAssignment& a, const MatchAssignment& b)
{
	return a.matchId == b.matchId && a.player1 == b.player1 && a.player2 == b.player2
		&& a.ruleFile == b.ruleFile && a.scoreToWin == b.scoreToWin && a.speed == b.speed
		&& a.bestOf == b.bestOf && a.nextGame == b.nextGame && a.awaitingDecision == b.awaitingDecision
		&& a.wins1 == b.wins1 && a.wins2 == b.wins2 && a.nextGameDelay == b.nextGameDelay
		&& a.bot == b.bot && a.botDifficulty == b.botDifficulty;
}

bool operator!=(const MatchAssignment& a, const MatchAssignment& b)
{
	return !(a == b);
}

int clampNextGameDelay(int seconds)
{
	if (seconds < MIN_NEXT_GAME_DELAY)
		return MIN_NEXT_GAME_DELAY;
	if (seconds > MAX_NEXT_GAME_DELAY)
		return MAX_NEXT_GAME_DELAY;
	return seconds;
}

bool isValidRuleFileName(const std::string& name)
{
	const std::string ending = ".lua";
	if (name.size() <= ending.size() || name.size() > MAX_RULE_FILE_NAME)
		return false;
	if (name.compare(name.size() - ending.size(), ending.size(), ending) != 0)
		return false;
	for (std::size_t i = 0; i < name.size() - ending.size(); ++i)
	{
		if (!isPlainNameChar(name[i]))
			return false;
	}
	return true;
}

bool isValidBotName(const std::string& name)
{
	if (name.empty() || name.size() > MAX_BOT_NAME)
		return false;
	for (char c : name)
	{
		if (!isPlainNameChar(c))
			return false;
	}
	return true;
}

std::string trainingBotName(const std::string& bot)
{
	std::string name = "Bot " + bot;
	if (name.size() > MAX_SHOWN_NAME)
		name.resize(MAX_SHOWN_NAME);
	return name;
}

bool parseMatchAssignments(const std::string& xml, const std::set<int>& knownPlayers,
						std::vector<MatchAssignment>& matches, std::vector<std::string>& warnings,
						std::string& error)
{
	tinyxml2::XMLDocument doc;
	const tinyxml2::XMLElement* root = parsePlayersFile(doc, xml, error);
	if (!root)
		return false;
	parseMatchAssignments(root, knownPlayers, matches, warnings);
	return true;
}

void parseMatchAssignments(const tinyxml2::XMLElement* root, const std::set<int>& knownPlayers,
						std::vector<MatchAssignment>& matches, std::vector<std::string>& warnings)
{
	std::vector<MatchAssignment> found;
	int index = 0;
	for (const tinyxml2::XMLElement* element = root->FirstChildElement("match"); element;
		 element = element->NextSiblingElement("match"))
	{
		++index;
		const std::string where = "match entry " + std::to_string(index);

		// a training match has a bot instead of player 2
		const bool training = element->Attribute("bot") != nullptr;
		if (!element->Attribute("id") || !element->Attribute("player1") || training == (element->Attribute("player2") != nullptr))
		{
			warnings.push_back(where + " needs an id, player1 and either player2 or a bot, skipped");
			continue;
		}

		MatchAssignment match;
		if (!readIntAttribute(element, "id", 1, 2147483647, match.matchId))
		{
			warnings.push_back(where + " has an invalid id, skipped");
			continue;
		}
		const std::string named = "match " + std::to_string(match.matchId);

		if (!readIntAttribute(element, "player1", 1, 2147483647, match.player1)
			|| !readIntAttribute(element, "player2", 1, 2147483647, match.player2)
			|| match.player1 == match.player2)
		{
			warnings.push_back(named + " has invalid players, skipped");
			continue;
		}
		if (knownPlayers.count(match.player1) == 0 || (!training && knownPlayers.count(match.player2) == 0))
		{
			warnings.push_back(named + " has a player that is not in the players file, skipped");
			continue;
		}
		if (training)
		{
			match.bot = element->Attribute("bot");
			if (!isValidBotName(match.bot))
			{
				warnings.push_back(named + " has an invalid bot, skipped");
				continue;
			}
			if (!readIntAttribute(element, "botDifficulty", MIN_BOT_DIFFICULTY, MAX_BOT_DIFFICULTY, match.botDifficulty))
			{
				warnings.push_back(named + " has an invalid botDifficulty, skipped");
				continue;
			}
		}

		if (const char* rule = element->Attribute("ruleFile"))
			match.ruleFile = rule;
		if (!isValidRuleFileName(match.ruleFile))
		{
			warnings.push_back(named + " has an invalid ruleFile, skipped");
			continue;
		}

		if (!readIntAttribute(element, "scoreToWin", MIN_MATCH_SCORE_TO_WIN, MAX_MATCH_SCORE_TO_WIN, match.scoreToWin))
		{
			warnings.push_back(named + " has an invalid scoreToWin, skipped");
			continue;
		}
		if (!readIntAttribute(element, "speed", MIN_MATCH_SPEED, MAX_MATCH_SPEED, match.speed))
		{
			warnings.push_back(named + " has an invalid speed, skipped");
			continue;
		}
		if (!readIntAttribute(element, "bestOf", 1, MAX_MATCH_BEST_OF, match.bestOf) || match.bestOf % 2 == 0)
		{
			warnings.push_back(named + " has an invalid bestOf, it must be odd, skipped");
			continue;
		}
		if (training && match.bestOf != 1)
		{
			warnings.push_back(named + " is a training match, which is one game, but has a bestOf, skipped");
			continue;
		}
		if (!readIntAttribute(element, "nextGame", 1, 2147483647, match.nextGame))
		{
			warnings.push_back(named + " has an invalid nextGame, skipped");
			continue;
		}
		if (element->Attribute("awaitingDecision")
			&& element->QueryBoolAttribute("awaitingDecision", &match.awaitingDecision) != tinyxml2::XML_SUCCESS)
		{
			warnings.push_back(named + " has an invalid awaitingDecision, skipped");
			continue;
		}

		if (!readIntAttribute(element, "wins1", 0, MAX_MATCH_WINS, match.wins1)
			|| !readIntAttribute(element, "wins2", 0, MAX_MATCH_WINS, match.wins2))
		{
			warnings.push_back(named + " has invalid wins, skipped");
			continue;
		}
		if (!readIntAttribute(element, "nextGameDelay", MIN_NEXT_GAME_DELAY, MAX_NEXT_GAME_DELAY, match.nextGameDelay))
		{
			warnings.push_back(named + " has an invalid nextGameDelay, skipped");
			continue;
		}
		const int needed = match.bestOf / 2 + 1;
		if (match.wins1 >= needed || match.wins2 >= needed)
		{
			warnings.push_back(named + " is already decided by its wins, skipped");
			continue;
		}

		found.push_back(match);
	}

	// a match id that appears more than once is not clear
	std::map<int, int> id_count;
	for (const auto& m : found)
		++id_count[m.matchId];

	std::vector<MatchAssignment> result;
	for (const auto& m : found)
	{
		if (id_count[m.matchId] == 1)
			result.push_back(m);
	}
	for (const auto& c : id_count)
	{
		if (c.second > 1)
			warnings.push_back("match " + std::to_string(c.first) + " appears " + std::to_string(c.second)
				+ " times, all of them skipped");
	}

	std::sort(result.begin(), result.end(),
			[](const MatchAssignment& a, const MatchAssignment& b){ return a.matchId < b.matchId; });
	matches.swap(result);
}
