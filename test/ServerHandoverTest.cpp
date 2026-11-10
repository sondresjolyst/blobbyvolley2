#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "FileSystem.h"
#include "FileWrite.h"
#include "GenericIO.h"
#include "Global.h"
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "PlayerInput.h"
#include "raknet/PacketEnumerations.h"
#include "raknet/RakClient.h"
#include "server/DedicatedServer.h"
#include "server/JoinCodes.h"
#include "server/NetworkGame.h"
#include "server/NetworkPlayer.h"
#include "server/ThreadSafeRakServer.h"
#include "server/Tournament.h"
#include "TestDir.h"
#include "TestServer.h"

// the server binary defines these
std::atomic<int> SWLS_PacketCount{0};
std::atomic<int> SWLS_Connections{0};
std::atomic<int> SWLS_Games{0};
std::atomic<int> SWLS_GameSteps{0};

#ifdef WIN32
void syslog(int, const char*, ...)
{
}
#endif

// Runs a tournament server and real clients over the loopback interface.
namespace
{
	// RakNet binds its sockets with SO_REUSEADDR, so a server can start on a port that another
	// socket uses and then miss its packets. A plain socket finds out whether the port is free.
	// Ports 20000 to 32000 lie below the ephemeral ranges, which start at 32768 on Linux and at
	// 49152 on macOS and Windows, so no client socket gets one of them.
	unsigned short freeTestPort()
	{
		static std::mt19937 random{std::random_device{}()};
		std::uniform_int_distribution<int> ports(20000, 32000);
#ifdef _WIN32
		static bool started = false;
		if (!started)
		{
			WSADATA data;
			started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
		}
#endif
		for (int attempt = 0; attempt < 200; ++attempt)
		{
			const unsigned short port = (unsigned short)ports(random);
			auto probe = socket(AF_INET, SOCK_DGRAM, 0);
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_ANY);
			address.sin_port = htons(port);
			const bool free = bind(probe, (sockaddr*)&address, sizeof(address)) == 0;
#ifdef _WIN32
			closesocket(probe);
#else
			close(probe);
#endif
			if (free)
				return port;
		}
		return 0;
	}

	const char* const OLA = "K7QM2XPR4T";
	const char* const KARI = "B9HNZ3WD6F";
	const char* const PER = "C2DEFGHJKM";

	std::string playersFile(const std::string& matches)
	{
		return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<tournament>\n")
			+ "\t<player id=\"12\" nickname=\"Ola\" code=\"K7QM2XPR4T\"/>\n"
			+ "\t<player id=\"17\" nickname=\"Kari\" code=\"B9HNZ3WD6F\"/>\n"
			+ "\t<player id=\"21\" nickname=\"Per\" code=\"C2DEFGHJKM\"/>\n"
			+ matches + "</tournament>\n";
	}

	struct Client
	{
		RakClient rak;
		std::vector<std::vector<unsigned char>> packets;
		bool closed = false;
		// checksums this client lets pass without an answer
		int checksumsToIgnore = 0;
		// sends its input on every drain once it is in a game, as the game does in every frame
		bool sendsInput = false;
		bool inGame = false;

		void enter(const std::string& name)
		{
			RakNet::BitStream stream;
			makeEnterServerPacket(stream, PlayerIdentity(name));
			rak.Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
		}

		void sendLobby(LobbyPacketType type, const std::function<void(GenericOut&)>& body)
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_LOBBY);
			stream.Write((unsigned char)type);
			auto out = createGenericWriter(&stream);
			body(*out);
			rak.Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
		}

		void drain()
		{
			while (packet_ptr packet = rak.Receive())
			{
				std::vector<unsigned char> data(packet->data, packet->data + packet->length);
				if (data.empty())
					continue;
				if (data[0] == ID_DISCONNECTION_NOTIFICATION || data[0] == ID_CONNECTION_LOST)
					closed = true;
				// a game starts once both players said whether they need the rules
				if (data[0] == ID_RULES_CHECKSUM && checksumsToIgnore > 0)
				{
					--checksumsToIgnore;
				}
				else if (data[0] == ID_RULES_CHECKSUM)
				{
					RakNet::BitStream answer;
					answer.Write((unsigned char)ID_RULES);
					answer.Write(false);
					rak.Send(&answer, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
				}
				if (data[0] == ID_GAME_READY)
					inGame = true;
				packets.push_back(std::move(data));
			}
			if (sendsInput && inGame && !closed)
			{
				RakNet::BitStream input;
				input.Write((unsigned char)ID_INPUT_UPDATE);
				input.Write((unsigned)1);
				PlayerInputAbs().writeTo(input);
				rak.Send(&input, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
			}
		}

		int count(unsigned char id) const
		{
			int n = 0;
			for (const auto& p : packets)
				n += p[0] == id ? 1 : 0;
			return n;
		}

		bool hasLobby(LobbyPacketType type) const
		{
			for (const auto& p : packets)
			{
				if (p.size() >= 2 && p[0] == ID_LOBBY && p[1] == (unsigned char)type)
					return true;
			}
			return false;
		}

		bool refusedWith(EntryRefusedReason reason) const
		{
			for (const auto& p : packets)
			{
				if (p.size() >= 3 && p[0] == ID_LOBBY && p[1] == (unsigned char)LobbyPacketType::ENTRY_REFUSED
					&& p[2] == (unsigned char)reason)
					return true;
			}
			return false;
		}

		// the number of open games in the last server status
		int openGames() const
		{
			int games = -1;
			for (const auto& p : packets)
			{
				if (p.size() < 2 || p[0] != ID_LOBBY || p[1] != (unsigned char)LobbyPacketType::SERVER_STATUS)
					continue;
				RakNet::BitStream stream(const_cast<unsigned char*>(p.data()), p.size(), false);
				auto in = createGenericReader(&stream);
				unsigned char byte = 0;
				in->byte(byte);
				in->byte(byte);
				unsigned int players = 0;
				in->uint32(players);
				ServerStatusLists lists;
				lists.read(*in);
				games = (int)lists.gameIds.size();
			}
			return games;
		}

		// number of packets so far, to look only at what comes later
		std::size_t mark() const { return packets.size(); }
		int countSince(std::size_t from, unsigned char id) const
		{
			int n = 0;
			for (std::size_t i = from; i < packets.size(); ++i)
				n += packets[i][0] == id ? 1 : 0;
			return n;
		}
	};

	struct ServerFixture
	{
		FileSystem fs;
		std::unique_ptr<DedicatedServer> server;
		std::shared_ptr<RecordingEvents> events = std::make_shared<RecordingEvents>();
		std::vector<std::unique_ptr<Client>> clients;
		unsigned short port = 0;

		ServerFixture(const std::string& matches, bool director) : fs("./test")
		{
			fs.addToSearchPath(BLOBBY_TEST_DATA_DIR);
			startOnFreePort();
			std::unique_ptr<JoinCodeGate> gate(new JoinCodeGate(""));
			BOOST_REQUIRE(gate->applyContent(playersFile(matches)));
			server->setJoinCodeGate(std::move(gate));
			if (director)
			{
				std::unique_ptr<TournamentDirector> d(new TournamentDirector(4, 3000));
				d->setEvents(events);
				server->setTournamentDirector(std::move(d));
			}
		}

		// tries free ports until the server starts
		void startOnFreePort()
		{
			for (int attempt = 0; attempt < 50 && !server; ++attempt)
			{
				ServerInfo info("test");
				info.port = freeTestPort();
				if (info.port == 0)
					continue;
				try
				{
					server.reset(new DedicatedServer(info, {"default.lua"}, {75.f}, 8));
					port = info.port;
				}
				catch (...)
				{
				}
			}
			BOOST_REQUIRE(server);
		}

		~ServerFixture()
		{
			for (auto& c : clients)
				c->rak.Disconnect(0);
		}

		Client& connect()
		{
			clients.emplace_back(new Client());
			Client& client = *clients.back();
			BOOST_REQUIRE(client.rak.Connect("127.0.0.1", port, 0, 0, RAKNET_THREAD_SLEEP_TIME));
			BOOST_REQUIRE(pump([&](){ return client.count(ID_CONNECTION_REQUEST_ACCEPTED) > 0; }));
			return client;
		}

		// runs the server loop until done is true, false after timeout_ms
		bool pump(const std::function<bool()>& done, int timeout_ms = 8000)
		{
			auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
			while (std::chrono::steady_clock::now() < end)
			{
				server->processPackets();
				server->updateGames();
				for (auto& c : clients)
					c->drain();
				if (done())
					return true;
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return false;
		}

		// runs the server loop for a while, so late packets would show
		void settle(int ms = 300)
		{
			pump([](){ return false; }, ms);
		}
	};

	const char* const MATCH_7 = "\t<match id=\"7\" player1=\"12\" player2=\"17\" bestOf=\"3\"/>\n";

	// the name the client was told it plays against, from the last ID_GAME_READY
	std::string gameReadyOpponentOf(const Client& client)
	{
		std::string opponent;
		for (const auto& p : client.packets)
		{
			if (p[0] != ID_GAME_READY)
				continue;
			RakNet::BitStream stream(const_cast<unsigned char*>(p.data()), p.size(), false);
			opponent = readGameReady(stream).opponent;
		}
		return opponent;
	}
}

BOOST_AUTO_TEST_SUITE( ServerHandoverTest )

BOOST_AUTO_TEST_CASE( code_entered_again_moves_to_the_new_connection_in_the_lobby )
{
	ServerFixture f("", true);
	Client& first = f.connect();
	first.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return first.hasLobby(LobbyPacketType::TOURNAMENT_STATUS); }));

	Client& second = f.connect();
	const std::size_t before = first.mark();
	second.enter(OLA);
	// the reason arrives before the connection closes
	BOOST_REQUIRE(f.pump([&](){ return second.hasLobby(LobbyPacketType::TOURNAMENT_STATUS)
		&& first.refusedWith(EntryRefusedReason::CODE_IN_USE); }));
	BOOST_REQUIRE(f.pump([&](){ return first.closed; }));
	BOOST_CHECK(!second.hasLobby(LobbyPacketType::ENTRY_REFUSED));

	// nothing for the new holder reaches the old connection
	f.settle();
	BOOST_CHECK_EQUAL(first.countSince(before, ID_LOBBY), 1);
	BOOST_CHECK_EQUAL(f.server->getWaitingPlayers(), 1);
	BOOST_CHECK_EQUAL(f.server->getConnectedClients(), 1);
}

