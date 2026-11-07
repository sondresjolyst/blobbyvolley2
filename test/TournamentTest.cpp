#include <boost/test/unit_test.hpp>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "server/JoinCodes.h"
#include "server/MatchAssignments.h"
#include "server/Tournament.h"
#include "server/NetworkPlayer.h"
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "GenericIO.h"
#include "TestServer.h"

namespace
{
	const char* const PLAYERS =
		"\t<player id=\"12\" nickname=\"Ola\" code=\"K7QM2XPR4T\"/>\n"
		"\t<player id=\"17\" nickname=\"Kari\" code=\"B9HNZ3WD6F\"/>\n"
		"\t<player id=\"21\" nickname=\"Per\" code=\"C2DEFGHJKM\"/>\n"
		"\t<player id=\"25\" nickname=\"Siri\" code=\"N3PQRSTUVW\"/>\n";

	std::string playersFile(const std::string& matches)
	{
		return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<tournament>\n") + PLAYERS + matches + "</tournament>\n";
	}

	const std::set<int> KNOWN{12, 17, 21, 25};

	std::vector<MatchAssignment> parse(const std::string& matches, std::vector<std::string>& warnings)
	{
		std::vector<MatchAssignment> result;
		std::string error;
		BOOST_REQUIRE(parseMatchAssignments(playersFile(matches), KNOWN, result, warnings, error));
		return result;
	}

	MatchAssignment makeMatch(int id, int p1, int p2, int bestOf = 1)
	{
		MatchAssignment m;
		m.matchId = id;
		m.player1 = p1;
		m.player2 = p2;
		m.bestOf = bestOf;
		return m;
	}

	struct DirectorFixture
	{
		explicit DirectorFixture(unsigned maxRunning = 0, unsigned delay = 5000)
		: director(maxRunning, delay)
		, events(std::make_shared<RecordingEvents>())
		{
			director.setEvents(events);
			int counter = 0;
			director.setReportIdFunction([counter]() mutable { return "report-" + std::to_string(++counter); });
		}

		std::vector<int> startedIds(unsigned long long now)
		{
			std::vector<int> ids;
			for (const auto& order : director.takeGamesToStart(now))
				ids.push_back(order.match.matchId);
			return ids;
		}

		TournamentDirector director;
		std::shared_ptr<RecordingEvents> events;
		WallTime start = std::chrono::system_clock::from_time_t(1795716000);
		WallTime end = std::chrono::system_clock::from_time_t(1795716312);
	};
}

BOOST_AUTO_TEST_SUITE(TournamentTest)

// - - - - - - - - - - - - - - - - - -
// 			parsing
// - - - - - - - - - - - - - - - - - -

