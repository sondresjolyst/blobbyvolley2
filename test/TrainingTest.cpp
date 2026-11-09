#include <boost/test/unit_test.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "server/MatchAssignments.h"
#include "server/SiteConfig.h"
#include "server/SiteSync.h"
#include "server/Tournament.h"
#include "TestServer.h"

// Training matches of one player against a bot that the server runs.
namespace
{
	std::string playersFile(const std::string& matches)
	{
		return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<tournament>\n")
			+ "\t<player id=\"12\" nickname=\"Ola\" code=\"K7QM2XPR4T\"/>\n"
			+ "\t<player id=\"17\" nickname=\"Kari\" code=\"B9HNZ3WD6F\"/>\n"
			+ "\t<player id=\"21\" nickname=\"Per\" code=\"C2DEFGHJKM\"/>\n"
			+ matches + "</tournament>\n";
	}

	const std::set<int> KNOWN{12, 17, 21};

	std::vector<MatchAssignment> parse(const std::string& matches, std::vector<std::string>& warnings)
	{
		std::vector<MatchAssignment> result;
		std::string error;
		BOOST_REQUIRE(parseMatchAssignments(playersFile(matches), KNOWN, result, warnings, error));
		return result;
	}

	MatchAssignment training(int id, int player, const std::string& bot = "reduced", int difficulty = 13)
	{
		MatchAssignment m;
		m.matchId = id;
		m.player1 = player;
		m.bot = bot;
		m.botDifficulty = difficulty;
		return m;
	}

	MatchAssignment match(int id, int player1, int player2)
	{
		MatchAssignment m;
		m.matchId = id;
		m.player1 = player1;
		m.player2 = player2;
		return m;
	}

	const WallTime START = WallTime() + std::chrono::hours(500000);

	EnvironmentFunction env(const std::map<std::string, std::string>& values)
	{
		auto copy = std::make_shared<std::map<std::string, std::string>>(values);
		return [copy](const char* name) -> const char* {
			auto found = copy->find(name);
			return found == copy->end() ? nullptr : found->second.c_str();
		};
	}
}

BOOST_AUTO_TEST_SUITE( TrainingAssignmentTest )

