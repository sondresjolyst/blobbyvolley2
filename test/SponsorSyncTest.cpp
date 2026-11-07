#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "raknet/BitStream.h"

#include "FileSystem.h"
#include "NetworkMessage.h"
#include "Sha256.h"
#include "SponsorSync.h"

using namespace sponsors;

namespace
{
	void putU32(std::string& d, std::size_t at, std::uint32_t v)
	{
		for(int i = 0; i < 4; ++i)
			d[at + i] = char((v >> (8 * i)) & 0xFF);
	}

	// an uncompressed 24-bit BMP whose pixels all have the given value
	std::string makeBmp(int width, int height, unsigned char fill = 0x40)
	{
		const std::uint32_t stride = (width * 3 + 3) / 4 * 4;
		const std::uint32_t size = 54 + stride * height;
		std::string d(size, char(fill));
		std::fill(d.begin(), d.begin() + 54, char(0));
		d[0] = 'B';
		d[1] = 'M';
		putU32(d, 2, size);
		putU32(d, 10, 54);
		putU32(d, 14, 40);
		putU32(d, 18, width);
		putU32(d, 22, height);
		d[26] = 1;
		d[28] = 24;
		return d;
	}

	// a 32-bit BI_BITFIELDS BMP with a V5 header, the largest kind of logo
	std::string makeBmp32(int width, int height, unsigned char fill)
	{
		const std::uint32_t offset = 14 + 124;
		const std::uint32_t size = offset + width * 4 * height;
		std::string d(size, char(fill));
		std::fill(d.begin(), d.begin() + offset, char(0));
		d[0] = 'B';
		d[1] = 'M';
		putU32(d, 2, size);
		putU32(d, 10, offset);
		putU32(d, 14, 124);
		putU32(d, 18, width);
		putU32(d, 22, height);
		d[26] = 1;
		d[28] = 32;
		putU32(d, 30, 3);
		putU32(d, 54, 0x00FF0000u);
		putU32(d, 58, 0x0000FF00u);
		putU32(d, 62, 0x000000FFu);
		putU32(d, 66, 0xFF000000u);
		return d;
	}

	LogoFile makeLogo(unsigned char fill, int width = 8, int height = 4)
	{
		const std::string data = makeBmp(width, height, fill);
		return LogoFile{sha256Hex(data), data};
	}

	struct MemoryCache : public LogoCache
	{
		bool load(const std::string& hash, std::string& data) override
		{
			auto found = files.find(hash);
			if(found == files.end())
				return false;
			data = found->second;
			return true;
		}
		void store(const std::string& hash, const std::string& data) override
		{
			files[hash] = data;
		}
		void keepOnly(const std::set<std::string>& keep) override
		{
			for(auto it = files.begin(); it != files.end(); )
				it = keep.count(it->first) ? std::next(it) : files.erase(it);
		}
		std::map<std::string, std::string> files;
	};

	// a stream positioned after ID_SPONSORS, as the game hands it on
	std::unique_ptr<RakNet::BitStream> received(const RakNet::BitStream& sent)
	{
		std::unique_ptr<RakNet::BitStream> copy(new RakNet::BitStream(sent.GetData(), sent.GetNumberOfBytesUsed(), true));
		unsigned char id = 0;
		copy->Read(id);
		BOOST_REQUIRE_EQUAL(id, (unsigned char)ID_SPONSORS);
		return copy;
	}

	// a BOARD packet written by hand, for data the sender would never make
	std::unique_ptr<RakNet::BitStream> rawAnnouncement(const std::string& style, const std::vector<std::pair<std::string, std::uint32_t>>& logos,
									  int count = -1)
	{
		std::unique_ptr<RakNet::BitStream> s(new RakNet::BitStream());
		s->Write((unsigned char)ID_SPONSORS);
		s->Write((unsigned char)SponsorPacketType::BOARD);
		s->Write(std::string(64, 'a').data(), 64);
		s->Write((unsigned char)style.size());
		s->Write(style.data(), (int)style.size());
		s->Write((unsigned char)(count < 0 ? logos.size() : count));
		for(const auto& logo : logos)
		{
			s->Write(logo.first.data(), (int)logo.first.size());
			s->Write((unsigned int)logo.second);
		}
		return s;
	}

