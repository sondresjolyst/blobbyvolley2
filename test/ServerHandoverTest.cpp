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
#include "GenericIO.h"
#include "Global.h"
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "raknet/PacketEnumerations.h"
#include "raknet/RakClient.h"
#include "server/DedicatedServer.h"
#include "server/JoinCodes.h"
#include "server/Tournament.h"

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

	struct RecordingEvents : public TournamentEvents
	{
		std::vector<GameReport> games;
		void matchConnected(int, int, int) override {}
		void gameEnded(const GameReport& report) override { games.push_back(report); }
		void seriesEnded(const SeriesResult&) override {}
	};

	struct Client
	{
		RakClient rak;
		std::vector<std::vector<unsigned char>> packets;
		bool closed = false;

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
				if (data[0] == ID_RULES_CHECKSUM)
				{
					RakNet::BitStream answer;
					answer.Write((unsigned char)ID_RULES);
					answer.Write(false);
					rak.Send(&answer, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
				}
				packets.push_back(std::move(data));
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

		// the server cannot bind a port in use, so it tries random ports until one is free
		void startOnFreePort()
		{
			std::mt19937 random{std::random_device{}()};
			std::uniform_int_distribution<int> ports(20000, 60000);
			for (int attempt = 0; attempt < 50 && !server; ++attempt)
			{
				ServerInfo info("test");
				info.port = (unsigned short)ports(random);
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

	// the new connection is refused and the game goes on
	Client& again = f.connect();
	again.enter(OLA);
	BOOST_REQUIRE(f.pump([&](){ return again.refusedWith(EntryRefusedReason::CODE_IN_USE); }));
	BOOST_REQUIRE(f.pump([&](){ return again.closed; }));
	f.settle();
	BOOST_CHECK(!ola.closed);
	BOOST_CHECK(!ola.hasLobby(LobbyPacketType::ENTRY_REFUSED));
	BOOST_CHECK_EQUAL(kari.count(ID_OPPONENT_DISCONNECTED), 0);
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
