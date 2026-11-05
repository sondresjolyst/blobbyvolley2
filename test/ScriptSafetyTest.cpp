#include <boost/test/unit_test.hpp>

#include <string>

#include "FileRead.h"
#include "FileSystem.h"
#include "FileWrite.h"
#include "GameLogic.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

namespace
{
	const std::string RULES_FILE = "script_safety_test.lua";

	int appendChunk(lua_State*, const void* data, size_t size, void* target)
	{
		static_cast<std::string*>(target)->append(static_cast<const char*>(data), size);
		return 0;
	}

	std::string compileChunk(lua_State* state, const char* source)
	{
		std::string chunk;
		BOOST_REQUIRE_EQUAL( luaL_loadstring(state, source), LUA_OK );
		lua_dump(state, appendChunk, &chunk, 0);
		lua_settop(state, 0);
		return chunk;
	}

	struct ScriptFixture
	{
		ScriptFixture() : mFS("./test")
		{
			mFS.setWriteDir(".");
			mFS.addToSearchPath(".");
			mFS.addToSearchPath(BLOBBY_TEST_DATA_DIR);
			mFS.mkdir("rules");
		}

		~ScriptFixture()
		{
			mFS.deleteFile("rules/" + RULES_FILE);
		}

		void writeRules(const std::string& source)
		{
			FileWrite file("rules/" + RULES_FILE);
			file.write(source.data(), source.size());
			file.close();
		}

		FileSystem mFS;
	};
}

BOOST_AUTO_TEST_SUITE( ScriptSafetyTest )

BOOST_FIXTURE_TEST_CASE( scripts_load_as_text_only, ScriptFixture )
{
	lua_State* state = luaL_newstate();

	writeRules("x = 1\n");
	BOOST_CHECK_EQUAL( FileRead::readLuaScript("rules/" + RULES_FILE, state), LUA_OK );
	lua_settop(state, 0);

	writeRules(compileChunk(state, "x = 1"));
	BOOST_CHECK_NE( FileRead::readLuaScript("rules/" + RULES_FILE, state), LUA_OK );

	lua_close(state);
}

BOOST_FIXTURE_TEST_CASE( rules_cannot_score_for_invalid_player, ScriptFixture )
{
	writeRules("score(100, 1)\nscore(-1, 1)\nmistake(100, 0, 1)\nscore(0, 1)\n");

	GameLogicPtr logic = createGameLogic(RULES_FILE, 15);

	BOOST_CHECK_EQUAL( logic->getScore(LEFT_PLAYER), 1 );
	BOOST_CHECK_EQUAL( logic->getScore(RIGHT_PLAYER), 0 );
	BOOST_CHECK_EQUAL( logic->getTouches(LEFT_PLAYER), 0 );
	BOOST_CHECK_EQUAL( logic->getTouches(RIGHT_PLAYER), 0 );
}

BOOST_FIXTURE_TEST_CASE( failing_win_check_does_not_end_the_game, ScriptFixture )
{
	writeRules("function IsWinning(l, r)\nerror(\"broken rule\")\nend\nscore(0, 1)\n");

	GameLogicPtr logic = createGameLogic(RULES_FILE, 15);

	BOOST_CHECK_EQUAL( logic->getScore(LEFT_PLAYER), 1 );
	BOOST_CHECK_EQUAL( logic->getWinningPlayer(), NO_PLAYER );
}

BOOST_AUTO_TEST_SUITE_END()