BOOST_AUTO_TEST_CASE( parse_reads_all_fields )
{
	std::vector<std::string> warnings;
	auto matches = parse("<match id=\"7\" player1=\"12\" player2=\"17\" ruleFile=\"blitz.lua\" scoreToWin=\"5\" "
						 "speed=\"90\" bestOf=\"3\" nextGame=\"2\" awaitingDecision=\"true\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	BOOST_CHECK(warnings.empty());
	const MatchAssignment& m = matches.front();
	BOOST_CHECK_EQUAL(m.matchId, 7);
	BOOST_CHECK_EQUAL(m.player1, 12);
	BOOST_CHECK_EQUAL(m.player2, 17);
	BOOST_CHECK_EQUAL(m.ruleFile, "blitz.lua");
	BOOST_CHECK_EQUAL(m.scoreToWin, 5);
	BOOST_CHECK_EQUAL(m.speed, 90);
	BOOST_CHECK_EQUAL(m.bestOf, 3);
	BOOST_CHECK_EQUAL(m.nextGame, 2);
	BOOST_CHECK(m.awaitingDecision);
}

BOOST_AUTO_TEST_CASE( parse_uses_site_defaults )
{
	std::vector<std::string> warnings;
	auto matches = parse("<match id=\"7\" player1=\"12\" player2=\"17\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	const MatchAssignment& m = matches.front();
	BOOST_CHECK_EQUAL(m.ruleFile, "default.lua");
	BOOST_CHECK_EQUAL(m.scoreToWin, 15);
	BOOST_CHECK_EQUAL(m.speed, 75);
	BOOST_CHECK_EQUAL(m.bestOf, 1);
	BOOST_CHECK_EQUAL(m.nextGame, 1);
	BOOST_CHECK(!m.awaitingDecision);
}

BOOST_AUTO_TEST_CASE( parse_skips_bad_matches )
{
	std::vector<std::string> warnings;
	auto matches = parse(
		"<match player1=\"12\" player2=\"17\"/>\n"
		"<match id=\"0\" player1=\"12\" player2=\"17\"/>\n"
		"<match id=\"2\" player1=\"12\" player2=\"12\"/>\n"
		"<match id=\"3\" player1=\"12\" player2=\"99\"/>\n"
		"<match id=\"4\" player1=\"12\" player2=\"17\" ruleFile=\"../secret.lua\"/>\n"
		"<match id=\"5\" player1=\"12\" player2=\"17\" ruleFile=\"default.txt\"/>\n"
		"<match id=\"6\" player1=\"12\" player2=\"17\" scoreToWin=\"100\"/>\n"
		"<match id=\"8\" player1=\"12\" player2=\"17\" speed=\"29\"/>\n"
		"<match id=\"9\" player1=\"12\" player2=\"17\" speed=\"121\"/>\n"
		"<match id=\"10\" player1=\"12\" player2=\"17\" bestOf=\"2\"/>\n"
		"<match id=\"11\" player1=\"12\" player2=\"17\" nextGame=\"0\"/>\n"
		"<match id=\"13\" player1=\"12\" player2=\"17\" awaitingDecision=\"maybe\"/>\n"
		"<match id=\"14\" player1=\"x\" player2=\"17\"/>\n"
		"<match id=\"20\" player1=\"21\" player2=\"25\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	BOOST_CHECK_EQUAL(matches.front().matchId, 20);
	BOOST_CHECK_EQUAL(warnings.size(), 13u);
}

BOOST_AUTO_TEST_CASE( parse_drops_duplicate_match_ids_and_sorts )
{
	std::vector<std::string> warnings;
	auto matches = parse(
		"<match id=\"9\" player1=\"21\" player2=\"25\"/>\n"
		"<match id=\"7\" player1=\"12\" player2=\"17\"/>\n"
		"<match id=\"7\" player1=\"21\" player2=\"25\"/>\n"
		"<match id=\"3\" player1=\"12\" player2=\"21\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 2u);
	BOOST_CHECK_EQUAL(matches[0].matchId, 3);
	BOOST_CHECK_EQUAL(matches[1].matchId, 9);
	BOOST_CHECK_EQUAL(warnings.size(), 1u);
}

BOOST_AUTO_TEST_CASE( parse_rejects_files_that_are_not_players_files )
{
	std::vector<MatchAssignment> matches;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_CHECK(!parseMatchAssignments("<tournament>", KNOWN, matches, warnings, error));
	BOOST_CHECK(!error.empty());
	error.clear();
	BOOST_CHECK(!parseMatchAssignments("<players/>", KNOWN, matches, warnings, error));
	BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE( rule_file_names )
{
	BOOST_CHECK(isValidRuleFileName("default.lua"));
	BOOST_CHECK(isValidRuleFileName("one_hit-wonder2.lua"));
	BOOST_CHECK(!isValidRuleFileName(".lua"));
	BOOST_CHECK(!isValidRuleFileName("rules/default.lua"));
	BOOST_CHECK(!isValidRuleFileName("..\\default.lua"));
	BOOST_CHECK(!isValidRuleFileName("default.lua.txt"));
	BOOST_CHECK(!isValidRuleFileName("default"));
	BOOST_CHECK(!isValidRuleFileName(std::string(70, 'a') + ".lua"));
}

BOOST_AUTO_TEST_CASE( gate_loads_matches_with_players )
{
	JoinCodeGate gate("");
	BOOST_CHECK_EQUAL(gate.getGeneration(), 0u);
	BOOST_REQUIRE(gate.applyContent(playersFile("<match id=\"7\" player1=\"12\" player2=\"17\" bestOf=\"3\"/>\n")));
	BOOST_CHECK_EQUAL(gate.getGeneration(), 1u);
	BOOST_REQUIRE_EQUAL(gate.getMatches().size(), 1u);
	BOOST_CHECK_EQUAL(gate.getMatches().front().bestOf, 3);
	BOOST_CHECK_EQUAL(gate.getTable().size(), 4u);

	// a broken file keeps the last matches
	BOOST_CHECK(!gate.applyContent("<tournament>"));
	BOOST_CHECK_EQUAL(gate.getGeneration(), 1u);
	BOOST_CHECK_EQUAL(gate.getMatches().size(), 1u);
}

BOOST_AUTO_TEST_CASE( gate_skips_matches_with_players_without_id )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(
		"<tournament>\n"
		"\t<player nickname=\"Ola\" code=\"K7QM2XPR4T\"/>\n"
		"\t<player id=\"17\" nickname=\"Kari\" code=\"B9HNZ3WD6F\"/>\n"
		"\t<match id=\"7\" player1=\"0\" player2=\"17\"/>\n"
		"</tournament>\n"));
	BOOST_CHECK(gate.getMatches().empty());
	BOOST_CHECK_EQUAL(gate.getTable().size(), 2u);
}

BOOST_AUTO_TEST_CASE( gate_finds_connected_players_by_id )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(playersFile("")));

	std::string nickname;
	BOOST_REQUIRE(gate.admit(makeID(1000), "k7qm2xpr4t", nickname) == JoinCodeGate::Result::ACCEPTED);
	BOOST_CHECK_EQUAL(gate.playerIdOf(makeID(1000)), 12);
	BOOST_CHECK_EQUAL(gate.playerIdOf(makeID(1001)), 0);

	PlayerID found;
	BOOST_REQUIRE(gate.findConnection(12, found));
	BOOST_CHECK(found == makeID(1000));
	BOOST_CHECK(!gate.findConnection(17, found));
	BOOST_CHECK(!gate.findConnection(0, found));

	BOOST_REQUIRE(gate.getTable().findById(17) != nullptr);
	BOOST_CHECK_EQUAL(gate.getTable().findById(17)->nickname, "Kari");
	BOOST_CHECK(gate.getTable().findById(0) == nullptr);

	gate.release(makeID(1000));
	BOOST_CHECK(!gate.findConnection(12, found));
	BOOST_CHECK_EQUAL(gate.playerIdOf(makeID(1000)), 0);
}

BOOST_AUTO_TEST_CASE( players_file_without_matches_is_read_as_before )
{
	JoinCodeTable table;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE(table.parse(playersFile("<match id=\"7\" player1=\"12\" player2=\"17\"/>\n"), warnings, error));
	BOOST_CHECK(warnings.empty());
	BOOST_CHECK_EQUAL(table.size(), 4u);
	BOOST_REQUIRE(table.find("K7QM2XPR4T") != nullptr);
	BOOST_CHECK_EQUAL(table.find("K7QM2XPR4T")->nickname, "Ola");
}

// - - - - - - - - - - - - - - - - - -
// 			matching connected players
// - - - - - - - - - - - - - - - - - -

BOOST_AUTO_TEST_CASE( match_starts_when_both_players_are_connected )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17) });

	BOOST_CHECK(f.startedIds(0).empty());

	f.director.playerConnected(12);
	BOOST_CHECK(f.startedIds(0).empty());
	PlayerTournamentStatus waiting = f.director.statusOf(12, 0);
	BOOST_CHECK(waiting.status == TournamentStatus::WAITING_FOR_OPPONENT);
	BOOST_CHECK_EQUAL(waiting.matchId, 7);
	BOOST_CHECK_EQUAL(waiting.opponentId, 17);
	BOOST_CHECK_EQUAL(waiting.game, 1);

	f.director.playerConnected(17);
	auto orders = f.director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].match.player1, 12);
	BOOST_CHECK_EQUAL(orders[0].match.player2, 17);
	BOOST_CHECK_EQUAL(orders[0].game, 1);
	BOOST_CHECK(f.director.statusOf(17, 0).status == TournamentStatus::PLAYING);

	// a running match is not started twice
	BOOST_CHECK(f.startedIds(0).empty());

	BOOST_REQUIRE_EQUAL(f.events->connected.size(), 1u);
	BOOST_CHECK_EQUAL(f.events->connected[0].matchId, 7);
	BOOST_CHECK_EQUAL(f.events->connected[0].player1, 12);
	BOOST_CHECK_EQUAL(f.events->connected[0].player2, 17);
}

