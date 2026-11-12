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

#include <cstddef>
#include <functional>
#include <string>

struct JoinLink;

// What the game and the launcher share, see doc/launcher.md. The launcher starts the game from a
// version folder and sets two environment variables. The two talk through files in the folder
// launcher in the settings folder of the game.

/// The launcher sets this to its own path, so the game registers the launcher as link handler.
constexpr const char* LAUNCHER_PATH_ENV = "BLOBBY_LAUNCHER";
/// The launcher sets this to 1 when it can install updates in its folder.
constexpr const char* LAUNCHER_UPDATES_ENV = "BLOBBY_LAUNCHER_UPDATES";

/// The settings folder of the game, as PHYSFS_getPrefDir names it.
constexpr const char* PREF_ORGANIZATION = "sjolyst-innovation";
constexpr const char* PREF_APPLICATION = "blobbyvolley2-tournament";

/// The folder of the launcher files in the settings folder.
constexpr const char* LAUNCHER_STATE_FOLDER = "launcher";
/// The game writes this file when the player picks Update now, and then closes.
constexpr const char* UPDATE_REQUEST_FILE = "update-request";

/// Longest update request file the launcher reads.
constexpr std::size_t MAX_UPDATE_REQUEST_BYTES = 1024;

typedef std::function<const char*(const char*)> LauncherEnvironment;

/// True when the game runs from a launcher that can install updates.
bool launcherCanUpdate(const LauncherEnvironment& env);

/// The path of the launcher that started the game, empty without one.
std::string launcherPath(const LauncherEnvironment& env);

/// The file the game writes once it runs, such as started-0.3.0. A version that the launcher
/// tried but that never wrote it failed to start.
std::string startedMarkerName(const std::string& version);
/// The file the launcher writes before it starts a version, such as tried-0.3.0.
std::string triedMarkerName(const std::string& version);

/// The join link of a server for the given link, such as blobby://join?server=host:7000&code=ABCDEFGHJK.
std::string formatJoinLink(const JoinLink& link);

/// The content of an update request. link is the join link to start the new game with, or empty.
std::string makeUpdateRequest(const std::string& link);

/*! Reads an update request.
	\details Returns the join link in it, or an empty string when there is none or it does not look
	like a join link. The game checks the link again when it starts.
*/
std::string readUpdateRequest(const std::string& content);
