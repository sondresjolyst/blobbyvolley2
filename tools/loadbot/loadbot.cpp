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

/* A headless client for load tests of a tournament server. It joins with a join code,
   plays every game the server starts with a simple bot, or with a bot script of the game,
   and prints one JSON line per event.
   It opens no window and plays no sound. See tools/loadbot/README.md. */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "DuelMatchState.h"
#include "FileSystem.h"
#include "GenericIO.h"
#include "Global.h"
#include "IScriptableComponent.h"
#include "LoadbotArgs.h"
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "PlayerInput.h"
#include "raknet/BitStream.h"
#include "raknet/PacketEnumerations.h"
#include "raknet/RakClient.h"
#include "ScriptBot.h"
#include "TournamentProtocol.h"

namespace
{
	const int EXIT_STOPPED = 0;
	const int EXIT_USAGE = 2;
	const int EXIT_REFUSED = 3;
	const int EXIT_SCRIPT = 4;

	// the bot steps a little faster than a game at 75 frames per second
	const std::chrono::milliseconds STEP_TIME(13);
	// the longest a bot script may take for one step before the bot sends its last input again
	const std::chrono::milliseconds SCRIPT_BUDGET(10);
	// script_slow is logged at most this often
	const std::chrono::seconds SCRIPT_SLOW_LOG_TIME(1);
	// a failed connection is tried again after this, twice as long after each failure
	const std::chrono::seconds FIRST_RETRY(2);
	const std::chrono::seconds MAX_RETRY(30);
	// the net is in the middle of the 800 wide field
	const float NET_X = 400;
	const float HOME_X = 200;

	std::atomic<bool> gStop{false};
	// the join code is never printed in full
	std::string gCodePrefix;
	// the nickname the server sent, empty until the first status
	std::string gName;

	void onSignal(int)
	{
		gStop = true;
	}

	/// Sleeps, but returns early when the bot is told to stop.
	void sleepUnlessStopped(std::chrono::milliseconds time)
	{
		const auto end = std::chrono::steady_clock::now() + time;
		while (!gStop && std::chrono::steady_clock::now() < end)
			std::this_thread::sleep_for(std::min(time, std::chrono::milliseconds(100)));
	}

