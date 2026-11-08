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
#include <vector>

/// The skill of the simple bot when none is given.
const int DEFAULT_SKILL = 70;
/// The difficulty of a script bot is the bot strength of the options screen of the game.
/// 0 is the strongest bot and 25 the weakest. The game has 13 for its right player by default.
const int MIN_DIFFICULTY = 0;
const int MAX_DIFFICULTY = 25;
const int DEFAULT_DIFFICULTY = 13;

/// The command line of the load test bot.
struct LoadbotArgs
{
	std::string host;
	unsigned short port = 0;
	std::string code;
	/// only for the simple bot
	int skill = DEFAULT_SKILL;
	/// a script from the scripts folder of the game, without .lua. Empty for the simple bot.
	std::string script;
	int difficulty = DEFAULT_DIFFICULTY;
};

const char* const LOADBOT_USAGE =
	"usage: blobby-loadbot <host> <port> <join code> [skill 0 to 100]\n"
	"       blobby-loadbot <host> <port> <join code> --script <name> [--difficulty 0 to 25]\n";

/// Reads the arguments after the program name. scripts holds the names of the bot scripts there are,
/// and is empty when the scripts folder was not found.
/// Returns false and sets error when the arguments are not valid.
bool parseLoadbotArgs(const std::vector<std::string>& args, const std::vector<std::string>& scripts,
					LoadbotArgs& out, std::string& error);
