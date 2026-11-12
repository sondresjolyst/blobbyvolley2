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

#include <string>

// What the game does for the launcher. The files go to the settings folder through PhysFS.

/// True when a launcher that can install updates started the game.
bool startedByUpdatingLauncher();

/// Tells the launcher that this version runs, so it does not go back to the previous version.
/// Does nothing when no launcher started the game.
void markStartedForLauncher();

/// Asks the launcher to update when the game ends. link is the join link to start the new game with,
/// or empty. False when the request could not be written.
bool requestLauncherUpdate(const std::string& link);
