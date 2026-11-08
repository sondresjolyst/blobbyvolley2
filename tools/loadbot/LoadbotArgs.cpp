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

#include "LoadbotArgs.h"

#include <algorithm>
#include <cstdlib>
#include <cerrno>

namespace
{
	/// Reads a whole decimal number. Returns false for empty text, other characters or an overflow.
	bool readInteger(const std::string& text, long& value)
	{
		if (text.empty())
			return false;
		char* end = nullptr;
		errno = 0;
		value = std::strtol(text.c_str(), &end, 10);
		return errno == 0 && end == text.c_str() + text.size();
	}

	std::string scriptList(std::vector<std::string> scripts)
	{
		std::sort(scripts.begin(), scripts.end());
		std::string list;
		for (const auto& name : scripts)
			list += (list.empty() ? "" : ", ") + name;
		return list.empty() ? "none found" : list;
	}
}

bool parseLoadbotArgs(const std::vector<std::string>& args, const std::vector<std::string>& scripts,
					LoadbotArgs& out, std::string& error)
{
	out = LoadbotArgs();
	std::vector<std::string> positional;
	bool haveScript = false;
	bool haveDifficulty = false;
	std::string difficultyText;

	for (std::size_t i = 0; i < args.size(); ++i)
	{
		const std::string& arg = args[i];
		std::string name = arg;
		std::string value;
		bool inlineValue = false;
		if (arg.compare(0, 2, "--") == 0)
		{
			auto equals = arg.find('=');
			if (equals != std::string::npos)
			{
				name = arg.substr(0, equals);
				value = arg.substr(equals + 1);
				inlineValue = true;
			}
		}
		else
		{
			positional.push_back(arg);
			continue;
		}

		if (name != "--script" && name != "--difficulty")
		{
			error = "unknown option " + name;
			return false;
		}
		if (!inlineValue)
		{
			if (i + 1 >= args.size())
			{
				error = name + " needs a value";
				return false;
			}
			value = args[++i];
		}
		if (name == "--script")
		{
			if (haveScript)
			{
				error = "--script is given twice";
				return false;
			}
			haveScript = true;
			out.script = value;
		}
		else
		{
			if (haveDifficulty)
			{
				error = "--difficulty is given twice";
				return false;
			}
			haveDifficulty = true;
			difficultyText = value;
		}
	}

	if (positional.size() < 3)
	{
		error = "the host, the port and the join code are needed";
		return false;
	}
	if (positional.size() > 4)
	{
		error = "too many arguments";
		return false;
	}

	out.host = positional[0];
	long port = 0;
	if (!readInteger(positional[1], port) || port < 1 || port > 65535)
	{
		error = "the port must be a number from 1 to 65535";
		return false;
	}
	out.port = (unsigned short)port;
	out.code = positional[2];

	if (positional.size() == 4)
	{
		if (haveScript)
		{
			error = "the skill only applies to the simple bot, not to --script";
			return false;
		}
		// the skill was always clamped, so values outside still work as before
		out.skill = std::max(0, std::min(100, std::atoi(positional[3].c_str())));
	}

	if (haveDifficulty && !haveScript)
	{
		error = "--difficulty only applies to a bot with --script";
		return false;
	}

	if (haveScript && std::find(scripts.begin(), scripts.end(), out.script) == scripts.end())
	{
		error = "unknown script " + out.script + ". The scripts are: " + scriptList(scripts);
		return false;
	}

	if (haveDifficulty)
	{
		long difficulty = 0;
		if (!readInteger(difficultyText, difficulty) || difficulty < MIN_DIFFICULTY || difficulty > MAX_DIFFICULTY)
		{
			error = "--difficulty must be a number from " + std::to_string(MIN_DIFFICULTY) + " to "
				+ std::to_string(MAX_DIFFICULTY);
			return false;
		}
		out.difficulty = (int)difficulty;
	}
	return true;
}
