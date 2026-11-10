#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <thread>

#include "raknet/BitStream.h"
#include "raknet/PacketEnumerations.h"
#include "raknet/RakClient.h"
#include "raknet/RakServer.h"
#include "Global.h"

// A client dials a port that a relay forwards to the server, as a router or a container does
// when the outside port differs from the port the server listens on.

namespace
{
#ifdef _WIN32
	typedef SOCKET Socket;
	void closeSocket(Socket s) { closesocket(s); }
#else
	typedef int Socket;
	void closeSocket(Socket s) { close(s); }
#endif

	const unsigned char TEST_MESSAGE = ID_RESERVED9 + 1;

	sockaddr_in loopback(unsigned short port)
	{
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(port);
		return address;
	}

	unsigned short boundPort(Socket s)
	{
		sockaddr_in address{};
		socklen_t length = sizeof(address);
		getsockname(s, (sockaddr*)&address, &length);
		return ntohs(address.sin_port);
	}

	// a plain UDP socket on a free port from 20000 to 32000, or on any port for 0
	Socket bindSocket(bool randomPort)
	{
		static std::mt19937 random{std::random_device{}()};
		std::uniform_int_distribution<int> ports(20000, 32000);
		for (int attempt = 0; attempt < 200; ++attempt)
		{
			Socket s = socket(AF_INET, SOCK_DGRAM, 0);
			sockaddr_in address = loopback(randomPort ? (unsigned short)ports(random) : 0);
			if (bind(s, (sockaddr*)&address, sizeof(address)) == 0)
				return s;
			closeSocket(s);
		}
		BOOST_FAIL("no free port");
		return 0;
	}

	// Forwards each datagram from the front port to the server and each answer back to the
	// last sender on the front port.
	struct Relay
	{
		Socket front;
		Socket back;
		sockaddr_in server;
		std::atomic<bool> stop{false};
		std::thread worker;

		explicit Relay(unsigned short serverPort) : server(loopback(serverPort))
		{
			front = bindSocket(true);
			back = bindSocket(false);
			worker = std::thread([this](){ run(); });
		}

		~Relay()
		{
			stop = true;
			worker.join();
			closeSocket(front);
			closeSocket(back);
		}

		void run()
		{
			sockaddr_in client{};
			bool haveClient = false;
			char buffer[2048];
			while (!stop)
			{
				fd_set sockets;
				FD_ZERO(&sockets);
				FD_SET(front, &sockets);
				FD_SET(back, &sockets);
				timeval wait{0, 20000};
				int highest = (int)(front > back ? front : back);
				if (select(highest + 1, &sockets, nullptr, nullptr, &wait) <= 0)
					continue;

				if (FD_ISSET(front, &sockets))
				{
					sockaddr_in from{};
					socklen_t length = sizeof(from);
					int size = recvfrom(front, buffer, sizeof(buffer), 0, (sockaddr*)&from, &length);
					if (size > 0)
					{
						client = from;
						haveClient = true;
						sendto(back, buffer, size, 0, (sockaddr*)&server, sizeof(server));
					}
				}
				if (FD_ISSET(back, &sockets))
				{
					int size = recvfrom(back, buffer, sizeof(buffer), 0, nullptr, nullptr);
					if (size > 0 && haveClient)
						sendto(front, buffer, size, 0, (sockaddr*)&client, sizeof(client));
				}
			}
		}
	};

	bool waitFor(const std::function<bool()>& done, int timeout_ms)
	{
		auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
		while (std::chrono::steady_clock::now() < end)
		{
			if (done())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return done();
	}

	unsigned short startServer(RakServer& server)
	{
		// RakNet binds with SO_REUSEADDR, so a plain socket checks that the port is free
		for (int attempt = 0; attempt < 50; ++attempt)
		{
			Socket probe = bindSocket(true);
			unsigned short port = boundPort(probe);
			closeSocket(probe);
			if (server.Start(4, 1, port))
				return port;
		}
		return 0;
	}

	struct ForwardedServerFixture
	{
		RakServer server;
		RakClient client;
		unsigned short serverPort = 0;
		std::unique_ptr<Relay> relay;

		ForwardedServerFixture()
		{
#ifdef _WIN32
			WSADATA data;
			WSAStartup(MAKEWORD(2, 2), &data);
#endif
			serverPort = startServer(server);
			BOOST_REQUIRE(serverPort != 0);
			relay.reset(new Relay(serverPort));
			BOOST_REQUIRE(boundPort(relay->front) != serverPort);
		}

		~ForwardedServerFixture()
		{
			client.Disconnect(0);
			server.Disconnect(0);
			relay.reset();
#ifdef _WIN32
			WSACleanup();
#endif
		}
	};
}

BOOST_FIXTURE_TEST_SUITE( rakpeer_port_forward, ForwardedServerFixture )

BOOST_AUTO_TEST_CASE( client_connects_through_a_forwarded_port )
{
	const unsigned short frontPort = boundPort(relay->front);
	BOOST_REQUIRE(client.Connect("127.0.0.1", frontPort, 0, 0, RAKNET_THREAD_SLEEP_TIME));

	PlayerID acceptedFrom = UNASSIGNED_PLAYER_ID;
	bool incoming = false;
	bool serverGotMessage = false;
	bool clientGotMessage = false;
	PlayerID clientId = UNASSIGNED_PLAYER_ID;
	BOOST_CHECK(waitFor([&]()
	{
		while (packet_ptr packet = client.Receive())
		{
			if (packet->data[0] == ID_CONNECTION_REQUEST_ACCEPTED)
			{
				acceptedFrom = packet->playerId;
				RakNet::BitStream stream;
				stream.Write(TEST_MESSAGE);
				client.Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
			}
			else if (packet->data[0] == TEST_MESSAGE)
				clientGotMessage = true;
		}
		while (packet_ptr packet = server.Receive())
		{
			if (packet->data[0] == ID_NEW_INCOMING_CONNECTION)
			{
				incoming = true;
				clientId = packet->playerId;
				RakNet::BitStream stream;
				stream.Write(TEST_MESSAGE);
				server.Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, clientId, false);
			}
			else if (packet->data[0] == TEST_MESSAGE)
				serverGotMessage = true;
		}
		return incoming && serverGotMessage && clientGotMessage;
	}, 3000));

	BOOST_CHECK(incoming);
	BOOST_CHECK(serverGotMessage);
	BOOST_CHECK(clientGotMessage);
	// the client keeps the address it dialled
	BOOST_CHECK_EQUAL(acceptedFrom.port, frontPort);
	BOOST_CHECK_EQUAL(client.GetServerID().port, frontPort);
	// and learns its own address as the server sees it, which is the back of the relay
	BOOST_CHECK_EQUAL(client.GetPlayerID().port, boundPort(relay->back));
	BOOST_CHECK_EQUAL(clientId.port, boundPort(relay->back));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( rakpeer_player_id )

BOOST_AUTO_TEST_CASE( text_shows_the_address_in_order )
{
	PlayerID id;
	id.binaryAddress = inet_addr("172.18.0.6");
	id.port = 2400;
	BOOST_CHECK_EQUAL(id.toString(), "172.18.0.6:2400");
}

BOOST_AUTO_TEST_SUITE_END()