BOOST_AUTO_TEST_CASE( handover_removes_the_open_game_of_the_old_holder )
{
	// a gate without matches keeps the casual lobby, where players open games
	ServerFixture f("", false);
	Client& first = f.connect();
	first.enter(OLA);
	Client& watcher = f.connect();
	watcher.enter(KARI);
	BOOST_REQUIRE(f.pump([&](){ return first.openGames() == 0 && watcher.openGames() == 0; }));

	first.sendLobby(LobbyPacketType::OPEN_GAME, [](GenericOut& out){
		out.uint32(0);
		out.uint32(15);
		out.uint32(0);
		out.string("");
	});
	BOOST_REQUIRE(f.pump([&](){ return watcher.openGames() == 1; }));

	Client& second = f.connect();
	second.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return first.refusedWith(EntryRefusedReason::CODE_IN_USE) && watcher.openGames() == 0; }));
	BOOST_REQUIRE(f.pump([&](){ return first.closed; }));
	BOOST_CHECK_EQUAL(f.server->getWaitingPlayers(), 2);
}

BOOST_AUTO_TEST_CASE( code_of_a_player_in_a_running_game_is_not_handed_over )
{
	ServerFixture f(MATCH_7, true);
	Client& ola = f.connect();
	ola.enter(OLA);
	Client& kari = f.connect();
	kari.enter(KARI);
	BOOST_REQUIRE(f.pump([&](){ return ola.count(ID_GAME_READY) > 0 && kari.count(ID_GAME_READY) > 0; }));

	// both players are clearly there, they send their input all the time
	ola.sendsInput = true;
	kari.sendsInput = true;
	f.settle();

	// the new connection is refused and the game goes on
	Client& again = f.connect();
	again.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return again.refusedWith(EntryRefusedReason::CODE_IN_USE); }));
	BOOST_REQUIRE(f.pump([&](){ return again.closed; }));
	f.settle();
	BOOST_CHECK(!ola.closed);
	BOOST_CHECK(!ola.hasLobby(LobbyPacketType::ENTRY_REFUSED));
	BOOST_CHECK_EQUAL(kari.count(ID_OPPONENT_DISCONNECTED), 0);
	BOOST_CHECK_EQUAL(kari.count(ID_PAUSE), 0);
	BOOST_CHECK(f.events->games.empty());

	// once the connection of the player is gone, the code is free again
	ola.rak.Disconnect(100);
	BOOST_REQUIRE(f.pump([&](){ return kari.count(ID_OPPONENT_DISCONNECTED) > 0 && !f.events->games.empty(); }));
	Client& back = f.connect();
	back.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return back.hasLobby(LobbyPacketType::TOURNAMENT_STATUS); }));
	BOOST_CHECK(!back.hasLobby(LobbyPacketType::ENTRY_REFUSED));

	// the game counts as left by that player, the series goes on
	BOOST_REQUIRE_EQUAL(f.events->games.size(), 1u);
	BOOST_CHECK(!f.events->games[0].finished);
	BOOST_CHECK_EQUAL(f.events->games[0].matchId, 7);
	BOOST_CHECK_EQUAL(f.events->games[0].leftPlayerId, 12);
}

