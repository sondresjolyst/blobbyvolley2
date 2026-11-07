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
   plays every game the server starts with a simple bot and prints one JSON line per event.
   It opens no window and plays no sound. See tools/loadbot/README.md. */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>

#include "DuelMatchState.h"
#include "GenericIO.h"
#include "Global.h"
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "PlayerInput.h"
#include "raknet/BitStream.h"
#include "raknet/PacketEnumerations.h"
#include "raknet/RakClient.h"
#include "server/Tournament.h"

// the shared server sources count into these, as in servermain.cpp
std::atomic<int> SWLS_PacketCount{0};
std::atomic<int> SWLS_Connections{0};
std::atomic<int> SWLS_Games{0};
std::atomic<int> SWLS_GameSteps{0};

namespace
{
	// the bot steps a little faster than a game at 75 frames per second
	const std::chrono::milliseconds STEP_TIME(13);
	const std::chrono::seconds RECONNECT_DELAY(2);
	// the net is in the middle of the 800 wide field
	const float NET_X = 400;
	const float HOME_X = 200;

	std::string gCode;

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

	/// Prints one event as a line of JSON. extra holds more fields, without the braces.
	void logEvent(const char* event, const std::string& extra = std::string())
	{
		std::printf("{\"t\":%.3f,\"code\":\"%s\",\"event\":\"%s\"%s%s}\n", wallSeconds(), gCode.c_str(), event,
					extra.empty() ? "" : ",", extra.c_str());
		std::fflush(stdout);
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

		void start(unsigned now)
		{
			*this = GameStats();
			startedAt = lastUpdate = now;
		}

		void update(unsigned now, int echoedTime)
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
			unsigned rtt = now - (unsigned)echoedTime;
			if (echoedTime > 0 && rtt < 10000)
			{
				rttSum += rtt;
				++rttCount;
				rttMax = std::max(rttMax, rtt);
			}
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
			return buffer;
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

	/// Plays on one connection until it is lost.
	void playConnection(const std::string& host, unsigned short port, int skill, std::mt19937& rng)
	{
		auto client = std::make_shared<RakClient>();
		if (!client->Connect(host.c_str(), port, 0, 0, RAKNET_THREAD_SLEEP_TIME))
		{
			logEvent("connect_failed");
			return;
		}
		logEvent("connecting");

		DuelMatchState state;
		GameStats stats;
		bool playing = false;
		bool haveState = false;
		int targetOffset = 0;

		auto endGame = [&](const char* why, int won)
		{
			if (!playing)
				return;
			logEvent("game_end", stats.json(why, won, state, ticks()));
			playing = false;
		};

		while (true)
		{
			packet_ptr packet;
			while ((packet = client->Receive()))
			{
				switch (packet->data[0])
				{
					case ID_CONNECTION_REQUEST_ACCEPTED:
					{
						// the server swaps the join code in the name field for the nickname
						PlayerIdentity me(gCode, Color(255, 0, 0), false, LEFT_PLAYER);
						RakNet::BitStream stream;
						makeEnterServerPacket(stream, me);
						client->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
						logEvent("connected");
						break;
					}
					case ID_CONNECTION_ATTEMPT_FAILED:
					case ID_NO_FREE_INCOMING_CONNECTIONS:
						logEvent("connect_failed", "\"packet\":" + std::to_string((int)packet->data[0]));
						return;
					case ID_CONNECTION_LOST:
					case ID_DISCONNECTION_NOTIFICATION:
						endGame("disconnected", -1);
						logEvent("disconnected", "\"packet\":" + std::to_string((int)packet->data[0]));
						return;
					case ID_LOBBY:
					{
						RakNet::BitStream stream(packet->data, packet->length, false);
						auto in = createGenericReader(&stream);
						unsigned char type = 0;
						in->byte(type);	// ID_LOBBY
						in->byte(type);
						if ((LobbyPacketType)type == LobbyPacketType::ENTRY_REFUSED)
						{
							unsigned char reason = 0xFF;
							stream.Read(reason);
							logEvent("refused", "\"reason\":" + std::to_string((int)reason));
						}
						else if ((LobbyPacketType)type == LobbyPacketType::TOURNAMENT_STATUS)
						{
							TournamentStatusData status = readTournamentStatus(*in);
							logEvent("status", "\"status\":" + std::to_string((int)status.status)
								+ ",\"opponent\":\"" + status.opponent + "\",\"game\":" + std::to_string(status.game)
								+ ",\"bestOf\":" + std::to_string(status.bestOf) + ",\"own\":" + std::to_string(status.ownWins)
								+ ",\"opp\":" + std::to_string(status.opponentWins) + ",\"name\":\"" + status.ownName + "\"");
						}
						break;
					}
					case ID_RULES_CHECKSUM:
					{
						RakNet::BitStream stream(packet->data, packet->length, false);
						stream.IgnoreBytes(1);	// ID_RULES_CHECKSUM
						int checksum = 0;
						int scoreToWin = 0;
						stream.Read(checksum);
						stream.Read(scoreToWin);
						// answer at once, like the game does in the frame that reads the packet,
						// and say that the rules are cached
						RakNet::BitStream answer;
						answer.Write((unsigned char)ID_RULES);
						answer.Write(false);
						client->Send(&answer, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
						logEvent("game_offered", "\"scoreToWin\":" + std::to_string(scoreToWin));
						break;
					}
					case ID_GAME_READY:
					{
						RakNet::BitStream stream(packet->data, packet->length, false);
						stream.IgnoreBytes(1);	// ID_GAME_READY
						int speed = 0;
						char name[16] = {0};
						stream.Read(speed);
						stream.Read(name, sizeof(name));
						name[sizeof(name) - 1] = '\0';
						playing = true;
						haveState = false;
						stats.start(ticks());
						targetOffset = std::uniform_int_distribution<int>(10, 34)(rng);
						logEvent("game_start", "\"speed\":" + std::to_string(speed) + ",\"opponent\":\"" + name + "\"");
						break;
					}
					case ID_GAME_UPDATE:
					{
						RakNet::BitStream stream(packet->data, packet->length, false);
						stream.IgnoreBytes(1);	// ID_GAME_UPDATE
						int echoedTime = 0;
						stream.Read(echoedTime);
						auto in = createGenericReader(&stream);
						in->generic<DuelMatchState>(state);
						haveState = true;
						if (playing)
							stats.update(ticks(), echoedTime);
						break;
					}
					case ID_WIN_NOTIFICATION:
					{
						RakNet::BitStream stream(packet->data, packet->length, false);
						stream.IgnoreBytes(1);	// ID_WIN_NOTIFICATION
						int winner = NO_PLAYER;
						stream.Read(winner);
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
				RakNet::BitStream stream;
				stream.Write((unsigned char)ID_INPUT_UPDATE);
				stream.Write(ticks());
				botInput(state, skill, targetOffset, rng).writeTo(stream);
				client->Send(&stream, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
			}
			std::this_thread::sleep_for(STEP_TIME);
		}
	}
}

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: blobby-loadbot <host> <port> <join code> [skill 0 to 100]\n");
		return 2;
	}
	const std::string host = argv[1];
	const unsigned short port = (unsigned short)std::atoi(argv[2]);
	gCode = argv[3];
	const int skill = argc > 4 ? std::atoi(argv[4]) : 70;
	std::mt19937 rng((unsigned)std::hash<std::string>()(gCode));

	// a lost connection is tried again, as a player would
	while (true)
	{
		playConnection(host, port, skill, rng);
		std::this_thread::sleep_for(RECONNECT_DELAY);
	}
}
