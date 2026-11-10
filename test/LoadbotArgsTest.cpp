#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include "DuelMatchState.h"
#include "LoadbotArgs.h"
#include "NetworkMessage.h"
#include "raknet/BitStream.h"
#include "raknet/PacketEnumerations.h"

namespace
{
	const std::vector<std::string> SCRIPTS = {"reduced", "axji-0-2", "com_11", "gintonicV9", "hyp014", "Union"};

	bool parse(const std::vector<std::string>& args, LoadbotArgs& out, std::string& error)
	{
		return parseLoadbotArgs(args, SCRIPTS, out, error);
	}

	std::string failure(const std::vector<std::string>& args)
	{
		LoadbotArgs out;
		std::string error;
		BOOST_REQUIRE(!parse(args, out, error));
		BOOST_CHECK(!error.empty());
		return error;
	}
}

BOOST_AUTO_TEST_SUITE(loadbot_args)

BOOST_AUTO_TEST_CASE(reads_the_positional_arguments)
{
	LoadbotArgs args;
	std::string error;
	BOOST_REQUIRE(parse({"blobby-server", "1234", "SMKAAAAAA2"}, args, error));
	BOOST_CHECK_EQUAL(args.host, "blobby-server");
	BOOST_CHECK_EQUAL(args.port, 1234);
	BOOST_CHECK_EQUAL(args.code, "SMKAAAAAA2");
	BOOST_CHECK_EQUAL(args.skill, DEFAULT_SKILL);
	BOOST_CHECK(args.script.empty());

	BOOST_REQUIRE(parse({"host", "1234", "CODE", "40"}, args, error));
	BOOST_CHECK_EQUAL(args.skill, 40);
	BOOST_CHECK(args.script.empty());
}

BOOST_AUTO_TEST_CASE(refuses_a_skill_out_of_range)
{
	LoadbotArgs args;
	std::string error;
	BOOST_REQUIRE(parse({"host", "1234", "CODE", "0"}, args, error));
	BOOST_CHECK_EQUAL(args.skill, 0);
	BOOST_REQUIRE(parse({"host", "1234", "CODE", "100"}, args, error));
	BOOST_CHECK_EQUAL(args.skill, 100);

	BOOST_CHECK(failure({"host", "1234", "CODE", "101"}).find("0 to 100") != std::string::npos);
	failure({"host", "1234", "CODE", "-1"});
	failure({"host", "1234", "CODE", "strong"});
	failure({"host", "1234", "CODE", "50x"});
}

BOOST_AUTO_TEST_CASE(reads_a_script_with_the_default_difficulty)
{
	LoadbotArgs args;
	std::string error;
	BOOST_REQUIRE(parse({"host.docker.internal", "1234", "CODE", "--script", "hyp014"}, args, error));
	BOOST_CHECK_EQUAL(args.host, "host.docker.internal");
	BOOST_CHECK_EQUAL(args.code, "CODE");
	BOOST_CHECK_EQUAL(args.script, "hyp014");
	BOOST_CHECK_EQUAL(args.difficulty, DEFAULT_DIFFICULTY);
	BOOST_CHECK_EQUAL(DEFAULT_DIFFICULTY, 13);
}

BOOST_AUTO_TEST_CASE(reads_the_options_anywhere_and_with_equals)
{
	LoadbotArgs args;
	std::string error;
	BOOST_REQUIRE(parse({"--difficulty", "0", "host", "--script=Union", "1234", "CODE"}, args, error));
	BOOST_CHECK_EQUAL(args.host, "host");
	BOOST_CHECK_EQUAL(args.port, 1234);
	BOOST_CHECK_EQUAL(args.code, "CODE");
	BOOST_CHECK_EQUAL(args.script, "Union");
	BOOST_CHECK_EQUAL(args.difficulty, 0);

	BOOST_REQUIRE(parse({"host", "1234", "CODE", "--script", "reduced", "--difficulty=25"}, args, error));
	BOOST_CHECK_EQUAL(args.difficulty, 25);
}

