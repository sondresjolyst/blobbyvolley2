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

#include "InstallDir.h"

#include <algorithm>

#include "FsUtil.h"
#include "LauncherLink.h"

const char* const COMPLETE_MARKER = "complete.txt";

namespace
{
	const std::string APP_PREFIX = "app-";
	const std::string STAGING_PREFIX = ".staging-";
	const std::string TRASH_PREFIX = ".trash-";

	bool startsWith(const std::string& text, const std::string& prefix)
	{
		return text.compare(0, prefix.size(), prefix) == 0;
	}

	// renames the folder out of the way first, so a folder in use stays whole
	void removeFolder(const std::string& installDir, const std::string& name)
	{
		const std::string trash = fsu::join(installDir, TRASH_PREFIX + name);
		fsu::removeTree(trash);
		if (fsu::rename(fsu::join(installDir, name), trash))
			fsu::removeTree(trash);
	}
}

std::string appFolderName(const AppVersion& version)
{
	return APP_PREFIX + version.toString();
}

bool parseAppFolderName(const std::string& name, AppVersion& version)
{
	if (!startsWith(name, APP_PREFIX))
		return false;
	return parseAppVersion(name.substr(APP_PREFIX.size()), version);
}

std::string gameExecutableName()
{
#ifdef _WIN32
	return "blobby.exe";
#else
	return "blobby";
#endif
}

std::vector<InstalledVersion> listInstalledVersions(const std::string& installDir)
{
	std::vector<InstalledVersion> found;
	std::vector<std::string> names;
	if (!fsu::list(installDir, names))
		return found;
	for (const auto& name : names)
	{
		InstalledVersion item;
		if (!parseAppFolderName(name, item.version))
			continue;
		item.path = fsu::join(installDir, name);
		if (fsu::isFile(fsu::join(item.path, COMPLETE_MARKER)))
			found.push_back(item);
	}
	std::sort(found.begin(), found.end(),
			  [](const InstalledVersion& a, const InstalledVersion& b) { return b.version < a.version; });
	return found;
}

bool StartHistory::failed(const AppVersion& version) const
{
	const std::string name = version.toString();
	return tried.count(name) && !started.count(name);
}

StartHistory readStartHistory(const std::string& stateDir)
{
	StartHistory history;
	std::vector<std::string> names;
	if (stateDir.empty() || !fsu::list(stateDir, names))
		return history;
	const std::string tried = triedMarkerName("");
	const std::string started = startedMarkerName("");
	for (const auto& name : names)
	{
		AppVersion version;
		if (startsWith(name, tried) && parseAppVersion(name.substr(tried.size()), version))
			history.tried.insert(version.toString());
		else if (startsWith(name, started) && parseAppVersion(name.substr(started.size()), version))
			history.started.insert(version.toString());
	}
	return history;
}

void markTried(const std::string& stateDir, const AppVersion& version)
{
	if (!stateDir.empty())
		fsu::writeFile(fsu::join(stateDir, triedMarkerName(version.toString())), "");
}

int pickVersionToStart(const std::vector<InstalledVersion>& installed, const StartHistory& history)
{
	for (std::size_t i = 0; i < installed.size(); ++i)
	{
		if (!history.failed(installed[i].version))
			return (int)i;
	}
	return installed.empty() ? -1 : 0;
}

void removeOldVersions(const std::string& installDir, const std::vector<InstalledVersion>& installed, int chosen)
{
	std::vector<std::string> names;
	if (!fsu::list(installDir, names))
		return;
	for (const auto& name : names)
	{
		// what an install or a removal left behind
		if (startsWith(name, STAGING_PREFIX) || startsWith(name, TRASH_PREFIX))
			fsu::removeTree(fsu::join(installDir, name));
	}
	for (std::size_t i = 0; i < installed.size(); ++i)
	{
		if (i < 2 || (int)i == chosen)
			continue;
		removeFolder(installDir, appFolderName(installed[i].version));
	}
}

bool isTranslocated(const std::string& path)
{
	return path.find("/AppTranslocation/") != std::string::npos;
}