BOOST_AUTO_TEST_CASE( players_without_a_match_wait )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17) });
	f.director.playerConnected(21);
	f.director.playerConnected(25);
	BOOST_CHECK(f.startedIds(0).empty());
	BOOST_CHECK(f.director.statusOf(21, 0).status == TournamentStatus::NO_MATCH);
	BOOST_CHECK(f.director.statusOf(0, 0).status == TournamentStatus::NO_MATCH);
}

BOOST_AUTO_TEST_CASE( several_matches_run_at_once_without_limit )
{
	DirectorFixture f(0);
	f.director.setAssignments({ makeMatch(7, 12, 17), makeMatch(8, 21, 25) });
	for (int id : {12, 17, 21, 25})
		f.director.playerConnected(id);
	BOOST_CHECK_EQUAL(f.startedIds(0).size(), 2u);
}

BOOST_AUTO_TEST_CASE( one_match_at_a_time )
{
	DirectorFixture f(1, 0);
	f.director.setAssignments({ makeMatch(8, 21, 25), makeMatch(7, 12, 17) });
	for (int id : {12, 17, 21, 25})
		f.director.playerConnected(id);

	// the lower match id goes first
	std::vector<int> first = f.startedIds(0);
	BOOST_REQUIRE_EQUAL(first.size(), 1u);
	BOOST_CHECK_EQUAL(first[0], 7);
	BOOST_CHECK(f.director.statusOf(21, 0).status == TournamentStatus::WAITING_FOR_SLOT);
	BOOST_CHECK(f.startedIds(0).empty());

	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	std::vector<int> second = f.startedIds(0);
	BOOST_REQUIRE_EQUAL(second.size(), 1u);
	BOOST_CHECK_EQUAL(second[0], 8);
}

BOOST_AUTO_TEST_CASE( player_plays_one_match_at_a_time )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17), makeMatch(9, 12, 21) });
	for (int id : {12, 17, 21})
		f.director.playerConnected(id);

	std::vector<int> started = f.startedIds(0);
	BOOST_REQUIRE_EQUAL(started.size(), 1u);
	BOOST_CHECK_EQUAL(started[0], 7);
	BOOST_CHECK(f.director.statusOf(21, 0).status == TournamentStatus::WAITING_FOR_SLOT);
}

BOOST_AUTO_TEST_CASE( awaiting_decision_holds_the_match )
{
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	m.awaitingDecision = true;
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_CHECK(f.startedIds(0).empty());
	BOOST_CHECK(f.director.statusOf(12, 0).status == TournamentStatus::WAITING_FOR_ORGANIZER);

	// the organizer restarted the game, which moves the next game on
	m.awaitingDecision = false;
	m.nextGame = 3;
	f.director.setAssignments({ m });
	auto orders = f.director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 3);
}

BOOST_AUTO_TEST_CASE( game_that_cannot_be_created_waits_for_a_new_assignment )
{
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17);
	m.ruleFile = "missing.lua";
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameNotStarted(7);
	BOOST_CHECK(f.startedIds(0).empty());
	BOOST_CHECK(f.director.statusOf(12, 0).status == TournamentStatus::WAITING_FOR_ORGANIZER);

	// the same assignment again changes nothing
	f.director.setAssignments({ m });
	BOOST_CHECK(f.startedIds(0).empty());

	m.ruleFile = "default.lua";
	f.director.setAssignments({ m });
	BOOST_CHECK_EQUAL(f.startedIds(0).size(), 1u);
}

BOOST_AUTO_TEST_CASE( game_can_retry_when_a_player_was_missing )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameNotStarted(7, true);
	BOOST_CHECK_EQUAL(f.startedIds(0).size(), 1u);
}

BOOST_AUTO_TEST_CASE( reconnected_players_are_matched_again )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17) });
	f.director.playerConnected(12);
	f.director.playerDisconnected(12);
	f.director.playerConnected(17);
	BOOST_CHECK(f.startedIds(0).empty());
	f.director.setConnectedPlayers({12, 17});
	BOOST_CHECK_EQUAL(f.startedIds(0).size(), 1u);
}

// - - - - - - - - - - - - - - - - - -
// 			series progress
// - - - - - - - - - - - - - - - - - -

