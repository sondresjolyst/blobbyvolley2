#include <boost/test/unit_test.hpp>

#include <chrono>
#include <functional>
#include <random>
#include <thread>

#include "raknet/GetTime.h"
#include "raknet/PacketEnumerations.h"
#include "raknet/PeerTimers.h"
#include "raknet/RakClient.h"
#include "raknet/RakServer.h"
#include "raknet/ReliabilityLayer.h"
#include "Global.h"
#include "TestClock.h"

// These tests run a server and a client over the loopback interface on a clock that wraps
// during the test, so they check the time code where RakPeer calls it.

namespace
{
	// the clock is set this far before the wrap when the server starts
	const unsigned int START_BEFORE_WRAP = 3000;

	// a server that lets the tests read and change the record of its client
	struct OpenServer : public RakServer
	{
		RemoteSystemStruct* client()
		{
			for (unsigned i = 0; i < remoteSystemListSize; ++i)
				if (remoteSystemList[i].playerId != UNASSIGNED_PLAYER_ID)
					return remoteSystemList + i;
			return nullptr;
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

	struct WrappingClockFixture
	{
		OpenServer server;
		RakClient client;
		PlayerID clientId = UNASSIGNED_PLAYER_ID;

		WrappingClockFixture()
		{
			setRakNetTime(0u - START_BEFORE_WRAP);

			static std::mt19937 random{std::random_device{}()};
			std::uniform_int_distribution<int> ports(20000, 32000);
			unsigned short port = 0;
			for (int attempt = 0; attempt < 50 && port == 0; ++attempt)
			{
				unsigned short candidate = (unsigned short)ports(random);
				if (server.Start(4, 1, candidate))
					port = candidate;
			}
			BOOST_REQUIRE(port != 0);
			BOOST_REQUIRE(client.Connect("127.0.0.1", port, 0, 0, RAKNET_THREAD_SLEEP_TIME));

			bool accepted = false;
			bool incoming = false;
			BOOST_REQUIRE(waitFor([&]()
			{
				while (packet_ptr packet = client.Receive())
					if (packet->data[0] == ID_CONNECTION_REQUEST_ACCEPTED)
						accepted = true;
				while (packet_ptr packet = server.Receive())
					if (packet->data[0] == ID_NEW_INCOMING_CONNECTION)
					{
						incoming = true;
						clientId = packet->playerId;
					}
				return accepted && incoming;
			}, 2000));
			BOOST_REQUIRE(server.client() != nullptr);
			// the clock has not reached the wrap yet
			BOOST_REQUIRE(RakNet::GetTime() > 0x80000000u);
		}

		~WrappingClockFixture()
		{
			client.Disconnect(0);
			server.Disconnect(0);
			resetRakNetTime();
		}

		// true when the record of the client says a keepalive went out, which sets the last
		// reliable send ahead of now
		bool keepaliveSent()
		{
			RakPeer::RemoteSystemStruct* record = server.client();
			return record && RakNet::TimeElapsed(RakNet::GetTime(), record->lastReliableSend) > (int)TIMEOUT_TIME / 2;
		}
	};
}

BOOST_FIXTURE_TEST_SUITE( rakpeer_time, WrappingClockFixture )

BOOST_AUTO_TEST_CASE( keepalive_goes_out_after_five_quiet_seconds_across_the_wrap )
{
	// let the reliable messages of the connect get their acks
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	BOOST_REQUIRE(!keepaliveSent());

	// five quiet seconds later the clock has wrapped
	advanceRakNetTime(5100);
	BOOST_CHECK(RakNet::GetTime() < 0x80000000u);
	BOOST_CHECK(waitFor([&](){ return keepaliveSent(); }, 2000));
}

BOOST_AUTO_TEST_CASE( scheduled_ping_waits_for_its_time_across_the_wrap )
{
	// the server pings twice when the client connects, so let both answers arrive first
	BOOST_REQUIRE(waitFor([&](){ return server.client()->lowestPing != -1; }, 2000));
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	// a ping that never got an answer is sent again 3 s later, after the wrap
	RakPeer::RemoteSystemStruct* record = server.client();
	record->nextPingTime = RakNet::GetTime() + 3000;
	record->lowestPing = -1;
	// without a last ping the answer is never skipped as a spike
	record->pingTime = -1;
	BOOST_REQUIRE(record->nextPingTime < 0x80000000u);

	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	BOOST_CHECK_EQUAL(record->lowestPing, -1);

	advanceRakNetTime(3100);
	BOOST_CHECK(waitFor([&](){ return record->lowestPing != -1; }, 2000));
}

BOOST_AUTO_TEST_CASE( disconnect_waits_for_its_notification_across_the_wrap )
{
	// the server goes away without a word, so the notification of the client never gets its ack
	server.Disconnect(0);

	// the wait of the client ends after the wrap
	setRakNetTime(0u - 100);
	auto start = std::chrono::steady_clock::now();
	client.Disconnect(800);
	auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	BOOST_CHECK_GE(waited, 700);
	BOOST_CHECK_LT(waited, 5000);
}

BOOST_AUTO_TEST_CASE( long_connected_system_that_starts_to_close_is_still_dropped )
{
	// as if the client connected more than 2^31 ms ago
	RakPeer::RemoteSystemStruct* record = server.client();
	record->connectionTime = RakNet::GetTime() - 0x80000100u;

	// while it is connected the record follows the clock
	BOOST_CHECK(waitFor([&]()
	{
		int age = RakNet::TimeElapsed(record->connectionTime, RakNet::GetTime());
		return age >= 0 && age < 1000;
	}, 1000));

	// the client goes away without a word and the server closes the connection
	client.Disconnect(0);
	server.Kick(clientId);
	BOOST_REQUIRE(waitFor([&](){ return record->connectMode == RakPeer::RemoteSystemStruct::DISCONNECT_ASAP; }, 1000));
	// so its notification waits for an ack that never comes
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	BOOST_REQUIRE(server.client() != nullptr);

	// the limit of 10 s counts from when it began to close
	advanceRakNetTime(10100);
	BOOST_CHECK(waitFor([&](){ return server.client() == nullptr; }, 2000));
}

BOOST_AUTO_TEST_SUITE_END()