BOOST_AUTO_TEST_CASE( new_connection_takes_the_place_of_a_silent_player_in_a_running_game )
{
	ServerFixture f(MATCH_7, true);
	Client& ola = f.connect();
	ola.enter(OLA);
	Client& kari = f.connect();
	kari.enter(KARI);
	ola.sendsInput = true;
	kari.sendsInput = true;
	BOOST_REQUIRE(f.pump([&](){ return ola.inGame && kari.inGame; }));
	f.settle();

	// the game of Ola stops sending, and Ola starts it again at once with the same code
	ola.sendsInput = false;
	Client& back = f.connect();
	back.sendsInput = true;
	back.enter(OLA);

	// the entry waits until the game paused for Ola, then the new connection takes Ola's place
	BOOST_REQUIRE(f.pump([&](){ return back.inGame; }));
	BOOST_CHECK(!back.hasLobby(LobbyPacketType::ENTRY_REFUSED));
	BOOST_CHECK(ola.refusedWith(EntryRefusedReason::CODE_IN_USE));
	BOOST_CHECK_EQUAL(kari.count(ID_PAUSE), 1);
	BOOST_CHECK(f.pump([&](){ return kari.count(ID_UNPAUSE) == 1; }));
	// the client learns it plays a tournament before the game reaches it
	BOOST_CHECK(back.hasLobby(LobbyPacketType::TOURNAMENT_STATUS));
	BOOST_CHECK_EQUAL(back.count(ID_RULES_CHECKSUM), 1);
	BOOST_CHECK_EQUAL(gameReadyOpponentOf(back), "Kari");
	BOOST_CHECK_EQUAL(kari.count(ID_GAME_READY), 1);

	// the game goes on with the new connection, the old one is closed
	BOOST_REQUIRE(f.pump([&](){ return ola.closed; }));
	const std::size_t before = back.mark();
	f.settle(1000);
	BOOST_CHECK_GT(back.countSince(before, ID_GAME_UPDATE), 10);
	BOOST_CHECK_EQUAL(kari.count(ID_OPPONENT_DISCONNECTED), 0);
	BOOST_CHECK_EQUAL(kari.count(ID_PAUSE), 1);
	BOOST_CHECK(f.events->games.empty());
	BOOST_CHECK_EQUAL(f.server->getActiveGamesCount(), 1);
}

