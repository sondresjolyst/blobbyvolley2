#include <boost/test/unit_test.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "DuelMatch.h"
#include "DuelMatchState.h"
#include "FileSystem.h"
#include "FileWrite.h"
#include "GameLogic.h"
#include "ScriptBot.h"
#include "TestDir.h"

namespace
{
	const std::string TEST_SCRIPT = "loadbot_script_test";

	/// The data folder of the repository, with a write folder for scripts of the tests in front of it.
	struct ScriptFixture
	{
		ScriptFixture() : mFS("./test")
		{
			mFS.setWriteDir(testDir());
			mFS.addToSearchPath(BLOBBY_TEST_DATA_DIR);
			mFS.mkdir("scripts");
		}

		~ScriptFixture()
		{
			mFS.deleteFile("scripts/" + TEST_SCRIPT + ".lua");
		}

		/// A bot script that runs body in each of its functions.
		void writeScript(const std::string& body)
		{
			std::string source = "function OnServe(ballready)\n" + body + "\nend\n"
				"function OnOpponentServe()\n" + body + "\nend\n"
				"function OnGame()\n" + body + "\nend\n";
			FileWrite file("scripts/" + TEST_SCRIPT + ".lua");
			file.write(source.data(), source.size());
			file.close();
		}

		FileSystem mFS;
	};

	DuelMatchState startState()
	{
		DuelMatch match(false, FALLBACK_RULES_NAME, 15);
		return match.getState();
	}

	/// Steps until the script reports an error, for at most 30 seconds.
	ScriptBot::Step stepUntilError(ScriptBot& bot, const DuelMatchState& state)
	{
		auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		ScriptBot::Step step;
		while (step.error.empty() && std::chrono::steady_clock::now() < end)
			step = bot.step(state);
		return step;
	}
}

BOOST_AUTO_TEST_SUITE(loadbot_script)

BOOST_AUTO_TEST_CASE(finds_the_scripts_of_the_game)
{
	FileSystem fs("./test");
	fs.addToSearchPath(BLOBBY_TEST_DATA_DIR);
	const std::vector<std::string> expected = {"Union", "axji-0-2", "com_11", "gintonicV9", "hyp014", "reduced"};
	std::vector<std::string> scripts = findBotScripts();
	BOOST_CHECK_EQUAL_COLLECTIONS(scripts.begin(), scripts.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(finds_no_scripts_without_a_scripts_folder)
{
	FileSystem fs("./test");
	BOOST_CHECK(findBotScripts().empty());
}

BOOST_FIXTURE_TEST_CASE(every_script_of_the_game_plays, ScriptFixture)
{
	DuelMatchState state = startState();
	for (const auto& name : {"Union", "axji-0-2", "com_11", "gintonicV9", "hyp014", "reduced"})
	{
		BOOST_TEST_CONTEXT(name)
		{
			// a long budget, so a slow test machine does not make the steps late
			ScriptBot bot(name, 13, 15, std::chrono::milliseconds(2000));
			for (int i = 0; i < 20; ++i)
			{
				ScriptBot::Step step = bot.step(state);
				BOOST_REQUIRE(step.error.empty());
				BOOST_REQUIRE(!step.late);
				BOOST_CHECK_GE(step.microseconds, 0);
			}
		}
	}
}

BOOST_FIXTURE_TEST_CASE(reports_a_lua_error, ScriptFixture)
{
	writeScript("error(\"broken bot\")");
	ScriptBot bot(TEST_SCRIPT, 13, 15, std::chrono::milliseconds(2000));
	ScriptBot::Step step = stepUntilError(bot, startState());
	BOOST_CHECK(step.error.find("broken bot") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(a_slow_script_does_not_block_the_step, ScriptFixture)
{
	// the instruction limit of the scripts stops the loop after a while
	writeScript("while true do end");
	ScriptBot bot(TEST_SCRIPT, 13, 15, std::chrono::milliseconds(1));

	auto start = std::chrono::steady_clock::now();
	ScriptBot::Step step = bot.step(startState());
	auto took = std::chrono::steady_clock::now() - start;
	BOOST_CHECK(step.late);
	BOOST_CHECK(step.error.empty());
	BOOST_CHECK(took < std::chrono::milliseconds(500));

	step = stepUntilError(bot, startState());
	BOOST_CHECK(step.error.find("too long") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(refuses_a_script_that_does_not_load, ScriptFixture)
{
	writeScript("this is not lua");
	BOOST_CHECK_THROW(ScriptBot(TEST_SCRIPT, 13, 15, std::chrono::milliseconds(10)), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()
