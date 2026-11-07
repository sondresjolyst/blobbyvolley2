#include <boost/test/unit_test.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "server/SiteSync.h"
#include "server/FileUtil.h"
#include "server/JoinCodes.h"
#include "server/MatchAssignments.h"
#include "base64.h"
#include "TestDir.h"

namespace
{
	// all files of these tests go below the test folder of the run
	std::string inFolder(const std::string& name)
	{
		const std::string folder = testPath("site");
		makeFolder(folder);
		return folder + "/" + name;
	}

	std::string readFile(const std::string& path)
	{
		std::string content;
		readWholeFile(path, 16 * 1024 * 1024, content);
		return content;
	}

	bool contains(const std::string& text, const std::string& part)
	{
		return text.find(part) != std::string::npos;
	}

	std::string syncJson(const std::string& version, const std::string& matches)
	{
		return "{\"serverTime\":\"2026-11-20T18:00:00.000Z\",\"syncVersion\":\"" + version + "\",\"eventId\":1,"
			"\"players\":[{\"playerId\":12,\"nickname\":\"Ola\",\"code\":\"K7QM2XPR4T\"},"
			"{\"playerId\":17,\"nickname\":\"Kari\",\"code\":\"B9HNZ3WD6F\"}],"
			"\"matches\":[" + matches + "]}";
	}

	std::string matchJson(int nextGame, bool awaiting, int wins1, int wins2, int bestOf = 3)
	{
		return "{\"matchId\":7,\"state\":\"started\",\"bestOf\":" + std::to_string(bestOf) + ",\"nextGame\":" + std::to_string(nextGame)
			+ ",\"ruleFile\":\"default.lua\",\"scoreToWin\":15,\"speed\":75,\"awaitingDecision\":" + (awaiting ? "true" : "false")
			+ ",\"wins1\":" + std::to_string(wins1) + ",\"wins2\":" + std::to_string(wins2)
			+ ",\"players\":[{\"playerId\":12,\"code\":\"K7QM2XPR4T\"},{\"playerId\":17,\"code\":\"B9HNZ3WD6F\"}]}";
	}

	GameReport makeReport(int matchId, int game, bool finished, int winnerId, const std::string& id)
	{
		GameReport r;
		r.reportId = id;
		r.matchId = matchId;
		r.game = game;
		r.finished = finished;
		r.winnerId = winnerId;
		r.player1 = 12;
		r.player2 = 17;
		r.points1 = winnerId == 12 ? 15 : 3;
		r.points2 = winnerId == 17 ? 15 : 3;
		return r;
	}

	void putU32(std::string& d, std::size_t at, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
			d[at + i] = char((v >> (8 * i)) & 0xFF);
	}

	// a 24-bit BMP that the game accepts
	std::string makeBmp(int width, int height)
	{
		const std::uint32_t stride = (std::uint32_t(width) * 3 + 3) / 4 * 4;
		const std::uint32_t size = 54 + stride * std::uint32_t(height);
		std::string d(size, '\0');
		d[0] = 'B';
		d[1] = 'M';
		putU32(d, 2, size);
		putU32(d, 10, 54);
		putU32(d, 14, 40);
		putU32(d, 18, std::uint32_t(width));
		putU32(d, 22, std::uint32_t(height));
		d[26] = 1;
		d[28] = 24;
		for (std::uint32_t i = 54; i < size; ++i)
			d[i] = char(i * 7);
		return d;
	}

	HttpResponse answer(long status, const std::string& body)
	{
		HttpResponse r;
		r.status = status;
		r.body = body;
		return r;
	}

	HttpResponse noAnswer()
	{
		HttpResponse r;
		r.error = "Could not connect to server";
		return r;
	}

	// remembers each request and answers with handler
	class FakeHttp : public HttpClient
	{
		public:
			std::function<HttpResponse(const HttpRequest&)> handler;
			std::vector<HttpRequest> requests;

			HttpResponse send(const HttpRequest& request) override
			{
				requests.push_back(request);
				return handler(request);
			}

			std::vector<HttpRequest> posts(const std::string& path) const
			{
				std::vector<HttpRequest> result;
				for (const auto& r : requests)
				{
					if (r.post && r.path == path)
						result.push_back(r);
				}
				return result;
			}

			int count(const std::string& path) const
			{
				int n = 0;
				for (const auto& r : requests)
					n += r.path == path ? 1 : 0;
				return n;
			}
	};

	// a fake site that stores games and checks their order
	struct FakeSite
	{
		bool up = true;
		std::string version = "v1";
		int nextGame = 1;
		std::vector<int> stored;
		std::string refuseWith;

		HttpResponse operator()(const HttpRequest& request)
		{
			if (!up)
				return noAnswer();
			if (request.path == "/api/server/sync")
				return answer(200, syncJson(version, matchJson(nextGame, false, 0, 0)));
			if (request.path == "/api/server/games")
			{
				if (!refuseWith.empty())
					return answer(200, "{\"status\":\"refused\",\"reason\":\"" + refuseWith + "\"}");
				const auto pos = request.body.find("\"game\":");
				const int game = std::stoi(request.body.substr(pos + 7));
				if (game > nextGame)
					return answer(200, "{\"status\":\"refused\",\"reason\":\"game-ahead\"}");
				stored.push_back(game);
				nextGame = game + 1;
				return answer(200, "{\"status\":\"stored\"}");
			}
			if (contains(request.path, "/connected"))
				return answer(200, "{\"status\":\"started\"}");
			if (request.path == "/api/server/live")
				return answer(200, "{\"status\":\"stored\"}");
			return answer(404, "{}");
		}
	};

	SitePaths testPaths(const std::string& prefix)
	{
		SitePaths paths;
		paths.playersFile = inFolder(prefix + "_players.xml");
		paths.refusedFile = inFolder(prefix + "_refused.jsonl");
		std::remove(paths.playersFile.c_str());
		std::remove(paths.refusedFile.c_str());
		return paths;
	}

	std::vector<MatchAssignment> matchesOf(const std::string& xml)
	{
		JoinCodeTable table;
		std::vector<std::string> warnings;
		std::string error;
		BOOST_REQUIRE(table.parse(xml, warnings, error));
		std::vector<MatchAssignment> matches;
		BOOST_REQUIRE(parseMatchAssignments(xml, table.playerIds(), matches, warnings, error));
		return matches;
	}
}

BOOST_AUTO_TEST_SUITE( site_sync )

