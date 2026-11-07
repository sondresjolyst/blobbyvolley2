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

#include <cstring>
#include <fstream>
#include <set>
#include <utility>

#include "tinyxml2.h"

#include "NetworkPlayer.h"
#include "StringUtil.h"

namespace
{
	const char JOIN_CODE_ALPHABET[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

	// characters of a code or name that are shown in the log
	const std::size_t LOG_PREFIX_LENGTH = 3;

	bool isJoinCodeChar(char c)
	{
		return c != '\0' && std::strchr(JOIN_CODE_ALPHABET, c) != nullptr;
	}

	// checks that text is valid UTF-8 without control characters
	bool isCleanUtf8(const std::string& text)
	{
		std::size_t i = 0;
		while (i < text.size())
		{
			unsigned char c = text[i];
			std::size_t extra = 0;
			if (c < 0x20 || c == 0x7F)
				return false;
			if (c < 0x80)
				extra = 0;
			else if (c >= 0xC2 && c <= 0xDF)
				extra = 1;
			else if (c >= 0xE0 && c <= 0xEF)
				extra = 2;
			else if (c >= 0xF0 && c <= 0xF4)
				extra = 3;
			else
				return false;

			for (std::size_t k = 1; k <= extra; ++k)
			{
				if (i + k >= text.size())
					return false;
				unsigned char follow = text[i + k];
				if ((follow & 0xC0) != 0x80)
					return false;
			}
			i += extra + 1;
		}
		return true;
	}
}

std::string normalizeJoinCode(const std::string& raw)
{
	std::string code = trim(raw, " ");
	for (auto& c : code)
	{
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
	}

	if (!isValidJoinCode(code))
		return "";
	return code;
}

bool isValidJoinCode(const std::string& code)
{
	if (code.size() != JOIN_CODE_LENGTH)
		return false;
	for (char c : code)
	{
		if (!isJoinCodeChar(c))
			return false;
	}
	return true;
}

bool isValidNickname(const std::string& name)
{
	return !name.empty() && name.size() <= MAX_NICKNAME_BYTES && isCleanUtf8(name);
}

std::string joinCodeLogPrefix(const std::string& raw)
{
	std::string prefix;
	for (char c : raw)
	{
		if (prefix.size() >= LOG_PREFIX_LENGTH)
			break;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			prefix += c;
		else
			prefix += '?';
	}
	return prefix + "...";
}

bool JoinCodeTable::parse(const std::string& xml, std::vector<std::string>& warnings, std::string& error)
{
	tinyxml2::XMLDocument doc;
	if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
	{
		error = std::string("the players file is not valid XML: ") + doc.ErrorName();
		return false;
	}

	const tinyxml2::XMLElement* root = doc.RootElement();
	if (!root || std::strcmp(root->Name(), "tournament") != 0)
	{
		error = "the players file has no <tournament> root element";
		return false;
	}

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
		if (player->Attribute("id"))
		{
			if (player->QueryIntAttribute("id", &id) != tinyxml2::XML_SUCCESS || id < 1)
			{
				warnings.push_back(where + " has an invalid id, skipped");
				continue;
			}
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
	return true;
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

	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file)
	{
		error = "cannot open players file " + path;
		return false;
	}

	std::string data;
	char buffer[4096];
	while (file)
	{
		file.read(buffer, sizeof(buffer));
		data.append(buffer, static_cast<std::size_t>(file.gcount()));
		if (data.size() > MAX_PLAYERS_FILE_BYTES)
		{
			error = "players file " + path + " is larger than 1 MB";
			return false;
		}
	}
	if (file.bad())
	{
		error = "cannot read players file " + path;
		return false;
	}

	content.swap(data);
	return true;
}

JoinCodeGate::JoinCodeGate(std::string path, unsigned reload_interval_ms)
: mPath(std::move(path))
, mReloadIntervalMs(reload_interval_ms)
{
}

void JoinCodeGate::setLogFunction(LogFunction log)
{
	mLog = std::move(log);
}

void JoinCodeGate::log(LogLevel level, const std::string& message) const
{
	if (mLog)
		mLog(level, message);
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
			log(LogLevel::PROBLEM, error + ", keeping " + std::to_string(mTable.size()) + " known players");
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

	JoinCodeTable table;
	std::vector<std::string> warnings;
	std::string error;
	if (!table.parse(xml, warnings, error))
	{
		log(LogLevel::PROBLEM, error + ", keeping " + std::to_string(mTable.size()) + " known players");
		return false;
	}

	std::vector<MatchAssignment> matches;
	if (!parseMatchAssignments(xml, table.playerIds(), matches, warnings, error))
	{
		log(LogLevel::PROBLEM, error + ", keeping " + std::to_string(mTable.size()) + " known players");
		return false;
	}

	for (const auto& w : warnings)
		log(LogLevel::PROBLEM, "players file: " + w);

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
	log(LogLevel::INFO, "loaded " + std::to_string(mTable.size()) + " tournament players and "
		+ std::to_string(mMatches.size()) + " matches");
	return true;
}

JoinCodeGate::Result JoinCodeGate::admit(const PlayerID& player, const std::string& name, std::string& nickname)
{
	std::string code = normalizeJoinCode(name);
	const JoinCodeEntry* entry = code.empty() ? nullptr : mTable.find(code);
	if (!entry)
	{
		log(LogLevel::INFO, "refused unknown join code " + joinCodeLogPrefix(name));
		return Result::UNKNOWN_CODE;
	}

	auto owner = mCodeOwners.find(code);
	if (owner != mCodeOwners.end() && owner->second != player)
	{
		log(LogLevel::INFO, "refused join code " + joinCodeLogPrefix(code) + ", already in use by " + entry->nickname);
		return Result::CODE_IN_USE;
	}

	// a player holds one code at a time
	release(player);
	mCodeOwners[code] = player;
	mPlayerCodes[player] = code;
	nickname = entry->nickname;
	log(LogLevel::INFO, "join code " + joinCodeLogPrefix(code) + " accepted for " + nickname);
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
		log(LogLevel::INFO, "refused join code " + joinCodeLogPrefix(code) + ", it moved to another connection a moment ago");
		return Handover::TOO_SOON;
	}
	if (holderCanLeave && !holderCanLeave(owner->second))
	{
		log(LogLevel::INFO, "refused join code " + joinCodeLogPrefix(code) + ", its player is in a running game");
		return Handover::HOLDER_BUSY;
	}

	holder = owner->second;
	mHandedOverAt[code] = now_ms;
	log(LogLevel::INFO, "join code " + joinCodeLogPrefix(code) + " moves to a new connection");
	return Handover::HAND_OVER;
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
