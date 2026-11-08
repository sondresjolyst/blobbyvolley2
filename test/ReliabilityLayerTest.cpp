#include <boost/test/unit_test.hpp>

#include <vector>

#include "raknet/BitStream.h"
#include "raknet/GetTime.h"
#include "raknet/PeerTimers.h"
#include "raknet/SocketLayer.h"
#include "raknet/ReliabilityLayer.h"

namespace
{
	struct Part
	{
		unsigned char reliability;
		unsigned int splitPacketId;
		unsigned int splitPacketIndex;
		unsigned int splitPacketCount;
		std::vector<unsigned char> bytes;
	};

	struct ReliabilityLayerFixture
	{
		ReliabilityLayer layer;
		unsigned short packetNumber = 0;
		unsigned int now = 1000;

		// encodes one message in the same layout as WriteToBitStreamFromInternalPacket
		void receive(const Part& part, unsigned short bitLength, unsigned int bytesPresent)
		{
			RakNet::BitStream stream;
			stream.Write(packetNumber++);
			stream.Write(false);
			unsigned char reliability = part.reliability;
			stream.WriteBits(&reliability, 3, true);
			if (reliability == UNRELIABLE_SEQUENCED || reliability == RELIABLE_SEQUENCED || reliability == RELIABLE_ORDERED)
			{
				unsigned char channel = 0;
				stream.WriteBits(&channel, 5, true);
				stream.Write((unsigned char)0);
			}
			bool isSplit = part.splitPacketCount > 0;
			stream.Write(isSplit);
			if (isSplit)
			{
				stream.Write(part.splitPacketId);
				stream.WriteCompressed(part.splitPacketIndex);
				stream.WriteCompressed(part.splitPacketCount);
			}
			stream.WriteCompressed(bitLength);
			stream.WriteAlignedBytes(part.bytes.data(), bytesPresent);
			layer.HandleSocketReceiveFromConnectedPlayer((const char*)stream.GetData(), stream.GetNumberOfBytesUsed(), now);
		}

		void receive(const Part& part)
		{
			receive(part, (unsigned short)(part.bytes.size() * 8), part.bytes.size());
		}

		// returns all delivered messages
		std::vector<std::vector<unsigned char>> delivered()
		{
			std::vector<std::vector<unsigned char>> result;
			char* data = nullptr;
			int bits;
			while ((bits = layer.Receive(&data)) > 0)
			{
				result.emplace_back((unsigned char*)data, (unsigned char*)data + BITS_TO_BYTES(bits));
				delete[] data;
			}
			return result;
		}
	};

	std::vector<unsigned char> makeBytes(unsigned int count, unsigned char seed)
	{
		std::vector<unsigned char> bytes(count);
		for (unsigned int i = 0; i < count; ++i)
			bytes[i] = (unsigned char)(seed + i);
		return bytes;
	}

	Part makePart(unsigned char reliability, unsigned int id, unsigned int index, unsigned int count, const std::vector<unsigned char>& whole, unsigned int offset, unsigned int length)
	{
		Part part{reliability, id, index, count, std::vector<unsigned char>(whole.begin() + offset, whole.begin() + offset + length)};
		return part;
	}
}

BOOST_AUTO_TEST_SUITE( reliability_layer )