BOOST_AUTO_TEST_CASE( outbox_survives_a_restart_in_order )
{
	const std::string path = inFolder("outbox_restart.jsonl");
	std::remove(path.c_str());
	{
		Outbox outbox(path);
		outbox.load();
		outbox.addConnected(7, 12, 17);
		outbox.addGame(makeReport(7, 1, true, 17, "11111111-1111-4111-8111-111111111111"));
		outbox.addGame(makeReport(7, 2, false, 0, "22222222-2222-4222-8222-222222222222"));
	}
	Outbox again(path);
	again.load();
	auto entries = again.entries();
	BOOST_REQUIRE_EQUAL(entries.size(), 3u);
	BOOST_CHECK_EQUAL(entries[0].kind, "connected");
	BOOST_CHECK_EQUAL(entries[0].path, "/api/server/matches/7/connected");
	BOOST_CHECK_EQUAL(entries[0].body, "{\"playerIds\":[12,17]}");
	BOOST_CHECK_EQUAL(entries[1].kind, "game");
	BOOST_CHECK_EQUAL(entries[1].game, 1);
	BOOST_CHECK(entries[1].finished);
	BOOST_CHECK_EQUAL(entries[1].winnerId, 17);
	BOOST_CHECK(contains(entries[1].body, "\"reportId\":\"11111111-1111-4111-8111-111111111111\""));
	BOOST_CHECK_EQUAL(entries[2].game, 2);
	BOOST_CHECK(!entries[2].finished);
	BOOST_CHECK(entries[0].seq < entries[1].seq && entries[1].seq < entries[2].seq);

	// new reports continue the order
	again.addConnected(9, 21, 25);
	BOOST_CHECK(again.entries().back().seq > entries[2].seq);
	std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE( outbox_sends_one_report_per_match_at_a_time )
{
	Outbox outbox("");
	outbox.addGame(makeReport(7, 1, true, 12, "a"));
	outbox.addGame(makeReport(9, 1, true, 12, "b"));
	outbox.addGame(makeReport(7, 2, true, 12, "c"));

	auto first = outbox.sendable();
	BOOST_REQUIRE_EQUAL(first.size(), 2u);
	BOOST_CHECK(first[0].matchId == 7 && first[0].game == 1);
	BOOST_CHECK(first[1].matchId == 9 && first[1].game == 1);

	outbox.remove(first[0].seq);
	auto second = outbox.sendable();
	BOOST_REQUIRE_EQUAL(second.size(), 2u);
	BOOST_CHECK(second[0].matchId == 9);
	BOOST_CHECK(second[1].matchId == 7 && second[1].game == 2);
}

BOOST_AUTO_TEST_CASE( outbox_keeps_a_broken_file )
{
	const std::string path = inFolder("outbox_broken.jsonl");
	std::string error;
	BOOST_REQUIRE(writeFileAtomically(path, "not json\n{\"seq\":4,\"kind\":\"connected\",\"matchId\":7,"
		"\"path\":\"/api/server/matches/7/connected\",\"body\":{\"playerIds\":[12,17]}}\n", error));
	std::vector<std::string> logged;
	Outbox outbox(path, [&](bool, const std::string& m){ logged.push_back(m); });
	outbox.load();
	BOOST_CHECK_EQUAL(outbox.size(), 1u);
	BOOST_CHECK(contains(readFile(path + ".bad"), "not json"));
	BOOST_CHECK(!contains(readFile(path), "not json"));
	outbox.addConnected(9, 21, 25);
	BOOST_CHECK_EQUAL(outbox.entries().back().seq, 5u);
	std::remove(path.c_str());
	std::remove((path + ".bad").c_str());
}

BOOST_AUTO_TEST_CASE( answers_are_final_or_retried )
{
	std::string detail;
	BOOST_CHECK(classifySiteAnswer("game", answer(200, "{\"status\":\"stored\"}"), detail) == SiteAnswer::DONE);
	BOOST_CHECK(classifySiteAnswer("game", answer(200, "{\"status\":\"duplicate\"}"), detail) == SiteAnswer::DONE);
	BOOST_CHECK(classifySiteAnswer("game", answer(200, "{\"status\":\"game-closed\"}"), detail) == SiteAnswer::DONE);
	BOOST_CHECK(classifySiteAnswer("connected", answer(200, "{\"status\":\"started\"}"), detail) == SiteAnswer::DONE);
	BOOST_CHECK(classifySiteAnswer("connected", answer(200, "{\"status\":\"already-started\"}"), detail) == SiteAnswer::DONE);

	BOOST_CHECK(classifySiteAnswer("game", answer(200, "{\"status\":\"refused\",\"reason\":\"game-ahead\"}"), detail) == SiteAnswer::REFUSED);
	BOOST_CHECK_EQUAL(detail, "refused game-ahead");
	BOOST_CHECK(classifySiteAnswer("connected", answer(200, "{\"status\":\"refused\",\"reason\":\"players\"}"), detail) == SiteAnswer::REFUSED);
	BOOST_CHECK(classifySiteAnswer("game", answer(400, "{\"error\":\"invalid\",\"field\":\"points\"}"), detail) == SiteAnswer::REFUSED);
	BOOST_CHECK_EQUAL(detail, "HTTP 400, invalid points");
	BOOST_CHECK(classifySiteAnswer("game", answer(404, "<html>"), detail) == SiteAnswer::REFUSED);
	// a 200 that is not the site, such as a proxy page, is tried again
	BOOST_CHECK(classifySiteAnswer("game", answer(200, "<html>"), detail) == SiteAnswer::RETRY);
	BOOST_CHECK(classifySiteAnswer("connected", answer(200, ""), detail) == SiteAnswer::RETRY);

	BOOST_CHECK(classifySiteAnswer("game", answer(401, "{\"error\":\"unauthorized\"}"), detail) == SiteAnswer::AUTH);
	BOOST_CHECK(classifySiteAnswer("game", answer(503, "{\"error\":\"not-configured\"}"), detail) == SiteAnswer::AUTH);
	BOOST_CHECK_EQUAL(detail, "not-configured");

	BOOST_CHECK(classifySiteAnswer("game", answer(503, "Service Unavailable"), detail) == SiteAnswer::RETRY);
	BOOST_CHECK(classifySiteAnswer("game", answer(500, ""), detail) == SiteAnswer::RETRY);
	BOOST_CHECK(classifySiteAnswer("game", answer(502, ""), detail) == SiteAnswer::RETRY);
	BOOST_CHECK(classifySiteAnswer("game", answer(429, ""), detail) == SiteAnswer::RETRY);
	BOOST_CHECK(classifySiteAnswer("game", noAnswer(), detail) == SiteAnswer::RETRY);
}

BOOST_AUTO_TEST_CASE( sync_maps_to_the_players_file )
{
	const std::string json = "{\"syncVersion\":\"3f9a\",\"eventId\":1,"
		"\"players\":[{\"playerId\":12,\"nickname\":\"Ola & \\\"Co\\\" <x>\",\"code\":\"K7QM2XPR4T\"},"
		"{\"playerId\":17,\"nickname\":\"Kari\",\"code\":\"B9HNZ3WD6F\"},"
		"{\"playerId\":\"x\",\"nickname\":\"Bad\",\"code\":\"C2DEFGHJKM\"}],"
		"\"matches\":[{\"matchId\":7,\"state\":\"called\",\"bestOf\":5,\"nextGame\":3,\"ruleFile\":\"blitz.lua\","
		"\"scoreToWin\":11,\"speed\":90,\"awaitingDecision\":true,\"wins1\":1,\"wins2\":1,\"nextGameDelay\":20,"
		"\"players\":[{\"playerId\":17,\"code\":\"B9HNZ3WD6F\"},{\"playerId\":12,\"code\":\"K7QM2XPR4T\"}]},"
		"{\"matchId\":8,\"players\":[]}]}";
	std::string xml;
	std::string version;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE(buildPlayersFile(json, {}, xml, version, warnings, error));
	BOOST_CHECK_EQUAL(version, "3f9a");
	BOOST_CHECK_EQUAL(warnings.size(), 2u);

	JoinCodeTable table;
	std::vector<std::string> tableWarnings;
	BOOST_REQUIRE(table.parse(xml, tableWarnings, error));
	BOOST_CHECK_EQUAL(table.size(), 2u);
	BOOST_REQUIRE(table.find("K7QM2XPR4T"));
	BOOST_CHECK_EQUAL(table.find("K7QM2XPR4T")->playerId, 12);

	auto matches = matchesOf(xml);
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	const MatchAssignment& m = matches[0];
	BOOST_CHECK_EQUAL(m.matchId, 7);
	BOOST_CHECK_EQUAL(m.player1, 17);
	BOOST_CHECK_EQUAL(m.player2, 12);
	BOOST_CHECK_EQUAL(m.ruleFile, "blitz.lua");
	BOOST_CHECK_EQUAL(m.scoreToWin, 11);
	BOOST_CHECK_EQUAL(m.speed, 90);
	BOOST_CHECK_EQUAL(m.bestOf, 5);
	BOOST_CHECK_EQUAL(m.nextGame, 3);
	BOOST_CHECK(m.awaitingDecision);
	BOOST_CHECK_EQUAL(m.wins1, 1);
	BOOST_CHECK_EQUAL(m.wins2, 1);
	BOOST_CHECK_EQUAL(m.nextGameDelay, 20);
}

BOOST_AUTO_TEST_CASE( nickname_is_escaped_in_the_players_file )
{
	const std::string json = "{\"syncVersion\":\"1\",\"players\":[{\"playerId\":12,\"nickname\":\"A&B\\\"<\",\"code\":\"K7QM2XPR4T\"}],\"matches\":[]}";
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE(buildPlayersFile(json, {}, xml, version, warnings, error));
	JoinCodeTable table;
	BOOST_REQUIRE(table.parse(xml, warnings, error));
	BOOST_REQUIRE(table.find("K7QM2XPR4T"));
	BOOST_CHECK_EQUAL(table.find("K7QM2XPR4T")->nickname, "A&B\"<");
}

BOOST_AUTO_TEST_CASE( a_sync_without_version_or_lists_is_refused )
{
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_CHECK(!buildPlayersFile("[]", {}, xml, version, warnings, error));
	BOOST_CHECK(!buildPlayersFile("{\"players\":[],\"matches\":[]}", {}, xml, version, warnings, error));
	BOOST_CHECK(!buildPlayersFile("{\"syncVersion\":\"1\",\"players\":{}}", {}, xml, version, warnings, error));
	BOOST_CHECK(!buildPlayersFile("<html>", {}, xml, version, warnings, error));
	BOOST_CHECK(xml.empty());
}

BOOST_AUTO_TEST_CASE( no_event_gives_an_empty_players_file )
{
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE(buildPlayersFile("{\"syncVersion\":\"e\",\"eventId\":null,\"players\":[],\"matches\":[]}", {}, xml, version, warnings, error));
	JoinCodeTable table;
	BOOST_REQUIRE(table.parse(xml, warnings, error));
	BOOST_CHECK_EQUAL(table.size(), 0u);
}

BOOST_AUTO_TEST_CASE( pending_reports_count_in_the_players_file )
{
	Outbox outbox("");
	outbox.addConnected(7, 12, 17);
	outbox.addGame(makeReport(7, 1, true, 17, "a"));
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE(buildPlayersFile(syncJson("v", matchJson(1, false, 0, 0)), outbox.entries(), xml, version, warnings, error));
	auto matches = matchesOf(xml);
	BOOST_REQUIRE_EQUAL(matches.size(), 1u);
	BOOST_CHECK_EQUAL(matches[0].nextGame, 2);
	BOOST_CHECK_EQUAL(matches[0].wins1, 0);
	BOOST_CHECK_EQUAL(matches[0].wins2, 1);
	BOOST_CHECK(!matches[0].awaitingDecision);

	// an abandoned game waits for the organizer
	outbox.addGame(makeReport(7, 2, false, 0, "b"));
	BOOST_REQUIRE(buildPlayersFile(syncJson("v", matchJson(1, false, 0, 0)), outbox.entries(), xml, version, warnings, error));
	matches = matchesOf(xml);
	BOOST_CHECK_EQUAL(matches[0].nextGame, 3);
	BOOST_CHECK_EQUAL(matches[0].wins2, 1);
	BOOST_CHECK(matches[0].awaitingDecision);

	// reports the site already has do not count twice
	BOOST_REQUIRE(buildPlayersFile(syncJson("v", matchJson(3, true, 0, 1)), outbox.entries(), xml, version, warnings, error));
	matches = matchesOf(xml);
	BOOST_CHECK_EQUAL(matches[0].nextGame, 3);
	BOOST_CHECK_EQUAL(matches[0].wins2, 1);
	BOOST_CHECK(matches[0].awaitingDecision);
}

BOOST_AUTO_TEST_CASE( a_pending_win_that_decides_the_series_drops_the_match )
{
	Outbox outbox("");
	outbox.addGame(makeReport(7, 3, true, 12, "a"));
	std::string xml, version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE(buildPlayersFile(syncJson("v", matchJson(3, false, 1, 1)), outbox.entries(), xml, version, warnings, error));
	BOOST_CHECK(matchesOf(xml).empty());
}

BOOST_AUTO_TEST_CASE( sponsors_are_stored_by_hash )
{
	const std::string folder = inFolder("sp");
	const std::string bmp = makeBmp(2, 1);
	const std::string data = encode(bmp.data(), bmp.data() + bmp.size());
	const std::string hash = sha256Hex(bmp);
	const std::string other = makeBmp(3, 1);
	const std::string notBmp = "hello";
	const std::string json = "{\"eventId\":1,\"gameBoard\":\"led\",\"sponsorsVersion\":\"s1\",\"sponsors\":["
		"{\"sponsorId\":3,\"name\":\"Acme\",\"logo\":{\"format\":\"bmp\",\"width\":2,\"height\":1,\"bitsPerPixel\":24,"
		"\"hash\":\"" + hash + "\",\"data\":\"" + data + "\"}},"
		"{\"sponsorId\":4,\"name\":\"Evil\",\"logo\":{\"format\":\"bmp\",\"hash\":\"../../etc/passwd\",\"data\":\"" + data + "\"}},"
		"{\"sponsorId\":5,\"name\":\"Wrong\",\"logo\":{\"format\":\"bmp\",\"hash\":\"" + std::string(64, 'b') + "\",\"data\":\""
		+ encode(other.data(), other.data() + other.size()) + "\"}},"
		"{\"sponsorId\":6,\"name\":\"NotBmp\",\"logo\":{\"format\":\"bmp\",\"hash\":\"" + sha256Hex(notBmp) + "\",\"data\":\"aGVsbG8=\"}}]}";

	// a logo that is no longer listed is removed
	BOOST_REQUIRE(makeFolder(folder));
	BOOST_REQUIRE(makeFolder(folder + "/logos"));
	const std::string stale = folder + "/logos/" + std::string(64, 'c') + ".bmp";
	std::string error;
	BOOST_REQUIRE(writeFileAtomically(stale, "old", error));

	std::string version;
	std::vector<std::string> warnings;
	BOOST_REQUIRE_MESSAGE(storeSponsors(folder, json, version, warnings, error), error);
	BOOST_CHECK_EQUAL(version, "s1");
	BOOST_CHECK_EQUAL(warnings.size(), 3u);
	BOOST_CHECK_EQUAL(storedSponsorsVersion(folder), "s1");
	BOOST_CHECK(readFile(stale).empty());

	const std::string logo = readFile(folder + "/logos/" + hash + ".bmp");
	BOOST_CHECK(logo == bmp);
	BOOST_CHECK(readFile(folder + "/logos/" + std::string(64, 'b') + ".bmp").empty());
	const std::string board = readFile(folder + "/board.json");
	BOOST_CHECK(contains(board, "\"gameBoard\": \"led\""));
	BOOST_CHECK(contains(board, "\"logo\": \"logos/" + hash + ".bmp\""));
	BOOST_CHECK(contains(board, "\"name\": \"Acme\""));
	BOOST_CHECK(!contains(board, "Evil"));

	BOOST_CHECK(!storeSponsors(folder, "{\"gameBoard\":\"neon\",\"sponsorsVersion\":\"s2\",\"sponsors\":[]}", version, warnings, error));
	BOOST_CHECK_EQUAL(storedSponsorsVersion(folder), "s1");

	BOOST_REQUIRE(storeSponsors(folder, "{\"gameBoard\":\"off\",\"sponsorsVersion\":\"s3\",\"sponsors\":[]}", version, warnings, error));
	BOOST_CHECK_EQUAL(storedSponsorsVersion(folder), "s3");
	// no sponsor needs the logo anymore
	BOOST_CHECK(readFile(folder + "/logos/" + hash + ".bmp").empty());
	std::remove((folder + "/logos/" + hash + ".bmp").c_str());
	std::remove((folder + "/board.json").c_str());
}

BOOST_AUTO_TEST_CASE( the_stored_board_is_read_for_the_players )
{
	const std::string folder = inFolder("sb");
	const std::string bmp = makeBmp(2, 1);
	const std::string hash = sha256Hex(bmp);
	const std::string other = makeBmp(3, 1);
	const std::string otherHash = sha256Hex(other);
	const std::string json = "{\"eventId\":1,\"gameBoard\":\"padded\",\"sponsorsVersion\":\"s1\",\"sponsors\":["
		"{\"sponsorId\":3,\"name\":\"Acme\",\"logo\":{\"format\":\"bmp\",\"hash\":\"" + hash + "\",\"data\":\""
		+ encode(bmp.data(), bmp.data() + bmp.size()) + "\"}},"
		"{\"sponsorId\":4,\"name\":\"Beta\",\"logo\":{\"format\":\"bmp\",\"hash\":\"" + otherHash + "\",\"data\":\""
		+ encode(other.data(), other.data() + other.size()) + "\"}}]}";
	std::string version, error;
	std::vector<std::string> warnings;
	BOOST_REQUIRE_MESSAGE(storeSponsors(folder, json, version, warnings, error), error);

	sponsors::Board board;
	BOOST_REQUIRE_MESSAGE(loadStoredSponsors(folder, board, warnings, error), error);
	BOOST_CHECK(board.style == sponsors::BoardStyle::PADDED);
	BOOST_REQUIRE_EQUAL(board.logos.size(), 2u);
	BOOST_CHECK_EQUAL(board.logos[0].hash, hash);
	BOOST_CHECK(board.logos[0].data == bmp);
	BOOST_CHECK_EQUAL(board.logos[1].hash, otherHash);

	// the watcher reports a board once and again when it changes
	std::vector<std::string> problems;
	StoredSponsorBoard watcher(folder, [&](bool problem, const std::string& m){ if (problem) problems.push_back(m); });
	sponsors::Board polled;
	BOOST_CHECK(watcher.poll(polled));
	BOOST_CHECK_EQUAL(polled.logos.size(), 2u);
	BOOST_CHECK(!watcher.poll(polled));

	// a logo that is gone for a moment makes the watcher try again
	const std::string otherFile = folder + "/logos/" + otherHash + ".bmp";
	std::remove(otherFile.c_str());
	BOOST_REQUIRE(writeFileAtomically(folder + "/board.json", readFile(folder + "/board.json") + " ", error));
	BOOST_CHECK(!loadStoredSponsors(folder, board, warnings, error));
	BOOST_CHECK(!watcher.poll(polled));
	BOOST_CHECK(!watcher.poll(polled));
	BOOST_CHECK_EQUAL(problems.size(), 1u);
	BOOST_REQUIRE(writeFileAtomically(otherFile, other, error));
	BOOST_CHECK(watcher.poll(polled));

	// a hand written board: a path outside logos and a logo that does not match its name are skipped
	BOOST_REQUIRE(writeFileAtomically(folder + "/logos/" + std::string(64, 'd') + ".bmp", bmp, error));
	BOOST_REQUIRE(writeFileAtomically(folder + "/board.json",
		"{\"gameBoard\":\"off\",\"sponsors\":[{\"logo\":\"../board.json\"},{\"logo\":\"logos/" + std::string(64, 'd') + ".bmp\"},"
		"{\"logo\":\"logos/" + hash + ".bmp\"}]}", error));
	warnings.clear();
	BOOST_REQUIRE_MESSAGE(loadStoredSponsors(folder, board, warnings, error), error);
	BOOST_CHECK(board.style == sponsors::BoardStyle::OFF);
	BOOST_REQUIRE_EQUAL(board.logos.size(), 1u);
	BOOST_CHECK_EQUAL(board.logos[0].hash, hash);
	BOOST_CHECK_EQUAL(warnings.size(), 2u);

	// more than 32 entries stop at 32 logos, a repeated logo is read once,
	// and the missing logo after the first 32 is never looked at
	std::string many = "{\"gameBoard\":\"led\",\"sponsors\":[";
	for (int i = 0; i < 40; ++i)
		many += std::string(i ? "," : "") + "{\"logo\":\"logos/" + hash + ".bmp\"}";
	many += ",{\"logo\":\"logos/" + std::string(64, 'f') + ".bmp\"}]}";
	BOOST_REQUIRE(writeFileAtomically(folder + "/board.json", many, error));
	warnings.clear();
	BOOST_REQUIRE_MESSAGE(loadStoredSponsors(folder, board, warnings, error), error);
	BOOST_CHECK_EQUAL(board.logos.size(), sponsors::MAX_LOGOS);
	BOOST_CHECK_EQUAL(warnings.size(), 9u);

	// not JSON
	BOOST_REQUIRE(writeFileAtomically(folder + "/board.json", "nope", error));
	BOOST_CHECK(!loadStoredSponsors(folder, board, warnings, error));

	std::remove((folder + "/logos/" + hash + ".bmp").c_str());
	std::remove(otherFile.c_str());
	std::remove((folder + "/logos/" + std::string(64, 'd') + ".bmp").c_str());
	std::remove((folder + "/board.json").c_str());
}

BOOST_AUTO_TEST_CASE( sync_writes_the_players_file_only_for_a_new_version )
{
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	SitePaths paths = testPaths("sync");
	SiteLink link(http, outbox, paths, SiteLink::LogFunction());

	link.step(0);
	const std::string first = readFile(paths.playersFile);
	BOOST_CHECK(contains(first, "K7QM2XPR4T"));
	BOOST_CHECK(contains(first, "<match id=\"7\""));

	// the same version is not written again
	std::string error;
	BOOST_REQUIRE(writeFileAtomically(paths.playersFile, "changed by hand", error));
	link.step(3000);
	BOOST_CHECK_EQUAL(http.count("/api/server/sync"), 2);
	BOOST_CHECK_EQUAL(readFile(paths.playersFile), "changed by hand");

	site.version = "v2";
	site.nextGame = 2;
	link.step(6000);
	BOOST_CHECK(contains(readFile(paths.playersFile), "nextGame=\"2\""));
	std::remove(paths.playersFile.c_str());
}

BOOST_AUTO_TEST_CASE( sync_backs_off_while_the_site_is_down )
{
	FakeSite site;
	site.up = false;
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	std::vector<std::string> problems;
	SiteLink link(http, outbox, testPaths("backoff"), [&](bool problem, const std::string& m){ if (problem) problems.push_back(m); });

	link.step(0);
	BOOST_CHECK(link.isSiteDown());
	link.step(500);
	BOOST_CHECK_EQUAL(http.count("/api/server/sync"), 1);
	link.step(1000);
	BOOST_CHECK_EQUAL(http.count("/api/server/sync"), 2);
	link.step(2500);
	BOOST_CHECK_EQUAL(http.count("/api/server/sync"), 2);
	link.step(3000);
	BOOST_CHECK_EQUAL(http.count("/api/server/sync"), 3);
	// the outage is logged once
	BOOST_CHECK_EQUAL(problems.size(), 1u);

	site.up = true;
	link.step(100000);
	BOOST_CHECK(!link.isSiteDown());
}

BOOST_AUTO_TEST_CASE( reports_of_a_match_go_in_order_one_at_a_time )
{
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	outbox.addConnected(7, 12, 17);
	outbox.addGame(makeReport(7, 1, true, 12, "a"));
	outbox.addGame(makeReport(7, 2, true, 17, "b"));
	SiteLink link(http, outbox, testPaths("order"), SiteLink::LogFunction());

	link.step(0);
	BOOST_CHECK_EQUAL(http.posts("/api/server/matches/7/connected").size(), 1u);
	BOOST_CHECK(http.posts("/api/server/games").empty());
	link.step(1);
	BOOST_CHECK_EQUAL(http.posts("/api/server/games").size(), 1u);
	link.step(2);
	BOOST_REQUIRE_EQUAL(http.posts("/api/server/games").size(), 2u);
	BOOST_CHECK_EQUAL(outbox.size(), 0u);
	BOOST_CHECK((site.stored == std::vector<int>{1, 2}));
	BOOST_CHECK(contains(http.posts("/api/server/games")[0].body, "\"reportId\":\"a\""));
}

BOOST_AUTO_TEST_CASE( reports_wait_for_the_site_and_keep_their_order )
{
	FakeSite site;
	site.up = false;
	FakeHttp http;
	http.handler = std::ref(site);
	const std::string path = inFolder("outbox_outage.jsonl");
	std::remove(path.c_str());
	{
		Outbox outbox(path);
		SiteLink link(http, outbox, testPaths("outage"), SiteLink::LogFunction());
		outbox.addGame(makeReport(7, 1, true, 12, "a"));
		link.step(0);
		outbox.addGame(makeReport(7, 2, true, 17, "b"));
		for (unsigned long long t = 0; t < 120000; t += 250)
			link.step(t);
		// game 2 is never sent before game 1 has an answer
		for (const auto& r : http.posts("/api/server/games"))
			BOOST_CHECK(contains(r.body, "\"reportId\":\"a\""));
		BOOST_CHECK_EQUAL(outbox.size(), 2u);
	}

	// the server restarts with the outbox on disk, then the site comes back
	site.up = true;
	http.requests.clear();
	Outbox outbox(path);
	outbox.load();
	BOOST_CHECK_EQUAL(outbox.size(), 2u);
	SiteLink link(http, outbox, testPaths("outage"), SiteLink::LogFunction());
	for (unsigned long long t = 0; t < 1000; t += 250)
		link.step(t);
	BOOST_CHECK_EQUAL(outbox.size(), 0u);
	BOOST_CHECK((site.stored == std::vector<int>{1, 2}));
	// the delivered games stay until a sync shows them
	BOOST_CHECK(contains(readFile(path), "\"delivered\":true"));
	link.step(5000);
	BOOST_CHECK(readFile(path).empty());
	std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE( a_refused_report_is_logged_and_not_sent_again )
{
	FakeSite site;
	site.refuseWith = "state";
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	outbox.addGame(makeReport(7, 1, true, 12, "a"));
	SitePaths paths = testPaths("refused");
	std::vector<std::string> problems;
	SiteLink link(http, outbox, paths, [&](bool problem, const std::string& m){ if (problem) problems.push_back(m); });

	link.step(0);
	BOOST_CHECK_EQUAL(outbox.size(), 0u);
	link.step(5000);
	BOOST_CHECK_EQUAL(http.posts("/api/server/games").size(), 1u);
	const std::string refused = readFile(paths.refusedFile);
	BOOST_CHECK(contains(refused, "\"reason\":\"refused state\""));
	BOOST_CHECK(contains(refused, "\"reportId\":\"a\""));
	BOOST_REQUIRE(!problems.empty());
	BOOST_CHECK(contains(problems.back(), "refused state"));
	std::remove(paths.refusedFile.c_str());
}

BOOST_AUTO_TEST_CASE( game_ahead_is_final )
{
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	outbox.addGame(makeReport(7, 5, true, 12, "a"));
	SiteLink link(http, outbox, testPaths("ahead"), SiteLink::LogFunction());
	link.step(0);
	BOOST_CHECK_EQUAL(outbox.size(), 0u);
	BOOST_CHECK(site.stored.empty());
}

BOOST_AUTO_TEST_CASE( a_wrong_token_keeps_the_reports )
{
	FakeHttp http;
	http.handler = [](const HttpRequest&){ return answer(401, "{\"error\":\"unauthorized\"}"); };
	Outbox outbox("");
	outbox.addGame(makeReport(7, 1, true, 12, "a"));
	std::vector<std::string> problems;
	SiteLink link(http, outbox, testPaths("token"), [&](bool problem, const std::string& m){ if (problem) problems.push_back(m); });

	link.step(0);
	link.step(10000);
	BOOST_CHECK_EQUAL(outbox.size(), 1u);
	BOOST_CHECK_EQUAL(http.posts("/api/server/games").size(), 1u);
	link.step(31000);
	BOOST_CHECK_EQUAL(http.posts("/api/server/games").size(), 2u);
	BOOST_REQUIRE(!problems.empty());
	BOOST_CHECK(contains(problems[0], "refused the token"));
}

BOOST_AUTO_TEST_CASE( players_file_follows_the_outbox )
{
	FakeSite site;
	FakeHttp http;
	http.handler = [&](const HttpRequest& r){
		// the site is up for the sync but the reports do not get through
		if (r.post)
			return answer(502, "");
		return site(r);
	};
	Outbox outbox("");
	SitePaths paths = testPaths("follow");
	SiteLink link(http, outbox, paths, SiteLink::LogFunction());
	link.step(0);
	BOOST_CHECK(contains(readFile(paths.playersFile), "nextGame=\"1\""));

	outbox.addGame(makeReport(7, 1, true, 17, "a"));
	link.step(100);
	const std::string xml = readFile(paths.playersFile);
	BOOST_CHECK(contains(xml, "nextGame=\"2\""));
	BOOST_CHECK(contains(xml, "wins2=\"1\""));
	std::remove(paths.playersFile.c_str());
}

BOOST_AUTO_TEST_CASE( a_delivered_report_counts_until_a_newer_sync_has_it )
{
	FakeSite site;
	bool frozen = false;
	std::string frozenSync;
	FakeHttp http;
	http.handler = [&](const HttpRequest& r){
		if (r.path == "/api/server/sync" && frozen)
			return answer(200, frozenSync);
		return site(r);
	};
	Outbox outbox("");
	SitePaths paths = testPaths("delivered");
	SiteLink link(http, outbox, paths, SiteLink::LogFunction());
	link.step(0);

	// the sync of the site lags behind the report it took
	frozen = true;
	frozenSync = syncJson("v1", matchJson(1, false, 0, 0));
	outbox.addGame(makeReport(7, 1, true, 17, "a"));
	for (unsigned long long t = 100; t < 5000; t += 100)
	{
		link.step(t);
		const std::string xml = readFile(paths.playersFile);
		BOOST_CHECK(contains(xml, "nextGame=\"2\""));
		BOOST_CHECK(contains(xml, "wins2=\"1\""));
	}
	BOOST_CHECK_EQUAL(outbox.size(), 0u);

	// a sync that has the game does not count it twice
	frozen = false;
	link.step(10000);
	const std::string xml = readFile(paths.playersFile);
	BOOST_CHECK(contains(xml, "nextGame=\"2\""));
	BOOST_CHECK(contains(xml, "wins2=\"0\"") || contains(xml, "wins2=\"1\""));
	std::remove(paths.playersFile.c_str());
}

BOOST_AUTO_TEST_CASE( a_restart_during_an_outage_uses_the_saved_sync )
{
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	SitePaths paths = testPaths("saved");
	paths.syncFile = inFolder("saved_sync.json");
	std::remove(paths.syncFile.c_str());
	{
		Outbox outbox("");
		SiteLink link(http, outbox, paths, SiteLink::LogFunction());
		link.step(0);
	}
	BOOST_CHECK(contains(readFile(paths.syncFile), "\"syncVersion\":\"v1\""));

	// the server starts again while the site is down, with a game in the outbox
	site.up = false;
	Outbox outbox("");
	outbox.addGame(makeReport(7, 1, true, 12, "a"));
	SiteLink link(http, outbox, paths, SiteLink::LogFunction());
	link.step(0);
	const std::string xml = readFile(paths.playersFile);
	BOOST_CHECK(contains(xml, "nextGame=\"2\""));
	BOOST_CHECK(contains(xml, "wins1=\"1\""));
	outbox.addGame(makeReport(7, 2, true, 12, "b"));
	link.step(100);
	BOOST_CHECK(contains(readFile(paths.playersFile), "nextGame=\"3\""));
	std::remove(paths.playersFile.c_str());
	std::remove(paths.syncFile.c_str());
}

BOOST_AUTO_TEST_CASE( sha256_matches_known_values )
{
	BOOST_CHECK_EQUAL(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	BOOST_CHECK_EQUAL(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	BOOST_CHECK_EQUAL(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	BOOST_CHECK_EQUAL(sha256Hex(std::string(1000, 'a')), "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

BOOST_AUTO_TEST_CASE( a_restart_after_delivery_does_not_play_the_game_again )
{
	// the site takes game 1, goes down before a sync shows it, and the server restarts
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	SitePaths paths = testPaths("redeliver");
	paths.syncFile = inFolder("redeliver_sync.json");
	const std::string outboxPath = inFolder("redeliver_outbox.jsonl");
	std::remove(paths.syncFile.c_str());
	std::remove(outboxPath.c_str());
	{
		Outbox outbox(outboxPath);
		SiteLink link(http, outbox, paths, SiteLink::LogFunction());
		link.step(0);
		outbox.addGame(makeReport(7, 1, true, 17, "a"));
		link.step(100);
		BOOST_CHECK((site.stored == std::vector<int>{1}));
		BOOST_CHECK_EQUAL(outbox.size(), 0u);
		site.up = false;
	}

	Outbox outbox(outboxPath);
	outbox.load();
	BOOST_CHECK_EQUAL(outbox.size(), 0u);
	BOOST_REQUIRE_EQUAL(outbox.entries().size(), 1u);
	BOOST_CHECK(outbox.entries()[0].delivered);
	SiteLink link(http, outbox, paths, SiteLink::LogFunction());
	link.step(0);
	const std::string xml = readFile(paths.playersFile);
	BOOST_CHECK(contains(xml, "nextGame=\"2\""));
	BOOST_CHECK(contains(xml, "wins2=\"1\""));
	// a delivered game is not sent again
	BOOST_CHECK_EQUAL(http.posts("/api/server/games").size(), 1u);

	// once a sync shows the game, it leaves the outbox
	site.up = true;
	link.step(60000);
	BOOST_CHECK(outbox.entries().empty());
	BOOST_CHECK(contains(readFile(paths.playersFile), "nextGame=\"2\""));
	std::remove(paths.playersFile.c_str());
	std::remove(paths.syncFile.c_str());
	std::remove(outboxPath.c_str());
}

BOOST_AUTO_TEST_CASE( a_failed_players_file_write_is_tried_again )
{
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	SitePaths paths = testPaths("blocked");
	// a folder in the way makes the rename fail
	makeFolder(paths.playersFile);
	std::vector<std::string> problems;
	SiteLink link(http, outbox, paths, [&](bool problem, const std::string& m){ if (problem) problems.push_back(m); });
	link.step(0);
	BOOST_REQUIRE(!problems.empty());
	BOOST_CHECK(contains(problems.back(), "cannot write the players file"));

	// the same sync version is written once the folder is gone
	BOOST_REQUIRE(removeFolder(paths.playersFile));
	link.step(200);
	BOOST_CHECK_EQUAL(http.count("/api/server/sync"), 1);
	BOOST_CHECK(contains(readFile(paths.playersFile), "K7QM2XPR4T"));
	std::remove(paths.playersFile.c_str());
}

BOOST_AUTO_TEST_CASE( an_outbox_that_cannot_be_read_is_kept_aside )
{
	const std::string path = inFolder("outbox_large.jsonl");
	std::remove((path + ".bad").c_str());
	std::string error;
	const std::string content = "{\"seq\":1,\"kind\":\"connected\",\"matchId\":7,\"path\":\"/api/server/matches/7/connected\",\"body\":{}}\n";
	BOOST_REQUIRE(writeFileAtomically(path, content, error));
	std::vector<std::string> problems;
	Outbox outbox(path, [&](bool problem, const std::string& m){ if (problem) problems.push_back(m); }, 10);
	outbox.load();
	BOOST_CHECK_EQUAL(outbox.size(), 0u);
	BOOST_CHECK_EQUAL(readFile(path + ".bad"), content);
	BOOST_REQUIRE(!problems.empty());
	BOOST_CHECK(contains(problems[0], ".bad"));

	// a new report does not overwrite the old ones
	outbox.addConnected(9, 21, 25);
	BOOST_CHECK_EQUAL(readFile(path + ".bad"), content);
	BOOST_CHECK(contains(readFile(path), "matches/9/connected"));
	std::remove(path.c_str());
	std::remove((path + ".bad").c_str());
}

BOOST_AUTO_TEST_CASE( a_closed_game_leaves_the_outbox_at_once )
{
	FakeHttp http;
	http.handler = [](const HttpRequest& r){
		if (r.path == "/api/server/sync")
			return answer(200, syncJson("v1", matchJson(1, false, 0, 0)));
		return answer(200, "{\"status\":\"game-closed\"}");
	};
	Outbox outbox("");
	outbox.addGame(makeReport(7, 1, true, 17, "a"));
	SiteLink link(http, outbox, testPaths("closed"), SiteLink::LogFunction());
	link.step(0);
	BOOST_CHECK(outbox.entries().empty());
}

BOOST_AUTO_TEST_CASE( stop_flag_ends_a_step_before_the_next_request )
{
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	outbox.addGame(makeReport(7, 1, true, 17, "a"));
	SiteLink link(http, outbox, testPaths("stop"), SiteLink::LogFunction());
	std::atomic<bool> stop(true);
	link.setStopFlag(&stop);
	link.step(0);
	BOOST_CHECK(http.requests.empty());
	BOOST_CHECK_EQUAL(outbox.size(), 1u);
}

BOOST_AUTO_TEST_CASE( live_score_is_throttled_and_never_queued )
{
	FakeSite site;
	FakeHttp http;
	http.handler = std::ref(site);
	Outbox outbox("");
	SiteLink link(http, outbox, testPaths("live"), SiteLink::LogFunction());

	LiveScore score;
	score.matchId = 7;
	score.game = 1;
	score.player1 = 12;
	score.player2 = 17;
	for (int i = 1; i <= 3; ++i)
	{
		score.points1 = i;
		link.setLiveScore(score);
	}
	link.step(0);
	auto live = http.posts("/api/server/live");
	BOOST_REQUIRE_EQUAL(live.size(), 1u);
	BOOST_CHECK_EQUAL(live[0].body, "{\"game\":1,\"matchId\":7,\"points\":[{\"playerId\":12,\"points\":3},{\"playerId\":17,\"points\":0}]}");

	score.points1 = 4;
	link.setLiveScore(score);
	link.step(500);
	BOOST_CHECK_EQUAL(http.posts("/api/server/live").size(), 1u);
	link.step(1000);
	BOOST_CHECK_EQUAL(http.posts("/api/server/live").size(), 2u);

	// dropped while the site is down
	site.up = false;
	link.step(3000);
	score.points1 = 5;
	link.setLiveScore(score);
	link.step(3000);
	site.up = true;
	link.step(3100);
	BOOST_CHECK_EQUAL(http.posts("/api/server/live").size(), 2u);

	// a game that ended has no live score
	score.points1 = 6;
	link.setLiveScore(score);
	link.clearLiveScore(7);
	link.step(10000);
	BOOST_CHECK_EQUAL(http.posts("/api/server/live").size(), 2u);
}

BOOST_AUTO_TEST_CASE( outbox_loads_only_known_paths )
{
	const std::string path = inFolder("outbox_paths.jsonl");
	std::string error;
	BOOST_REQUIRE(writeFileAtomically(path,
		"{\"seq\":1,\"kind\":\"connected\",\"matchId\":7,\"path\":\"/api/server/matches/7/connected\",\"body\":{\"playerIds\":[12,17]}}\n"
		"{\"seq\":2,\"kind\":\"connected\",\"matchId\":7,\"path\":\"/api/server/matches/8/connected\",\"body\":{\"playerIds\":[12,17]}}\n"
		"{\"seq\":3,\"kind\":\"game\",\"matchId\":7,\"path\":\"@example.com/api/server/games\",\"body\":{\"game\":1}}\n"
		"{\"seq\":4,\"kind\":\"game\",\"matchId\":7,\"path\":\"/api/server/games\",\"body\":{\"game\":1}}\n", error));
	Outbox outbox(path, [](bool, const std::string&){});
	outbox.load();
	auto entries = outbox.entries();
	BOOST_REQUIRE_EQUAL(entries.size(), 2u);
	BOOST_CHECK_EQUAL(entries[0].seq, 1u);
	BOOST_CHECK_EQUAL(entries[1].seq, 4u);
	std::remove(path.c_str());
	std::remove((path + ".bad").c_str());
}

BOOST_AUTO_TEST_CASE( site_paths_start_with_a_slash )
{
	BOOST_CHECK(isSitePath("/api/server/sync"));
	BOOST_CHECK(!isSitePath(""));
	BOOST_CHECK(!isSitePath("@example.com/api"));
	BOOST_CHECK(!isSitePath("api/server/sync"));
}

BOOST_AUTO_TEST_CASE( site_text_is_made_safe_for_logs )
{
	BOOST_CHECK_EQUAL(siteTextForLog("bad\ntoken\x1b[31m"), "bad?token?[31m");
	BOOST_CHECK_EQUAL(siteTextForLog(std::string(1000, 'a')).size(), 200u);
	// a UTF-8 character is not cut in half
	BOOST_CHECK_EQUAL(siteTextForLog(std::string(199, 'a') + "\xc3\xa9"), std::string(199, 'a'));

	std::string detail;
	BOOST_CHECK(classifySiteAnswer("game", answer(401, "{\"error\":\"no\\nway\"}"), detail) == SiteAnswer::AUTH);
	BOOST_CHECK_EQUAL(detail, "no?way");
	BOOST_CHECK(classifySiteAnswer("game", answer(200, "{\"status\":\"refused\",\"reason\":\"x\\ry\"}"), detail) == SiteAnswer::REFUSED);
	BOOST_CHECK_EQUAL(detail, "refused x?y");
}

BOOST_AUTO_TEST_CASE( site_failures_back_off_with_the_retry_timing )
{
	SiteTiming timing;
	BOOST_CHECK_EQUAL(siteFailurePause(timing, 1), timing.firstRetry);
	BOOST_CHECK_EQUAL(siteFailurePause(timing, 2), timing.firstRetry * 2);
	BOOST_CHECK_EQUAL(siteFailurePause(timing, 3), timing.firstRetry * 4);
	BOOST_CHECK_EQUAL(siteFailurePause(timing, 20), timing.maxRetry);
	BOOST_CHECK_EQUAL(siteFailurePause(timing, 4000000000u), timing.maxRetry);
}

BOOST_AUTO_TEST_CASE( sync_next_game_delay_is_clamped )
{
	const std::string match = matchJson(1, false, 0, 0);
	const std::string slow = match.substr(0, match.size() - 1) + ",\"nextGameDelay\":500}";
	const std::string fast = match.substr(0, match.size() - 1) + ",\"nextGameDelay\":1}";
	for (const auto& m : {std::make_pair(slow, MAX_NEXT_GAME_DELAY), std::make_pair(fast, MIN_NEXT_GAME_DELAY)})
	{
		std::string xml;
		std::string version;
		std::vector<std::string> warnings;
		std::string error;
		BOOST_REQUIRE(buildPlayersFile(syncJson("d1", m.first), {}, xml, version, warnings, error));
		BOOST_CHECK_EQUAL(warnings.size(), 1u);
		auto matches = matchesOf(xml);
		BOOST_REQUIRE_EQUAL(matches.size(), 1u);
		BOOST_CHECK_EQUAL(matches[0].nextGameDelay, m.second);
	}
}

BOOST_AUTO_TEST_CASE( sponsors_with_bad_base64_are_skipped )
{
	const std::string folder = inFolder("sq");
	const std::string bmp = makeBmp(2, 1);
	const std::string data = encode(bmp.data(), bmp.data() + bmp.size());
	const std::string hash = sha256Hex(bmp);
	const std::string json = "{\"gameBoard\":\"led\",\"sponsorsVersion\":\"b1\",\"sponsors\":["
		"{\"sponsorId\":1,\"name\":\"Broken\",\"logo\":{\"format\":\"bmp\",\"hash\":\"" + std::string(64, 'd') + "\",\"data\":\"!!!!\"}},"
		"{\"sponsorId\":2,\"name\":\"Acme\",\"logo\":{\"format\":\"bmp\",\"hash\":\"" + hash + "\",\"data\":\"" + data + "\"}}]}";

	std::string version;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE_MESSAGE(storeSponsors(folder, json, version, warnings, error), error);
	BOOST_CHECK_EQUAL(warnings.size(), 1u);
	const std::string board = readFile(folder + "/board.json");
	BOOST_CHECK(contains(board, "\"name\": \"Acme\""));
	BOOST_CHECK(!contains(board, "Broken"));
	std::remove((folder + "/logos/" + hash + ".bmp").c_str());
	std::remove((folder + "/board.json").c_str());
}

BOOST_AUTO_TEST_CASE( sponsors_are_capped_and_names_are_cut )
{
	const std::string folder = inFolder("sr");
	const std::string bmp = makeBmp(2, 1);
	const std::string data = encode(bmp.data(), bmp.data() + bmp.size());
	const std::string hash = sha256Hex(bmp);
	std::string list;
	for (int i = 0; i < 40; ++i)
	{
		if (i > 0)
			list += ",";
		list += "{\"sponsorId\":" + std::to_string(i + 1) + ",\"name\":\"" + std::string(100, 'n')
			+ "\",\"logo\":{\"format\":\"bmp\",\"hash\":\"" + hash + "\",\"data\":\"" + data + "\"}}";
	}
	const std::string json = "{\"gameBoard\":\"led\",\"sponsorsVersion\":\"c1\",\"sponsors\":[" + list + "]}";

	std::string version;
	std::vector<std::string> warnings;
	std::string error;
	BOOST_REQUIRE_MESSAGE(storeSponsors(folder, json, version, warnings, error), error);
	BOOST_CHECK_EQUAL(warnings.size(), 1u);

	sponsors::Board board;
	std::vector<std::string> loadWarnings;
	BOOST_REQUIRE_MESSAGE(loadStoredSponsors(folder, board, loadWarnings, error), error);
	BOOST_CHECK_EQUAL(board.logos.size(), sponsors::MAX_LOGOS);
	BOOST_CHECK(loadWarnings.empty());
	const std::string stored = readFile(folder + "/board.json");
	BOOST_CHECK(contains(stored, "\"" + std::string(64, 'n') + "\""));
	BOOST_CHECK(!contains(stored, std::string(65, 'n')));
	std::remove((folder + "/logos/" + hash + ".bmp").c_str());
	std::remove((folder + "/board.json").c_str());
}

BOOST_AUTO_TEST_SUITE_END()
