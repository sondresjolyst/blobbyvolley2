#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "DuelMatch.h"
#include "DuelMatchState.h"
#include "FileSystem.h"
#include "GameConstants.h"
#include "InputSource.h"
#include "PlayerInput.h"

// Plays whole matches with fixed inputs and compares a hash of every game state with a
// stored value. A failure means game behaviour changed: physics, rules or match logic.
// If the change is intended, update the expected value and explain it in the commit.

namespace
{
	const int SCORE_TO_WIN = 15;
	const int MAX_STEPS = 30000;

	class StateHash
	{
		public:
			template<class T>
			void add(const T& value)
			{
				const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&value);
				for(size_t i = 0; i < sizeof(T); ++i)
				{
					mHash ^= bytes[i];
					mHash *= 1099511628211ULL;
				}
			}

			uint64_t get() const { return mHash; }

		private:
			uint64_t mHash = 1469598103934665603ULL;
	};

	std::string playMatch(const std::string& rules)
	{
		DuelMatch match(false, rules, SCORE_TO_WIN);
		auto left = std::make_shared<InputSource>();
		auto right = std::make_shared<InputSource>();
		match.setInputSources(left, right);

		StateHash hash;
		uint32_t seed = 12345;
		PlayerInput leftInput;
		PlayerInput rightInput;
		int step = 0;
		int eventCount = 0;

		for(; step < MAX_STEPS && match.winningPlayer() == NO_PLAYER; ++step)
		{
			if(step % 15 == 0)
			{
				seed = seed * 1664525u + 1013904223u;
				const int bits = seed >> 24;
				leftInput = PlayerInput((bits & 3) == 1, (bits & 3) == 2, (bits & 4) != 0);
				rightInput = PlayerInput((bits & 24) == 8, (bits & 24) == 16, (bits & 32) != 0);
			}
			left->setInput(leftInput);
			right->setInput(rightInput);
			match.step();

			for(const auto& event : match.getEvents())
			{
				hash.add(static_cast<int>(event.event));
				hash.add(static_cast<int>(event.side));
				hash.add(event.intensity);
				++eventCount;
			}

			const DuelMatchState state = match.getState();
			hash.add(state.worldState.ballPosition.x);
			hash.add(state.worldState.ballPosition.y);
			hash.add(state.worldState.ballVelocity.x);
			hash.add(state.worldState.ballVelocity.y);
			hash.add(state.worldState.ballRotation);
			for(int player = 0; player < MAX_PLAYERS; ++player)
			{
				hash.add(state.worldState.blobPosition[player].x);
				hash.add(state.worldState.blobPosition[player].y);
				hash.add(state.worldState.blobVelocity[player].x);
				hash.add(state.worldState.blobVelocity[player].y);
				hash.add(state.worldState.blobState[player]);
				hash.add(state.getScore(static_cast<PlayerSide>(player)));
			}
		}

		std::ostringstream summary;
		summary << "steps=" << step << " events=" << eventCount
			<< " score=" << match.getScore(LEFT_PLAYER) << "-" << match.getScore(RIGHT_PLAYER)
			<< " winner=" << static_cast<int>(match.winningPlayer())
			<< " hash=" << std::hex << std::setw(16) << std::setfill('0') << hash.get();
		return summary.str();
	}

	struct DataFixture
	{
		DataFixture() : mFS("./test")
		{
			mFS.addToSearchPath(BLOBBY_TEST_DATA_DIR);
		}
		FileSystem mFS;
	};

	struct GoldenMatch
	{
		const char* rules;
		const char* expected;
	};

	const GoldenMatch GOLDEN_MATCHES[] = {
		{ "default.lua", "steps=9275 events=296 score=12-15 winner=1 hash=db069dd789c54c1d" },
		{ "classic.lua", "steps=28069 events=901 score=15-11 winner=0 hash=2e7583cb5771744d" },
		{ "blitz.lua", "steps=696 events=26 score=1-3 winner=1 hash=dc772df255fdf73e" },
		{ "back_defence.lua", "steps=10985 events=250 score=15-12 winner=0 hash=3413d5fde9b926aa" },
		{ "firewall.lua", "steps=9275 events=296 score=135-159 winner=1 hash=2795a61a98a2b7de" },
		{ "jumping_jack.lua", "steps=9484 events=320 score=14-16 winner=1 hash=7c373294cd3ec2a0" },
		{ "one_hit_wonder.lua", "steps=10015 events=278 score=14-16 winner=1 hash=5ebbd71b15feb513" },
		{ "sticky_mode.lua", "steps=7503 events=163 score=15-10 winner=0 hash=8b9db8443ea3263e" },
		{ "tennis.lua", "steps=9710 events=304 score=12-15 winner=1 hash=90ade1c75322860e" },
		{ "the_double.lua", "steps=9386 events=290 score=15-13 winner=0 hash=e8c95409566dbcd0" },
	};
}

BOOST_AUTO_TEST_SUITE( MatchGoldenTest )

BOOST_FIXTURE_TEST_CASE( same_inputs_give_same_match, DataFixture )
{
	BOOST_CHECK_EQUAL( playMatch("default.lua"), playMatch("default.lua") );
}

BOOST_FIXTURE_TEST_CASE( matches_equal_stored_results, DataFixture )
{
	for(const auto& golden : GOLDEN_MATCHES)
	{
		BOOST_TEST_CONTEXT( "rules " << golden.rules )
		{
			BOOST_CHECK_EQUAL( playMatch(golden.rules), golden.expected );
		}
	}
}

BOOST_AUTO_TEST_SUITE_END()