BOOST_AUTO_TEST_CASE( training_match_has_a_bot_instead_of_player2 )
{
	std::vector<std::string> warnings;
	auto matches = parse("<match id=\"9\" player1=\"12\" bot=\"hyp014\" botDifficulty=\"3\" ruleFile=\"blitz.lua\" scoreToWin=\"7\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	BOOST_CHECK(warnings.empty());
	BOOST_CHECK(matches[0].isTraining());
	BOOST_CHECK_EQUAL(matches[0].player1, 12);
	BOOST_CHECK_EQUAL(matches[0].player2, 0);
	BOOST_CHECK_EQUAL(matches[0].bot, "hyp014");
	BOOST_CHECK_EQUAL(matches[0].botDifficulty, 3);
	BOOST_CHECK_EQUAL(matches[0].ruleFile, "blitz.lua");
	BOOST_CHECK_EQUAL(matches[0].scoreToWin, 7);
	BOOST_CHECK_EQUAL(matches[0].bestOf, 1);
	// the bot is no player
	BOOST_CHECK(!matches[0].hasPlayer(0));
	BOOST_CHECK(matches[0].hasPlayer(12));
}

BOOST_AUTO_TEST_CASE( training_difficulty_defaults_to_the_game_default )
{
	std::vector<std::string> warnings;
	auto matches = parse("<match id=\"9\" player1=\"12\" bot=\"reduced\"/>\n", warnings);
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	BOOST_CHECK_EQUAL(matches[0].botDifficulty, DEFAULT_BOT_DIFFICULTY);
}

BOOST_AUTO_TEST_CASE( bad_training_matches_are_skipped )
{
	std::vector<std::string> warnings;
	auto matches = parse(
		// both a player 2 and a bot
		"<match id=\"1\" player1=\"12\" player2=\"17\" bot=\"reduced\"/>\n"
		// a bot name with a folder
		"<match id=\"2\" player1=\"12\" bot=\"../rules/default\"/>\n"
		// an empty bot name
		"<match id=\"3\" player1=\"12\" bot=\"\"/>\n"
		// difficulty out of range
		"<match id=\"4\" player1=\"12\" bot=\"reduced\" botDifficulty=\"26\"/>\n"
		"<match id=\"5\" player1=\"12\" bot=\"reduced\" botDifficulty=\"-1\"/>\n"
		// training is one game
		"<match id=\"6\" player1=\"12\" bot=\"reduced\" bestOf=\"3\"/>\n"
		// an unknown player
		"<match id=\"7\" player1=\"99\" bot=\"reduced\"/>\n"
		// no bot and no player 2
		"<match id=\"8\" player1=\"12\"/>\n", warnings);
	BOOST_CHECK(matches.empty());
	BOOST_CHECK_EQUAL(warnings.size(), 8u);
}

BOOST_AUTO_TEST_CASE( bot_names_and_shown_names )
{
	BOOST_CHECK(isValidBotName("reduced"));
	BOOST_CHECK(isValidBotName("axji-0-2"));
	BOOST_CHECK(isValidBotName("com_11"));
	BOOST_CHECK(!isValidBotName(""));
	BOOST_CHECK(!isValidBotName("reduced.lua"));
	BOOST_CHECK(!isValidBotName("old/reduced"));
	BOOST_CHECK(!isValidBotName(std::string(41, 'a')));
	BOOST_CHECK_EQUAL(trainingBotName("reduced"), "Bot reduced");
	BOOST_CHECK_EQUAL(trainingBotName("gintonicV9"), "Bot gintonicV9");
	// the name field of the game holds 15 characters
	BOOST_CHECK_EQUAL(trainingBotName("averyveryverylongbot").size(), 15u);
}

BOOST_AUTO_TEST_CASE( a_changed_bot_is_a_changed_assignment )
{
	BOOST_CHECK(training(9, 12) == training(9, 12));
	BOOST_CHECK(training(9, 12) != training(9, 12, "hyp014"));
	BOOST_CHECK(training(9, 12) != training(9, 12, "reduced", 4));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( TrainingSyncTest )

BOOST_AUTO_TEST_CASE( sync_training_match_becomes_a_bot_match )
{
	const std::string json = R"({"syncVersion":"v1","eventId":1,
		"players":[{"playerId":12,"nickname":"Ola","code":"K7QM2XPR4T"}],
		"matches":[{"matchId":40,"state":"called","bestOf":1,"nextGame":1,"ruleFile":"default.lua","scoreToWin":15,"speed":75,
			"nextGameDelay":10,"awaitingDecision":false,"wins1":0,"wins2":0,
			"players":[{"playerId":12,"code":"K7QM2XPR4T"}],"training":{"bot":"gintonicV9","difficulty":4}}]})";
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE(buildPlayersFile(json, {}, xml, version, warnings, error));
	BOOST_CHECK(warnings.empty());

	std::vector<MatchAssignment> matches;
	std::vector<std::string> parseWarnings;
	BOOST_REQUIRE(parseMatchAssignments(xml, {12}, matches, parseWarnings, error));
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	BOOST_CHECK(matches[0].isTraining());
	BOOST_CHECK_EQUAL(matches[0].matchId, 40);
	BOOST_CHECK_EQUAL(matches[0].player1, 12);
	BOOST_CHECK_EQUAL(matches[0].bot, "gintonicV9");
	BOOST_CHECK_EQUAL(matches[0].botDifficulty, 4);
}

BOOST_AUTO_TEST_CASE( sync_training_match_needs_one_player_and_a_valid_bot )
{
	const std::string json = R"({"syncVersion":"v1","eventId":1,
		"players":[{"playerId":12,"nickname":"Ola","code":"K7QM2XPR4T"},{"playerId":17,"nickname":"Kari","code":"B9HNZ3WD6F"}],
		"matches":[
			{"matchId":40,"players":[{"playerId":12},{"playerId":17}],"training":{"bot":"reduced","difficulty":4}},
			{"matchId":41,"players":[{"playerId":12}],"training":{"bot":"reduced","difficulty":30}},
			{"matchId":42,"players":[{"playerId":12}],"training":{"difficulty":3}},
			{"matchId":43,"players":[{"playerId":12}]}
		]})";
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE(buildPlayersFile(json, {}, xml, version, warnings, error));
	BOOST_CHECK_EQUAL(warnings.size(), 4u);
	BOOST_CHECK(xml.find("<match") == std::string::npos);
}