BOOST_AUTO_TEST_CASE( code_of_a_player_paused_for_its_opponent_is_not_handed_over )
{
	ServerFixture f(MATCH_7, true);
	Client& ola = f.connect();
	ola.enter(OLA);
	Client& kari = f.connect();
	kari.enter(KARI);
	ola.sendsInput = true;
	kari.sendsInput = true;
	BOOST_REQUIRE(f.pump([&](){ return ola.inGame && kari.inGame; }));

	// Kari goes silent and Ola is paused. A client in the pause sends nothing, but it is still there.
	kari.sendsInput = false;
	BOOST_REQUIRE(f.pump([&](){ return ola.count(ID_PAUSE) == 1; }));
	ola.sendsInput = false;
	f.settle(500);

	Client& again = f.connect();
	again.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return again.refusedWith(EntryRefusedReason::CODE_IN_USE); }));
	f.settle();
	BOOST_CHECK(!ola.hasLobby(LobbyPacketType::ENTRY_REFUSED));
	BOOST_CHECK(!ola.closed);
}

BOOST_AUTO_TEST_CASE( code_entered_twice_quickly_ends_with_one_connection )
{
	ServerFixture f("", true);
	Client& first = f.connect();
	first.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return first.hasLobby(LobbyPacketType::TOURNAMENT_STATUS); }));

	Client& second = f.connect();
	Client& third = f.connect();
	second.enter(OLA);
	third.enter(OLA);
	// the entries arrive in either order. One of the two gets the code, the other is refused.
	BOOST_REQUIRE(f.pump([&](){ return first.refusedWith(EntryRefusedReason::CODE_IN_USE)
		&& (second.refusedWith(EntryRefusedReason::CODE_IN_USE) != third.refusedWith(EntryRefusedReason::CODE_IN_USE)); }));
	Client& loser = second.refusedWith(EntryRefusedReason::CODE_IN_USE) ? second : third;
	Client& holder = &loser == &second ? third : second;
	BOOST_REQUIRE(f.pump([&](){ return first.closed && loser.closed && holder.hasLobby(LobbyPacketType::TOURNAMENT_STATUS); }));
	f.settle();

	BOOST_CHECK(!holder.hasLobby(LobbyPacketType::ENTRY_REFUSED));
	BOOST_CHECK(!holder.closed);
	BOOST_CHECK_EQUAL(f.server->getWaitingPlayers(), 1);
	BOOST_CHECK_EQUAL(f.server->getConnectedClients(), 1);

	// another code is not touched
	Client& per = f.connect();
	per.enter(PER);
	BOOST_REQUIRE(f.pump([&](){ return per.hasLobby(LobbyPacketType::TOURNAMENT_STATUS); }));
	BOOST_CHECK(!holder.closed);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( NetworkGameStartTest )

namespace
{
	// a bare server with two connected clients, so a test decides when a game starts
	struct BareServerFixture
	{
		FileSystem fs;
		ThreadSafeRakServer server;
		Client left;
		Client right;
		std::vector<PlayerID> ids;
		// gets the packets of the clients, as the server does for a running game
		std::shared_ptr<NetworkGame> game;

		BareServerFixture() : fs("./test")
		{
			fs.addToSearchPath(BLOBBY_TEST_DATA_DIR);
			unsigned short port = 0;
			for (int attempt = 0; attempt < 50 && port == 0; ++attempt)
			{
				unsigned short candidate = freeTestPort();
				if (candidate == 0)
					continue;
				if (server.access([&](RakServer& srv){ return srv.Start(4, 1, candidate); }))
					port = candidate;
			}
			BOOST_REQUIRE(port != 0);

			BOOST_REQUIRE(left.rak.Connect("127.0.0.1", port, 0, 0, RAKNET_THREAD_SLEEP_TIME));
			BOOST_REQUIRE(right.rak.Connect("127.0.0.1", port, 0, 0, RAKNET_THREAD_SLEEP_TIME));
			// the ids of the clients as the server sees them
			BOOST_REQUIRE(pump([&](){ return ids.size() == 2 && left.count(ID_CONNECTION_REQUEST_ACCEPTED) > 0
				&& right.count(ID_CONNECTION_REQUEST_ACCEPTED) > 0; }, 8000));
		}

