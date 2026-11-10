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

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "PlayerInput.h"

struct DuelMatchState;

/// The names of the bot scripts in the folder scripts of the file system, without .lua. Sorted.
/// Empty when there is no such folder.
std::vector<std::string> findBotScripts();

/// Plays with one of the bot scripts of the game, through the same ScriptedInputSource as a local game.
/// The script always plays the left blob. The load test bot asks the server for the left side, and the
/// server then sends it the game from the left and mirrors its input back, see tools/loadbot/loadbot.cpp.
/// The server bot of a training match mirrors the state and the input itself, see NetworkGame::setBot.
///
/// The script runs on a thread of its own, so a slow script cannot stall the network loop.
class ScriptBot
{
	public:
		/// What one step of the script gave.
		struct Step
		{
			PlayerInputAbs input;
			/// the script did not finish within the budget, so input is the input of an earlier step
			bool late = false;
			/// the message of a lua error, empty when the script ran without one
			std::string error;
			/// how long the script took for the result in input, -1 when it is late
			long microseconds = -1;
		};

		/// Loads the script. Throws when it cannot be loaded.
		ScriptBot(const std::string& script, int difficulty, int scoreToWin, std::chrono::milliseconds budget);
		/// Returns at once, also when the script still runs. The script then stops on its thread.
		~ScriptBot();

		ScriptBot(const ScriptBot&) = delete;
		ScriptBot& operator=(const ScriptBot&) = delete;

		/// Gives the script the newest state and waits at most the budget for its input.
		Step step(const DuelMatchState& state);

	private:
		struct Worker;
		std::shared_ptr<Worker> mWorker;
		std::chrono::milliseconds mBudget;
		PlayerInputAbs mLastInput;
};
