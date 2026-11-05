#include <boost/test/unit_test.hpp>

#include <vector>

#include "raknet/BitStream.h"
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
			layer.HandleSocketReceiveFromConnectedPlayer((const char*)stream.GetData(), stream.GetNumberOfBytesUsed());
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

BOOST_AUTO_TEST_SUITE_END()