	double wallSeconds()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() / 1000.0;
	}

	unsigned ticks()
	{
		using namespace std::chrono;
		static const auto start = steady_clock::now();
		return (unsigned)duration_cast<milliseconds>(steady_clock::now() - start).count();
	}

	/// A text as a quoted JSON string.
	std::string jsonString(const std::string& text)
	{
		std::string out = "\"";
		for (unsigned char c : text)
		{
			if (c == '"' || c == '\\')
			{
				out += '\\';
				out += (char)c;
			}
			else if (c < 0x20)
			{
				char escaped[8];
				std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
				out += escaped;
			}
			else
				out += (char)c;
		}
		return out + "\"";
	}

	/// Prints one event as a line of JSON. extra holds more fields, without the braces.
	void logEvent(const char* event, const std::string& extra = std::string())
	{
		char time[32];
		std::snprintf(time, sizeof(time), "%.3f", wallSeconds());
		std::string line = std::string("{\"t\":") + time + ",\"code\":" + jsonString(gCodePrefix)
			+ ",\"name\":" + jsonString(gName) + ",\"event\":" + jsonString(event);
		if (!extra.empty())
			line += "," + extra;
		std::printf("%s}\n", line.c_str());
		std::fflush(stdout);
	}

	const char* refusedReasonName(unsigned char reason)
	{
		switch ((EntryRefusedReason)reason)
		{
			case EntryRefusedReason::UNKNOWN_CODE:
				return "unknown code";
			case EntryRefusedReason::CODE_IN_USE:
				return "code in use";
			case EntryRefusedReason::OUTDATED_CLIENT:
				return "outdated client";
		}
		return "unknown reason";
	}

	/// What a client sees of one game, for the game_end line.
	struct GameStats
	{
		unsigned startedAt = 0;
		unsigned lastUpdate = 0;
		unsigned updates = 0;
		unsigned maxGap = 0;
		unsigned gapsOver100ms = 0;
		unsigned long rttSum = 0;
		unsigned rttCount = 0;
		unsigned rttMax = 0;
		// the longest time the bot script took for one input, -1 for the simple bot
		long scriptMaxUs = -1;
		// the steps the script did not finish within its budget
		unsigned scriptSlowSteps = 0;

		void start(unsigned now)
		{
			*this = GameStats();
			startedAt = lastUpdate = now;
		}

		void update(unsigned now, unsigned inputTime)
		{
			if (updates > 0)
			{
				unsigned gap = now - lastUpdate;
				maxGap = std::max(maxGap, gap);
				if (gap > 100)
					++gapsOver100ms;
			}
			++updates;
			lastUpdate = now;
			// the server sends back the time of the last input it got from us
			unsigned rtt = now - inputTime;
			if (inputTime > 0 && rtt < 10000)
			{
				rttSum += rtt;
				++rttCount;
				rttMax = std::max(rttMax, rtt);
			}
		}

		void scriptTime(long microseconds)
		{
			scriptMaxUs = std::max(scriptMaxUs, microseconds);
		}

		std::string json(const char* why, int won, const DuelMatchState& state, unsigned now) const
		{
			char buffer[400];
			std::snprintf(buffer, sizeof(buffer),
				"\"why\":\"%s\",\"won\":%d,\"duration_ms\":%u,\"updates\":%u,\"max_gap_ms\":%u,\"gaps_over_100ms\":%u,"
				"\"rtt_avg_ms\":%ld,\"rtt_max_ms\":%u,\"score_own\":%d,\"score_opp\":%d",
				why, won, now - startedAt, updates, maxGap, gapsOver100ms,
				rttCount ? (long)(rttSum / rttCount) : -1L, rttMax,
				state.getScore(LEFT_PLAYER), state.getScore(RIGHT_PLAYER));
			std::string line = buffer;
			if (scriptMaxUs >= 0)
				line += ",\"script_max_us\":" + std::to_string(scriptMaxUs)
					+ ",\"script_slow_steps\":" + std::to_string(scriptSlowSteps);
			return line;
		}
	};

	/// Moves under the ball on its own side and jumps when the ball is close. The server shows each
	/// client its game from the left, so the bot always plays the left blob.
	PlayerInputAbs botInput(const DuelMatchState& state, int skill, int targetOffset, std::mt19937& rng)
	{
		std::uniform_int_distribution<int> percent(0, 99);
		Vector2 ball = state.getBallPosition();
		Vector2 blob = state.getBlobPosition(LEFT_PLAYER);
		float target = ball.x < NET_X + 20 ? ball.x - targetOffset : HOME_X;
		bool sloppy = percent(rng) >= skill;
		bool left = blob.x > target + 6;
		bool right = blob.x < target - 6;
		if (sloppy && percent(rng) < 30)
			std::swap(left, right);
		bool jump = !sloppy && ball.x < NET_X && std::fabs(ball.x - blob.x) < 70 && ball.y > 250;
		return PlayerInputAbs(left, right, jump);
	}

	/// Loads the script once at the start, so a script that cannot run stops the bot at once.
	bool checkScript(const LoadbotArgs& args)
	{
		try
		{
			ScriptBot bot(args.script, args.difficulty, 0, SCRIPT_BUDGET);
			return true;
		}
		catch (const ScriptException& e)
		{
			std::fprintf(stderr, "the script %s could not be loaded: %s\n", args.script.c_str(), e.luaerror.c_str());
		}
		catch (const std::exception& e)
		{
			std::fprintf(stderr, "the script %s could not be loaded: %s\n", args.script.c_str(), e.what());
		}
		return false;
	}

	enum class Outcome
	{
		STOPPED,
		REFUSED,
		// the connection was up and then got lost
		LOST,
		CONNECT_FAILED
	};

	/// Plays on one connection until it ends.
	Outcome playConnection(const LoadbotArgs& args, std::mt19937& rng)
	{
		const std::string& code = args.code;
		auto client = std::make_shared<RakClient>();
		if (!client->Connect(args.host.c_str(), args.port, 0, 0, RAKNET_THREAD_SLEEP_TIME))
		{
			logEvent("connect_failed");
			return Outcome::CONNECT_FAILED;
		}
		logEvent("connecting");

		DuelMatchState state;
		GameStats stats;
		bool connected = false;
		bool playing = false;
		bool haveState = false;
		int targetOffset = 0;
		int scoreToWin = 0;
		std::unique_ptr<ScriptBot> scriptBot;
		// slow script steps since the last script_slow line
		unsigned slowSteps = 0;
		auto lastSlowLog = std::chrono::steady_clock::now() - SCRIPT_SLOW_LOG_TIME;
		// the bot steps at a fixed rate, also when the script takes a while
		auto nextStep = std::chrono::steady_clock::now();

		auto endGame = [&](const char* why, int won)
		{
			if (!playing)
				return;
			logEvent("game_end", stats.json(why, won, state, ticks()));
			playing = false;
		};
		auto close = [&](Outcome outcome)
		{
			client->Disconnect(25);
			return outcome;
		};

		while (!gStop)
		{
			packet_ptr packet;
			while ((packet = client->Receive()))
			{
				RakNet::BitStream stream(packet->data, packet->length, false);
				switch (packet->data[0])
				{
					case ID_CONNECTION_REQUEST_ACCEPTED:
					{
						// the server swaps the join code in the name field for the nickname.
						// The bot asks for the left side, so the server always shows it the game from the left.
						// A tournament game mirrors a player who wants the left side but plays on the right
						// (DedicatedServer.cpp, switchRight), and the lobby does the same (MatchMaker.cpp, switchSide).
						// NetworkGame::broadcastPhysicState then sends it the state with swapSides(), and the
						// handler of ID_INPUT_UPDATE in NetworkGame.cpp swaps its input back.
						PlayerIdentity me(code, Color(255, 0, 0), false, LEFT_PLAYER);
						RakNet::BitStream enter;
						makeEnterServerPacket(enter, me);
						client->Send(&enter, LOW_PRIORITY, RELIABLE_ORDERED, 0);
						connected = true;
						logEvent("connected");
						break;
					}
					case ID_CONNECTION_ATTEMPT_FAILED:
					case ID_NO_FREE_INCOMING_CONNECTIONS:
						logEvent("connect_failed", "\"packet\":" + std::to_string((int)packet->data[0]));
						return close(Outcome::CONNECT_FAILED);
					case ID_CONNECTION_LOST:
					case ID_DISCONNECTION_NOTIFICATION:
						endGame("disconnected", -1);
						logEvent("disconnected", "\"packet\":" + std::to_string((int)packet->data[0]));
						return close(connected ? Outcome::LOST : Outcome::CONNECT_FAILED);
					case ID_LOBBY:
					{
						auto in = createGenericReader(&stream);
						unsigned char type = 0;
						in->byte(type);	// ID_LOBBY
						in->byte(type);
						if ((LobbyPacketType)type == LobbyPacketType::ENTRY_REFUSED)
						{
							unsigned char reason = 0xFF;
							stream.Read(reason);
							logEvent("refused", "\"reason\":" + jsonString(refusedReasonName(reason)));
							return close(Outcome::REFUSED);
						}
						if ((LobbyPacketType)type == LobbyPacketType::TOURNAMENT_STATUS)
						{
							TournamentStatusData status = readTournamentStatus(*in);
							if (!status.ownName.empty())
								gName = status.ownName;
							logEvent("status", "\"status\":" + std::to_string((int)status.status)
								+ ",\"opponent\":" + jsonString(status.opponent) + ",\"game\":" + std::to_string(status.game)
								+ ",\"bestOf\":" + std::to_string(status.bestOf) + ",\"own\":" + std::to_string(status.ownWins)
								+ ",\"opp\":" + std::to_string(status.opponentWins));
						}
						break;
					}
					case ID_RULES_CHECKSUM:
					{
						RulesChecksumData rules;
						if (!readRulesChecksum(stream, rules))
						{
							// the client ignores such a packet too, so no game starts
							logEvent("rules_rejected", "\"scoreToWin\":" + std::to_string(rules.scoreToWin));
							break;
						}
						// answer at once, like the client does in the frame that reads the packet.
						// The bot does not simulate the game, so it never asks for the rules file.
						RakNet::BitStream answer;
						makeRulesAnswerPacket(answer, false);
						client->Send(&answer, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
						scoreToWin = rules.scoreToWin;
						logEvent("game_offered", "\"scoreToWin\":" + std::to_string(rules.scoreToWin));
						break;
					}
					case ID_GAME_READY:
					{
						GameReadyData ready = readGameReady(stream);
						playing = true;
						haveState = false;
						stats.start(ticks());
						targetOffset = std::uniform_int_distribution<int>(10, 34)(rng);
						scriptBot.reset();
						if (!args.script.empty())
						{
							// a new game gets a new script, as a local game does
							try
							{
								scriptBot.reset(new ScriptBot(args.script, args.difficulty, scoreToWin, SCRIPT_BUDGET));
								stats.scriptMaxUs = 0;
								slowSteps = 0;
							}
							catch (const std::exception&)
							{
								logEvent("script_failed", "\"script\":" + jsonString(args.script));
							}
						}
						logEvent("game_start", "\"speed\":" + std::to_string(ready.speed) + ",\"opponent\":" + jsonString(ready.opponent));
						break;
					}
					case ID_GAME_UPDATE:
					{
						unsigned inputTime = readGameUpdateTime(stream);
						readGameUpdateState(stream, state);
						haveState = true;
						if (playing)
							stats.update(ticks(), inputTime);
						break;
					}
					case ID_WIN_NOTIFICATION:
					{
						int winner = NO_PLAYER;
						if (readWinNotification(stream, winner))
							endGame("won_notification", winner == LEFT_PLAYER ? 1 : 0);
						break;
					}
					case ID_OPPONENT_DISCONNECTED:
						endGame("opponent_disconnected", -1);
						break;
					default:
						break;
				}
			}

			if (playing && haveState)
			{
				RakNet::BitStream input;
				input.Write((unsigned char)ID_INPUT_UPDATE);
				input.Write(ticks());
				PlayerInputAbs next;
				bool haveInput = false;
				if (scriptBot)
				{
					ScriptBot::Step step = scriptBot->step(state);
					if (!step.error.empty())
					{
						// the simple bot plays the rest of the game
						logEvent("script_error", "\"script\":" + jsonString(args.script) + ",\"message\":" + jsonString(step.error));
						scriptBot.reset();
					}
					else
					{
						next = step.input;
						haveInput = true;
						if (step.late)
						{
							++stats.scriptSlowSteps;
							++slowSteps;
							auto now = std::chrono::steady_clock::now();
							if (now - lastSlowLog >= SCRIPT_SLOW_LOG_TIME)
							{
								logEvent("script_slow", "\"steps\":" + std::to_string(slowSteps) + ",\"budget_ms\":"
									+ std::to_string(SCRIPT_BUDGET.count()));
								slowSteps = 0;
								lastSlowLog = now;
							}
						}
						else
							stats.scriptTime(step.microseconds);
					}
				}
				if (!haveInput)
					next = botInput(state, args.skill, targetOffset, rng);
				next.writeTo(input);
				client->Send(&input, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
			}
			nextStep += STEP_TIME;
			auto now = std::chrono::steady_clock::now();
			// after a long stall the bot does not try to catch up
			if (nextStep < now)
				nextStep = now;
			std::this_thread::sleep_until(nextStep);
		}
		endGame("stopped", -1);
		return close(Outcome::STOPPED);
	}
}

int main(int argc, char** argv)
{
	// the bot scripts are in the data folder, installed or below the working directory
	FileSystem fileSystem(argv[0]);
#ifdef BLOBBY_DATA_DIR
	fileSystem.addToSearchPath(BLOBBY_DATA_DIR);
#endif
	fileSystem.addToSearchPath("data");

	LoadbotArgs args;
	std::string error;
	if (!parseLoadbotArgs(std::vector<std::string>(argv + 1, argv + argc), findBotScripts(), args, error))
	{
		std::fprintf(stderr, "%s\n%s", error.c_str(), LOADBOT_USAGE);
		return EXIT_USAGE;
	}
	if (!args.script.empty() && !checkScript(args))
		return EXIT_SCRIPT;
	gCodePrefix = joinCodeLogPrefix(args.code);
	std::mt19937 rng((unsigned)std::hash<std::string>()(args.code));

	// docker stop sends SIGTERM, which ends the bot at once
	std::signal(SIGTERM, onSignal);
	std::signal(SIGINT, onSignal);

	std::chrono::milliseconds retry = FIRST_RETRY;
	while (!gStop)
	{
		switch (playConnection(args, rng))
		{
			case Outcome::STOPPED:
				break;
			case Outcome::REFUSED:
				std::fprintf(stderr, "the server refused the join code %s, the bot stops\n", gCodePrefix.c_str());
				return EXIT_REFUSED;
			case Outcome::LOST:
				// a lost connection is tried again soon, as a player would
				retry = FIRST_RETRY;
				sleepUnlessStopped(retry);
				break;
			case Outcome::CONNECT_FAILED:
				sleepUnlessStopped(retry);
				retry = std::min<std::chrono::milliseconds>(retry * 2, MAX_RETRY);
				break;
		}
	}
	logEvent("stopped");
	return EXIT_STOPPED;
}
