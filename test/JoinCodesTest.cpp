#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "server/EntryDeadlines.h"
#include "server/JoinCodes.h"
#include "server/NetworkPlayer.h"
#include "TestDir.h"
#include "TestServer.h"

namespace
{
	const char* const VALID_FILE =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<tournament>\n"
		"\t<player id=\"12\" nickname=\"Ola\" code=\"K7QM2XPR4T\"/>\n"
		"\t<player id=\"17\" nickname=\"Kari\" code=\"B9HNZ3WD6F\"/>\n"
		"</tournament>\n";

	struct LogCapture
	{
		std::vector<std::string> lines;
		ServerLog function()
		{
			return [this](bool, const std::string& line){ lines.push_back(line); };
		}
		bool contains(const std::string& text) const
		{
			for (const auto& l : lines)
			{
				if (l.find(text) != std::string::npos)
					return true;
			}
			return false;
		}
	};

	// a players file in the test folder that is removed after the test
	struct TempFile
	{
		explicit TempFile(const std::string& name) : path(testPath(name)) { std::remove(path.c_str()); }
		~TempFile() { std::remove(path.c_str()); }
		void write(const std::string& content) const
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out << content;
		}
		std::string path;
	};

	// writes an ID_ENTER_SERVER body as the client does, without the message id
	void writeEnterServer(RakNet::BitStream& stream, const char* name, std::size_t name_bytes)
	{
		char buffer[PLAYER_NAME_BUFFER] = {0};
		std::memcpy(buffer, name, name_bytes);
		stream.Write((int)RIGHT_PLAYER);
		stream.Write(buffer, sizeof(buffer));
		stream.Write((int)0x00FF00);
	}
}

BOOST_AUTO_TEST_SUITE(JoinCodesTest)

BOOST_AUTO_TEST_CASE( normalize_accepts_spaces_and_lower_case )
{
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR4T"), "K7QM2XPR4T");
	BOOST_CHECK_EQUAL(normalizeJoinCode("k7qm2xpr4t"), "K7QM2XPR4T");
	BOOST_CHECK_EQUAL(normalizeJoinCode("  K7QM2XPR4T "), "K7QM2XPR4T");
}

BOOST_AUTO_TEST_CASE( normalize_rejects_invalid_codes )
{
	BOOST_CHECK_EQUAL(normalizeJoinCode(""), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("   "), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("Ola"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR4"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR4TT"), "");
	// 0, O, 1, I and L are not in the alphabet
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR40"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR4O"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR41"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR4I"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2XPR4L"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2 XPR4"), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode(std::string("K7QM2\0PR4T", 10)), "");
	BOOST_CHECK_EQUAL(normalizeJoinCode("K7QM2\xC3\xA6PR4"), "");
}

BOOST_AUTO_TEST_CASE( log_prefix_hides_the_code )
{
	std::string prefix = joinCodeLogPrefix("K7QM2XPR4T");
	BOOST_CHECK_EQUAL(prefix, "K7Q...");
	BOOST_CHECK_EQUAL(joinCodeLogPrefix(""), "...");
	BOOST_CHECK_EQUAL(joinCodeLogPrefix("a\nb%s"), "a?b...");
}

BOOST_AUTO_TEST_CASE( table_parses_valid_file )
{
	JoinCodeTable table;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE(table.parse(VALID_FILE, warnings, error));
	BOOST_CHECK(warnings.empty());
	BOOST_CHECK_EQUAL(table.size(), 2u);

	const JoinCodeEntry* ola = table.find("K7QM2XPR4T");
	BOOST_REQUIRE(ola != nullptr);
	BOOST_CHECK_EQUAL(ola->nickname, "Ola");
	BOOST_CHECK_EQUAL(ola->playerId, 12);

	BOOST_CHECK(table.find("ABCDEFGHJK") == nullptr);
	BOOST_CHECK(table.find("k7qm2xpr4t") == nullptr);
	BOOST_CHECK(table.find("") == nullptr);
}