	bool readsAnnouncement(const RakNet::BitStream& sent, Announcement& announcement)
	{
		auto in = received(sent);
		SponsorPacketType type;
		BOOST_REQUIRE(readPacketType(*in, type));
		BOOST_REQUIRE(type == SponsorPacketType::BOARD);
		return readAnnouncement(*in, announcement);
	}

	struct PhysfsFixture
	{
		PhysfsFixture() : fs("./test/")
		{
			fs.setWriteDir(".");
			fs.addToSearchPath(".", true);
		}
		FileSystem fs;
	};
}

BOOST_AUTO_TEST_SUITE( sponsor_sync )

BOOST_AUTO_TEST_CASE( the_message_ids_of_older_packets_do_not_move )
{
	BOOST_CHECK_EQUAL((int)ID_GAME_READY, ID_RESERVED9 + 6);
	BOOST_CHECK_EQUAL((int)ID_RULES, ID_RESERVED9 + 18);
	BOOST_CHECK_EQUAL((int)ID_LOBBY, ID_RESERVED9 + 20);
	BOOST_CHECK_EQUAL((int)ID_SPONSORS, ID_LOBBY + 1);
}

BOOST_AUTO_TEST_CASE( an_announcement_reads_back )
{
	Board board;
	board.style = BoardStyle::WOOD;
	LogoFile a = makeLogo(1);
	LogoFile b = makeLogo(2, 10, 6);
	board.logos = {a, b, a};

	RakNet::BitStream sent;
	writeAnnouncement(sent, board, boardVersion(board));
	Announcement read;
	BOOST_REQUIRE(readsAnnouncement(sent, read));
	BOOST_CHECK_EQUAL(read.version, boardVersion(board));
	BOOST_CHECK_EQUAL(read.styleName, "wood");
	BOOST_CHECK(read.style() == BoardStyle::WOOD);
	BOOST_REQUIRE_EQUAL(read.logos.size(), 3u);
	BOOST_CHECK_EQUAL(read.logos[0].hash, a.hash);
	BOOST_CHECK_EQUAL(read.logos[1].hash, b.hash);
	BOOST_CHECK_EQUAL(read.logos[1].size, b.data.size());
	BOOST_CHECK_EQUAL(read.logos[2].hash, a.hash);
	BOOST_CHECK_EQUAL(read.uniqueHashes().size(), 2u);
}

BOOST_AUTO_TEST_CASE( the_version_follows_style_and_logos )
{
	Board board;
	board.logos = {makeLogo(1), makeLogo(2)};
	const std::string version = boardVersion(board);
	BOOST_CHECK(isSha256Hex(version));

	Board same = board;
	BOOST_CHECK_EQUAL(boardVersion(same), version);
	same.style = BoardStyle::SLIM;
	BOOST_CHECK_NE(boardVersion(same), version);
	Board swapped = board;
	std::swap(swapped.logos[0], swapped.logos[1]);
	BOOST_CHECK_NE(boardVersion(swapped), version);
}

BOOST_AUTO_TEST_CASE( a_request_and_a_logo_read_back )
{
	const LogoFile logo = makeLogo(3);
	RakNet::BitStream request;
	writeRequest(request, std::string(64, 'f'), {logo.hash});
	auto in = received(request);
	SponsorPacketType type;
	BOOST_REQUIRE(readPacketType(*in, type));
	BOOST_CHECK(type == SponsorPacketType::REQUEST);
	std::string version;
	std::vector<std::string> hashes;
	BOOST_REQUIRE(readRequest(*in, version, hashes));
	BOOST_CHECK_EQUAL(version, std::string(64, 'f'));
	BOOST_REQUIRE_EQUAL(hashes.size(), 1u);
	BOOST_CHECK_EQUAL(hashes[0], logo.hash);

	RakNet::BitStream sent;
	writeLogo(sent, logo);
	auto logoIn = received(sent);
	BOOST_REQUIRE(readPacketType(*logoIn, type));
	BOOST_CHECK(type == SponsorPacketType::LOGO);
	LogoFile read;
	BOOST_REQUIRE(readLogo(*logoIn, read));
	BOOST_CHECK_EQUAL(read.hash, logo.hash);
	BOOST_CHECK(read.data == logo.data);
}

