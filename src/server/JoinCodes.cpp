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

#include "JoinCodes.h"

#include <set>
#include <utility>

#include "tinyxml2.h"

#include "FileUtil.h"
#include "NetworkPlayer.h"
#include "StringUtil.h"

bool JoinCodeTable::parse(const std::string& xml, std::vector<std::string>& warnings, std::string& error)
{
	tinyxml2::XMLDocument doc;
	const tinyxml2::XMLElement* root = parsePlayersFile(doc, xml, error);
	if (!root)
		return false;
	parse(root, warnings);
	return true;
}

void JoinCodeTable::parse(const tinyxml2::XMLElement* root, std::vector<std::string>& warnings)
{
	std::vector<JoinCodeEntry> entries;
	int index = 0;
	for (const tinyxml2::XMLElement* player = root->FirstChildElement("player"); player;
		 player = player->NextSiblingElement("player"))
	{
		++index;
		const std::string where = "player entry " + std::to_string(index);

		const char* code_attr = player->Attribute("code");
		const char* nick_attr = player->Attribute("nickname");
		if (!code_attr || !nick_attr)
		{
			warnings.push_back(where + " needs a code and a nickname, skipped");
			continue;
		}

		std::string code = code_attr;
		if (!isValidJoinCode(code))
		{
			warnings.push_back(where + " has an invalid code " + joinCodeLogPrefix(code) + ", skipped");
			continue;
		}

		std::string nickname = nick_attr;
		if (!isValidNickname(nickname))
		{
			warnings.push_back(where + " has an empty, too long or invalid nickname, skipped");
			continue;
		}

		int id = 0;
		if (!readIntAttribute(player, "id", 1, 2147483647, id))
		{
			warnings.push_back(where + " has an invalid id, skipped");
			continue;
		}

		JoinCodeEntry entry;
		entry.code = code;
		entry.nickname = nickname;
		entry.playerId = id;
		entries.push_back(entry);
	}

	// a code that appears more than once belongs to nobody
	std::map<std::string, int> code_count;
	for (const auto& e : entries)
		++code_count[e.code];

	// a player id that appears more than once is not clear either
	std::map<int, int> id_count;
	for (const auto& e : entries)
	{
		if (e.playerId > 0)
			++id_count[e.playerId];
	}

	std::map<std::string, JoinCodeEntry> result;
	std::set<std::string> nicknames;
	for (const auto& e : entries)
	{
		if (code_count[e.code] > 1)
			continue;
		if (e.playerId > 0 && id_count[e.playerId] > 1)
			continue;
		if (!nicknames.insert(lowerAscii(e.nickname)).second)
			warnings.push_back("nickname " + e.nickname + " is used by more than one player");
		result[e.code] = e;
	}

	for (const auto& c : id_count)
	{
		if (c.second > 1)
			warnings.push_back("player id " + std::to_string(c.first) + " is used by "
				+ std::to_string(c.second) + " entries, all of them skipped");
	}

	for (const auto& c : code_count)
	{
		if (c.second > 1)
			warnings.push_back("code " + joinCodeLogPrefix(c.first) + " is used by "
				+ std::to_string(c.second) + " entries, all of them skipped");
	}

	mEntries.swap(result);
}

const JoinCodeEntry* JoinCodeTable::find(const std::string& code) const
{
	auto found = mEntries.find(code);
	if (found == mEntries.end())
		return nullptr;
	return &found->second;
}

const JoinCodeEntry* JoinCodeTable::findById(int playerId) const
{
	if (playerId <= 0)
		return nullptr;
	for (const auto& e : mEntries)
	{
		if (e.second.playerId == playerId)
			return &e.second;
	}
	return nullptr;
}

std::set<int> JoinCodeTable::playerIds() const
{
	std::set<int> ids;
	for (const auto& e : mEntries)
	{
		if (e.second.playerId > 0)
			ids.insert(e.second.playerId);
	}
	return ids;
}

std::size_t JoinCodeTable::size() const
{
	return mEntries.size();
}

bool readJoinCodeFile(const std::string& path, std::string& content, std::string& error)
{
	if (path.empty())
	{
		error = "no players file is set";
		return false;
	}

	ReadFileError why = ReadFileError::NONE;
	if (readWholeFile(path, MAX_PLAYERS_FILE_BYTES, content, &why))
		return true;
	if (why == ReadFileError::CANNOT_OPEN)
		error = "cannot open players file " + path;
	else if (why == ReadFileError::TOO_LARGE)
		error = "players file " + path + " is larger than 1 MB";
	else
		error = "cannot read players file " + path;
	return false;
}

JoinCodeGate::JoinCodeGate(std::string path, unsigned reload_interval_ms)
: mPath(std::move(path))
, mReloadIntervalMs(reload_interval_ms)
{
}

void JoinCodeGate::setLogFunction(ServerLog log)
{
	mLog = std::move(log);
}

void JoinCodeGate::log(bool problem, const std::string& message) const
{
	if (mLog)
		mLog(problem, message);
}

bool JoinCodeGate::reload()
{
	std::string content;
	std::string error;
	if (!readJoinCodeFile(mPath, content, error))
	{
		// log each problem once, not on every poll
		if (error != mLastError)
		{
			log(true, error + ", keeping " + std::to_string(mTable.size()) + " known players");
			mLastError = error;
		}
		return false;
	}
	mLastError.clear();

	if (mHasContent && content == mLastContent)
		return false;

	return applyContent(content);
}

void JoinCodeGate::poll(unsigned long long now_ms)
{
	if (mPolled && now_ms - mLastPollMs < mReloadIntervalMs)
		return;
	mPolled = true;
	mLastPollMs = now_ms;
	reload();
}

