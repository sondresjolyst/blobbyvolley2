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

#include "Tournament.h"

#include <cstdio>
#include <ctime>
#include <random>
#include <utility>

#include "FileUtil.h"
#include "GenericIO.h"
#include "JoinCodes.h"

// - - - - - - - - - - - - - - - - - -
// 			helpers
// - - - - - - - - - - - - - - - - - -

std::string formatUtcTime(WallTime time)
{
	std::time_t seconds = std::chrono::system_clock::to_time_t(time);
	std::tm parts{};
#ifdef _WIN32
	gmtime_s(&parts, &seconds);
#else
	gmtime_r(&seconds, &parts);
#endif
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
	return buffer;
}

std::string makeReportId()
{
	static std::mt19937_64 generator = [](){
		std::random_device device;
		std::seed_seq seed{ device(), device(), device(), device(),
			static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) };
		return std::mt19937_64(seed);
	}();

	unsigned char bytes[16];
	for (int i = 0; i < 16; i += 8)
	{
		unsigned long long value = generator();
		for (int k = 0; k < 8; ++k)
			bytes[i + k] = static_cast<unsigned char>(value >> (8 * k));
	}
	// version 4, variant 1
	bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
	bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

	char text[37];
	std::snprintf(text, sizeof(text),
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
		bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
	return text;
}

std::string gameReportJson(const GameReport& r)
{
	// every string here is made by the server, so nothing needs escaping
	std::string json = "{\"reportId\":\"" + r.reportId + "\"";
	json += ",\"matchId\":" + std::to_string(r.matchId);
	json += ",\"game\":" + std::to_string(r.game);
	json += std::string(",\"status\":\"") + (r.finished ? "finished" : "abandoned") + "\"";
	json += ",\"winnerId\":" + (r.finished ? std::to_string(r.winnerId) : std::string("null"));
	if (!r.finished && r.leftPlayerId > 0)
		json += ",\"leftPlayerId\":" + std::to_string(r.leftPlayerId);
	json += ",\"points\":[{\"playerId\":" + std::to_string(r.player1) + ",\"points\":" + std::to_string(r.points1) + "}";
	json += ",{\"playerId\":" + std::to_string(r.player2) + ",\"points\":" + std::to_string(r.points2) + "}]";
	json += ",\"startedAt\":\"" + formatUtcTime(r.startedAt) + "\"";
	json += ",\"endedAt\":\"" + formatUtcTime(r.endedAt) + "\"}";
	return json;
}

void writeTournamentStatus(RakNet::BitStream& stream, const PlayerTournamentStatus& status,
						const std::string& opponent, const std::string& ownName, unsigned nextGameInMs)
{
	stream.Write( (unsigned char)ID_LOBBY );
	stream.Write( (unsigned char)LobbyPacketType::TOURNAMENT_STATUS );
	auto out = createGenericWriter( &stream );
	out->byte( (unsigned char)status.status );
	out->string( opponent );
	out->uint32( status.game );
	out->uint32( status.bestOf );
	out->uint32( status.ownWins );
	out->uint32( status.opponentWins );
	out->string( ownName );
	out->uint32( nextGameInMs );
}

TournamentStatusData readTournamentStatus(GenericIn& in)
{
	TournamentStatusData data;
	unsigned char status = 0;
	in.byte(status);
	if (status <= (unsigned char)TournamentStatus::MATCH_OVER)
		data.status = (TournamentStatus)status;
	in.string(data.opponent);
	in.uint32(data.game);
	in.uint32(data.bestOf);
	in.uint32(data.ownWins);
	in.uint32(data.opponentWins);
	in.string(data.ownName);
	in.uint32(data.nextGameInMs);
	if (data.nextGameInMs > MAX_NEXT_GAME_DELAY_MS)
		data.nextGameInMs = MAX_NEXT_GAME_DELAY_MS;
	if (!isValidNickname(data.opponent))
		data.opponent.clear();
	if (!isValidNickname(data.ownName))
		data.ownName.clear();
	return data;
}

// - - - - - - - - - - - - - - - - - -
// 			events
// - - - - - - - - - - - - - - - - - -

TournamentEvents::~TournamentEvents() = default;

void TournamentEvents::liveScore(int, int, int, int, int, int)
{
}

TournamentLog::TournamentLog(std::string resultsPath, ServerLog log)
: mPath(std::move(resultsPath))
, mLog(std::move(log))
{
}

void TournamentLog::matchConnected(int matchId, int player1, int player2)
{
	if (mLog)
		mLog(false, "match " + std::to_string(matchId) + ": players " + std::to_string(player1)
			+ " and " + std::to_string(player2) + " are connected, the match starts");
	append("{\"event\":\"connected\",\"matchId\":" + std::to_string(matchId)
		+ ",\"playerIds\":[" + std::to_string(player1) + "," + std::to_string(player2) + "]"
		+ ",\"at\":\"" + formatUtcTime(std::chrono::system_clock::now()) + "\"}");
}