BOOST_AUTO_TEST_CASE( announcements_over_the_limits_are_refused )
{
	const std::string hash = makeLogo(1).hash;
	Announcement read;

	// within the limits
	BOOST_CHECK(readsAnnouncement(*rawAnnouncement("led", {{hash, 100}}), read));

	// too many logos
	std::vector<std::pair<std::string, std::uint32_t>> many;
	for(std::size_t i = 0; i <= MAX_LOGOS; ++i)
		many.push_back({sha256Hex(std::to_string(i)), 100});
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("led", many), read));

	// a logo too large or empty
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("led", {{hash, (std::uint32_t)MAX_FILE_SIZE + 1}}), read));
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("led", {{hash, 0}}), read));

	// too much together
	std::vector<std::pair<std::string, std::uint32_t>> large;
	for(std::size_t i = 0; i * MAX_FILE_SIZE <= MAX_TOTAL_SIZE; ++i)
		large.push_back({sha256Hex(std::to_string(i)), (std::uint32_t)MAX_FILE_SIZE});
	BOOST_REQUIRE(large.size() <= MAX_LOGOS);
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("led", large), read));

	// the same logo counts once, but must have one size
	std::vector<std::pair<std::string, std::uint32_t>> repeated(MAX_LOGOS, {hash, (std::uint32_t)MAX_FILE_SIZE});
	BOOST_CHECK(readsAnnouncement(*rawAnnouncement("led", repeated), read));
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("led", {{hash, 100}, {hash, 101}}), read));

	// a hash that is not lower case hex, an empty style and a count past the end
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("led", {{std::string(64, 'G'), 100}}), read));
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("", {{hash, 100}}), read));
	BOOST_CHECK(!readsAnnouncement(*rawAnnouncement("led", {{hash, 100}}, 2), read));
}

BOOST_AUTO_TEST_CASE( bad_requests_and_logos_are_refused )
{
	SponsorPacketType type;
	RakNet::BitStream unknown;
	unknown.Write((unsigned char)ID_SPONSORS);
	unknown.Write((unsigned char)7);
	auto unknownIn = received(unknown);
	BOOST_CHECK(!readPacketType(*unknownIn, type));

	// a logo larger than the rest of the packet
	RakNet::BitStream shortLogo;
	shortLogo.Write((unsigned char)ID_SPONSORS);
	shortLogo.Write((unsigned char)SponsorPacketType::LOGO);
	shortLogo.Write(makeLogo(1).hash.data(), 64);
	shortLogo.Write((unsigned int)500);
	shortLogo.Write("abc", 3);
	auto shortIn = received(shortLogo);
	BOOST_REQUIRE(readPacketType(*shortIn, type));
	LogoFile logo;
	BOOST_CHECK(!readLogo(*shortIn, logo));

	// a logo over the size limit
	LogoFile huge{std::string(64, 'a'), std::string(MAX_FILE_SIZE + 1, 'x')};
	RakNet::BitStream hugeLogo;
	writeLogo(hugeLogo, huge);
	auto hugeIn = received(hugeLogo);
	BOOST_REQUIRE(readPacketType(*hugeIn, type));
	BOOST_CHECK(!readLogo(*hugeIn, logo));

	// a request for too many logos
	std::vector<std::string> many(MAX_LOGOS + 1, makeLogo(1).hash);
	RakNet::BitStream request;
	writeRequest(request, std::string(64, 'a'), many);
	auto requestIn = received(request);
	BOOST_REQUIRE(readPacketType(*requestIn, type));
	std::string version;
	std::vector<std::string> hashes;
	BOOST_CHECK(!readRequest(*requestIn, version, hashes));
}

BOOST_AUTO_TEST_CASE( a_logo_must_match_its_hash_and_be_a_bmp )
{
	const LogoFile good = makeLogo(1);
	std::string error;
	BOOST_CHECK(checkLogo(good.hash, good.data, &error));
	BOOST_CHECK(!checkLogo(makeLogo(2).hash, good.data, &error));
	BOOST_CHECK(!checkLogo("abc", good.data, &error));
	const std::string text = "not a bmp";
	BOOST_CHECK(!checkLogo(sha256Hex(text), text, &error));
	// wider than a logo may be
	const std::string wide = makeBmp(MAX_LOGO_WIDTH + 1, 4);
	BOOST_CHECK(!checkLogo(sha256Hex(wide), wide, &error));
}