BOOST_AUTO_TEST_CASE( best_of_three_series )
{
	DirectorFixture f(0, 5000);
	f.director.setAssignments({ makeMatch(7, 12, 17, 3) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);

	// game 1: player 2 wins
	BOOST_REQUIRE_EQUAL(f.startedIds(1000).size(), 1u);
	f.director.gameFinished(7, 2, 11, 15, f.start, f.end, 1000);
	BOOST_REQUIRE_EQUAL(f.events->games.size(), 1u);
	const GameReport& g1 = f.events->games[0];
	BOOST_CHECK_EQUAL(g1.reportId, "report-1");
	BOOST_CHECK_EQUAL(g1.matchId, 7);
	BOOST_CHECK_EQUAL(g1.game, 1);
	BOOST_CHECK(g1.finished);
	BOOST_CHECK_EQUAL(g1.winnerId, 17);
	BOOST_CHECK_EQUAL(g1.points1, 11);
	BOOST_CHECK_EQUAL(g1.points2, 15);
	BOOST_CHECK(f.events->series.empty());

	// the next game waits for the pause
	PlayerTournamentStatus between = f.director.statusOf(12, 2000);
	BOOST_CHECK(between.status == TournamentStatus::NEXT_GAME);
	BOOST_CHECK_EQUAL(between.game, 2);
	BOOST_CHECK_EQUAL(between.bestOf, 3);
	BOOST_CHECK_EQUAL(between.ownWins, 0);
	BOOST_CHECK_EQUAL(between.opponentWins, 1);
	BOOST_CHECK(f.startedIds(5999).empty());

	// game 2: player 1 wins
	auto orders = f.director.takeGamesToStart(6000);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 2);
	f.director.gameFinished(7, 1, 15, 9, f.start, f.end, 7000);
	BOOST_CHECK(f.events->series.empty());

	// game 3 decides
	orders = f.director.takeGamesToStart(20000);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 3);
	f.director.gameFinished(7, 1, 15, 14, f.start, f.end, 21000);

	BOOST_REQUIRE_EQUAL(f.events->games.size(), 3u);
	BOOST_CHECK_EQUAL(f.events->games[2].game, 3);
	BOOST_CHECK_EQUAL(f.events->games[2].winnerId, 12);
	BOOST_REQUIRE_EQUAL(f.events->series.size(), 1u);
	const SeriesResult& s = f.events->series[0];
	BOOST_CHECK_EQUAL(s.matchId, 7);
	BOOST_CHECK_EQUAL(s.winnerId, 12);
	BOOST_CHECK_EQUAL(s.wins1, 2);
	BOOST_CHECK_EQUAL(s.wins2, 1);
	BOOST_CHECK_EQUAL(s.bestOf, 3);

	// a decided match does not start again
	BOOST_CHECK(f.startedIds(100000).empty());
	PlayerTournamentStatus over = f.director.statusOf(17, 100000);
	BOOST_CHECK(over.status == TournamentStatus::MATCH_OVER);
	BOOST_CHECK_EQUAL(over.ownWins, 1);
	BOOST_CHECK_EQUAL(over.opponentWins, 2);

	// the connected event came once for the whole series
	BOOST_CHECK_EQUAL(f.events->connected.size(), 1u);
}

BOOST_AUTO_TEST_CASE( best_of_three_ends_early_after_two_wins )
{
	DirectorFixture f(0, 0);
	f.director.setAssignments({ makeMatch(7, 12, 17, 3) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 2, f.start, f.end, 0);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 4, f.start, f.end, 0);
	BOOST_CHECK_EQUAL(f.events->series.size(), 1u);
	BOOST_CHECK(f.startedIds(0).empty());
	BOOST_REQUIRE(f.director.getSeries(7) != nullptr);
	BOOST_CHECK(f.director.getSeries(7)->decided);
}

BOOST_AUTO_TEST_CASE( best_of_one_is_decided_by_one_game )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 2, 3, 15, f.start, f.end, 0);
	BOOST_REQUIRE_EQUAL(f.events->series.size(), 1u);
	BOOST_CHECK_EQUAL(f.events->series[0].winnerId, 17);
}

BOOST_AUTO_TEST_CASE( abandoned_game_holds_the_match_until_the_organizer_decides )
{
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);

	f.director.playerDisconnected(17);
	f.director.gameAbandoned(7, 4, 6, f.start, f.end, 17);
	BOOST_REQUIRE_EQUAL(f.events->games.size(), 1u);
	const GameReport& r = f.events->games[0];
	BOOST_CHECK(!r.finished);
	BOOST_CHECK_EQUAL(r.winnerId, 0);
	BOOST_CHECK_EQUAL(r.leftPlayerId, 17);
	BOOST_CHECK_EQUAL(r.game, 1);
	BOOST_CHECK_EQUAL(r.points1, 4);
	BOOST_CHECK_EQUAL(r.points2, 6);

	// both players back, but the match waits for the organizer
	f.director.playerConnected(17);
	BOOST_CHECK(f.startedIds(0).empty());
	BOOST_CHECK(f.director.statusOf(12, 0).status == TournamentStatus::WAITING_FOR_ORGANIZER);

	// the same file again changes nothing
	f.director.setAssignments({ m });
	BOOST_CHECK(f.startedIds(0).empty());

	// the site marks the match as awaiting a decision, which still holds it
	m.awaitingDecision = true;
	f.director.setAssignments({ m });
	BOOST_CHECK(f.startedIds(0).empty());

	// the organizer restarts the game
	m.awaitingDecision = false;
	f.director.setAssignments({ m });
	auto orders = f.director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 2);
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->wins1 + f.director.getSeries(7)->wins2, 0);
}

BOOST_AUTO_TEST_CASE( abandoned_game_is_released_by_a_new_next_game )
{
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameAbandoned(7, 0, 0, f.start, f.end);
	BOOST_CHECK(f.startedIds(0).empty());

	m.nextGame = 2;
	f.director.setAssignments({ m });
	auto orders = f.director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 2);
}

BOOST_AUTO_TEST_CASE( restart_takes_the_series_score_from_the_file )
{
	// a fresh director, as after a server restart, in the middle of a best of 3
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	m.nextGame = 2;
	m.wins1 = 1;
	m.wins2 = 0;
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);

	PlayerTournamentStatus waiting = f.director.statusOf(17, 0);
	BOOST_CHECK_EQUAL(waiting.ownWins, 0);
	BOOST_CHECK_EQUAL(waiting.opponentWins, 1);

	auto orders = f.director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 2);

	// one more win for player 1 decides the series
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	BOOST_REQUIRE_EQUAL(f.events->series.size(), 1u);
	BOOST_CHECK_EQUAL(f.events->series[0].wins1, 2);
	BOOST_CHECK_EQUAL(f.events->series[0].wins2, 0);
	BOOST_CHECK(f.startedIds(100000).empty());
}

BOOST_AUTO_TEST_CASE( local_win_counts_until_the_file_brings_new_wins )
{
	DirectorFixture f(0, 0);
	MatchAssignment m = makeMatch(7, 12, 17, 5);
	m.wins1 = 0;
	m.wins2 = 0;
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->wins1, 1);

	// the old numbers from the file do not wipe the local win
	f.director.setAssignments({ m });
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->wins1, 1);

	// the site brings its count
	m.nextGame = 2;
	m.wins1 = 1;
	f.director.setAssignments({ m });
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->wins1, 1);
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->wins2, 0);
}