		~BareServerFixture()
		{
			if (game)
				game->finish();
			left.rak.Disconnect(0);
			right.rak.Disconnect(0);
			server.access([](RakServer& srv){ srv.Disconnect(0); });
		}

		bool pump(const std::function<bool()>& done, int timeout_ms)
		{
			auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
			while (std::chrono::steady_clock::now() < end)
			{
				while (packet_ptr packet = server.access([](RakServer& srv){ return srv.Receive(); }))
				{
					if (packet->data[0] == ID_NEW_INCOMING_CONNECTION)
						ids.push_back(packet->playerId);
					else if (game)
						game->injectPacket(packet);
				}
				left.drain();
				right.drain();
				if (done())
					return true;
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return false;
		}
	};
}

BOOST_FIXTURE_TEST_CASE( rules_checksum_goes_out_when_the_game_starts, BareServerFixture )
{
	NetworkPlayer leftPlayer(ids[0], "left", Color(255, 0, 0), LEFT_PLAYER);
	NetworkPlayer rightPlayer(ids[1], "right", Color(0, 0, 255), RIGHT_PLAYER);
	game = std::make_shared<NetworkGame>(&server, leftPlayer, rightPlayer, false, false, "default.lua", 15, 75.f);

	// nothing goes out before start, so an answer cannot come before the players know the game
	pump([](){ return false; }, 500);
	BOOST_CHECK_EQUAL(left.count(ID_RULES_CHECKSUM) + right.count(ID_RULES_CHECKSUM), 0);

	game->start();
	BOOST_CHECK(pump([&](){ return left.count(ID_RULES_CHECKSUM) == 1 && right.count(ID_RULES_CHECKSUM) == 1; }, 8000));

	// a second start does nothing
#ifdef NDEBUG
	game->start();
	pump([](){ return false; }, 300);
	BOOST_CHECK_EQUAL(left.count(ID_RULES_CHECKSUM), 1);
#endif
	game->finish();
}

BOOST_FIXTURE_TEST_CASE( game_ends_when_a_player_never_answers, BareServerFixture )
{
	NetworkPlayer leftPlayer(ids[0], "left", Color(255, 0, 0), LEFT_PLAYER);
	NetworkPlayer rightPlayer(ids[1], "right", Color(0, 0, 255), RIGHT_PLAYER);
	right.checksumsToIgnore = 100;
	game = std::make_shared<NetworkGame>(&server, leftPlayer, rightPlayer, false, false, "default.lua", 15, 75.f);
	game->setRulesChecksumRepeat(std::chrono::milliseconds(200), 3);
	game->start();

	BOOST_REQUIRE(pump([&](){ return !game->isGameValid(); }, 8000));
	BOOST_CHECK(game->didNotStart());
	// the silent player was asked three times, the other one once, and learns the game is off
	BOOST_CHECK(pump([&](){ return left.count(ID_OPPONENT_DISCONNECTED) == 1; }, 2000));
	BOOST_CHECK_EQUAL(right.count(ID_RULES_CHECKSUM), 3);
	BOOST_CHECK_EQUAL(left.count(ID_RULES_CHECKSUM), 1);
	BOOST_CHECK_EQUAL(right.count(ID_OPPONENT_DISCONNECTED), 0);
	BOOST_CHECK_EQUAL(left.count(ID_GAME_READY) + right.count(ID_GAME_READY), 0);
	game->finish();
}

BOOST_AUTO_TEST_CASE( game_starts_when_the_first_checksum_gets_no_answer )
{
	ServerFixture f(MATCH_7, true);
	Client& ola = f.connect();
	ola.checksumsToIgnore = 1;
	ola.enter(OLA);
	Client& kari = f.connect();
	kari.enter(KARI);

	// the server asks again after a few seconds, the client that answered is not asked twice
	BOOST_REQUIRE(f.pump([&](){ return ola.count(ID_GAME_READY) > 0 && kari.count(ID_GAME_READY) > 0; }));
	BOOST_CHECK_EQUAL(ola.count(ID_RULES_CHECKSUM), 2);
	BOOST_CHECK_EQUAL(kari.count(ID_RULES_CHECKSUM), 1);
	BOOST_CHECK_EQUAL(ola.count(ID_GAME_READY), 1);
	BOOST_CHECK_EQUAL(kari.count(ID_GAME_READY), 1);
}

namespace
{
	// Scores a point on every step: 15-15 after 30 steps, then 16-15 and 17-15.
	const char* const SCRIPTED_RULES = R"(
__AUTHOR__ = "test"
__TITLE__ = "scripted points"
steps = 0
function IsWinning(lscore, rscore)
	if lscore >= SCORE_TO_WIN and lscore >= rscore + 2 then
		return true
	end
	if rscore >= SCORE_TO_WIN and rscore >= lscore + 2 then
		return true
	end
	return false
end
function OnGame()
	steps = steps + 1
	if steps <= 30 and steps % 2 == 0 then
		score(RIGHT_PLAYER, 1)
	else
		score(LEFT_PLAYER, 1)
	end
end
)";