BOOST_AUTO_TEST_CASE( the_board_is_limited )
{
	Board board;
	LogoFile good = makeLogo(1);
	LogoFile bad = makeLogo(2);
	bad.data[60] ^= 1;
	board.logos = {good, bad, good};
	std::vector<std::string> warnings;
	limitBoard(board, warnings);
	BOOST_CHECK_EQUAL(board.logos.size(), 2u);
	BOOST_CHECK_EQUAL(warnings.size(), 1u);

	// at most MAX_LOGOS
	Board many;
	for(std::size_t i = 0; i < MAX_LOGOS + 3; ++i)
		many.logos.push_back(makeLogo((unsigned char)i));
	warnings.clear();
	limitBoard(many, warnings);
	BOOST_CHECK_EQUAL(many.logos.size(), MAX_LOGOS);
	BOOST_CHECK_EQUAL(warnings.size(), 3u);

	// at most MAX_TOTAL_SIZE together, the same logo counts once
	Board large;
	for(int i = 0; i < 32; ++i)
	{
		const std::string data = makeBmp32(MAX_LOGO_WIDTH, MAX_LOGO_HEIGHT, (unsigned char)i);
		large.logos.push_back(LogoFile{sha256Hex(data), data});
	}
	const std::size_t each = large.logos[0].data.size();
	large.logos.push_back(large.logos[0]);
	warnings.clear();
	limitBoard(large, warnings);
	std::set<std::string> unique;
	for(const auto& logo : large.logos)
		unique.insert(logo.hash);
	BOOST_CHECK_EQUAL(unique.size(), MAX_TOTAL_SIZE / each);
	BOOST_CHECK(unique.size() < 32u);
	BOOST_CHECK_EQUAL(large.logos.size(), unique.size() + 1);
	BOOST_CHECK(!warnings.empty());
}

BOOST_AUTO_TEST_CASE( the_server_sends_each_logo_once_per_version )
{
	BoardSender sender;
	BOOST_CHECK(!sender.hasBoard());
	Board board;
	board.logos = {makeLogo(1), makeLogo(2)};
	sender.setBoard(board);
	BOOST_REQUIRE(sender.hasBoard());

	std::set<std::string> sent;
	auto ask = [&](const std::string& version, const std::vector<std::string>& hashes) {
		RakNet::BitStream request;
		writeRequest(request, version, hashes);
		auto in = received(request);
		SponsorPacketType type;
		BOOST_REQUIRE(readPacketType(*in, type));
		return sender.answer(*in, sent);
	};

	// another version gets nothing
	BOOST_CHECK(ask(std::string(64, 'a'), {board.logos[0].hash}).empty());
	// a logo that is not on the board gets nothing
	BOOST_CHECK(ask(sender.version(), {makeLogo(9).hash}).empty());

	auto answer = ask(sender.version(), {board.logos[1].hash, board.logos[0].hash, board.logos[1].hash});
	BOOST_REQUIRE_EQUAL(answer.size(), 2u);
	BOOST_CHECK_EQUAL(answer[0]->hash, board.logos[1].hash);
	BOOST_CHECK_EQUAL(answer[1]->hash, board.logos[0].hash);

	// asking again does not make the server send them again
	BOOST_CHECK(ask(sender.version(), {board.logos[0].hash}).empty());
}

