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
#include "TournamentProtocol.h"

/* includes */
#include <cstring>

#include "GenericIO.h"
#include "StringUtil.h"

/* implementation */
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

bool isValidScoreToWin(int scoreToWin)
{
	return scoreToWin >= MIN_MATCH_SCORE_TO_WIN && scoreToWin <= MAX_MATCH_SCORE_TO_WIN;
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

TournamentStatusData readTournamentStatus(GenericIn& in)
{
	TournamentStatusData data;
	unsigned char status = 0;
	in.byte(status);
	if (status <= (unsigned char)TournamentStatus::MATCH_OVER)
		data.status = (TournamentStatus)status;
	in.string(data.opponent);
	in.uint32(data.game);
	in.uint32(data.bestOf);
	in.uint32(data.ownWins);
	in.uint32(data.opponentWins);
	in.string(data.ownName);
	in.uint32(data.nextGameInMs);
	if (data.nextGameInMs > MAX_NEXT_GAME_DELAY_MS)
		data.nextGameInMs = MAX_NEXT_GAME_DELAY_MS;
	if (!isValidNickname(data.opponent))
		data.opponent.clear();
	if (!isValidNickname(data.ownName))
		data.ownName.clear();
	return data;
}