	void writeScriptedRules(FileSystem& fs)
	{
		fs.setWriteDir(testDir());
		fs.mkdir("rules");
		FileWrite file("rules/scripted_points.lua");
		file.write(SCRIPTED_RULES, std::char_traits<char>::length(SCRIPTED_RULES));
		file.close();
	}

	// the first packet of a type that a client got
	std::vector<unsigned char> firstPacket(const Client& client, unsigned char id)
	{
		for (const auto& p : client.packets)
		{
			if (p[0] == id)
				return p;
		}
		BOOST_FAIL("packet not received");
		return {};
	}
}

BOOST_FIXTURE_TEST_CASE( game_ends_at_17_15_and_the_win_notification_has_the_final_score, BareServerFixture )
{
	writeScriptedRules(fs);
	NetworkPlayer leftPlayer(ids[0], "left", Color(255, 0, 0), LEFT_PLAYER);
	NetworkPlayer rightPlayer(ids[1], "right", Color(0, 0, 255), LEFT_PLAYER);
	// the right player sees itself on the left
	game = std::make_shared<NetworkGame>(&server, leftPlayer, rightPlayer, false, true, "scripted_points.lua", 15, 75.f);
	game->start();

	BOOST_REQUIRE(pump([&](){ return left.count(ID_WIN_NOTIFICATION) > 0 && right.count(ID_WIN_NOTIFICATION) > 0; }, 8000));
	// a little longer, to see that the game stays over
	pump([](){ return false; }, 300);

	// 16-15 did not end the game
	BOOST_CHECK_EQUAL(game->getWinner(), LEFT_PLAYER);
	BOOST_CHECK_EQUAL(game->getScore(LEFT_PLAYER), 17);
	BOOST_CHECK_EQUAL(game->getScore(RIGHT_PLAYER), 15);
	BOOST_CHECK_EQUAL(left.count(ID_WIN_NOTIFICATION), 1);
	BOOST_CHECK_EQUAL(right.count(ID_WIN_NOTIFICATION), 1);

	// the last game update is sent once and unreliably, so the final score is in the reliable notification too
	std::vector<unsigned char> leftPacket = firstPacket(left, ID_WIN_NOTIFICATION);
	RakNet::BitStream leftWin(leftPacket.data(), leftPacket.size(), false);
	WinNotificationData leftData;
	BOOST_REQUIRE(readWinNotification(leftWin, leftData));
	BOOST_CHECK_EQUAL(leftData.winner, LEFT_PLAYER);
	BOOST_REQUIRE(leftData.hasScore);
	BOOST_CHECK_EQUAL(leftData.leftScore, 17);
	BOOST_CHECK_EQUAL(leftData.rightScore, 15);

	std::vector<unsigned char> rightPacket = firstPacket(right, ID_WIN_NOTIFICATION);
	RakNet::BitStream rightWin(rightPacket.data(), rightPacket.size(), false);
	WinNotificationData rightData;
	BOOST_REQUIRE(readWinNotification(rightWin, rightData));
	BOOST_CHECK_EQUAL(rightData.winner, RIGHT_PLAYER);
	BOOST_REQUIRE(rightData.hasScore);
	BOOST_CHECK_EQUAL(rightData.leftScore, 15);
	BOOST_CHECK_EQUAL(rightData.rightScore, 17);
	game->finish();
}

namespace
{
	// Scores a point for the left player every ten steps and never ends.
	const char* const STEADY_RULES = R"(
__AUTHOR__ = "test"
__TITLE__ = "steady points"
steps = 0
function IsWinning(lscore, rscore)
	return false
end
function OnGame()
	steps = steps + 1
	if steps % 10 == 0 then
		score(LEFT_PLAYER, 1)
	end
end
)";

	void writeSteadyRules(FileSystem& fs)
	{
		fs.setWriteDir(testDir());
		fs.mkdir("rules");
		FileWrite file("rules/steady_points.lua");
		file.write(STEADY_RULES, std::char_traits<char>::length(STEADY_RULES));
		file.close();
	}
}

