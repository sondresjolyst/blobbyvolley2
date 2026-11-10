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

#include "ScriptBot.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include "DuelMatch.h"
#include "DuelMatchState.h"
#include "FileSystem.h"
#include "GameLogic.h"
#include "ScriptedInputSource.h"

std::vector<std::string> findBotScripts()
{
	FileSystem& fs = FileSystem::getSingleton();
	if (!fs.isDirectory("scripts"))
		return {};
	auto scripts = fs.enumerateFiles("scripts", ".lua", false);
	std::sort(scripts.begin(), scripts.end());
	return scripts;
}

/// The script and the match it reads. Only the thread of the worker touches them after the start.
struct ScriptBot::Worker
{
	Worker(const std::string& script, int difficulty, int scoreToWin)
		// the rules run on the server, the match only holds the state the server sends
		: match(true, FALLBACK_RULES_NAME, scoreToWin > 0 ? scoreToWin : 15)
		, input("scripts/" + script, LEFT_PLAYER, (unsigned)difficulty, &match)
	{
	}

	void run()
	{
		std::unique_lock<std::mutex> lock(mutex);
		while (true)
		{
			wake.wait(lock, [this] { return stop || hasRequest; });
			if (stop)
				return;
			DuelMatchState state = request;
			hasRequest = false;
			busy = true;
			lock.unlock();

			auto start = std::chrono::steady_clock::now();
			PlayerInputAbs next;
			std::string error;
			try
			{
				match.setState(state);
				next = input.getNextInput();
				error = input.takeLuaError();
			}
			catch (const std::exception& e)
			{
				error = e.what();
			}
			long microseconds = (long)std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - start).count();

			lock.lock();
			result = next;
			resultError = error;
			resultMicroseconds = microseconds;
			hasResult = true;
			busy = false;
			done.notify_all();
		}
	}

	DuelMatch match;
	ScriptedInputSource input;

	std::mutex mutex;
	std::condition_variable wake;
	std::condition_variable done;
	bool stop = false;
	bool hasRequest = false;
	bool busy = false;
	DuelMatchState request;
	bool hasResult = false;
	PlayerInputAbs result;
	std::string resultError;
	long resultMicroseconds = 0;
};

ScriptBot::ScriptBot(const std::string& script, int difficulty, int scoreToWin, std::chrono::milliseconds budget)
	: mWorker(std::make_shared<Worker>(script, difficulty, scoreToWin))
	, mBudget(budget)
{
	// the thread keeps the worker alive, so it can still finish a step after the bot is gone
	std::shared_ptr<Worker> worker = mWorker;
	std::thread([worker] { worker->run(); }).detach();
}

ScriptBot::~ScriptBot()
{
	std::lock_guard<std::mutex> lock(mWorker->mutex);
	mWorker->stop = true;
	mWorker->wake.notify_all();
}

ScriptBot::Step ScriptBot::step(const DuelMatchState& state)
{
	const auto deadline = std::chrono::steady_clock::now() + mBudget;
	Worker& worker = *mWorker;
	std::unique_lock<std::mutex> lock(worker.mutex);

	Step step;
	// a step that was late before finished since, so its input is newer than the last one
	if (worker.hasResult)
	{
		mLastInput = worker.result;
		worker.hasResult = false;
		step.error = worker.resultError;
	}
	// while the script is still busy with an older state, the newest state waits for the next step
	if (!worker.busy)
	{
		worker.request = state;
		worker.hasRequest = true;
		worker.wake.notify_all();
	}

	if (step.error.empty() && worker.done.wait_until(lock, deadline, [&worker] { return worker.hasResult; }))
	{
		mLastInput = worker.result;
		worker.hasResult = false;
		step.error = worker.resultError;
		step.microseconds = worker.resultMicroseconds;
	}
	else if (step.error.empty())
		step.late = true;

	step.input = mLastInput;
	return step;
}