BOOST_AUTO_TEST_CASE( table_rejects_bad_files )
{
	std::vector<std::string> warnings;
	std::string error;
	JoinCodeTable table;

	BOOST_CHECK(!table.parse("", warnings, error));
	BOOST_CHECK(!error.empty());
	BOOST_CHECK(!table.parse("<tournament><player", warnings, error));
	BOOST_CHECK(!table.parse("<players><player nickname=\"Ola\" code=\"K7QM2XPR4T\"/></players>", warnings, error));
	BOOST_CHECK(!table.parse("{\"players\": []}", warnings, error));
	BOOST_CHECK_EQUAL(table.size(), 0u);

	// a failed parse keeps the old entries
	BOOST_REQUIRE(table.parse(VALID_FILE, warnings, error));
	BOOST_CHECK(!table.parse("<oops", warnings, error));
	BOOST_CHECK_EQUAL(table.size(), 2u);
}

BOOST_AUTO_TEST_CASE( table_accepts_empty_player_list )
{
	JoinCodeTable table;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_CHECK(table.parse("<tournament/>", warnings, error));
	BOOST_CHECK_EQUAL(table.size(), 0u);
}

BOOST_AUTO_TEST_CASE( table_skips_bad_entries )
{
	const std::string xml =
		"<tournament>"
		"<player nickname=\"NoCode\"/>"
		"<player code=\"ABCDEFGHJK\"/>"
		"<player nickname=\"Short\" code=\"ABC\"/>"
		"<player nickname=\"Zero\" code=\"ABCDEFGH0K\"/>"
		"<player nickname=\"\" code=\"ABCDEFGHJM\"/>"
		"<player nickname=\"SixteenBytesLong\" code=\"ABCDEFGHJN\"/>"
		"<player nickname=\"Bad&#9;Tab\" code=\"ABCDEFGHJP\"/>"
		"<player nickname=\"BadId\" id=\"x\" code=\"ABCDEFGHJQ\"/>"
		"<player nickname=\"NegId\" id=\"-3\" code=\"ABCDEFGHJR\"/>"
		"<player nickname=\"\xC3\xA6\xC3\xB8\xC3\xA5\xC3\xA6\xC3\xB8\xC3\xA5\xC3\xA6x\" code=\"ABCDEFGHJS\"/>"
		"<player nickname=\"Fine\" code=\"ABCDEFGHJT\"/>"
		"<match id=\"7\"/>"
		"</tournament>";

	JoinCodeTable table;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE(table.parse(xml, warnings, error));
	BOOST_CHECK_EQUAL(warnings.size(), 9u);
	BOOST_CHECK_EQUAL(table.size(), 2u);

	// 15 bytes of UTF-8 fit the name field
	const JoinCodeEntry* norse = table.find("ABCDEFGHJS");
	BOOST_REQUIRE(norse != nullptr);
	BOOST_CHECK_EQUAL(norse->nickname.size(), 15u);
	BOOST_CHECK_EQUAL(norse->playerId, 0);
	BOOST_CHECK(table.find("ABCDEFGHJT") != nullptr);

	for (const auto& w : warnings)
		BOOST_CHECK(w.find("ABCDEFGH") == std::string::npos);
}

BOOST_AUTO_TEST_CASE( table_drops_duplicate_codes )
{
	const std::string xml =
		"<tournament>"
		"<player nickname=\"Ola\" code=\"K7QM2XPR4T\"/>"
		"<player nickname=\"Kari\" code=\"K7QM2XPR4T\"/>"
		"<player nickname=\"Per\" code=\"B9HNZ3WD6F\"/>"
		"<player nickname=\"per\" code=\"C9HNZ3WD6F\"/>"
		"</tournament>";

	JoinCodeTable table;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE(table.parse(xml, warnings, error));
	BOOST_CHECK(table.find("K7QM2XPR4T") == nullptr);
	// duplicate nicknames are a warning only
	BOOST_CHECK(table.find("B9HNZ3WD6F") != nullptr);
	BOOST_CHECK(table.find("C9HNZ3WD6F") != nullptr);
	BOOST_CHECK_EQUAL(table.size(), 2u);
	BOOST_CHECK_EQUAL(warnings.size(), 2u);
}

BOOST_AUTO_TEST_CASE( read_file_reports_missing_file )
{
	std::string content = "old";
	std::string error;
	BOOST_CHECK(!readJoinCodeFile(testPath("does_not_exist_players.xml"), content, error));
	BOOST_CHECK(!error.empty());
	BOOST_CHECK_EQUAL(content, "old");
	BOOST_CHECK(!readJoinCodeFile("", content, error));
}

