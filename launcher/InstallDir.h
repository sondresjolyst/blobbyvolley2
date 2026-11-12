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

#include "UpdateManifest.h"

// The version folders next to the launcher, such as app-0.3.0, see doc/launcher.md.

/// A version folder counts only when it holds this file. The package build writes it last.
extern const char* const COMPLETE_MARKER;

/// The folder name of a version, such as app-0.3.0.
std::string appFolderName(const AppVersion& version);
/// Reads a folder name like app-0.3.0.
bool parseAppFolderName(const std::string& name, AppVersion& version);

/// The game program in a version folder.
std::string gameExecutableName();

struct InstalledVersion
{
	AppVersion version;
	/// full path of the folder
	std::string path;
};

/// The complete version folders in installDir, newest first.
std::vector<InstalledVersion> listInstalledVersions(const std::string& installDir);

/// Which versions the launcher started and which of them got running, from the marker files.
struct StartHistory
{
	std::set<std::string> tried;
	std::set<std::string> started;

	/// True when the launcher started the version and the game never said that it runs.
	bool failed(const AppVersion& version) const;
};

StartHistory readStartHistory(const std::string& stateDir);

/// Remembers that the launcher starts version now.
void markTried(const std::string& stateDir, const AppVersion& version);

/*! The version to start: the newest one that did not fail.
	\details A version fails when the launcher started it and the game never wrote its started marker.
	When every version failed, the newest one is started. Returns -1 when the list is empty.
*/
int pickVersionToStart(const std::vector<InstalledVersion>& installed, const StartHistory& history);

/*! Removes the version folders that are not needed and what failed installs left behind.
	\details It keeps the two newest complete versions and the one at chosen. A folder is first renamed,
	so one whose files are still in use on Windows is left alone.
*/
void removeOldVersions(const std::string& installDir, const std::vector<InstalledVersion>& installed, int chosen);

/// True when macOS runs the app from a random read-only copy, which it does for an app that was
/// started from the download folder without being moved.
bool isTranslocated(const std::string& path);