void TournamentLog::gameEnded(const GameReport& r)
{
	if (mLog)
	{
		std::string text = "match " + std::to_string(r.matchId) + " game " + std::to_string(r.game);
		if (r.finished)
			text += " finished " + std::to_string(r.points1) + ":" + std::to_string(r.points2)
				+ ", winner " + std::to_string(r.winnerId);
		else
		{
			text += " abandoned at " + std::to_string(r.points1) + ":" + std::to_string(r.points2);
			if (r.leftPlayerId > 0)
				text += ", player " + std::to_string(r.leftPlayerId) + " left";
		}
		mLog(false, text);
	}
	append("{\"event\":\"game\",\"report\":" + gameReportJson(r) + "}");
}

void TournamentLog::seriesEnded(const SeriesResult& r)
{
	if (mLog)
		mLog(false, "match " + std::to_string(r.matchId) + " is decided " + std::to_string(r.wins1) + ":"
			+ std::to_string(r.wins2) + ", winner " + std::to_string(r.winnerId));
	append("{\"event\":\"series\",\"matchId\":" + std::to_string(r.matchId)
		+ ",\"bestOf\":" + std::to_string(r.bestOf)
		+ ",\"winnerId\":" + std::to_string(r.winnerId)
		+ ",\"wins\":[{\"playerId\":" + std::to_string(r.player1) + ",\"wins\":" + std::to_string(r.wins1) + "}"
		+ ",{\"playerId\":" + std::to_string(r.player2) + ",\"wins\":" + std::to_string(r.wins2) + "}]"
		+ ",\"at\":\"" + formatUtcTime(std::chrono::system_clock::now()) + "\"}");
}

void TournamentLog::append(const std::string& line)
{
	if (mPath.empty())
		return;
	if (!appendLine(mPath, line) && mLog)
		mLog(true, "cannot write the results file " + mPath);
}

// - - - - - - - - - - - - - - - - - -
// 			director
// - - - - - - - - - - - - - - - - - -

bool PlayerTournamentStatus::operator==(const PlayerTournamentStatus& o) const
{
	return status == o.status && matchId == o.matchId && opponentId == o.opponentId && game == o.game
		&& bestOf == o.bestOf && ownWins == o.ownWins && opponentWins == o.opponentWins
		&& nextGameAt == o.nextGameAt;
}

TournamentDirector::TournamentDirector(unsigned maxRunning, unsigned nextGameDelayMs)
: mMaxRunning(maxRunning)
, mNextGameDelayMs(nextGameDelayMs)
, mMakeReportId(makeReportId)
{
}

void TournamentDirector::setEvents(std::shared_ptr<TournamentEvents> events)
{
	mEvents = std::move(events);
}

void TournamentDirector::setReportIdFunction(std::function<std::string()> make)
{
	mMakeReportId = std::move(make);
}

void TournamentDirector::setAssignments(const std::vector<MatchAssignment>& matches)
{
	std::set<int> listed;
	for (const auto& m : matches)
		listed.insert(m.matchId);

	// keep matches that are gone, so they are not played again if they come back
	for (auto& entry : mSeries)
	{
		if (listed.count(entry.first) == 0)
			entry.second.listed = false;
	}

	for (const auto& m : matches)
	{
		auto found = mSeries.find(m.matchId);
		if (found == mSeries.end())
		{
			Series series;
			series.match = m;
			series.nextGame = m.nextGame;
			takeWins(series, m);
			mSeries[m.matchId] = series;
			continue;
		}

		Series& series = found->second;
		const bool returning = !series.listed;
		series.listed = true;

		if (m.player1 != series.match.player1 || m.player2 != series.match.player2)
		{
			// other players, or the same players on other sides, make a new series.
			// A running game ends and is reported with the players it started with.
			++series.epoch;
			series.match = m;
			series.nextGame = m.nextGame;
			series.wins1 = 0;
			series.wins2 = 0;
			series.decided = false;
			series.failed = false;
			series.held = false;
			series.cooling = false;
			series.connectedSent = false;
			takeWins(series, m);
			continue;
		}

		if (returning)
		{
			// the file is the truth for a match that comes back
			if (!series.running)
				series.nextGame = m.nextGame;
			series.match = m;
			takeWins(series, m);
			continue;
		}

		if (series.match != m)
		{
			const MatchAssignment old = series.match;
			// a changed assignment may fix a game that could not be created
			series.failed = false;
			// the organizer decided about an abandoned game
			if (m.nextGame > old.nextGame || (old.awaitingDecision && !m.awaitingDecision))
				series.held = false;
			// the site moves the next game on when the organizer restarts a game. A lower number
			// means the site refused a game the server played, so the file is the truth then too.
			if (!series.running && m.nextGame != old.nextGame)
				series.nextGame = m.nextGame;
			series.match = m;
			// new numbers from the site replace the local count
			if (m.wins1 != old.wins1 || m.wins2 != old.wins2)
				takeWins(series, m);
		}
	}
}