BOOST_AUTO_TEST_CASE( lower_next_game_from_the_file_is_taken_after_a_refused_report )
{
	DirectorFixture f(0, 0);
	MatchAssignment m = makeMatch(7, 12, 17, 5);
	m.wins1 = 0;
	m.wins2 = 0;
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->nextGame, 2);

	// the file counts the game while its report waits
	m.nextGame = 2;
	m.wins1 = 1;
	f.director.setAssignments({ m });
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->nextGame, 2);

	// the site refused the report, so the file goes back and the server follows
	m.nextGame = 1;
	m.wins1 = 0;
	f.director.setAssignments({ m });
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->nextGame, 1);
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->wins1, 0);
	auto orders = f.director.takeGamesToStart(1);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 1);

	// a lower number does not change a running game
	m.nextGame = 5;
	f.director.setAssignments({ m });
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->nextGame, 1);
}

BOOST_AUTO_TEST_CASE( lower_next_game_does_not_release_an_abandoned_game )
{
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	m.nextGame = 2;
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameAbandoned(7, 0, 0, f.start, f.end);

	m.nextGame = 1;
	f.director.setAssignments({ m });
	BOOST_CHECK(f.startedIds(0).empty());
	BOOST_CHECK(f.director.statusOf(12, 0).status == TournamentStatus::WAITING_FOR_ORGANIZER);
}

BOOST_AUTO_TEST_CASE( corrected_score_lowers_the_local_count )
{
	DirectorFixture f(0, 0);
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);

	// player 1 wins two games and the series locally
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 4, f.start, f.end, 0);
	BOOST_CHECK(f.director.getSeries(7)->decided);
	BOOST_CHECK(f.startedIds(0).empty());

	// the organizer corrected one of those games to player 2
	m.nextGame = 3;
	m.wins1 = 1;
	m.wins2 = 1;
	f.director.setAssignments({ m });
	const TournamentDirector::Series* s = f.director.getSeries(7);
	BOOST_CHECK_EQUAL(s->wins1, 1);
	BOOST_CHECK_EQUAL(s->wins2, 1);
	BOOST_CHECK(!s->decided);

	auto orders = f.director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 3);
}

BOOST_AUTO_TEST_CASE( parse_reads_wins_and_refuses_decided_matches )
{
	std::vector<std::string> warnings;
	auto matches = parse(
		"<match id=\"1\" player1=\"12\" player2=\"17\" bestOf=\"3\" wins1=\"1\" wins2=\"1\"/>\n"
		"<match id=\"2\" player1=\"21\" player2=\"25\" bestOf=\"3\" wins1=\"2\"/>\n"
		"<match id=\"3\" player1=\"21\" player2=\"25\" wins2=\"-1\"/>\n"
		"<match id=\"4\" player1=\"21\" player2=\"25\" bestOf=\"99\" wins1=\"100\"/>\n"
		"<match id=\"5\" player1=\"21\" player2=\"25\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 2u);
	BOOST_CHECK_EQUAL(matches[0].wins1, 1);
	BOOST_CHECK_EQUAL(matches[0].wins2, 1);
	BOOST_CHECK_EQUAL(matches[1].matchId, 5);
	BOOST_CHECK_EQUAL(matches[1].wins1, -1);
	BOOST_CHECK_EQUAL(matches[1].wins2, -1);
	BOOST_CHECK_EQUAL(warnings.size(), 3u);
}

BOOST_AUTO_TEST_CASE( removed_match_finishes_its_running_game )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17, 3) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);

	f.director.setAssignments({});
	BOOST_REQUIRE(f.director.getSeries(7) != nullptr);
	f.director.gameFinished(7, 1, 15, 0, f.start, f.end, 0);
	BOOST_CHECK_EQUAL(f.events->games.size(), 1u);
	// the series is kept, so the match goes on where it was if it comes back
	BOOST_REQUIRE(f.director.getSeries(7) != nullptr);
	BOOST_CHECK(!f.director.getSeries(7)->listed);
	BOOST_CHECK_EQUAL(f.director.getSeries(7)->wins1, 1);
	BOOST_CHECK(f.startedIds(100000).empty());
	BOOST_CHECK(f.director.statusOf(12, 100000).status == TournamentStatus::NO_MATCH);
}

BOOST_AUTO_TEST_CASE( changed_players_start_a_new_series )
{
	DirectorFixture f(0, 0);
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	f.director.setAssignments({ m });
	for (int id : {12, 17, 21})
		f.director.playerConnected(id);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);

	// while game 2 runs, the file swaps the players
	MatchAssignment swapped = makeMatch(7, 17, 12, 3);
	f.director.setAssignments({ swapped });
	const TournamentDirector::Series* s = f.director.getSeries(7);
	BOOST_CHECK_EQUAL(s->wins1, 0);
	BOOST_CHECK_EQUAL(s->wins2, 0);
	BOOST_CHECK_EQUAL(s->nextGame, 1);

	// the running game is reported with the players it started with and does not count
	f.director.gameFinished(7, 1, 15, 9, f.start, f.end, 0);
	BOOST_REQUIRE_EQUAL(f.events->games.size(), 2u);
	BOOST_CHECK_EQUAL(f.events->games[1].game, 2);
	BOOST_CHECK_EQUAL(f.events->games[1].player1, 12);
	BOOST_CHECK_EQUAL(f.events->games[1].winnerId, 12);
	BOOST_CHECK_EQUAL(s->wins1 + s->wins2, 0);
	BOOST_CHECK(f.events->series.empty());

	// the new series starts at its own first game, with player 17 on the left
	auto orders = f.director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].game, 1);
	BOOST_CHECK_EQUAL(orders[0].match.player1, 17);

	// another opponent is a new series too
	f.director.gameAbandoned(7, 0, 0, f.start, f.end);
	f.director.setAssignments({ makeMatch(7, 17, 21, 3) });
	BOOST_CHECK(!f.director.getSeries(7)->held);
	BOOST_CHECK_EQUAL(f.startedIds(0).size(), 1u);
}

BOOST_AUTO_TEST_CASE( decided_match_that_comes_back_is_not_played_again )
{
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17);
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	BOOST_REQUIRE_EQUAL(f.events->series.size(), 1u);

	// the match leaves the file and comes back, as from an old sync
	f.director.setAssignments({});
	f.director.setAssignments({ m });
	BOOST_CHECK(f.startedIds(100000).empty());
	BOOST_CHECK_EQUAL(f.events->games.size(), 1u);
	BOOST_CHECK(f.director.statusOf(12, 100000).status == TournamentStatus::MATCH_OVER);
}

