#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "raknet/BitStream.h"

#include "DuelMatch.h"
#include "DuelMatchState.h"
#include "FileRead.h"
#include "FileSystem.h"
#include "FileWrite.h"
#include "GenericIO.h"
#include "InputSource.h"
#include "PlayerInput.h"
#include "base64.h"
#include "replays/IReplayLoader.h"
#include "replays/ReplayPlayer.h"
#include "replays/ReplayRecorder.h"
#include "replays/ReplaySavePoint.h"

// Records a short match with ReplayRecorder, saves it and loads it again. The valid file has to
// play back to the same states. Edited copies of the file have to be rejected while loading.

namespace
{
	const char* const RULES = "default.lua";
	const int MATCH_STEPS = 1600;
	const char* const VALID_FILE = "replaytest_valid.bvr";
	const char* const EDITED_FILE = "replaytest_edited.bvr";

	struct ReplayFixture
	{
		ReplayFixture() : mFS("./test")
		{
			mFS.setWriteDir(".");
			mFS.addToSearchPath(".");
			mFS.addToSearchPath(BLOBBY_TEST_DATA_DIR);
		}

		~ReplayFixture()
		{
			mFS.deleteFile(VALID_FILE);
			mFS.deleteFile(EDITED_FILE);
		}

		FileSystem mFS;
	};

	// plays a match with fixed inputs, records it and returns the state after every step
	std::vector<DuelMatchState> recordMatch(ReplayRecorder& recorder)
	{
		DuelMatch match(false, RULES);
		auto left = std::make_shared<InputSource>();
		auto right = std::make_shared<InputSource>();
		match.setInputSources(left, right);

		recorder.setPlayerNames("left", "right");
		recorder.setPlayerColors(Color(255, 0, 0), Color(0, 0, 255));
		recorder.setGameSpeed(75);
		recorder.setGameRules(RULES);

		std::vector<DuelMatchState> states;
		uint32_t seed = 12345;
		PlayerInput leftInput;
		PlayerInput rightInput;
		for(int step = 0; step < MATCH_STEPS; ++step)
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

			states.push_back(match.getState());
			recorder.record(match.getState());
			match.step();
		}
		states.push_back(match.getState());
		recorder.record(match.getState());
		recorder.finalize(match.getScore(LEFT_PLAYER), match.getScore(RIGHT_PLAYER));
		return states;
	}

	bool sameState(const DuelMatchState& a, const DuelMatchState& b)
	{
		bool same = a.worldState.ballPosition == b.worldState.ballPosition &&
			a.worldState.ballVelocity == b.worldState.ballVelocity &&
			a.worldState.ballRotation == b.worldState.ballRotation;
		for(int player = 0; player < MAX_PLAYERS; ++player)
		{
			const PlayerSide side = static_cast<PlayerSide>(player);
			same = same && a.worldState.blobPosition[player] == b.worldState.blobPosition[player] &&
				a.worldState.blobVelocity[player] == b.worldState.blobVelocity[player] &&
				a.worldState.blobState[player] == b.worldState.blobState[player] &&
				a.getScore(side) == b.getScore(side);
		}
		return same;
	}

	std::string readText(const std::string& filename)
	{
		FileRead file(filename);
		std::string text(file.length(), '\0');
		file.readRawBytes(&text[0], text.size());
		return text;
	}

	void writeText(const std::string& filename, const std::string& text)
	{
		FileWrite file(filename);
		file.write(text);
		file.close();
	}

	// replaces the value of the first occurrence of attribute="..." after the given anchor
	std::string replaceAttribute(std::string xml, const std::string& anchor, const std::string& value)
	{
		const std::size_t start = xml.find("value=\"", xml.find(anchor)) + 7;
		const std::size_t end = xml.find('"', start);
		return xml.replace(start, end - start, value);
	}

	std::string elementText(const std::string& xml, const std::string& element)
	{
		const std::size_t start = xml.find("<" + element + ">") + element.size() + 2;
		const std::size_t end = xml.find("</" + element + ">");
		return xml.substr(start, end - start);
	}

	std::string replaceElementText(std::string xml, const std::string& element, const std::string& text)
	{
		const std::size_t start = xml.find("<" + element + ">") + element.size() + 2;
		const std::size_t end = xml.find("</" + element + ">");
		return xml.replace(start, end - start, text);
	}

	std::string saveValidReplay(std::vector<DuelMatchState>* states = nullptr)
	{
		ReplayRecorder recorder;
		std::vector<DuelMatchState> recorded = recordMatch(recorder);
		if(states)
			*states = recorded;

		FileWrite file(VALID_FILE);
		recorder.save(file);
		file.close();
		return readText(VALID_FILE);
	}

	std::string setSavePointStep(const std::string& xml, unsigned int step)
	{
		std::vector<uint8_t> data = decode(elementText(xml, "states"));
		RakNet::BitStream input(data.data(), data.size(), false);
		std::vector<ReplaySavePoint> savePoints;
		createGenericReader(&input)->generic<std::vector<ReplaySavePoint> >(savePoints);
		BOOST_REQUIRE(savePoints.size() > 1);
		savePoints[1].step = step;

		RakNet::BitStream output;
		createGenericWriter(&output)->generic<std::vector<ReplaySavePoint> >(savePoints);
		const char* begin = reinterpret_cast<const char*>(output.GetData());
		return replaceElementText(xml, "states", encode(begin, begin + output.GetNumberOfBytesUsed(), 80));
	}

	void loadEdited(const std::string& xml)
	{
		writeText(EDITED_FILE, xml);
		std::unique_ptr<IReplayLoader> loader(IReplayLoader::createReplayLoader(std::string(EDITED_FILE)));
	}
}