BOOST_AUTO_TEST_CASE( read_file_rejects_large_file )
{
	TempFile file("joincodes_test_large.xml");
	file.write(std::string(MAX_PLAYERS_FILE_BYTES + 1, ' '));
	std::string content;
	std::string error;
	BOOST_CHECK(!readJoinCodeFile(file.path, content, error));
	BOOST_CHECK(content.empty());
}

BOOST_AUTO_TEST_CASE( gate_loads_and_reloads_file )
{
	TempFile file("joincodes_test_players.xml");
	LogCapture log;
	JoinCodeGate gate(file.path);
	gate.setLogFunction(log.function());

	// missing file: nobody gets in and the error is logged once
	BOOST_CHECK(!gate.reload());
	BOOST_CHECK(!gate.reload());
	BOOST_CHECK_EQUAL(gate.getTable().size(), 0u);
	BOOST_CHECK_EQUAL(log.lines.size(), 1u);

	file.write(VALID_FILE);
	BOOST_CHECK(gate.reload());
	BOOST_CHECK_EQUAL(gate.getTable().size(), 2u);

	// unchanged content is not parsed again
	BOOST_CHECK(!gate.reload());

	// a broken file keeps the last good table
	file.write("<tournament><player nickname=");
	BOOST_CHECK(!gate.reload());
	BOOST_CHECK_EQUAL(gate.getTable().size(), 2u);

	// a removed file keeps the last good table too
	std::remove(file.path.c_str());
	BOOST_CHECK(!gate.reload());
	BOOST_CHECK_EQUAL(gate.getTable().size(), 2u);

	file.write("<tournament><player nickname=\"Per\" code=\"B9HNZ3WD6F\"/></tournament>");
	BOOST_CHECK(gate.reload());
	BOOST_CHECK_EQUAL(gate.getTable().size(), 1u);
	BOOST_CHECK(gate.getTable().find("K7QM2XPR4T") == nullptr);

	for (const auto& l : log.lines)
	{
		BOOST_CHECK(l.find("K7QM2XPR4T") == std::string::npos);
		BOOST_CHECK(l.find("B9HNZ3WD6F") == std::string::npos);
	}
}

BOOST_AUTO_TEST_CASE( gate_poll_waits_for_interval )
{
	TempFile file("joincodes_test_poll.xml");
	file.write("<tournament/>");
	JoinCodeGate gate(file.path, 2000);

	gate.poll(5000);
	BOOST_CHECK_EQUAL(gate.getTable().size(), 0u);

	file.write(VALID_FILE);
	gate.poll(6000);
	BOOST_CHECK_EQUAL(gate.getTable().size(), 0u);
	gate.poll(7000);
	BOOST_CHECK_EQUAL(gate.getTable().size(), 2u);
}

BOOST_AUTO_TEST_CASE( gate_admits_known_codes_once )
{
	LogCapture log;
	JoinCodeGate gate("");
	gate.setLogFunction(log.function());
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));

	std::string nickname;
	BOOST_CHECK(gate.admit(makeID(1), "Ola", nickname) == JoinCodeGate::Result::UNKNOWN_CODE);
	BOOST_CHECK(gate.admit(makeID(1), "ABCDEFGHJK", nickname) == JoinCodeGate::Result::UNKNOWN_CODE);
	BOOST_CHECK(gate.admit(makeID(1), "", nickname) == JoinCodeGate::Result::UNKNOWN_CODE);
	BOOST_CHECK(nickname.empty());

	BOOST_CHECK(gate.admit(makeID(1), "k7qm2xpr4t", nickname) == JoinCodeGate::Result::ACCEPTED);
	BOOST_CHECK_EQUAL(nickname, "Ola");

	// a second connection with the same code is refused
	std::string other;
	BOOST_CHECK(gate.admit(makeID(2), "K7QM2XPR4T", other) == JoinCodeGate::Result::CODE_IN_USE);
	BOOST_CHECK(other.empty());

	// other codes still work
	BOOST_CHECK(gate.admit(makeID(2), "B9HNZ3WD6F", other) == JoinCodeGate::Result::ACCEPTED);
	BOOST_CHECK_EQUAL(other, "Kari");

	// when the first player leaves, the code is free again
	gate.release(makeID(1));
	gate.release(makeID(99));
	BOOST_CHECK(gate.admit(makeID(3), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);

	for (const auto& l : log.lines)
	{
		BOOST_CHECK(l.find("K7QM2XPR4T") == std::string::npos);
		BOOST_CHECK(l.find("B9HNZ3WD6F") == std::string::npos);
	}
	BOOST_CHECK(log.contains("K7Q..."));
}