void TournamentDirector::takeWins(Series& series, const MatchAssignment& m)
{
	if (m.wins1 >= 0)
		series.wins1 = m.wins1;
	if (m.wins2 >= 0)
		series.wins2 = m.wins2;
	// a corrected score can open a series again
	const int needed = series.match.bestOf / 2 + 1;
	series.decided = series.wins1 >= needed || series.wins2 >= needed;
}

void TournamentDirector::playerConnected(int playerId)
{
	if (playerId > 0)
		mConnected.insert(playerId);
}

void TournamentDirector::setConnectedPlayers(const std::set<int>& playerIds)
{
	mConnected.clear();
	for (int id : playerIds)
		playerConnected(id);
}

void TournamentDirector::playerDisconnected(int playerId)
{
	mConnected.erase(playerId);
}

bool TournamentDirector::isConnected(int playerId) const
{
	return mConnected.count(playerId) != 0;
}

void TournamentDirector::updateCooldowns(unsigned long long now_ms)
{
	for (auto& s : mSeries)
	{
		if (s.second.cooling && now_ms >= s.second.cooldownUntil)
			s.second.cooling = false;
	}
}

bool TournamentDirector::isBusy(int playerId) const
{
	for (const auto& s : mSeries)
	{
		if (s.second.running && s.second.playing.hasPlayer(playerId))
			return true;
		if (s.second.cooling && s.second.match.hasPlayer(playerId))
			return true;
	}
	return false;
}

std::vector<TournamentDirector::GameOrder> TournamentDirector::takeGamesToStart(unsigned long long now_ms)
{
	updateCooldowns(now_ms);

	unsigned active = 0;
	for (const auto& s : mSeries)
	{
		if (s.second.running || s.second.cooling)
			++active;
	}

	std::vector<GameOrder> orders;
	// the map is sorted by match id, so lower ids go first
	for (auto& entry : mSeries)
	{
		Series& s = entry.second;
		if (!s.listed || s.running || s.cooling || s.decided || s.failed || s.held || s.match.awaitingDecision)
			continue;
		if (now_ms < s.retryAt)
			continue;
		if (!isConnected(s.match.player1) || !isConnected(s.match.player2))
			continue;
		if (isBusy(s.match.player1) || isBusy(s.match.player2))
			continue;
		if (mMaxRunning != 0 && active >= mMaxRunning)
			break;

		s.running = true;
		s.playing = s.match;
		s.playingGame = s.nextGame;
		s.playingEpoch = s.epoch;
		++active;
		if (!s.connectedSent)
		{
			s.connectedSent = true;
			if (mEvents)
				mEvents->matchConnected(s.match.matchId, s.match.player1, s.match.player2);
		}
		orders.push_back(GameOrder{s.match, s.nextGame});
	}
	return orders;
}

void TournamentDirector::gameNotStarted(int matchId, bool canRetry, unsigned long long retryAt_ms)
{
	auto found = mSeries.find(matchId);
	if (found == mSeries.end())
		return;
	found->second.running = false;
	found->second.failed = !canRetry;
	found->second.retryAt = canRetry ? retryAt_ms : 0;
}

void TournamentDirector::gameFinished(int matchId, int winner, int points1, int points2,
									WallTime startedAt, WallTime endedAt, unsigned long long now_ms)
{
	auto found = mSeries.find(matchId);
	if (found == mSeries.end() || !found->second.running)
		return;
	Series& s = found->second;
	s.running = false;

	GameReport report;
	report.reportId = mMakeReportId();
	report.matchId = matchId;
	report.game = s.playingGame;
	report.finished = true;
	report.winnerId = winner == 1 ? s.playing.player1 : s.playing.player2;
	report.player1 = s.playing.player1;
	report.points1 = points1;
	report.player2 = s.playing.player2;
	report.points2 = points2;
	report.startedAt = startedAt;
	report.endedAt = endedAt;

	if (s.playingEpoch != s.epoch)
	{
		// the series started over while this game ran, so the game does not count for it
		if (mEvents)
			mEvents->gameEnded(report);
		return;
	}

	++s.nextGame;
	if (winner == 1)
		++s.wins1;
	else
		++s.wins2;

	if (mEvents)
		mEvents->gameEnded(report);

	const int needed = s.match.bestOf / 2 + 1;
	if (s.wins1 >= needed || s.wins2 >= needed)
	{
		s.decided = true;
		if (mEvents)
		{
			SeriesResult result;
			result.matchId = matchId;
			result.bestOf = s.match.bestOf;
			result.winnerId = s.wins1 >= needed ? s.match.player1 : s.match.player2;
			result.player1 = s.match.player1;
			result.wins1 = s.wins1;
			result.player2 = s.match.player2;
			result.wins2 = s.wins2;
			mEvents->seriesEnded(result);
		}
	}
	else
	{
		s.cooling = true;
		// a match may set its own pause, otherwise the server config decides
		const unsigned delay = s.playing.nextGameDelay > 0 ? (unsigned)s.playing.nextGameDelay * 1000u : mNextGameDelayMs;
		s.cooldownUntil = now_ms + delay;
	}
}