BOOST_AUTO_TEST_SUITE( ReplayTest )

BOOST_FIXTURE_TEST_CASE( recorded_replay_plays_back_the_same_states, ReplayFixture )
{
	std::vector<DuelMatchState> states;
	saveValidReplay(&states);

	ReplayPlayer player;
	player.load(VALID_FILE);
	BOOST_CHECK_EQUAL(player.getReplayLength(), MATCH_STEPS + 1 + 75);
	BOOST_CHECK_EQUAL(player.getGameSpeed(), 75);
	BOOST_CHECK_EQUAL(player.getPlayerName(LEFT_PLAYER), "left");
	BOOST_CHECK_EQUAL(player.getPlayerName(RIGHT_PLAYER), "right");
	BOOST_CHECK(!player.getRules().empty());

	DuelMatch match(false, RULES);
	BOOST_REQUIRE(sameState(match.getState(), states[0]));
	for(std::size_t step = 1; step < states.size(); ++step)
	{
		BOOST_REQUIRE(player.play(&match));
		BOOST_REQUIRE_MESSAGE(sameState(match.getState(), states[step]), "state differs at step " << step);
	}
	while(player.play(&match))
	{
	}
	BOOST_CHECK(player.endOfFile());

	// jump back to a save point
	BOOST_CHECK(player.gotoPlayingPosition(750, &match));
	BOOST_CHECK_EQUAL(player.getReplayPosition(), 750);
	BOOST_CHECK(sameState(match.getState(), states[750]));
}

BOOST_FIXTURE_TEST_CASE( unchanged_copy_loads, ReplayFixture )
{
	const std::string xml = saveValidReplay();
	BOOST_CHECK_NO_THROW(loadEdited(xml));
}

BOOST_FIXTURE_TEST_CASE( truncated_file_is_rejected, ReplayFixture )
{
	const std::string xml = saveValidReplay();
	BOOST_CHECK_THROW(loadEdited(xml.substr(0, xml.size() / 2)), std::exception);
}

BOOST_FIXTURE_TEST_CASE( truncated_input_data_is_rejected, ReplayFixture )
{
	const std::string xml = saveValidReplay();
	const std::string input = elementText(xml, "input");
	// cut at a line end, so the shorter data is still valid base64
	const std::size_t lineEnd = input.find('\n', input.size() / 2);
	BOOST_REQUIRE(lineEnd != std::string::npos);
	BOOST_CHECK_THROW(loadEdited(replaceElementText(xml, "input", input.substr(0, lineEnd + 1))), std::exception);
}

BOOST_FIXTURE_TEST_CASE( game_length_larger_than_data_is_rejected, ReplayFixture )
{
	const std::string xml = saveValidReplay();
	BOOST_CHECK_THROW(loadEdited(replaceAttribute(xml, "\"game_length\"", "100000")), std::exception);
	BOOST_CHECK_THROW(loadEdited(replaceAttribute(xml, "\"game_length\"", "-5")), std::exception);
}

BOOST_FIXTURE_TEST_CASE( save_point_step_out_of_range_is_rejected, ReplayFixture )
{
	const std::string xml = saveValidReplay();
	BOOST_CHECK_NO_THROW(loadEdited(setSavePointStep(xml, 750)));
	BOOST_CHECK_THROW(loadEdited(setSavePointStep(xml, 100000)), std::exception);
	BOOST_CHECK_THROW(loadEdited(setSavePointStep(xml, 0xFFFFFFF0u)), std::exception);
}

BOOST_FIXTURE_TEST_CASE( misaligned_input_data_is_rejected, ReplayFixture )
{
	const std::string xml = saveValidReplay();
	std::string input = elementText(xml, "input");
	// split the first group with a space and pad the first line, so the line still ends on a
	// group boundary. One more group at the end keeps the data longer than game_length.
	const std::size_t lineEnd = input.find('\n');
	BOOST_REQUIRE(lineEnd != std::string::npos && lineEnd > 4);
	input.insert(lineEnd, "   ");
	input.insert(3, " ");
	input += "AAAA";
	BOOST_CHECK_THROW(loadEdited(replaceElementText(xml, "input", input)), std::exception);
}

BOOST_FIXTURE_TEST_CASE( unsupported_major_version_is_rejected, ReplayFixture )
{
	std::string xml = saveValidReplay();
	const std::size_t major = xml.find("major=\"2\"");
	BOOST_REQUIRE(major != std::string::npos);
	xml.replace(major, 9, "major=\"3\"");
	BOOST_CHECK_THROW(loadEdited(xml), VersionMismatchException);
}

BOOST_FIXTURE_TEST_CASE( game_speed_zero, ReplayFixture )
{
	ReplayRecorder recorder;
	recordMatch(recorder);
	recorder.setGameSpeed(0);
	FileWrite file(VALID_FILE);
	BOOST_CHECK_NO_THROW(recorder.save(file));
	file.close();

	BOOST_CHECK_THROW(loadEdited(readText(VALID_FILE)), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()
