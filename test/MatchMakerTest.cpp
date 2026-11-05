#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>

#include "FileSystem.h"
#include "GenericIO.h"
#include "NetworkMessage.h"
#include "server/MatchMaker.h"
#include "server/NetworkPlayer.h"

namespace
{
	struct MatchMakerFixture
	{
		MatchMakerFixture() : mFS("./test")
		{
			mFS.addToSearchPath(BLOBBY_TEST_DATA_DIR);
			mMaker.setSendFunction([](const RakNet::BitStream&, PlayerID){});
			mMaker.addGameSpeedOption(75);
			mMaker.addRuleOption("default.lua");

			mCreator.binaryAddress = 0x0100007F;
			mCreator.port = 1234;
			mMaker.addPlayer(mCreator, std::make_shared<NetworkPlayer>(mCreator, "creator", Color(255, 0, 0), LEFT_PLAYER));
		}

		// sends an OPEN_GAME lobby packet like a client does
		void sendOpenGame(unsigned speed, unsigned score, unsigned rules)
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)LobbyPacketType::OPEN_GAME);
			auto writer = createGenericWriter(&stream);
			writer->uint32(speed);
			writer->uint32(score);
			writer->uint32(rules);
			writer->generic<std::string>("");
			mMaker.receiveLobbyPacket(mCreator, stream);
		}

		FileSystem mFS;
		MatchMaker mMaker;
		PlayerID mCreator;
	};
}

BOOST_AUTO_TEST_SUITE(MatchMakerTest)

BOOST_FIXTURE_TEST_CASE( open_game_accepts_valid_indexes, MatchMakerFixture )
{
	sendOpenGame(0, 15, 0);
	BOOST_CHECK_EQUAL(mMaker.getOpenGamesCount(), 1u);
}

BOOST_FIXTURE_TEST_CASE( open_game_rejects_out_of_range_speed, MatchMakerFixture )
{
	sendOpenGame(1, 15, 0);
	BOOST_CHECK_EQUAL(mMaker.getOpenGamesCount(), 0u);
	sendOpenGame(0xFFFFFFFFu, 15, 0);
	BOOST_CHECK_EQUAL(mMaker.getOpenGamesCount(), 0u);
	BOOST_CHECK_EQUAL(mMaker.openGame(mCreator, -1, 0, 15), (unsigned)-1);
	BOOST_CHECK_EQUAL(mMaker.getOpenGamesCount(), 0u);
}

BOOST_FIXTURE_TEST_CASE( open_game_rejects_out_of_range_rules, MatchMakerFixture )
{
	sendOpenGame(0, 15, 1);
	BOOST_CHECK_EQUAL(mMaker.getOpenGamesCount(), 0u);
	sendOpenGame(0, 15, 0xFFFFFFFFu);
	BOOST_CHECK_EQUAL(mMaker.getOpenGamesCount(), 0u);
	BOOST_CHECK_EQUAL(mMaker.openGame(mCreator, 0, -1, 15), (unsigned)-1);
	BOOST_CHECK_EQUAL(mMaker.getOpenGamesCount(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