BOOST_AUTO_TEST_CASE( the_player_asks_for_missing_logos_and_caches_them )
{
	BoardSender sender;
	Board board;
	board.style = BoardStyle::PRINTED;
	board.logos = {makeLogo(1), makeLogo(2), makeLogo(1)};
	sender.setBoard(board);

	MemoryCache cache;
	cache.files[makeLogo(7).hash] = makeLogo(7).data;	// from an older board
	BoardReceiver receiver(cache);

	RakNet::BitStream announcement;
	sender.writeAnnouncement(announcement);
	auto in = received(announcement);
	RakNet::BitStream request;
	BOOST_REQUIRE(receiver.handle(*in, request) == BoardReceiver::Result::REQUEST);
	BOOST_CHECK(!receiver.ready());
	BOOST_CHECK(receiver.logoFiles().empty());

	// the server answers the request
	auto requestIn = received(request);
	SponsorPacketType type;
	BOOST_REQUIRE(readPacketType(*requestIn, type));
	std::set<std::string> sent;
	auto logos = sender.answer(*requestIn, sent);
	BOOST_REQUIRE_EQUAL(logos.size(), 2u);

	RakNet::BitStream answer;
	RakNet::BitStream first;
	writeLogo(first, *logos[0]);
	auto firstIn = received(first);
	BOOST_CHECK(receiver.handle(*firstIn, answer) == BoardReceiver::Result::NOTHING);
	RakNet::BitStream second;
	writeLogo(second, *logos[1]);
	auto secondIn = received(second);
	BOOST_REQUIRE(receiver.handle(*secondIn, answer) == BoardReceiver::Result::READY);

	BOOST_CHECK(receiver.ready());
	BOOST_CHECK(receiver.style() == BoardStyle::PRINTED);
	auto files = receiver.logoFiles();
	BOOST_REQUIRE_EQUAL(files.size(), 3u);
	BOOST_CHECK(std::string(files[0].begin(), files[0].end()) == board.logos[0].data);
	BOOST_CHECK(std::string(files[1].begin(), files[1].end()) == board.logos[1].data);
	BOOST_CHECK(std::string(files[2].begin(), files[2].end()) == board.logos[0].data);

	// the logos are cached and the old one is gone
	BOOST_CHECK_EQUAL(cache.files.size(), 2u);
	BOOST_CHECK(cache.files.count(board.logos[0].hash) == 1);
	BOOST_CHECK(cache.files.count(makeLogo(7).hash) == 0);

	// after a restart the cache is enough
	BoardReceiver again(cache);
	auto announcementAgain = received(announcement);
	RakNet::BitStream noRequest;
	BOOST_CHECK(again.handle(*announcementAgain, noRequest) == BoardReceiver::Result::READY);
	BOOST_CHECK_EQUAL(noRequest.GetNumberOfBytesUsed(), 0);
	BOOST_CHECK_EQUAL(again.logoFiles().size(), 3u);
}

BOOST_AUTO_TEST_CASE( the_player_refuses_bad_logos )
{
	const LogoFile good = makeLogo(1);
	MemoryCache cache;
	// a broken cached file is asked for again
	cache.files[good.hash] = good.data.substr(0, 60);
	BoardReceiver receiver(cache);

	Board board;
	board.logos = {good};
	RakNet::BitStream announcement;
	writeAnnouncement(announcement, board, boardVersion(board));
	auto in = received(announcement);
	RakNet::BitStream request;
	BOOST_REQUIRE(receiver.handle(*in, request) == BoardReceiver::Result::REQUEST);

	RakNet::BitStream answer;
	// data that does not match the hash
	LogoFile changed = good;
	changed.data[60] ^= 1;
	RakNet::BitStream wrong;
	writeLogo(wrong, changed);
	auto wrongIn = received(wrong);
	BOOST_CHECK(receiver.handle(*wrongIn, answer) == BoardReceiver::Result::NOTHING);
	BOOST_CHECK(!receiver.problem().empty());
	BOOST_CHECK(cache.files[good.hash] != good.data);

	// a logo that is not on the board
	RakNet::BitStream other;
	writeLogo(other, makeLogo(2));
	auto otherIn = received(other);
	BOOST_CHECK(receiver.handle(*otherIn, answer) == BoardReceiver::Result::NOTHING);
	BOOST_CHECK_EQUAL(cache.files.count(makeLogo(2).hash), 0u);

	// a broken announcement keeps the board that waits
	auto broken = rawAnnouncement("led", {{good.hash, 0}});
	auto brokenIn = received(*broken);
	BOOST_CHECK(receiver.handle(*brokenIn, answer) == BoardReceiver::Result::NOTHING);

	RakNet::BitStream right;
	writeLogo(right, good);
	auto rightIn = received(right);
	BOOST_CHECK(receiver.handle(*rightIn, answer) == BoardReceiver::Result::READY);
	BOOST_CHECK(cache.files[good.hash] == good.data);
}