BOOST_AUTO_TEST_CASE( returning_match_takes_the_wins_of_the_file )
{
	DirectorFixture f;
	MatchAssignment m = makeMatch(7, 12, 17, 3);
	f.director.setAssignments({ m });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 0);
	f.director.setAssignments({});

	// the site reset the score while the match was away
	m.wins1 = 0;
	m.wins2 = 0;
	m.nextGame = 3;
	f.director.setAssignments({ m });
	const TournamentDirector::Series* s = f.director.getSeries(7);
	BOOST_CHECK_EQUAL(s->wins1, 0);
	BOOST_CHECK_EQUAL(s->nextGame, 3);
}

BOOST_AUTO_TEST_CASE( missing_player_is_tried_again_later )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	f.director.gameNotStarted(7, true, 1000);
	BOOST_CHECK(f.startedIds(500).empty());
	BOOST_CHECK(f.startedIds(999).empty());
	BOOST_CHECK_EQUAL(f.startedIds(1000).size(), 1u);
}

BOOST_AUTO_TEST_CASE( duplicate_player_ids_are_dropped )
{
	JoinCodeTable table;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE(table.parse(
		"<tournament>\n"
		"\t<player id=\"12\" nickname=\"Ola\" code=\"K7QM2XPR4T\"/>\n"
		"\t<player id=\"12\" nickname=\"Olav\" code=\"B9HNZ3WD6F\"/>\n"
		"\t<player id=\"21\" nickname=\"Per\" code=\"C2DEFGHJKM\"/>\n"
		"\t<player nickname=\"Anon\" code=\"N3PQRSTUVW\"/>\n"
		"</tournament>\n", warnings, error));
	BOOST_CHECK_EQUAL(table.size(), 2u);
	BOOST_CHECK(table.find("K7QM2XPR4T") == nullptr);
	BOOST_CHECK(table.find("B9HNZ3WD6F") == nullptr);
	BOOST_CHECK(table.findById(12) == nullptr);
	BOOST_CHECK(table.find("N3PQRSTUVW") != nullptr);
	BOOST_CHECK_EQUAL(warnings.size(), 1u);
}

BOOST_AUTO_TEST_CASE( score_to_win_from_a_server_is_checked )
{
	BOOST_CHECK(isValidScoreToWin(1));
	BOOST_CHECK(isValidScoreToWin(99));
	BOOST_CHECK(!isValidScoreToWin(0));
	BOOST_CHECK(!isValidScoreToWin(100));
	BOOST_CHECK(!isValidScoreToWin(-5));
}

BOOST_AUTO_TEST_CASE( enter_server_announces_tournament_support )
{
	PlayerIdentity player("K7QM2XPR4T", Color(255, 0, 0), false, RIGHT_PLAYER);
	RakNet::BitStream stream;
	makeEnterServerPacket(stream, player);

	// the fields an old server reads come first and are unchanged
	RakNet::BitStream in(stream.GetData(), stream.GetNumberOfBytesUsed(), false);
	in.IgnoreBytes(1);
	NetworkPlayer read(makeID(1000), in);
	BOOST_CHECK_EQUAL(read.getName(), "K7QM2XPR4T");
	BOOST_CHECK(read.getDesiredSide() == RIGHT_PLAYER);
	BOOST_CHECK_EQUAL(readClientFeatures(in) & CLIENT_FEATURE_TOURNAMENT, (uint32_t)CLIENT_FEATURE_TOURNAMENT);

	// an old client sends nothing after the colour
	RakNet::BitStream old;
	old.Write((int)RIGHT_PLAYER);
	char name[PLAYER_NAME_BUFFER] = "K7QM2XPR4T";
	old.Write(name, sizeof(name));
	old.Write((int)0xFF0000);
	RakNet::BitStream oldIn(old.GetData(), old.GetNumberOfBytesUsed(), false);
	NetworkPlayer oldPlayer(makeID(1001), oldIn);
	BOOST_CHECK_EQUAL(readClientFeatures(oldIn), 0u);

	// something else after the colour is not a feature announcement
	RakNet::BitStream other;
	other.Write((int)RIGHT_PLAYER);
	other.Write(name, sizeof(name));
	other.Write((int)0xFF0000);
	other.Write((uint32_t)12345);
	other.Write((uint32_t)CLIENT_FEATURE_TOURNAMENT);
	RakNet::BitStream otherIn(other.GetData(), other.GetNumberOfBytesUsed(), false);
	NetworkPlayer otherPlayer(makeID(1002), otherIn);
	BOOST_CHECK_EQUAL(readClientFeatures(otherIn), 0u);
}