void TournamentDirector::gameAbandoned(int matchId, int points1, int points2, WallTime startedAt, WallTime endedAt,
									int leftPlayerId)
{
	auto found = mSeries.find(matchId);
	if (found == mSeries.end() || !found->second.running)
		return;
	Series& s = found->second;
	s.running = false;

	GameReport report;
	report.reportId = mMakeReportId();
	report.matchId = matchId;
	report.game = s.playingGame;
	report.finished = false;
	if (s.playing.hasPlayer(leftPlayerId))
		report.leftPlayerId = leftPlayerId;
	report.player1 = s.playing.player1;
	report.points1 = points1;
	report.player2 = s.playing.player2;
	report.points2 = points2;
	report.startedAt = startedAt;
	report.endedAt = endedAt;

	if (mEvents)
		mEvents->gameEnded(report);

	if (s.playingEpoch != s.epoch)
		return;

	// the game is played again under the next number, as after a restart on the site,
	// but only once the organizer decided
	++s.nextGame;
	s.held = true;
}

void TournamentDirector::reportLiveScore(int matchId, int points1, int points2)
{
	auto found = mSeries.find(matchId);
	if (found == mSeries.end() || !found->second.running || !mEvents)
		return;
	const Series& s = found->second;
	mEvents->liveScore(matchId, s.playingGame, s.playing.player1, points1, s.playing.player2, points2);
}

PlayerTournamentStatus TournamentDirector::statusOf(int playerId, unsigned long long now_ms) const
{
	PlayerTournamentStatus result;
	if (playerId <= 0)
		return result;

	auto fill = [&](const Series& s, TournamentStatus status) {
		const bool first = s.match.player1 == playerId;
		result.status = status;
		result.matchId = s.match.matchId;
		result.opponentId = s.match.opponentOf(playerId);
		result.game = s.nextGame;
		result.bestOf = s.match.bestOf;
		result.ownWins = first ? s.wins1 : s.wins2;
		result.opponentWins = first ? s.wins2 : s.wins1;
	};

	// a game that is played or about to continue comes first
	for (const auto& entry : mSeries)
	{
		const Series& s = entry.second;
		if (s.running && s.playing.hasPlayer(playerId))
		{
			fill(s, TournamentStatus::PLAYING);
			result.opponentId = s.playing.opponentOf(playerId);
			result.game = s.playingGame;
			return result;
		}
		if (!s.match.hasPlayer(playerId))
			continue;
		if (s.cooling && now_ms < s.cooldownUntil)
		{
			fill(s, TournamentStatus::NEXT_GAME);
			result.nextGameAt = s.cooldownUntil;
			return result;
		}
	}

	// then the open match with the lowest id
	for (const auto& entry : mSeries)
	{
		const Series& s = entry.second;
		if (!s.listed || s.decided || !s.match.hasPlayer(playerId))
			continue;

		if (s.failed || s.held || s.match.awaitingDecision)
			fill(s, TournamentStatus::WAITING_FOR_ORGANIZER);
		else if (!isConnected(s.match.opponentOf(playerId)))
			fill(s, TournamentStatus::WAITING_FOR_OPPONENT);
		else
			fill(s, TournamentStatus::WAITING_FOR_SLOT);
		return result;
	}

	// at last the decided match with the highest id
	for (auto it = mSeries.rbegin(); it != mSeries.rend(); ++it)
	{
		if (it->second.decided && it->second.match.hasPlayer(playerId))
		{
			fill(it->second, TournamentStatus::MATCH_OVER);
			return result;
		}
	}

	return result;
}

const TournamentDirector::Series* TournamentDirector::getSeries(int matchId) const
{
	auto found = mSeries.find(matchId);
	if (found == mSeries.end())
		return nullptr;
	return &found->second;
}