BOOST_FIXTURE_TEST_CASE( silent_player_pauses_the_game_until_it_is_back, BareServerFixture )
{
	writeSteadyRules(fs);
	NetworkPlayer leftPlayer(ids[0], "left", Color(255, 0, 0), LEFT_PLAYER);
	NetworkPlayer rightPlayer(ids[1], "right", Color(0, 0, 255), RIGHT_PLAYER);
	game = std::make_shared<NetworkGame>(&server, leftPlayer, rightPlayer, false, false, "steady_points.lua", 15, 75.f);
	game->setPauseAllowed(false);
	game->setSilenceLimit(std::chrono::milliseconds(300));
	left.sendsInput = true;
	right.sendsInput = true;
	game->start();
	BOOST_REQUIRE(pump([&](){ return game->getScore(LEFT_PLAYER) >= 3; }, 8000));
	BOOST_CHECK(!game->isWaitingForPlayer());

	// the right player sends nothing anymore, the game pauses and only the left player is told
	right.sendsInput = false;
	BOOST_REQUIRE(pump([&](){ return game->isWaitingForPlayer() && left.count(ID_PAUSE) == 1; }, 3000));
	BOOST_CHECK(game->isMissing(RIGHT_PLAYER));
	BOOST_CHECK(!game->isMissing(LEFT_PLAYER));
	BOOST_CHECK_EQUAL(right.count(ID_PAUSE), 0);

	// no points while the game waits, and the left player cannot end the pause
	pump([](){ return false; }, 200);
	const int paused = game->getScore(LEFT_PLAYER);
	RakNet::BitStream unpause;
	unpause.Write((unsigned char)ID_UNPAUSE);
	left.rak.Send(&unpause, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
	pump([](){ return false; }, 1500);
	BOOST_CHECK_EQUAL(game->getScore(LEFT_PLAYER), paused);
	BOOST_CHECK(game->isWaitingForPlayer());
	BOOST_CHECK_EQUAL(left.count(ID_UNPAUSE), 0);

	// the player is back, the game goes on
	right.sendsInput = true;
	BOOST_REQUIRE(pump([&](){ return !game->isWaitingForPlayer() && left.count(ID_UNPAUSE) == 1; }, 3000));
	BOOST_CHECK(!game->isMissing(RIGHT_PLAYER));
	BOOST_CHECK(pump([&](){ return game->getScore(LEFT_PLAYER) >= paused + 3; }, 3000));
	BOOST_CHECK(game->isGameValid());
	game->finish();
}

BOOST_FIXTURE_TEST_CASE( game_without_a_silence_limit_never_waits, BareServerFixture )
{
	writeSteadyRules(fs);
	NetworkPlayer leftPlayer(ids[0], "left", Color(255, 0, 0), LEFT_PLAYER);
	NetworkPlayer rightPlayer(ids[1], "right", Color(0, 0, 255), RIGHT_PLAYER);
	game = std::make_shared<NetworkGame>(&server, leftPlayer, rightPlayer, false, false, "steady_points.lua", 15, 75.f);
	game->start();
	BOOST_REQUIRE(pump([&](){ return left.inGame && right.inGame; }, 8000));
	pump([](){ return false; }, 1000);
	BOOST_CHECK(!game->isWaitingForPlayer());
	BOOST_CHECK_EQUAL(left.count(ID_PAUSE) + right.count(ID_PAUSE), 0);
	game->finish();
}

BOOST_FIXTURE_TEST_CASE( rules_checksum_has_the_points_of_the_match_when_the_rules_change_them, BareServerFixture )
{
	NetworkPlayer leftPlayer(ids[0], "left", Color(255, 0, 0), LEFT_PLAYER);
	NetworkPlayer rightPlayer(ids[1], "right", Color(0, 0, 255), RIGHT_PLAYER);
	// firewall plays to ten times the points of the match. The client applies the rules file itself.
	game = std::make_shared<NetworkGame>(&server, leftPlayer, rightPlayer, false, false, "firewall.lua", 15, 75.f);
	game->start();
	BOOST_REQUIRE(pump([&](){ return left.count(ID_RULES_CHECKSUM) > 0 && right.count(ID_RULES_CHECKSUM) > 0; }, 8000));

	for (Client* client : {&left, &right})
	{
		std::vector<unsigned char> packet = firstPacket(*client, ID_RULES_CHECKSUM);
		RakNet::BitStream stream(packet.data(), packet.size(), false);
		RulesChecksumData rules;
		BOOST_CHECK(readRulesChecksum(stream, rules));
		BOOST_CHECK_EQUAL(rules.scoreToWin, 15);
	}
	game->finish();
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( TrainingServerTest )

namespace
{
	// the name the client was told it plays against, from the last ID_GAME_READY
	std::string gameReadyOpponent(const Client& client)
	{
		std::string opponent;
		for (const auto& p : client.packets)
		{
			if (p[0] != ID_GAME_READY)
				continue;
			RakNet::BitStream stream(const_cast<unsigned char*>(p.data()), p.size(), false);
			opponent = readGameReady(stream).opponent;
		}
		return opponent;
	}
}

BOOST_AUTO_TEST_CASE( server_bot_plays_a_training_match_to_the_end )
{
	ServerFixture f("\t<match id=\"9\" player1=\"12\" bot=\"reduced\" botDifficulty=\"0\" scoreToWin=\"1\"/>\n", true);
	Client& ola = f.connect();
	ola.enter(OLA);

	// the game starts with one player, against the bot of the server
	BOOST_REQUIRE(f.pump([&](){ return ola.count(ID_GAME_READY) > 0; }));
	BOOST_CHECK_EQUAL(gameReadyOpponent(ola), "Bot reduced");
	BOOST_REQUIRE_EQUAL(f.events->connected.size(), 1u);
	BOOST_CHECK_EQUAL(f.events->connected[0].player1, 12);
	BOOST_CHECK_EQUAL(f.events->connected[0].player2, 0);

	// the player only jumps, which serves the ball, and the bot of the server plays it
	BOOST_REQUIRE(f.pump([&](){
		RakNet::BitStream input;
		input.Write((unsigned char)ID_INPUT_UPDATE);
		input.Write((unsigned)1);
		PlayerInputAbs(false, false, true).writeTo(input);
		ola.rak.Send(&input, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
		return !f.events->games.empty();
	}, 90000));
	const GameReport& report = f.events->games[0];
	BOOST_CHECK(report.training);
	BOOST_CHECK(report.finished);
	BOOST_CHECK(report.winnerId == 0 || report.winnerId == 12);
	// the default rules need a lead of two points
	const int winner = report.winnerId == 0 ? report.points2 : report.points1;
	const int loser = report.winnerId == 0 ? report.points1 : report.points2;
	BOOST_CHECK_GE(winner, 2);
	BOOST_CHECK_EQUAL(winner - loser, 2);
	BOOST_CHECK(f.pump([&](){ return ola.count(ID_WIN_NOTIFICATION) == 1; }));
	BOOST_REQUIRE_EQUAL(f.events->series.size(), 1u);
	BOOST_CHECK_EQUAL(f.events->series[0].winnerId, report.winnerId);
}

BOOST_AUTO_TEST_CASE( player_who_leaves_abandons_the_training )
{
	ServerFixture f("\t<match id=\"9\" player1=\"12\" bot=\"reduced\" scoreToWin=\"15\"/>\n", true);
	Client& ola = f.connect();
	ola.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return ola.count(ID_GAME_READY) > 0; }));
	f.settle(500);

	ola.rak.Disconnect(100);
	BOOST_REQUIRE(f.pump([&](){ return !f.events->games.empty(); }));
	const GameReport& report = f.events->games[0];
	BOOST_CHECK(report.training);
	BOOST_CHECK(!report.finished);
	BOOST_CHECK_EQUAL(report.leftPlayerId, 12);
}