BOOST_AUTO_TEST_CASE(refuses_an_unknown_script_and_lists_the_scripts)
{
	std::string error = failure({"host", "1234", "CODE", "--script", "nosuchbot"});
	BOOST_CHECK(error.find("nosuchbot") != std::string::npos);
	BOOST_CHECK(error.find("Union, axji-0-2, com_11, gintonicV9, hyp014, reduced") != std::string::npos);

	// the name is the file name without .lua and without a folder
	failure({"host", "1234", "CODE", "--script", "hyp014.lua"});
	failure({"host", "1234", "CODE", "--script", "../scripts/hyp014"});
	failure({"host", "1234", "CODE", "--script", "old/hyp010"});
	failure({"host", "1234", "CODE", "--script", "union"});
	failure({"host", "1234", "CODE", "--script", ""});
}

BOOST_AUTO_TEST_CASE(says_when_the_scripts_folder_is_missing)
{
	LoadbotArgs args;
	std::string error;
	BOOST_REQUIRE(!parseLoadbotArgs({"host", "1234", "CODE", "--script", "reduced"}, {}, args, error));
	BOOST_CHECK(error.find("scripts folder not found") != std::string::npos);
	// the simple bot needs no scripts
	BOOST_CHECK(parseLoadbotArgs({"host", "1234", "CODE"}, {}, args, error));
}

BOOST_AUTO_TEST_CASE(refuses_a_difficulty_out_of_range)
{
	failure({"host", "1234", "CODE", "--script", "reduced", "--difficulty", "-1"});
	failure({"host", "1234", "CODE", "--script", "reduced", "--difficulty", "26"});
	failure({"host", "1234", "CODE", "--script", "reduced", "--difficulty", "strong"});
	failure({"host", "1234", "CODE", "--script", "reduced", "--difficulty", "5x"});
	failure({"host", "1234", "CODE", "--script", "reduced", "--difficulty", ""});
}

BOOST_AUTO_TEST_CASE(keeps_the_skill_and_the_difficulty_apart)
{
	failure({"host", "1234", "CODE", "--difficulty", "5"});
	failure({"host", "1234", "CODE", "70", "--script", "reduced"});
}

BOOST_AUTO_TEST_CASE(refuses_broken_command_lines)
{
	failure({});
	failure({"host", "1234"});
	failure({"host", "1234", "CODE", "70", "extra"});
	failure({"host", "port", "CODE"});
	failure({"host", "0", "CODE"});
	failure({"host", "65536", "CODE"});
	failure({"host", "1234", "CODE", "--script"});
	failure({"host", "1234", "CODE", "--skill", "50"});
	failure({"host", "1234", "CODE", "--script", "reduced", "--script", "hyp014"});
	failure({"host", "1234", "CODE", "--script", "reduced", "--difficulty", "1", "--difficulty", "2"});
}

BOOST_AUTO_TEST_CASE(the_win_notification_gives_the_final_score)
{
	DuelMatchState state;
	state.logicState.leftScore = 4;
	state.logicState.rightScore = 3;

	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_WIN_NOTIFICATION);
	stream.Write((int)LEFT_PLAYER);
	stream.Write(5);
	stream.Write(3);
	WinNotificationData win;
	BOOST_REQUIRE(readWinNotification(stream, win));
	applyWinScore(win, state);
	BOOST_CHECK_EQUAL(state.getScore(LEFT_PLAYER), 5);
	BOOST_CHECK_EQUAL(state.getScore(RIGHT_PLAYER), 3);
}

BOOST_AUTO_TEST_CASE(a_win_notification_without_score_keeps_the_last_score)
{
	DuelMatchState state;
	state.logicState.leftScore = 4;
	state.logicState.rightScore = 3;

	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_WIN_NOTIFICATION);
	stream.Write((int)LEFT_PLAYER);
	WinNotificationData win;
	BOOST_REQUIRE(readWinNotification(stream, win));
	applyWinScore(win, state);
	BOOST_CHECK_EQUAL(state.getScore(LEFT_PLAYER), 4);
	BOOST_CHECK_EQUAL(state.getScore(RIGHT_PLAYER), 3);
}

BOOST_AUTO_TEST_SUITE_END()