BOOST_AUTO_TEST_CASE( gate_keeps_codes_in_use_across_reload )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));
	std::string nickname;
	BOOST_REQUIRE(gate.admit(makeID(1), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);

	BOOST_REQUIRE(gate.applyContent(std::string(VALID_FILE) + " "));
	BOOST_CHECK(gate.admit(makeID(2), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::CODE_IN_USE);
}

BOOST_AUTO_TEST_CASE( network_player_name_is_replaced )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));

	RakNet::BitStream stream;
	writeEnterServer(stream, "k7qm2xpr4t", 10);
	NetworkPlayer player(makeID(1), stream);
	BOOST_CHECK_EQUAL(player.getName(), "k7qm2xpr4t");

	BOOST_CHECK(gate.admit(player) == JoinCodeGate::Result::ACCEPTED);
	BOOST_CHECK_EQUAL(player.getName(), "Ola");
	BOOST_CHECK(player.getDesiredSide() == RIGHT_PLAYER);
	BOOST_CHECK(player.getColor() == Color(0x00FF00));
	BOOST_CHECK_EQUAL(player.getIdentity().getName(), "Ola");
}

BOOST_AUTO_TEST_CASE( network_player_with_bad_name_is_refused )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));

	// 16 bytes without a terminating zero
	RakNet::BitStream full;
	writeEnterServer(full, "K7QM2XPR4TABCDEF", 16);
	NetworkPlayer long_name(makeID(1), full);
	BOOST_CHECK(gate.admit(long_name) == JoinCodeGate::Result::UNKNOWN_CODE);
	BOOST_CHECK_EQUAL(long_name.getName(), "K7QM2XPR4TABCDE");

	// a truncated packet
	RakNet::BitStream truncated;
	truncated.Write((int)LEFT_PLAYER);
	truncated.Write("K7QM", 4);
	NetworkPlayer short_packet(makeID(2), truncated);
	BOOST_CHECK(gate.admit(short_packet) == JoinCodeGate::Result::UNKNOWN_CODE);

	// an empty packet
	RakNet::BitStream empty;
	NetworkPlayer no_data(makeID(3), empty);
	BOOST_CHECK(gate.admit(no_data) == JoinCodeGate::Result::UNKNOWN_CODE);

	// a refused player does not take the code
	RakNet::BitStream ok;
	writeEnterServer(ok, "K7QM2XPR4T", 10);
	NetworkPlayer real(makeID(4), ok);
	BOOST_CHECK(gate.admit(real) == JoinCodeGate::Result::ACCEPTED);
}

