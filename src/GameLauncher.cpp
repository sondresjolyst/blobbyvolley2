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

#include "GameLauncher.h"

#include <cstdlib>
#include <iostream>

#include "FileSystem.h"
#include "FileWrite.h"
#include "ForkVersion.h"
#include "LauncherLink.h"

namespace
{
	const char* environment(const char* name)
	{
		return std::getenv(name);
	}

	bool writeStateFile(const std::string& name, const std::string& content)
	{
		try
		{
			FileSystem::getSingleton().probeDir(LAUNCHER_STATE_FOLDER);
			FileWrite file(std::string(LAUNCHER_STATE_FOLDER) + "/" + name);
			file.write(content);
			file.close();
			return true;
		}
		catch (std::exception& e)
		{
			std::cerr << "Cannot write " << name << " for the launcher: " << e.what() << std::endl;
			return false;
		}
	}
}

bool startedByUpdatingLauncher()
{
	return launcherCanUpdate(environment);
}

void markStartedForLauncher()
{
	if (!launcherPath(environment).empty())
		writeStateFile(startedMarkerName(BLOBBY_FORK_VERSION), "");
}

bool requestLauncherUpdate(const std::string& link)
{
	return writeStateFile(UPDATE_REQUEST_FILE, makeUpdateRequest(link));
}
