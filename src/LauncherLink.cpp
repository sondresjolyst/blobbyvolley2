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

#include "LauncherLink.h"

#include "JoinLink.h"

namespace
{
	const char* const REQUEST_HEADER = "blobby-update-request 1\n";
	const char* const LINK_PREFIX = "link=";
}

bool launcherCanUpdate(const LauncherEnvironment& env)
{
	const char* value = env(LAUNCHER_UPDATES_ENV);
	return value && std::string(value) == "1" && !launcherPath(env).empty();
}

std::string launcherPath(const LauncherEnvironment& env)
{
	const char* value = env(LAUNCHER_PATH_ENV);
	return value ? std::string(value) : std::string();
}

std::string startedMarkerName(const std::string& version)
{
	return "started-" + version;
}

std::string triedMarkerName(const std::string& version)
{
	return "tried-" + version;
}

std::string formatJoinLink(const JoinLink& link)
{
	return "blobby://join?server=" + link.host + ":" + std::to_string(link.port) + "&code=" + link.code;
}

std::string makeUpdateRequest(const std::string& link)
{
	std::string content = REQUEST_HEADER;
	if (!link.empty())
		content += std::string(LINK_PREFIX) + link + "\n";
	return content;
}

std::string readUpdateRequest(const std::string& content)
{
	const std::string header = REQUEST_HEADER;
	if (content.size() > MAX_UPDATE_REQUEST_BYTES || content.compare(0, header.size(), header) != 0)
		return std::string();
	std::string line = content.substr(header.size());
	if (!line.empty() && line[line.size() - 1] == '\n')
		line.erase(line.size() - 1);
	const std::string prefix = LINK_PREFIX;
	if (line.compare(0, prefix.size(), prefix) != 0)
		return std::string();
	const std::string link = line.substr(prefix.size());
	// only a single word of printable ASCII that starts like a join link, the game checks the rest
	if (link.size() > MAX_JOIN_LINK_BYTES || link.compare(0, 9, "blobby://") != 0)
		return std::string();
	for (char c : link)
	{
		if (c <= ' ' || c > '~')
			return std::string();
	}
	return link;
}