BOOST_AUTO_TEST_CASE( the_cache_holds_one_board_even_if_it_never_completes )
{
	MemoryCache cache;
	BoardReceiver receiver(cache);
	// boards whose logos never arrive
	for(unsigned char round = 0; round < 5; ++round)
	{
		Board board;
		board.logos = {makeLogo(round), makeLogo(round + 100)};
		// one logo of each board is cached, as if it arrived before the next board
		cache.store(board.logos[0].hash, board.logos[0].data);
		RakNet::BitStream announcement;
		writeAnnouncement(announcement, board, boardVersion(board));
		auto in = received(announcement);
		RakNet::BitStream answer;
		BOOST_CHECK(receiver.handle(*in, answer) == BoardReceiver::Result::REQUEST);
		BOOST_CHECK_EQUAL(cache.files.size(), 1u);
		BOOST_CHECK_EQUAL(cache.files.count(board.logos[0].hash), 1u);
	}
}

BOOST_AUTO_TEST_CASE( the_same_board_again_is_ignored )
{
	struct CountingCache : public MemoryCache
	{
		bool load(const std::string& hash, std::string& data) override
		{
			++loads;
			return MemoryCache::load(hash, data);
		}
		int loads = 0;
	} cache;
	const LogoFile logo = makeLogo(1);
	cache.files[logo.hash] = logo.data;
	BoardReceiver receiver(cache);
	Board board;
	board.logos = {logo};
	RakNet::BitStream announcement;
	writeAnnouncement(announcement, board, boardVersion(board));
	RakNet::BitStream answer;
	auto first = received(announcement);
	BOOST_REQUIRE(receiver.handle(*first, answer) == BoardReceiver::Result::READY);
	BOOST_CHECK_EQUAL(cache.loads, 1);
	auto second = received(announcement);
	BOOST_CHECK(receiver.handle(*second, answer) == BoardReceiver::Result::NOTHING);
	BOOST_CHECK_EQUAL(cache.loads, 1);
	BOOST_CHECK(receiver.ready());
	BOOST_CHECK_EQUAL(receiver.logoFiles().size(), 1u);

	// a new board is read again
	board.style = BoardStyle::SLIM;
	RakNet::BitStream changed;
	writeAnnouncement(changed, board, boardVersion(board));
	auto third = received(changed);
	BOOST_CHECK(receiver.handle(*third, answer) == BoardReceiver::Result::READY);
	BOOST_CHECK_EQUAL(cache.loads, 2);
}

BOOST_AUTO_TEST_CASE( style_off_and_unknown_styles )
{
	MemoryCache cache;
	BoardReceiver receiver(cache);
	Board off;
	off.style = BoardStyle::OFF;
	RakNet::BitStream announcement;
	writeAnnouncement(announcement, off, boardVersion(off));
	auto in = received(announcement);
	RakNet::BitStream request;
	BOOST_REQUIRE(receiver.handle(*in, request) == BoardReceiver::Result::READY);
	BOOST_CHECK(receiver.style() == BoardStyle::OFF);
	BOOST_CHECK(receiver.logoFiles().empty());

	// a style from a newer server shows as LED
	auto neon = rawAnnouncement("neon", {});
	auto neonIn = received(*neon);
	BOOST_REQUIRE(receiver.handle(*neonIn, request) == BoardReceiver::Result::READY);
	BOOST_CHECK(receiver.style() == BoardStyle::LED);

	receiver.reset();
	BOOST_CHECK(!receiver.ready());
}

BOOST_FIXTURE_TEST_CASE( the_file_cache_keeps_logos_by_hash, PhysfsFixture )
{
	FileLogoCache cache("bt_sponsor_cache");
	const LogoFile a = makeLogo(1);
	const LogoFile b = makeLogo(2);
	std::string data;
	BOOST_CHECK(!cache.load(a.hash, data));
	cache.store(a.hash, a.data);
	cache.store(b.hash, b.data);
	cache.store("../escape", a.data);
	BOOST_REQUIRE(cache.load(a.hash, data));
	BOOST_CHECK(data == a.data);
	BOOST_CHECK(!cache.load("../escape", data));

	cache.keepOnly({b.hash});
	BOOST_CHECK(!cache.load(a.hash, data));
	BOOST_REQUIRE(cache.load(b.hash, data));
	BOOST_CHECK(data == b.data);

	cache.keepOnly({});
	BOOST_CHECK(!cache.load(b.hash, data));
	fs.deleteFile("bt_sponsor_cache");
}

BOOST_AUTO_TEST_SUITE_END()