BOOST_AUTO_TEST_CASE( pending_training_report_counts_for_the_bot )
{
	const std::string json = R"({"syncVersion":"v1","eventId":1,
		"players":[{"playerId":12,"nickname":"Ola","code":"K7QM2XPR4T"}],
		"matches":[{"matchId":40,"bestOf":1,"nextGame":1,"wins1":0,"wins2":0,
			"players":[{"playerId":12}],"training":{"bot":"reduced","difficulty":13}}]})";
	OutboxEntry pending;
	pending.kind = "game";
	pending.matchId = 40;
	pending.game = 1;
	pending.finished = true;
	pending.winnerId = 0;
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE(buildPlayersFile(json, {pending}, xml, version, warnings, error));
	// the bot won the only game, so the match is decided and the parser leaves it out
	std::vector<MatchAssignment> matches;
	std::vector<std::string> parseWarnings;
	BOOST_REQUIRE(parseMatchAssignments(xml, {12}, matches, parseWarnings, error));
	BOOST_CHECK(matches.empty());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( TrainingDirectorTest )

BOOST_AUTO_TEST_CASE( training_starts_when_its_one_player_is_connected )
{
	auto events = std::make_shared<RecordingEvents>();
	TournamentDirector director(0, 3000);
	director.setEvents(events);
	director.setAssignments({training(9, 12)});
	BOOST_CHECK(director.takeGamesToStart(0).empty());

	director.playerConnected(12);
	auto orders = director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK(orders[0].match.isTraining());
	BOOST_REQUIRE_EQUAL(events->connected.size(), 1u);
	BOOST_CHECK_EQUAL(events->connected[0].player1, 12);
	BOOST_CHECK_EQUAL(events->connected[0].player2, 0);
	BOOST_CHECK(director.statusOf(12, 0).status == TournamentStatus::PLAYING);
}

BOOST_AUTO_TEST_CASE( training_waits_for_a_slot_not_for_an_opponent )
{
	TournamentDirector director(0, 3000);
	director.setMaxTraining(1);
	director.setAssignments({training(9, 12), training(10, 17)});
	director.playerConnected(12);
	director.playerConnected(17);
	auto orders = director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].match.matchId, 9);
	BOOST_CHECK(director.statusOf(17, 0).status == TournamentStatus::WAITING_FOR_SLOT);

	// the slot is free again when the first training is over
	director.gameFinished(9, 2, 3, 15, START, START, 0);
	orders = director.takeGamesToStart(0);
	BOOST_REQUIRE_EQUAL(orders.size(), 1u);
	BOOST_CHECK_EQUAL(orders[0].match.matchId, 10);
}

BOOST_AUTO_TEST_CASE( zero_max_training_turns_training_off )
{
	TournamentDirector director(0, 3000);
	director.setMaxTraining(0);
	director.setAssignments({training(9, 12)});
	director.playerConnected(12);
	BOOST_CHECK(director.takeGamesToStart(0).empty());
}

BOOST_AUTO_TEST_CASE( training_does_not_hold_up_the_tournament )
{
	// one tournament match at a time, and training has its own limit
	TournamentDirector director(1, 3000);
	director.setAssignments({training(5, 21), match(9, 12, 17)});
	for (int id : {12, 17, 21})
		director.playerConnected(id);
	auto orders = director.takeGamesToStart(0);
	BOOST_CHECK_EQUAL(orders.size(), 2u);
}

BOOST_AUTO_TEST_CASE( player_in_training_is_busy_for_a_real_match )
{
	TournamentDirector director(0, 3000);
	director.setAssignments({training(5, 12)});
	director.playerConnected(12);
	director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(director.takeGamesToStart(0).size(), 1u);
	// the site called a real match, the training plays to its end first
	director.setAssignments({match(9, 12, 17)});
	BOOST_CHECK(director.takeGamesToStart(0).empty());
	director.gameFinished(5, 1, 15, 3, START, START, 0);
	BOOST_CHECK_EQUAL(director.takeGamesToStart(0).size(), 1u);
}