BOOST_FIXTURE_TEST_CASE( valid_split_packet_reassembles, ReliabilityLayerFixture )
{
	auto whole = makeBytes(25, 1);
	receive(makePart(UNRELIABLE, 7, 2, 3, whole, 20, 5));
	receive(makePart(UNRELIABLE, 7, 0, 3, whole, 0, 10));
	BOOST_CHECK(delivered().empty());
	receive(makePart(UNRELIABLE, 7, 1, 3, whole, 10, 10));

	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK(result[0] == whole);
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( valid_sequenced_split_packet_reassembles, ReliabilityLayerFixture )
{
	auto whole = makeBytes(15, 3);
	receive(makePart(UNRELIABLE_SEQUENCED, 4, 1, 2, whole, 10, 5));
	receive(makePart(UNRELIABLE_SEQUENCED, 4, 0, 2, whole, 0, 10));

	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK(result[0] == whole);
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( split_index_at_or_above_count_is_dropped, ReliabilityLayerFixture )
{
	auto whole = makeBytes(30, 5);
	receive(makePart(UNRELIABLE, 9, 0, 2, whole, 0, 10));
	receive(makePart(UNRELIABLE, 9, 2, 2, whole, 20, 10));
	BOOST_CHECK(delivered().empty());

	receive(makePart(UNRELIABLE, 9, 1, 2, whole, 10, 10));
	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK(std::vector<unsigned char>(whole.begin(), whole.begin() + 20) == result[0]);
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( sequenced_split_index_at_or_above_count_is_dropped, ReliabilityLayerFixture )
{
	auto whole = makeBytes(30, 5);
	receive(makePart(UNRELIABLE_SEQUENCED, 9, 0, 2, whole, 0, 10));
	receive(makePart(UNRELIABLE_SEQUENCED, 9, 5, 2, whole, 20, 10));
	BOOST_CHECK(delivered().empty());

	receive(makePart(UNRELIABLE_SEQUENCED, 9, 1, 2, whole, 10, 10));
	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK(std::vector<unsigned char>(whole.begin(), whole.begin() + 20) == result[0]);
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( split_count_mismatch_is_dropped, ReliabilityLayerFixture )
{
	auto whole = makeBytes(30, 7);
	receive(makePart(UNRELIABLE, 3, 0, 2, whole, 0, 10));
	receive(makePart(UNRELIABLE, 3, 1, 3, whole, 10, 10));
	BOOST_CHECK(delivered().empty());
	BOOST_CHECK(!layer.IsDataWaiting());

	receive(makePart(UNRELIABLE, 4, 0, 3, whole, 0, 10));
	receive(makePart(UNRELIABLE, 4, 1, 2, whole, 10, 10));
	BOOST_CHECK(delivered().empty());
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( split_part_size_mismatch_is_dropped, ReliabilityLayerFixture )
{
	auto whole = makeBytes(18, 11);
	receive(makePart(UNRELIABLE, 6, 2, 3, whole, 8, 10));
	receive(makePart(UNRELIABLE, 6, 0, 3, whole, 0, 4));
	receive(makePart(UNRELIABLE, 6, 1, 3, whole, 4, 4));
	BOOST_CHECK(delivered().empty());
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( split_reliability_mismatch_is_dropped, ReliabilityLayerFixture )
{
	auto whole = makeBytes(20, 13);
	receive(makePart(UNRELIABLE_SEQUENCED, 8, 0, 2, whole, 0, 10));
	receive(makePart(UNRELIABLE, 8, 1, 2, whole, 10, 10));
	BOOST_CHECK(delivered().empty());
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( sequenced_split_duplicate_index_is_dropped, ReliabilityLayerFixture )
{
	auto whole = makeBytes(20, 17);
	receive(makePart(UNRELIABLE_SEQUENCED, 5, 0, 2, whole, 0, 10));
	receive(makePart(UNRELIABLE_SEQUENCED, 5, 0, 2, whole, 0, 10));
	BOOST_CHECK(delivered().empty());

	receive(makePart(UNRELIABLE_SEQUENCED, 5, 1, 2, whole, 10, 10));
	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK(result[0] == whole);
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( truncated_message_is_dropped, ReliabilityLayerFixture )
{
	Part part{UNRELIABLE, 0, 0, 0, makeBytes(10, 19)};
	receive(part, 80, 4);
	BOOST_CHECK(delivered().empty());
	BOOST_CHECK(!layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( split_count_above_limit_is_dropped, ReliabilityLayerFixture )
{
	auto whole = makeBytes(10, 23);
	receive(makePart(UNRELIABLE, 1, 0, MAX_SPLIT_PACKET_COUNT + 1, whole, 0, 10));
	BOOST_CHECK(!layer.IsDataWaiting());

	receive(makePart(UNRELIABLE, 2, 0, MAX_SPLIT_PACKET_COUNT, whole, 0, 10));
	BOOST_CHECK(layer.IsDataWaiting());
}

BOOST_FIXTURE_TEST_CASE( pending_split_ids_are_limited, ReliabilityLayerFixture )
{
	auto whole = makeBytes(20, 29);
	for (unsigned int id = 100; id < 100 + MAX_PENDING_SPLIT_IDS; ++id)
		receive(makePart(UNRELIABLE, id, 0, 2, whole, 0, 10));

	unsigned int extra = 100 + MAX_PENDING_SPLIT_IDS;
	receive(makePart(UNRELIABLE, extra, 0, 2, whole, 0, 10));
	receive(makePart(UNRELIABLE, extra, 1, 2, whole, 10, 10));
	BOOST_CHECK(delivered().empty());

	receive(makePart(UNRELIABLE, 100, 1, 2, whole, 10, 10));
	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK(result[0] == whole);
}

BOOST_FIXTURE_TEST_CASE( pending_split_bytes_are_limited, ReliabilityLayerFixture )
{
	const unsigned int partSize = 7000;
	auto big = makeBytes(partSize, 31);
	for (unsigned int index = 0; index * partSize <= MAX_PENDING_SPLIT_BYTES; ++index)
		receive(makePart(UNRELIABLE, 1, index, MAX_SPLIT_PACKET_COUNT, big, 0, partSize));

	// the list stays just below the limit, so another set of this size does not fit
	receive(makePart(UNRELIABLE, 2, 0, 2, big, 0, partSize));
	receive(makePart(UNRELIABLE, 2, 1, 2, big, 0, partSize));
	BOOST_CHECK(delivered().empty());
}

BOOST_FIXTURE_TEST_CASE( reliable_split_parts_expire, ReliabilityLayerFixture )
{
	auto whole = makeBytes(30, 41);
	receive(makePart(RELIABLE, 3, 0, 3, whole, 0, 10));
	receive(makePart(RELIABLE, 3, 1, 3, whole, 10, 10));
	now += SPLIT_PACKET_EXPIRY_TIME + 1;
	receive(makePart(RELIABLE, 3, 2, 3, whole, 20, 10));
	BOOST_CHECK(delivered().empty());
}

BOOST_FIXTURE_TEST_CASE( reliable_split_parts_within_expiry_reassemble, ReliabilityLayerFixture )
{
	auto whole = makeBytes(30, 43);
	receive(makePart(RELIABLE, 3, 0, 3, whole, 0, 10));
	receive(makePart(RELIABLE, 3, 1, 3, whole, 10, 10));
	now += SPLIT_PACKET_EXPIRY_TIME - 1000;
	receive(makePart(RELIABLE, 3, 2, 3, whole, 20, 10));
	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK(result[0] == whole);
}

BOOST_FIXTURE_TEST_CASE( late_packet_right_after_the_clock_starts_is_delivered, ReliabilityLayerFixture )
{
	// on some systems the clock starts at 0 when the program starts
	now = 1000;
	Part first{UNRELIABLE, 0, 0, 0, makeBytes(4, 47)};
	Part second{UNRELIABLE, 0, 0, 0, makeBytes(4, 53)};
	packetNumber = 1;
	receive(second);
	packetNumber = 0;
	receive(first);

	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 2u);
	BOOST_CHECK(result[0] == second.bytes);
	BOOST_CHECK(result[1] == first.bytes);
}

BOOST_FIXTURE_TEST_CASE( late_packet_across_the_clock_wrap_is_delivered, ReliabilityLayerFixture )
{
	now = 0xFFFFFFFFu - 2000;
	Part first{UNRELIABLE, 0, 0, 0, makeBytes(4, 59)};
	Part second{UNRELIABLE, 0, 0, 0, makeBytes(4, 61)};
	packetNumber = 1;
	receive(second);
	now = 3000;
	packetNumber = 0;
	receive(first);

	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 2u);
	BOOST_CHECK(result[1] == first.bytes);
}

BOOST_FIXTURE_TEST_CASE( a_packet_seen_after_a_gap_is_dropped_once, ReliabilityLayerFixture )
{
	// packet 1 leaves a hole for packet 0. A second packet 1 finds a received time in its
	// slot, not a hole, and is dropped. Packet 0 still fills its hole.
	now = 1000;
	Part late{UNRELIABLE, 0, 0, 0, makeBytes(4, 71)};
	Part early{UNRELIABLE, 0, 0, 0, makeBytes(4, 73)};
	packetNumber = 1;
	receive(late);
	now = 1500;
	packetNumber = 1;
	receive(late);
	packetNumber = 0;
	receive(early);

	auto result = delivered();
	BOOST_REQUIRE_EQUAL(result.size(), 2u);
	BOOST_CHECK(result[0] == late.bytes);
	BOOST_CHECK(result[1] == early.bytes);
}

BOOST_FIXTURE_TEST_CASE( a_packet_seen_before_is_still_dropped, ReliabilityLayerFixture )
{
	now = 1000;
	Part part{UNRELIABLE, 0, 0, 0, makeBytes(4, 67)};
	packetNumber = 0;
	receive(part);
	packetNumber = 0;
	receive(part);
	BOOST_CHECK_EQUAL(delivered().size(), 1u);
}

BOOST_AUTO_TEST_CASE( time_helpers_work_across_the_wrap_and_from_zero )
{
	// across the wrap
	BOOST_CHECK_EQUAL(RakNet::TimeElapsed(0xFFFFFF00u, 0x100u), 0x200);
	BOOST_CHECK(RakNet::TimeIsAfter(5u, 0xFFFFFFF0u));
	BOOST_CHECK(!RakNet::TimeIsAfter(0xFFFFFFF0u, 5u));
	// a clock that starts at 0
	BOOST_CHECK(RakNet::TimeIsAfter(10u, 0u));
	BOOST_CHECK_EQUAL(RakNet::TimeElapsed(5000u, 1000u), -4000);
	BOOST_CHECK(!RakNet::TimeIsAfter(1000u, 1000u));
	// away from the wrap the helpers agree with plain comparisons
	BOOST_CHECK_EQUAL(RakNet::TimeElapsed(1000u, 4000u), 3000);
	BOOST_CHECK(RakNet::TimeIsAfter(4000u, 1000u));
}

namespace
{
	struct SendFixture
	{
		ReliabilityLayer layer;
		PlayerID peer;

		SendFixture()
		{
			peer.binaryAddress = 0x0100007F;
			peer.port = 1234;
		}

		void sendReliable(unsigned int time)
		{
			char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
			layer.Send(data, 64, HIGH_PRIORITY, RELIABLE, 0, true, 576, time);
			layer.Update(INVALID_SOCKET, peer, 576, time);
		}

		unsigned resends() { return layer.GetStatistics()->messageResends; }
	};
}

BOOST_FIXTURE_TEST_CASE( reliable_message_is_not_resent_early_at_the_wrap, SendFixture )
{
	const unsigned int start = 0xFFFFFFFFu - 50;
	sendReliable(start);
	layer.Update(INVALID_SOCKET, peer, 576, start + 1);
	BOOST_CHECK_EQUAL(resends(), 0u);

	// without an ack it goes out again once the resend delay has passed, after the wrap
	layer.Update(INVALID_SOCKET, peer, 576, start + 2000);
	BOOST_CHECK_GE(resends(), 1u);
}

BOOST_FIXTURE_TEST_CASE( connection_without_acks_dies_across_the_wrap, SendFixture )
{
	const unsigned int start = 0xFFFFFFFFu - 50;
	sendReliable(start);
	layer.Update(INVALID_SOCKET, peer, 576, start + TIMEOUT_TIME / 2);
	BOOST_CHECK(!layer.IsDeadConnection());
	layer.Update(INVALID_SOCKET, peer, 576, start + TIMEOUT_TIME + 1000);
	BOOST_CHECK(layer.IsDeadConnection());
}

BOOST_FIXTURE_TEST_CASE( connection_without_acks_dies_on_a_clock_from_zero, SendFixture )
{
	sendReliable(100);
	layer.Update(INVALID_SOCKET, peer, 576, 100 + TIMEOUT_TIME / 2);
	BOOST_CHECK(!layer.IsDeadConnection());
	layer.Update(INVALID_SOCKET, peer, 576, 100 + TIMEOUT_TIME + 1000);
	BOOST_CHECK(layer.IsDeadConnection());
}

BOOST_AUTO_TEST_CASE( keepalive_is_due_after_five_quiet_seconds_across_the_wrap )
{
	const unsigned int last = 0xFFFFFFFFu - 1000;
	BOOST_CHECK(!RakNet::KeepAliveDue(last, last + 5000));
	BOOST_CHECK(RakNet::KeepAliveDue(last, last + 5001));
	// after a keepalive the last send lies ahead, also when that crosses the wrap
	const unsigned int ahead = last + TIMEOUT_TIME;
	BOOST_CHECK(!RakNet::KeepAliveDue(ahead, last + 1));
	BOOST_CHECK(!RakNet::KeepAliveDue(ahead, ahead + 5000));
	BOOST_CHECK(RakNet::KeepAliveDue(ahead, ahead + 5001));
}

BOOST_AUTO_TEST_CASE( keepalive_on_a_clock_from_zero )
{
	BOOST_CHECK(!RakNet::KeepAliveDue(0, 0));
	BOOST_CHECK(!RakNet::KeepAliveDue(0, 5000));
	BOOST_CHECK(RakNet::KeepAliveDue(0, 5001));
	BOOST_CHECK(!RakNet::KeepAliveDue(100 + TIMEOUT_TIME, 200));
}

BOOST_AUTO_TEST_CASE( not_connected_times_out_across_the_wrap_and_from_zero )
{
	const unsigned int start = 0xFFFFFFFFu - 3000;
	BOOST_CHECK(!RakNet::NotConnectedTimedOut(start, start + 10000));
	BOOST_CHECK(RakNet::NotConnectedTimedOut(start, start + 10001));
	BOOST_CHECK(!RakNet::NotConnectedTimedOut(0, 10000));
	BOOST_CHECK(RakNet::NotConnectedTimedOut(0, 10001));
	// a start time a little ahead of now is not a timeout
	BOOST_CHECK(!RakNet::NotConnectedTimedOut(500, 400));
}

BOOST_AUTO_TEST_CASE( ping_is_scheduled_across_the_wrap_and_from_zero )
{
	// 0 means ping now, also late on the clock
	BOOST_CHECK(RakNet::PingDue(0, 1));
	BOOST_CHECK(RakNet::PingDue(0, 0x90000000u));
	const unsigned int next = 0xFFFFFFFFu - 2000 + 5000;
	BOOST_CHECK(!RakNet::PingDue(next, 0xFFFFFFFFu - 2000));
	BOOST_CHECK(!RakNet::PingDue(next, next));
	BOOST_CHECK(RakNet::PingDue(next, next + 1));
	BOOST_CHECK(!RakNet::PingDue(5000, 4999));
	BOOST_CHECK(RakNet::PingDue(5000, 5001));
}

BOOST_AUTO_TEST_SUITE_END()