BOOST_AUTO_TEST_CASE( server_bot_never_pauses_a_training )
{
	ServerFixture f("\t<match id=\"9\" player1=\"12\" bot=\"reduced\" scoreToWin=\"15\"/>\n", true);
	Client& ola = f.connect();
	ola.sendsInput = true;
	ola.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return ola.inGame; }));

	// the bot has no connection and sends nothing, but only the player counts
	f.settle(3000);
	BOOST_CHECK_EQUAL(ola.count(ID_PAUSE), 0);
	BOOST_CHECK(f.events->games.empty());
	BOOST_CHECK_EQUAL(f.server->getActiveGamesCount(), 1);
}

BOOST_AUTO_TEST_CASE( unknown_bot_script_does_not_start_a_game )
{
	ServerFixture f("\t<match id=\"9\" player1=\"12\" bot=\"nosuchbot\"/>\n", true);
	Client& ola = f.connect();
	ola.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return ola.hasLobby(LobbyPacketType::TOURNAMENT_STATUS); }));
	f.settle(1000);
	BOOST_CHECK_EQUAL(ola.count(ID_RULES_CHECKSUM), 0);
	BOOST_CHECK_EQUAL(ola.count(ID_GAME_READY), 0);
	BOOST_CHECK(f.events->games.empty());
	BOOST_CHECK_EQUAL(f.server->getActiveGamesCount(), 0);
}

BOOST_AUTO_TEST_CASE( server_runs_a_normal_match_next_to_a_training )
{
	ServerFixture f("\t<match id=\"9\" player1=\"12\" bot=\"reduced\"/>\n"
		"\t<match id=\"10\" player1=\"17\" player2=\"21\"/>\n", true);
	Client& ola = f.connect();
	ola.enter(OLA);
	Client& kari = f.connect();
	kari.enter(KARI);
	Client& per = f.connect();
	per.enter(PER);
	BOOST_REQUIRE(f.pump([&](){ return ola.count(ID_GAME_READY) > 0 && kari.count(ID_GAME_READY) > 0 && per.count(ID_GAME_READY) > 0; }));
	BOOST_CHECK_EQUAL(gameReadyOpponent(ola), "Bot reduced");
	BOOST_CHECK_EQUAL(gameReadyOpponent(kari), "Per");
	BOOST_CHECK_EQUAL(f.server->getActiveGamesCount(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
