#include <boost/test/unit_test.hpp>

#include <string>

#include "DuelMatchState.h"
#include "FileSystem.h"
#include "GameLogic.h"

// A game is won with the points to win and a lead of two, whatever the rules file.

namespace
{
	struct RulesFixture
	{
		RulesFixture() : mFS("./test")
		{
			mFS.addToSearchPath(BLOBBY_TEST_DATA_DIR);
		}

		FileSystem mFS;
	};

	// One rally that the right player loses. The ball is served again and may hit the ground.
	void rightPlayerMisses(IGameLogic& logic)
	{
		logic.onServe();
		for (int i = 0; i < 20; ++i)
			logic.step(DuelMatchState());
		logic.onBallHitsGround(RIGHT_PLAYER);
	}

	// Starts at 15-15 with the points to win at 15 and lets the left player win rallies until
	// the score is 17-15. Checks the winner after every rally.
	void playFrom15All(const std::string& rules)
	{
		BOOST_TEST_CONTEXT(rules)
		{
			GameLogicPtr logic = createGameLogic(rules, 15);
			BOOST_REQUIRE_EQUAL(logic->getScoreToWin(), 15);
			logic->setScore(LEFT_PLAYER, 15);
			logic->setScore(RIGHT_PLAYER, 15);

			// classic rules give a point only to the player who served, so some rallies give none
			for (int rally = 0; rally < 6 && logic->getScore(LEFT_PLAYER) < 17; ++rally)
			{
				rightPlayerMisses(*logic);
				if (logic->getScore(LEFT_PLAYER) < 17)
					BOOST_CHECK_EQUAL(logic->getWinningPlayer(), NO_PLAYER);
			}
			BOOST_CHECK_EQUAL(logic->getScore(LEFT_PLAYER), 17);
			BOOST_CHECK_EQUAL(logic->getScore(RIGHT_PLAYER), 15);
			BOOST_CHECK_EQUAL(logic->getWinningPlayer(), LEFT_PLAYER);
		}
	}
}

BOOST_AUTO_TEST_SUITE( ScoreToWinTest )

BOOST_FIXTURE_TEST_CASE( default_rules_need_a_lead_of_two, RulesFixture )
{
	GameLogicPtr logic = createGameLogic("default.lua", 15);
	logic->setScore(LEFT_PLAYER, 14);
	logic->setScore(RIGHT_PLAYER, 15);

	rightPlayerMisses(*logic);
	BOOST_CHECK_EQUAL(logic->getScore(LEFT_PLAYER), 15);
	BOOST_CHECK_EQUAL(logic->getWinningPlayer(), NO_PLAYER);

	rightPlayerMisses(*logic);
	BOOST_CHECK_EQUAL(logic->getScore(LEFT_PLAYER), 16);
	BOOST_CHECK_EQUAL(logic->getWinningPlayer(), NO_PLAYER);

	rightPlayerMisses(*logic);
	BOOST_CHECK_EQUAL(logic->getScore(LEFT_PLAYER), 17);
	BOOST_CHECK_EQUAL(logic->getScore(RIGHT_PLAYER), 15);
	BOOST_CHECK_EQUAL(logic->getWinningPlayer(), LEFT_PLAYER);
}

BOOST_FIXTURE_TEST_CASE( default_rules_win_at_15_13, RulesFixture )
{
	GameLogicPtr logic = createGameLogic("default.lua", 15);
	logic->setScore(LEFT_PLAYER, 14);
	logic->setScore(RIGHT_PLAYER, 13);

	rightPlayerMisses(*logic);
	BOOST_CHECK_EQUAL(logic->getScore(LEFT_PLAYER), 15);
	BOOST_CHECK_EQUAL(logic->getWinningPlayer(), LEFT_PLAYER);
}

BOOST_FIXTURE_TEST_CASE( every_rules_file_with_the_match_points_needs_a_lead_of_two, RulesFixture )
{
	// classic has its own IsWinning, sticky mode and the fallback have none
	playFrom15All("default.lua");
	playFrom15All("classic.lua");
	playFrom15All("sticky_mode.lua");
	playFrom15All(FALLBACK_RULES_NAME);
}

BOOST_FIXTURE_TEST_CASE( a_rules_file_may_set_its_own_points_to_win, RulesFixture )
{
	BOOST_CHECK_EQUAL(createGameLogic("default.lua", 15)->getScoreToWin(), 15);
	BOOST_CHECK_EQUAL(createGameLogic("default.lua", 21)->getScoreToWin(), 21);
	BOOST_CHECK_EQUAL(createGameLogic("blitz.lua", 15)->getScoreToWin(), 2);
	BOOST_CHECK_EQUAL(createGameLogic("firewall.lua", 15)->getScoreToWin(), 150);
}

BOOST_AUTO_TEST_SUITE_END()