BOOST_AUTO_TEST_CASE( training_report_names_the_bot_as_player_0 )
{
	auto events = std::make_shared<RecordingEvents>();
	TournamentDirector director(0, 3000);
	director.setEvents(events);
	director.setReportIdFunction([](){ return std::string("00000000-0000-4000-8000-000000000001"); });
	director.setAssignments({training(9, 12)});
	director.playerConnected(12);
	BOOST_REQUIRE_EQUAL(director.takeGamesToStart(0).size(), 1u);
	director.gameFinished(9, 2, 4, 15, START, START + std::chrono::seconds(90), 0);

	BOOST_REQUIRE_EQUAL(events->games.size(), 1u);
	const GameReport& report = events->games[0];
	BOOST_CHECK(report.training);
	BOOST_CHECK_EQUAL(report.winnerId, 0);
	BOOST_CHECK_EQUAL(report.player1, 12);
	BOOST_CHECK_EQUAL(report.player2, 0);
	const std::string json = gameReportJson(report);
	BOOST_CHECK(json.find("\"winnerId\":0") != std::string::npos);
	BOOST_CHECK(json.find("{\"playerId\":0,\"points\":15}") != std::string::npos);
	BOOST_CHECK(json.find("\"training\":true}") != std::string::npos);
	// one game decides it
	BOOST_REQUIRE_EQUAL(events->series.size(), 1u);
	BOOST_CHECK(director.statusOf(12, 0).status == TournamentStatus::MATCH_OVER);
}

BOOST_AUTO_TEST_CASE( normal_report_has_no_training_flag )
{
	auto events = std::make_shared<RecordingEvents>();
	TournamentDirector director(0, 3000);
	director.setEvents(events);
	director.setAssignments({match(9, 12, 17)});
	director.playerConnected(12);
	director.playerConnected(17);
	BOOST_REQUIRE_EQUAL(director.takeGamesToStart(0).size(), 1u);
	director.gameFinished(9, 1, 15, 4, START, START, 0);
	BOOST_REQUIRE_EQUAL(events->games.size(), 1u);
	BOOST_CHECK(!events->games[0].training);
	BOOST_CHECK(gameReportJson(events->games[0]).find("training") == std::string::npos);
}

BOOST_AUTO_TEST_CASE( training_has_no_live_score )
{
	struct LiveEvents : public RecordingEvents
	{
		int live = 0;
		void liveScore(int, int, int, int, int, int) override { ++live; }
	};
	auto events = std::make_shared<LiveEvents>();
	TournamentDirector director(0, 3000);
	director.setEvents(events);
	director.setAssignments({training(9, 12)});
	director.playerConnected(12);
	BOOST_REQUIRE_EQUAL(director.takeGamesToStart(0).size(), 1u);
	director.reportLiveScore(9, 3, 4);
	BOOST_CHECK_EQUAL(events->live, 0);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( MaxTrainingTest )

BOOST_AUTO_TEST_CASE( max_training_from_the_environment )
{
	unsigned value = 99;
	std::string error;
	BOOST_CHECK(loadMaxTraining(env({}), 4, 32, value, error));
	BOOST_CHECK_EQUAL(value, 4u);
	BOOST_CHECK(loadMaxTraining(env({{"BLOBBY_MAX_TRAINING", ""}}), 4, 32, value, error));
	BOOST_CHECK_EQUAL(value, 4u);
	BOOST_CHECK(loadMaxTraining(env({{"BLOBBY_MAX_TRAINING", "0"}}), 4, 32, value, error));
	BOOST_CHECK_EQUAL(value, 0u);
	BOOST_CHECK(loadMaxTraining(env({{"BLOBBY_MAX_TRAINING", "32"}}), 4, 32, value, error));
	BOOST_CHECK_EQUAL(value, 32u);
	for (const char* bad : {"33", "-1", "+2", "02", "2 ", "two", "99999"})
	{
		error.clear();
		BOOST_CHECK_MESSAGE(!loadMaxTraining(env({{"BLOBBY_MAX_TRAINING", bad}}), 4, 32, value, error), bad);
		BOOST_CHECK(error.find("BLOBBY_MAX_TRAINING") != std::string::npos);
	}
}

BOOST_AUTO_TEST_SUITE_END()

#include "FileSystem.h"
#include "ScriptBot.h"

BOOST_AUTO_TEST_SUITE( TrainingBotTest )

BOOST_AUTO_TEST_CASE( unknown_bot_script_throws )
{
	FileSystem fs("./test");
	fs.addToSearchPath(BLOBBY_TEST_DATA_DIR);
	BOOST_CHECK_THROW(ScriptBot("nosuchbot", 13, 15, std::chrono::milliseconds(10)), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()