bool JoinCodeGate::applyContent(const std::string& xml)
{
	mLastContent = xml;
	mHasContent = true;

	// the players and the matches come from the same parsed file
	tinyxml2::XMLDocument doc;
	std::string error;
	const tinyxml2::XMLElement* root = parsePlayersFile(doc, xml, error);
	if (!root)
	{
		log(true, error + ", keeping " + std::to_string(mTable.size()) + " known players");
		return false;
	}

	JoinCodeTable table;
	std::vector<std::string> warnings;
	table.parse(root, warnings);
	std::vector<MatchAssignment> matches;
	parseMatchAssignments(root, table.playerIds(), matches, warnings);

	for (const auto& w : warnings)
		log(true, "players file: " + w);

	mTable = std::move(table);
	mMatches.swap(matches);

	// A code that left the file no longer belongs to anybody, so it cannot be handed over and its
	// player id is 0. The connection that held it stays as it is, the server does not close it.
	// It keeps its lobby place and a running game, and it gets no new tournament games.
	for (auto it = mCodeOwners.begin(); it != mCodeOwners.end(); )
	{
		if (mTable.find(it->first))
		{
			++it;
			continue;
		}
		mPlayerCodes.erase(it->second);
		mHandedOverAt.erase(it->first);
		it = mCodeOwners.erase(it);
	}
	++mGeneration;
	log(false, "loaded " + std::to_string(mTable.size()) + " tournament players and "
		+ std::to_string(mMatches.size()) + " matches");
	return true;
}

JoinCodeGate::Result JoinCodeGate::admit(const PlayerID& player, const std::string& name, std::string& nickname)
{
	std::string code = normalizeJoinCode(name);
	const JoinCodeEntry* entry = code.empty() ? nullptr : mTable.find(code);
	if (!entry)
	{
		log(false, "refused unknown join code " + joinCodeLogPrefix(name));
		return Result::UNKNOWN_CODE;
	}

	auto owner = mCodeOwners.find(code);
	if (owner != mCodeOwners.end() && owner->second != player)
	{
		log(false, "refused join code " + joinCodeLogPrefix(code) + ", already in use by " + entry->nickname);
		return Result::CODE_IN_USE;
	}

	// a player holds one code at a time
	release(player);
	mCodeOwners[code] = player;
	mPlayerCodes[player] = code;
	nickname = entry->nickname;
	log(false, "join code " + joinCodeLogPrefix(code) + " accepted for " + nickname);
	return Result::ACCEPTED;
}

JoinCodeGate::Result JoinCodeGate::admit(NetworkPlayer& player)
{
	std::string nickname;
	Result result = admit(player.getID(), player.getName(), nickname);
	if (result == Result::ACCEPTED)
		player.setName(nickname);
	return result;
}

JoinCodeGate::Handover JoinCodeGate::checkHandover(const PlayerID& player, const std::string& name,
												unsigned long long now_ms, const HolderCheck& holderCanLeave,
												PlayerID& holder)
{
	const std::string code = normalizeJoinCode(name);
	auto owner = mCodeOwners.find(code);
	// only a code of the current table moves, admit refuses any other
	if (owner == mCodeOwners.end() || owner->second == player || !mTable.find(code))
		return Handover::NONE;

	auto last = mHandedOverAt.find(code);
	if (last != mHandedOverAt.end() && now_ms < last->second + JOIN_CODE_HANDOVER_COOLDOWN_MS)
	{
		log(false, "refused join code " + joinCodeLogPrefix(code) + ", it moved to another connection a moment ago");
		return Handover::TOO_SOON;
	}
	if (holderCanLeave && !holderCanLeave(owner->second))
	{
		log(false, "refused join code " + joinCodeLogPrefix(code) + ", its player is in a running game");
		return Handover::HOLDER_BUSY;
	}

	holder = owner->second;
	mHandedOverAt[code] = now_ms;
	log(false, "join code " + joinCodeLogPrefix(code) + " moves to a new connection");
	return Handover::HAND_OVER;
}

bool JoinCodeGate::findHolder(const std::string& name, PlayerID& holder) const
{
	auto owner = mCodeOwners.find(normalizeJoinCode(name));
	if (owner == mCodeOwners.end())
		return false;
	holder = owner->second;
	return true;
}

void JoinCodeGate::release(const PlayerID& player)
{
	auto code = mPlayerCodes.find(player);
	if (code == mPlayerCodes.end())
		return;
	mCodeOwners.erase(code->second);
	mPlayerCodes.erase(code);
}

const JoinCodeTable& JoinCodeGate::getTable() const
{
	return mTable;
}

const std::string& JoinCodeGate::getPath() const
{
	return mPath;
}

const std::vector<MatchAssignment>& JoinCodeGate::getMatches() const
{
	return mMatches;
}

unsigned JoinCodeGate::getGeneration() const
{
	return mGeneration;
}

int JoinCodeGate::playerIdOf(const PlayerID& player) const
{
	auto code = mPlayerCodes.find(player);
	if (code == mPlayerCodes.end())
		return 0;
	const JoinCodeEntry* entry = mTable.find(code->second);
	return entry ? entry->playerId : 0;
}

bool JoinCodeGate::findConnection(int playerId, PlayerID& player) const
{
	if (playerId <= 0)
		return false;
	for (const auto& owner : mCodeOwners)
	{
		const JoinCodeEntry* entry = mTable.find(owner.first);
		if (entry && entry->playerId == playerId)
		{
			player = owner.second;
			return true;
		}
	}
	return false;
}