BOOST_AUTO_TEST_CASE( gate_hands_a_known_code_over_to_a_new_connection )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));
	std::string nickname;
	BOOST_REQUIRE(gate.admit(makeID(1), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);

	// the connection that holds the code does not take it from itself
	PlayerID holder;
	BOOST_CHECK(gate.checkHandover(makeID(1), "K7QM2XPR4T", 1000, {}, holder) == JoinCodeGate::Handover::NONE);
	// a free code needs no handover
	BOOST_CHECK(gate.checkHandover(makeID(2), "B9HNZ3WD6F", 1000, {}, holder) == JoinCodeGate::Handover::NONE);

	// from any address, knowing the code is enough
	PlayerID other = makeID(2);
	other.binaryAddress = 0x0500A8C0;
	BOOST_REQUIRE(gate.checkHandover(other, "k7qm2xpr4t", 1000, {}, holder) == JoinCodeGate::Handover::HAND_OVER);
	BOOST_CHECK(holder == makeID(1));
	// the server lets the old connection leave, then admits the new one
	gate.release(holder);
	BOOST_CHECK(gate.admit(other, "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);
	BOOST_CHECK_EQUAL(gate.playerIdOf(other), 12);
	BOOST_CHECK_EQUAL(gate.playerIdOf(makeID(1)), 0);
}

BOOST_AUTO_TEST_CASE( gate_keeps_a_handed_over_code_for_the_cooldown )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));
	std::string nickname;
	PlayerID holder;
	BOOST_REQUIRE(gate.admit(makeID(1), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);
	BOOST_REQUIRE(gate.checkHandover(makeID(2), "K7QM2XPR4T", 5000, {}, holder) == JoinCodeGate::Handover::HAND_OVER);
	gate.release(holder);
	BOOST_REQUIRE(gate.admit(makeID(2), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);

	// a third connection inside the cooldown is refused, so two clients cannot take turns
	BOOST_CHECK(gate.checkHandover(makeID(3), "K7QM2XPR4T", 5000 + JOIN_CODE_HANDOVER_COOLDOWN_MS - 1, {}, holder)
		== JoinCodeGate::Handover::TOO_SOON);
	BOOST_CHECK_EQUAL(gate.playerIdOf(makeID(2)), 12);

	// after the cooldown the code moves again
	BOOST_CHECK(gate.checkHandover(makeID(3), "K7QM2XPR4T", 5000 + JOIN_CODE_HANDOVER_COOLDOWN_MS, {}, holder)
		== JoinCodeGate::Handover::HAND_OVER);
	BOOST_CHECK(holder == makeID(2));
}

BOOST_AUTO_TEST_CASE( gate_does_not_hand_over_a_code_removed_by_a_reload )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));
	std::string nickname;
	BOOST_REQUIRE(gate.admit(makeID(1), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);

	BOOST_REQUIRE(gate.applyContent(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<tournament>\n"
		"\t<player id=\"17\" nickname=\"Kari\" code=\"B9HNZ3WD6F\"/>\n</tournament>\n"));

	// the old holder stays, the new connection is refused by admit
	PlayerID holder;
	BOOST_CHECK(gate.checkHandover(makeID(2), "K7QM2XPR4T", 1000, {}, holder) == JoinCodeGate::Handover::NONE);
	BOOST_CHECK(gate.admit(makeID(2), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::UNKNOWN_CODE);
	PlayerID connection;
	BOOST_CHECK(!gate.findConnection(12, connection));
	gate.release(makeID(1));

	// when the code comes back it is free
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));
	BOOST_CHECK(gate.checkHandover(makeID(2), "K7QM2XPR4T", 1000, {}, holder) == JoinCodeGate::Handover::NONE);
	BOOST_CHECK(gate.admit(makeID(2), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);
}

BOOST_AUTO_TEST_CASE( gate_lets_a_player_reconnect_after_a_lost_connection )
{
	JoinCodeGate gate("");
	BOOST_REQUIRE(gate.applyContent(VALID_FILE));
	std::string nickname;
	PlayerID holder;
	BOOST_REQUIRE(gate.admit(makeID(1), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);
	BOOST_REQUIRE(gate.checkHandover(makeID(2), "K7QM2XPR4T", 1000, {}, holder) == JoinCodeGate::Handover::HAND_OVER);
	gate.release(holder);
	BOOST_REQUIRE(gate.admit(makeID(2), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);

	// the connection is lost, and the player comes back inside the cooldown
	gate.release(makeID(2));
	BOOST_CHECK(gate.checkHandover(makeID(3), "K7QM2XPR4T", 1001, {}, holder) == JoinCodeGate::Handover::NONE);
	BOOST_CHECK(gate.admit(makeID(3), "K7QM2XPR4T", nickname) == JoinCodeGate::Result::ACCEPTED);
	BOOST_CHECK_EQUAL(nickname, "Ola");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( EntryDeadlinesTest )

BOOST_AUTO_TEST_CASE( connection_that_does_not_enter_expires )
{
	EntryDeadlines deadlines(std::chrono::seconds(10));
	auto start = EntryDeadlines::Clock::time_point();
	deadlines.connected(makeID(1), start);
	deadlines.connected(makeID(2), start + std::chrono::seconds(5));

	BOOST_CHECK(deadlines.takeExpired(start + std::chrono::seconds(9)).empty());

	auto expired = deadlines.takeExpired(start + std::chrono::seconds(10));
	BOOST_REQUIRE_EQUAL(expired.size(), 1u);
	BOOST_CHECK(expired[0] == makeID(1));
	BOOST_CHECK_EQUAL(deadlines.size(), 1u);
}

BOOST_AUTO_TEST_CASE( entered_connection_does_not_expire )
{
	EntryDeadlines deadlines(std::chrono::seconds(10));
	auto start = EntryDeadlines::Clock::time_point();
	deadlines.connected(makeID(1), start);
	deadlines.remove(makeID(1));

	BOOST_CHECK(deadlines.takeExpired(start + std::chrono::hours(1)).empty());
	BOOST_CHECK_EQUAL(deadlines.size(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
