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

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "MatchAssignments.h"
#include "NetworkMessage.h"

typedef std::chrono::system_clock::time_point WallTime;

/// Formats a time as ISO 8601 in UTC, such as 2026-11-20T18:04:05Z.
std::string formatUtcTime(WallTime time);

/// Makes a random UUID in the usual 8-4-4-4-12 form.
std::string makeReportId();

/// One game that ended. It holds the fields of a game report to the tournament site.
struct GameReport
{
	std::string reportId;
	int matchId = 0;
	int game = 0;
	/// True for a game that was played out, false for an abandoned game.
	bool finished = false;
	/// Player id of the winner, 0 for an abandoned game.
	int winnerId = 0;
	int player1 = 0;
	int points1 = 0;
	int player2 = 0;
	int points2 = 0;
	WallTime startedAt;
	WallTime endedAt;
};

/// A best-of series that is decided.
struct SeriesResult
{
	int matchId = 0;
	int bestOf = 1;
	int winnerId = 0;
	int player1 = 0;
	int wins1 = 0;
	int player2 = 0;
	int wins2 = 0;
};

/// The body of a game report, as JSON in the form the tournament site expects.
std::string gameReportJson(const GameReport& report);

/*! \class TournamentEvents
	\brief Receives what happens to tournament matches.
	\details The server calls these from its main loop. A reporter that talks to the
	tournament site must not block here. It should queue the event and send it later.
*/
class TournamentEvents
{
	public:
		virtual ~TournamentEvents();

		/// Both players of a match are connected and its first game on this server starts.
		virtual void matchConnected(int matchId, int player1, int player2) = 0;
		/// A game was played out or abandoned.
		virtual void gameEnded(const GameReport& report) = 0;
		/// A player has won the series.
		virtual void seriesEnded(const SeriesResult& result) = 0;
};

/*! \class TournamentLog
	\brief Logs tournament events and appends them to a results file.
	\details The results file has one JSON object per line. A line has an event field,
	which is connected, game or series. A game line holds the report body in report.
	An empty path only logs.
*/
class TournamentLog : public TournamentEvents
{
	public:
		typedef std::function<void(bool problem, const std::string&)> LogFunction;

		TournamentLog(std::string resultsPath, LogFunction log);

		void matchConnected(int matchId, int player1, int player2) override;
		void gameEnded(const GameReport& report) override;
		void seriesEnded(const SeriesResult& result) override;

	private:
		void append(const std::string& line);

		std::string mPath;
		LogFunction mLog;
};

/// What a player of the tournament waits for.
struct PlayerTournamentStatus
{
	TournamentStatus status = TournamentStatus::NO_MATCH;
	int matchId = 0;
	int opponentId = 0;
	int game = 0;
	int bestOf = 0;
	int ownWins = 0;
	int opponentWins = 0;

	bool operator==(const PlayerTournamentStatus& o) const;
	bool operator!=(const PlayerTournamentStatus& o) const { return !(*this == o); }
};

/*! \class TournamentDirector
	\brief Decides when assigned matches start and keeps the score of each series.
	\details The director knows nothing about the network. The server tells it which
	players are connected and how games end, and asks it which games to start.
	Player 1 of a match always plays on the left.
*/
class TournamentDirector
{
	public:
		/// A game the server should start now.
		struct GameOrder
		{
			MatchAssignment match;
			int game;
		};

		/// Progress of the series of one match.
		struct Series
		{
			MatchAssignment match;
			// still in the current assignments
			bool listed = true;
			int nextGame = 1;
			int wins1 = 0;
			int wins2 = 0;
			bool running = false;
			// the assignment and game number of the running game, taken when it started
			MatchAssignment playing;
			int playingGame = 0;
			// grows when the players of the match change, which starts a new series
			unsigned epoch = 0;
			unsigned playingEpoch = 0;
			bool decided = false;
			// the last game could not be created, wait for a changed assignment
			bool failed = false;
			// the last game was abandoned, wait until the organizer decides in the file
			bool held = false;
			// a game that could not start is not tried again before this time
			unsigned long long retryAt = 0;
			bool connectedSent = false;
			// the next game of the series waits until this time
			bool cooling = false;
			unsigned long long cooldownUntil = 0;
		};

		/// \param maxRunning most matches that play at the same time, 0 for no limit
		/// \param nextGameDelayMs pause between two games of a series
		TournamentDirector(unsigned maxRunning, unsigned nextGameDelayMs);

		void setEvents(std::shared_ptr<TournamentEvents> events);

		/// Replaces the assignments. A match that is no longer assigned plays its running game to the end.
		/// The director keeps every series it has seen, so a match that comes back is not played again.
		/// When the players of a match change, its series starts over.
		/// When the wins of a match in the file change, they replace the local series score,
		/// since the site is the source of truth. A local win stays until then.
		void setAssignments(const std::vector<MatchAssignment>& matches);

		void playerConnected(int playerId);
		/// Replaces the set of connected players, for example after the players file changed.
		void setConnectedPlayers(const std::set<int>& playerIds);
		void playerDisconnected(int playerId);
		bool isConnected(int playerId) const;

		/// Returns the games that can start now and marks them as running.
		/// now_ms is any monotonic time in milliseconds.
		std::vector<GameOrder> takeGamesToStart(unsigned long long now_ms);

		/// The server could not create the game. Unless canRetry is set, the match waits
		/// until its assignment changes. With canRetry it is tried again from retryAt_ms on.
		void gameNotStarted(int matchId, bool canRetry = false, unsigned long long retryAt_ms = 0);

		/// A running game was played out. winner is 1 or 2.
		void gameFinished(int matchId, int winner, int points1, int points2,
						WallTime startedAt, WallTime endedAt, unsigned long long now_ms);

		/// A running game ended without a winner, for example because a player left.
		/// The match then waits until its assignment brings a new nextGame or awaitingDecision false.
		void gameAbandoned(int matchId, int points1, int points2, WallTime startedAt, WallTime endedAt);

		PlayerTournamentStatus statusOf(int playerId, unsigned long long now_ms) const;

		/// The series of a match, or nullptr when the director does not know the match.
		const Series* getSeries(int matchId) const;

		/// Used to make report ids, so tests can use fixed ones.
		void setReportIdFunction(std::function<std::string()> make);

	private:
		void updateCooldowns(unsigned long long now_ms);
		bool isBusy(int playerId) const;
		static void takeWins(Series& series, const MatchAssignment& match);

		unsigned mMaxRunning;
		unsigned mNextGameDelayMs;
		std::shared_ptr<TournamentEvents> mEvents;
		std::function<std::string()> mMakeReportId;

		std::map<int, Series> mSeries;
		std::set<int> mConnected;
};