BOOST_AUTO_TEST_CASE( tournament_status_carries_the_own_nickname )
{
	PlayerTournamentStatus status;
	status.status = TournamentStatus::NEXT_GAME;
	status.game = 2;
	status.bestOf = 3;
	status.ownWins = 1;
	status.opponentWins = 0;

	auto read = [](RakNet::BitStream& stream) {
		RakNet::BitStream in(stream.GetData(), stream.GetNumberOfBytesUsed(), false);
		unsigned char id = 0;
		unsigned char type = 0;
		in.Read(id);
		in.Read(type);
		BOOST_CHECK_EQUAL(id, (unsigned char)ID_LOBBY);
		BOOST_CHECK_EQUAL(type, (unsigned char)LobbyPacketType::TOURNAMENT_STATUS);
		auto reader = createGenericReader(&in);
		return readTournamentStatus(*reader);
	};

	RakNet::BitStream stream;
	writeTournamentStatus(stream, status, "Ola", "Kari", 4200);
	TournamentStatusData data = read(stream);
	BOOST_CHECK_EQUAL(data.nextGameInMs, 4200u);
	BOOST_CHECK(data.status == TournamentStatus::NEXT_GAME);
	BOOST_CHECK_EQUAL(data.opponent, "Ola");
	BOOST_CHECK_EQUAL(data.ownName, "Kari");
	BOOST_CHECK_EQUAL(data.game, 2u);
	BOOST_CHECK_EQUAL(data.bestOf, 3u);
	BOOST_CHECK_EQUAL(data.ownWins, 1u);

	// UTF-8 nicknames up to 15 bytes are kept
	RakNet::BitStream utf;
	writeTournamentStatus(utf, status, "Ola", "Bj\xC3\xB8rn", 0);
	BOOST_CHECK_EQUAL(read(utf).ownName, "Bj\xC3\xB8rn");

	// names that are too long, broken UTF-8 or hold control characters are dropped
	for (const std::string& bad : { std::string(16, 'a'), std::string("Bj\xC3"), std::string("a\nb") })
	{
		RakNet::BitStream s;
		writeTournamentStatus(s, status, bad, bad, 0);
		TournamentStatusData d = read(s);
		BOOST_CHECK(d.ownName.empty());
		BOOST_CHECK(d.opponent.empty());
		BOOST_CHECK_EQUAL(d.bestOf, 3u);
	}

	// a packet without the field leaves the name empty
	RakNet::BitStream older;
	older.Write((unsigned char)ID_LOBBY);
	older.Write((unsigned char)LobbyPacketType::TOURNAMENT_STATUS);
	{
		auto out = createGenericWriter(&older);
		out->byte((unsigned char)TournamentStatus::NO_MATCH);
		out->string("");
		out->uint32(0);
		out->uint32(0);
		out->uint32(0);
		out->uint32(0);
	}
	BOOST_CHECK(read(older).ownName.empty());
	BOOST_CHECK_EQUAL(read(older).nextGameInMs, 0u);

	// a server cannot make a client wait for ever
	RakNet::BitStream slow;
	writeTournamentStatus(slow, status, "Ola", "Kari", 3600000);
	BOOST_CHECK_EQUAL(read(slow).nextGameInMs, MAX_NEXT_GAME_DELAY_MS);
	BOOST_CHECK_EQUAL(MAX_NEXT_GAME_DELAY_MS, 120000u);

	// the longest delay a server can use still counts down in full
	RakNet::BitStream longest;
	writeTournamentStatus(longest, status, "Ola", "Kari", MAX_NEXT_GAME_DELAY * 1000);
	BOOST_CHECK_EQUAL(read(longest).nextGameInMs, (unsigned)MAX_NEXT_GAME_DELAY * 1000);
}

BOOST_AUTO_TEST_CASE( next_game_status_tells_when_the_next_game_starts )
{
	DirectorFixture f(0, 5000);
	f.director.setAssignments({ makeMatch(7, 12, 17, 3) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(1000).size(), 1u);
	BOOST_CHECK_EQUAL(f.director.statusOf(12, 1000).nextGameAt, 0u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 1000);
	PlayerTournamentStatus s = f.director.statusOf(17, 2000);
	BOOST_CHECK(s.status == TournamentStatus::NEXT_GAME);
	BOOST_CHECK_EQUAL(s.nextGameAt, 6000u);
	// the status stays the same while the clock runs, so the server does not send it again
	BOOST_CHECK(s == f.director.statusOf(17, 3000));
}

BOOST_AUTO_TEST_CASE( abandoned_report_names_the_player_who_left )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17, 3) });
	f.director.playerConnected(12);
	f.director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 1u);
	// an id that is not a player of the game is not reported
	f.director.gameAbandoned(7, 1, 0, f.start, f.end, 99);
	BOOST_REQUIRE_EQUAL(f.events->games.size(), 1u);
	BOOST_CHECK_EQUAL(f.events->games[0].leftPlayerId, 0);
	BOOST_CHECK(gameReportJson(f.events->games[0]).find("leftPlayerId") == std::string::npos);
}

BOOST_AUTO_TEST_CASE( parse_reads_the_next_game_delay )
{
	std::vector<std::string> warnings;
	auto matches = parse(
		"<match id=\"1\" player1=\"12\" player2=\"17\" nextGameDelay=\"3\"/>\n"
		"<match id=\"2\" player1=\"21\" player2=\"25\" nextGameDelay=\"120\"/>\n"
		"<match id=\"3\" player1=\"12\" player2=\"21\"/>\n"
		"<match id=\"4\" player1=\"12\" player2=\"25\" nextGameDelay=\"2\"/>\n"
		"<match id=\"5\" player1=\"17\" player2=\"21\" nextGameDelay=\"121\"/>\n"
		"<match id=\"6\" player1=\"17\" player2=\"25\" nextGameDelay=\"soon\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 3u);
	BOOST_CHECK_EQUAL(matches[0].nextGameDelay, 3);
	BOOST_CHECK_EQUAL(matches[1].nextGameDelay, 120);
	BOOST_CHECK_EQUAL(matches[2].nextGameDelay, -1);
	BOOST_CHECK_EQUAL(warnings.size(), 3u);
}

BOOST_AUTO_TEST_CASE( next_game_delay_is_clamped )
{
	BOOST_CHECK_EQUAL(clampNextGameDelay(10), 10);
	BOOST_CHECK_EQUAL(clampNextGameDelay(3), 3);
	BOOST_CHECK_EQUAL(clampNextGameDelay(120), 120);
	BOOST_CHECK_EQUAL(clampNextGameDelay(0), 3);
	BOOST_CHECK_EQUAL(clampNextGameDelay(-5), 3);
	BOOST_CHECK_EQUAL(clampNextGameDelay(500), 120);
	BOOST_CHECK_EQUAL(DEFAULT_NEXT_GAME_DELAY, 10);
}

BOOST_AUTO_TEST_CASE( match_delay_overrides_the_server_delay )
{
	DirectorFixture f(0, 10000);
	MatchAssignment own = makeMatch(7, 12, 17, 3);
	own.nextGameDelay = 4;
	f.director.setAssignments({ own, makeMatch(8, 21, 25, 3) });
	for (int id : {12, 17, 21, 25})
		f.director.playerConnected(id);
	BOOST_REQUIRE_EQUAL(f.startedIds(0).size(), 2u);
	f.director.gameFinished(7, 1, 15, 3, f.start, f.end, 1000);
	f.director.gameFinished(8, 1, 15, 3, f.start, f.end, 1000);

	BOOST_CHECK_EQUAL(f.director.statusOf(12, 1000).nextGameAt, 5000u);
	BOOST_CHECK_EQUAL(f.director.statusOf(21, 1000).nextGameAt, 11000u);

	// match 7 starts after its own 4 seconds, match 8 waits for the server's 10
	std::vector<int> early = f.startedIds(5000);
	BOOST_REQUIRE_EQUAL(early.size(), 1u);
	BOOST_CHECK_EQUAL(early[0], 7);
	BOOST_CHECK(f.startedIds(10999).empty());
	BOOST_CHECK_EQUAL(f.startedIds(11000).size(), 1u);
}

BOOST_AUTO_TEST_CASE( reports_for_unknown_or_idle_matches_are_ignored )
{
	DirectorFixture f;
	f.director.setAssignments({ makeMatch(7, 12, 17) });
	f.director.gameFinished(7, 1, 15, 0, f.start, f.end, 0);
	f.director.gameAbandoned(8, 1, 0, f.start, f.end);
	BOOST_CHECK(f.events->games.empty());
}

// - - - - - - - - - - - - - - - - - -
// 			reports
// - - - - - - - - - - - - - - - - - -

BOOST_AUTO_TEST_CASE( utc_time_format )
{
	BOOST_CHECK_EQUAL(formatUtcTime(std::chrono::system_clock::from_time_t(0)), "1970-01-01T00:00:00Z");
	BOOST_CHECK_EQUAL(formatUtcTime(std::chrono::system_clock::from_time_t(1795716245)), "2026-11-26T18:04:05Z");
}

BOOST_AUTO_TEST_CASE( report_ids_are_uuids )
{
	std::string a = makeReportId();
	std::string b = makeReportId();
	BOOST_CHECK_NE(a, b);
	BOOST_REQUIRE_EQUAL(a.size(), 36u);
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (i == 8 || i == 13 || i == 18 || i == 23)
			BOOST_CHECK_EQUAL(a[i], '-');
		else
			BOOST_CHECK(std::isxdigit(static_cast<unsigned char>(a[i])) && !std::isupper(static_cast<unsigned char>(a[i])));
	}
	BOOST_CHECK_EQUAL(a[14], '4');
	BOOST_CHECK(a[19] == '8' || a[19] == '9' || a[19] == 'a' || a[19] == 'b');
}

