#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <vector>

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

BOOST_AUTO_TEST_CASE( server_status_from_match_maker_is_consistent )
{
	FileSystem fs("./test");
	fs.addToSearchPath(BLOBBY_TEST_DATA_DIR);
	MatchMaker maker;
	std::vector<unsigned char> last;
	maker.setSendFunction([&last](const RakNet::BitStream& stream, PlayerID){
		last.assign(stream.GetData(), stream.GetData() + stream.GetNumberOfBytesUsed());
	});
	maker.addGameSpeedOption(75);
	maker.addGameSpeedOption(100);
	maker.addRuleOption("default.lua");

	PlayerID creator;
	creator.binaryAddress = 0x0100007F;
	creator.port = 1234;
	maker.addPlayer(creator, std::make_shared<NetworkPlayer>(creator, "creator", Color(255, 0, 0), LEFT_PLAYER));
	maker.openGame(creator, 1, 0, 15);
	BOOST_REQUIRE_EQUAL(maker.getOpenGamesCount(), 1u);
	maker.addPlayer(PlayerID{0x0200007F, 1235}, std::make_shared<NetworkPlayer>(PlayerID{0x0200007F, 1235}, "guest", Color(0, 0, 255), LEFT_PLAYER));

	RakNet::BitStream packet(last.data(), last.size(), false);
	auto in = createGenericReader(&packet);
	unsigned char type = 0;
	in->byte(type);
	BOOST_REQUIRE_EQUAL(type, (unsigned char)ID_LOBBY);
	in->byte(type);
	BOOST_REQUIRE_EQUAL(type, (unsigned char)LobbyPacketType::SERVER_STATUS);
	unsigned int players = 0;
	in->uint32(players);
	ServerStatusLists lists;
	lists.read(*in);
	BOOST_CHECK_EQUAL(lists.gameIds.size(), 1u);
	BOOST_CHECK(lists.consistent());
}

BOOST_AUTO_TEST_CASE( server_status_lists_that_do_not_fit_are_rejected )
{
	ServerStatusLists good;
	good.speeds = {75};
	good.rules = {"Blobby Volley 2"};
	good.rulesAuthors = {"Blobby"};
	good.gameIds = {4};
	good.gameNames = {"game"};
	good.gameSpeeds = {0};
	good.gameRules = {0};
	good.gameScores = {15};
	good.gamePasswords = {false};
	BOOST_CHECK(good.consistent());

	ServerStatusLists lists = good;
	lists.speeds.clear();
	BOOST_CHECK(!lists.consistent());

	lists = good;
	lists.rules.clear();
	lists.rulesAuthors.clear();
	BOOST_CHECK(!lists.consistent());

	lists = good;
	lists.rulesAuthors.clear();
	BOOST_CHECK(!lists.consistent());

	lists = good;
	lists.gameNames.clear();
	BOOST_CHECK(!lists.consistent());

	lists = good;
	lists.gamePasswords.push_back(true);
	BOOST_CHECK(!lists.consistent());

	lists = good;
	lists.gameSpeeds = {1};
	BOOST_CHECK(!lists.consistent());

	lists = good;
	lists.gameRules = {1};
	BOOST_CHECK(!lists.consistent());
}

BOOST_AUTO_TEST_SUITE_END()