BOOST_AUTO_TEST_CASE( game_report_json_matches_the_site )
{
	GameReport r;
	r.reportId = "0b6f2f4e-8a51-4d4c-9a0e-5d2b8c3f1a77";
	r.matchId = 7;
	r.game = 1;
	r.finished = true;
	r.winnerId = 17;
	r.player1 = 12;
	r.points1 = 11;
	r.player2 = 17;
	r.points2 = 15;
	r.startedAt = std::chrono::system_clock::from_time_t(1795716000);
	r.endedAt = std::chrono::system_clock::from_time_t(1795716312);
	BOOST_CHECK_EQUAL(gameReportJson(r),
		"{\"reportId\":\"0b6f2f4e-8a51-4d4c-9a0e-5d2b8c3f1a77\",\"matchId\":7,\"game\":1,\"status\":\"finished\","
		"\"winnerId\":17,\"points\":[{\"playerId\":12,\"points\":11},{\"playerId\":17,\"points\":15}],"
		"\"startedAt\":\"2026-11-26T18:00:00Z\",\"endedAt\":\"2026-11-26T18:05:12Z\"}");

	r.finished = false;
	r.winnerId = 0;
	BOOST_CHECK(gameReportJson(r).find("\"status\":\"abandoned\",\"winnerId\":null,\"points\"") != std::string::npos);

	// the player who left comes after the missing winner
	r.leftPlayerId = 12;
	BOOST_CHECK(gameReportJson(r).find("\"status\":\"abandoned\",\"winnerId\":null,\"leftPlayerId\":12,\"points\"") != std::string::npos);

	// a finished game never names a player who left
	r.finished = true;
	r.winnerId = 17;
	BOOST_CHECK(gameReportJson(r).find("leftPlayerId") == std::string::npos);
}

BOOST_AUTO_TEST_CASE( tournament_log_appends_json_lines )
{
	const std::string path = "tournament_results_test.jsonl";
	std::remove(path.c_str());
	std::vector<std::string> logged;
	{
		TournamentLog log(path, [&](bool, const std::string& line){ logged.push_back(line); });
		log.matchConnected(7, 12, 17);
		GameReport r;
		r.reportId = "id";
		r.matchId = 7;
		r.game = 1;
		r.finished = true;
		r.winnerId = 12;
		r.player1 = 12;
		r.points1 = 15;
		r.player2 = 17;
		log.gameEnded(r);
		GameReport abandoned = r;
		abandoned.reportId = "id2";
		abandoned.game = 2;
		abandoned.finished = false;
		abandoned.winnerId = 0;
		abandoned.leftPlayerId = 17;
		log.gameEnded(abandoned);
		SeriesResult s;
		s.matchId = 7;
		s.winnerId = 12;
		s.player1 = 12;
		s.wins1 = 1;
		s.player2 = 17;
		log.seriesEnded(s);
	}

	std::ifstream file(path);
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(file, line))
		lines.push_back(line);
	file.close();
	std::remove(path.c_str());

	BOOST_REQUIRE_EQUAL(lines.size(), 4u);
	BOOST_CHECK_EQUAL(lines[0].find("{\"event\":\"connected\",\"matchId\":7,\"playerIds\":[12,17]"), 0u);
	BOOST_CHECK_EQUAL(lines[1].find("{\"event\":\"game\",\"report\":{\"reportId\":\"id\",\"matchId\":7"), 0u);
	BOOST_CHECK(lines[2].find("\"status\":\"abandoned\",\"winnerId\":null,\"leftPlayerId\":17,") != std::string::npos);
	BOOST_CHECK_EQUAL(lines[3].find("{\"event\":\"series\",\"matchId\":7,\"bestOf\":1,\"winnerId\":12"), 0u);
	BOOST_REQUIRE_EQUAL(logged.size(), 4u);
	BOOST_CHECK(logged[2].find("player 17 left") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
