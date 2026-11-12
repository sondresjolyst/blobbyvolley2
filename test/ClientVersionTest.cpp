#include <boost/test/unit_test.hpp>

#include <map>
#include <string>
#include <vector>

#include "ClientVersion.h"
#include "ForkVersion.h"
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "raknet/BitStream.h"
#include "server/NetworkPlayer.h"
#include "server/SiteConfig.h"

namespace
{
	ForkVersion version(uint32_t major, uint32_t minor, uint32_t patch)
	{
		ForkVersion v;
		v.major = major;
		v.minor = minor;
		v.patch = patch;
		return v;
	}

	EnvironmentFunction envOf(const std::map<std::string, std::string>& values)
	{
		return [values](const char* name) -> const char* {
			auto found = values.find(name);
			return found == values.end() ? nullptr : found->second.c_str();
		};
	}

	VersionPolicy policy(MinClientMode mode, const ForkVersion& minimum)
	{
		VersionPolicy p;
		p.mode = mode;
		p.minimum = minimum;
		return p;
	}

	// an ID_ENTER_SERVER packet as a client of version 0.2.0 sends it
	void makeOldEnterServerPacket(RakNet::BitStream& stream)
	{
		stream.Write((unsigned char)ID_ENTER_SERVER);
		stream.Write((int)LEFT_PLAYER);
		char name[16] = "Ola";
		stream.Write(name, sizeof(name));
		stream.Write((int)0);
		stream.Write(CLIENT_FEATURES_MARKER);
		stream.Write((uint32_t)CLIENT_FEATURE_TOURNAMENT);
	}
}

BOOST_AUTO_TEST_SUITE( client_version )

BOOST_AUTO_TEST_CASE( versions_compare_part_by_part )
{
	BOOST_CHECK(version(0, 2, 0) < version(0, 3, 0));
	BOOST_CHECK(version(0, 2, 9) < version(0, 3, 0));
	BOOST_CHECK(version(0, 9, 0) < version(0, 10, 0));
	BOOST_CHECK(version(0, 10, 0) < version(1, 0, 0));
	BOOST_CHECK(version(1, 2, 3) < version(1, 2, 4));
	BOOST_CHECK(!(version(1, 2, 3) < version(1, 2, 3)));
	BOOST_CHECK(!(version(1, 0, 0) < version(0, 99, 99)));
	BOOST_CHECK(version(1, 2, 3) == version(1, 2, 3));
	BOOST_CHECK_EQUAL(version(0, 10, 2).toString(), "0.10.2");
}

BOOST_AUTO_TEST_CASE( version_parse_takes_only_three_numbers )
{
	ForkVersion v;
	BOOST_CHECK(parseForkVersion("0.3.0", v));
	BOOST_CHECK(v == version(0, 3, 0));
	BOOST_CHECK(parseForkVersion("12.0.345678", v));
	BOOST_CHECK(v == version(12, 0, 345678));

	v = version(7, 7, 7);
	for (const char* bad : {"", "1", "1.2", "1.2.3.4", "1.2.", ".1.2", "01.2.3", "1.2.3 ", " 1.2.3",
							"v1.2.3", "1.-2.3", "1.2.x", "1234567.0.0", "1..2"})
	{
		BOOST_CHECK_MESSAGE(!parseForkVersion(bad, v), bad);
	}
	// a failed parse leaves the version alone
	BOOST_CHECK(v == version(7, 7, 7));
}

BOOST_AUTO_TEST_CASE( current_version_comes_from_version_txt )
{
	ForkVersion parsed;
	BOOST_REQUIRE(parseForkVersion(BLOBBY_FORK_VERSION, parsed));
	BOOST_CHECK(parsed == currentForkVersion());
}

BOOST_AUTO_TEST_CASE( download_url_must_be_http_or_https )
{
	BOOST_CHECK(isDownloadUrl("https://example.org/last-ned"));
	BOOST_CHECK(isDownloadUrl("http://192.168.1.10:8080/last-ned"));
	BOOST_CHECK(isDownloadUrl("https://example.org"));

	for (const char* bad : {"", "example.org/last-ned", "ftp://example.org", "file:///C:/game.exe",
							"javascript:alert(1)", "https://", "https:///path", "HTTPS://example.org",
							"https://example.org/last ned", "https://example.org/\nx", "blobby://join"})
	{
		BOOST_CHECK_MESSAGE(!isDownloadUrl(bad), bad);
	}
	BOOST_CHECK(isDownloadUrl("https://" + std::string(MAX_DOWNLOAD_URL_LENGTH - 8, 'a')));
	BOOST_CHECK(!isDownloadUrl("https://" + std::string(MAX_DOWNLOAD_URL_LENGTH - 7, 'a')));
	BOOST_CHECK(!isDownloadUrl("https://example.org/\xc3\xa6"));
}

BOOST_AUTO_TEST_CASE( no_minimum_means_no_notice )
{
	VersionPolicy off;
	BOOST_CHECK(checkClientVersion(off, false, ForkVersion()) == VersionNotice::NONE);
	BOOST_CHECK(checkClientVersion(off, true, version(0, 0, 1)) == VersionNotice::NONE);
}

BOOST_AUTO_TEST_CASE( older_client_is_warned_or_refused )
{
	const VersionPolicy warn = policy(MinClientMode::WARN, version(0, 3, 0));
	BOOST_CHECK(checkClientVersion(warn, true, version(0, 2, 9)) == VersionNotice::UPDATE_AVAILABLE);
	BOOST_CHECK(checkClientVersion(warn, true, version(0, 3, 0)) == VersionNotice::NONE);
	BOOST_CHECK(checkClientVersion(warn, true, version(1, 0, 0)) == VersionNotice::NONE);

	const VersionPolicy reject = policy(MinClientMode::REJECT, version(0, 3, 0));
	BOOST_CHECK(checkClientVersion(reject, true, version(0, 2, 0)) == VersionNotice::UPDATE_REQUIRED);
	BOOST_CHECK(checkClientVersion(reject, true, version(0, 3, 1)) == VersionNotice::NONE);
}

BOOST_AUTO_TEST_CASE( client_without_a_version_is_older_than_any_minimum )
{
	BOOST_CHECK(checkClientVersion(policy(MinClientMode::WARN, version(0, 0, 0)), false, ForkVersion())
				== VersionNotice::UPDATE_AVAILABLE);
	BOOST_CHECK(checkClientVersion(policy(MinClientMode::REJECT, version(0, 0, 0)), false, ForkVersion())
				== VersionNotice::UPDATE_REQUIRED);
}

BOOST_AUTO_TEST_CASE( enter_server_packet_carries_features_and_version )
{
	RakNet::BitStream stream;
	makeEnterServerPacket(stream, PlayerIdentity("Ola"));
	stream.IgnoreBytes(1);
	NetworkPlayer player(PlayerID(), stream);
	BOOST_CHECK_EQUAL(player.getName(), "Ola");
	BOOST_CHECK_EQUAL(readClientFeatures(stream), (uint32_t)CLIENT_FEATURE_TOURNAMENT);
	ForkVersion sent;
	BOOST_REQUIRE(readClientVersion(stream, sent));
	BOOST_CHECK(sent == currentForkVersion());
	BOOST_CHECK_EQUAL(stream.GetNumberOfUnreadBits(), 0);
}

BOOST_AUTO_TEST_CASE( server_of_0_2_0_reads_the_new_enter_packet )
{
	// a server of 0.2.0 reads the name, the color and the features, and leaves the version unread
	RakNet::BitStream stream;
	PlayerIdentity ola("Ola");
	makeEnterServerPacket(stream, ola);
	stream.IgnoreBytes(1);
	NetworkPlayer player(PlayerID(), stream);
	BOOST_CHECK_EQUAL(player.getName(), "Ola");
	BOOST_CHECK_EQUAL(readClientFeatures(stream), (uint32_t)CLIENT_FEATURE_TOURNAMENT);
	BOOST_CHECK_EQUAL(stream.GetNumberOfUnreadBits(), 128);
}

BOOST_AUTO_TEST_CASE( enter_packet_of_an_old_client_has_no_version )
{
	{
		RakNet::BitStream stream;
		makeOldEnterServerPacket(stream);
		stream.IgnoreBytes(1);
		NetworkPlayer player(PlayerID(), stream);
		BOOST_CHECK_EQUAL(readClientFeatures(stream), (uint32_t)CLIENT_FEATURE_TOURNAMENT);
		ForkVersion v = version(9, 9, 9);
		BOOST_CHECK(!readClientVersion(stream, v));
		BOOST_CHECK(v == version(9, 9, 9));
	}
	{
		// a client from before the fork sends no features either
		RakNet::BitStream stream;
		stream.Write((unsigned char)ID_ENTER_SERVER);
		stream.Write((int)LEFT_PLAYER);
		char name[16] = "Kari";
		stream.Write(name, sizeof(name));
		stream.Write((int)0);
		stream.IgnoreBytes(1);
		NetworkPlayer player(PlayerID(), stream);
		BOOST_CHECK_EQUAL(readClientFeatures(stream), 0u);
		ForkVersion v;
		BOOST_CHECK(!readClientVersion(stream, v));
	}
	{
		// trailing bytes without the marker are not a version
		RakNet::BitStream stream;
		stream.Write((uint32_t)0x12345678);
		stream.Write((uint32_t)1);
		stream.Write((uint32_t)2);
		stream.Write((uint32_t)3);
		ForkVersion v;
		BOOST_CHECK(!readClientVersion(stream, v));
	}
}

BOOST_AUTO_TEST_CASE( server_version_packet_round_trip )
{
	ServerVersionData sent;
	sent.server = version(0, 4, 1);
	sent.notice = VersionNotice::UPDATE_REQUIRED;
	sent.minimum = version(0, 4, 0);
	sent.downloadUrl = "https://example.org/last-ned";
	RakNet::BitStream stream;
	makeServerVersionPacket(stream, sent);

	RakNet::BitStream in(stream.GetData(), stream.GetNumberOfBytesUsed(), false);
	ServerVersionData read;
	BOOST_REQUIRE(readServerVersionPacket(in, read));
	BOOST_CHECK(read.server == sent.server);
	BOOST_CHECK(read.notice == sent.notice);
	BOOST_CHECK(read.minimum == sent.minimum);
	BOOST_CHECK_EQUAL(read.downloadUrl, sent.downloadUrl);
}

BOOST_AUTO_TEST_CASE( server_version_packet_without_a_link )
{
	ServerVersionData sent;
	sent.server = version(0, 3, 0);
	sent.notice = VersionNotice::NONE;
	RakNet::BitStream stream;
	makeServerVersionPacket(stream, sent);

	RakNet::BitStream in(stream.GetData(), stream.GetNumberOfBytesUsed(), false);
	ServerVersionData read;
	read.downloadUrl = "left over";
	BOOST_REQUIRE(readServerVersionPacket(in, read));
	BOOST_CHECK(read.notice == VersionNotice::NONE);
	BOOST_CHECK(read.downloadUrl.empty());
}

BOOST_AUTO_TEST_CASE( server_version_packet_drops_a_link_that_is_not_http )
{
	// a server that writes the fields by hand cannot make the game open another kind of address
	RakNet::BitStream stream;
	stream.Write((unsigned char)ID_SERVER_VERSION);
	for (int i = 0; i < 3; ++i)
		stream.Write((uint32_t)1);
	stream.Write((unsigned char)VersionNotice::UPDATE_AVAILABLE);
	for (int i = 0; i < 3; ++i)
		stream.Write((uint32_t)1);
	const std::string url = "file:///C:/Windows/System32/calc.exe";
	stream.Write((unsigned char)url.size());
	stream.Write(url.data(), (int)url.size());

	RakNet::BitStream in(stream.GetData(), stream.GetNumberOfBytesUsed(), false);
	ServerVersionData read;
	BOOST_REQUIRE(readServerVersionPacket(in, read));
	BOOST_CHECK(read.notice == VersionNotice::UPDATE_AVAILABLE);
	BOOST_CHECK(read.downloadUrl.empty());

	// and makeServerVersionPacket never sends one
	ServerVersionData bad;
	bad.downloadUrl = url;
	RakNet::BitStream out;
	makeServerVersionPacket(out, bad);
	RakNet::BitStream back(out.GetData(), out.GetNumberOfBytesUsed(), false);
	BOOST_REQUIRE(readServerVersionPacket(back, read));
	BOOST_CHECK(read.downloadUrl.empty());
}

BOOST_AUTO_TEST_CASE( server_version_packet_that_is_cut_or_odd_is_refused )
{
	ServerVersionData sent;
	sent.server = version(0, 3, 0);
	sent.notice = VersionNotice::UPDATE_AVAILABLE;
	sent.downloadUrl = "https://example.org/last-ned";
	RakNet::BitStream stream;
	makeServerVersionPacket(stream, sent);
	const int bytes = stream.GetNumberOfBytesUsed();

	// every cut packet is refused and leaves the data alone
	for (int length = 0; length < bytes; ++length)
	{
		RakNet::BitStream in(stream.GetData(), length, false);
		ServerVersionData read;
		read.downloadUrl = "kept";
		BOOST_CHECK_MESSAGE(!readServerVersionPacket(in, read), length);
		BOOST_CHECK_EQUAL(read.downloadUrl, "kept");
	}

	// an unknown notice
	std::vector<unsigned char> odd(stream.GetData(), stream.GetData() + bytes);
	odd[13] = 3;
	RakNet::BitStream in(odd.data(), odd.size(), false);
	ServerVersionData read;
	BOOST_CHECK(!readServerVersionPacket(in, read));

	// another packet type
	std::vector<unsigned char> other(stream.GetData(), stream.GetData() + bytes);
	other[0] = ID_LOBBY;
	RakNet::BitStream lobby(other.data(), other.size(), false);
	BOOST_CHECK(!readServerVersionPacket(lobby, read));
}

BOOST_AUTO_TEST_CASE( server_version_packet_with_more_fields_is_read )
{
	// a newer server may add fields at the end
	ServerVersionData sent;
	sent.server = version(1, 0, 0);
	sent.notice = VersionNotice::UPDATE_AVAILABLE;
	RakNet::BitStream stream;
	makeServerVersionPacket(stream, sent);
	stream.Write((uint32_t)0xDEADBEEF);

	RakNet::BitStream in(stream.GetData(), stream.GetNumberOfBytesUsed(), false);
	ServerVersionData read;
	BOOST_REQUIRE(readServerVersionPacket(in, read));
	BOOST_CHECK(read.server == version(1, 0, 0));
}

BOOST_AUTO_TEST_CASE( server_version_id_is_new )
{
	// clients of 0.2.0 know packet types up to ID_SPONSORS and skip any other
	BOOST_CHECK_EQUAL((int)ID_SERVER_VERSION, (int)ID_SPONSORS + 1);
	BOOST_CHECK((int)ID_SERVER_VERSION < 256);
}

BOOST_AUTO_TEST_CASE( version_policy_comes_from_environment_and_file )
{
	VersionPolicy p;
	std::vector<std::string> errors;

	// nothing set
	BOOST_CHECK(loadVersionPolicy(VersionPolicyFile(), envOf({}), p, errors));
	BOOST_CHECK(p.mode == MinClientMode::OFF);
	BOOST_CHECK(p.downloadUrl.empty());

	// the file, with warn as the default mode
	VersionPolicyFile file;
	file.minClientVersion = "0.3.0";
	file.downloadUrl = "https://example.org/last-ned";
	BOOST_CHECK(loadVersionPolicy(file, envOf({}), p, errors));
	BOOST_CHECK(p.mode == MinClientMode::WARN);
	BOOST_CHECK(p.minimum == version(0, 3, 0));
	BOOST_CHECK_EQUAL(p.downloadUrl, "https://example.org/last-ned");

	// the environment comes first, an empty variable keeps the file
	BOOST_CHECK(loadVersionPolicy(file, envOf({{"BLOBBY_MIN_CLIENT_VERSION", "0.4.1"}, {"BLOBBY_MIN_CLIENT_MODE", "reject"},
		{"BLOBBY_DOWNLOAD_URL", ""}}), p, errors));
	BOOST_CHECK(p.mode == MinClientMode::REJECT);
	BOOST_CHECK(p.minimum == version(0, 4, 1));
	BOOST_CHECK_EQUAL(p.downloadUrl, "https://example.org/last-ned");

	// a mode without a minimum checks nothing
	BOOST_CHECK(loadVersionPolicy(VersionPolicyFile(), envOf({{"BLOBBY_MIN_CLIENT_MODE", "reject"}}), p, errors));
	BOOST_CHECK(p.mode == MinClientMode::OFF);
	BOOST_CHECK(errors.empty());
}

BOOST_AUTO_TEST_CASE( bad_version_policy_is_an_error )
{
	const VersionPolicy before = policy(MinClientMode::WARN, version(1, 1, 1));
	for (const auto& env : std::vector<std::map<std::string, std::string>>{
			{{"BLOBBY_MIN_CLIENT_VERSION", "0.3"}},
			{{"BLOBBY_MIN_CLIENT_VERSION", "0.3.0"}, {"BLOBBY_MIN_CLIENT_MODE", "block"}},
			{{"BLOBBY_MIN_CLIENT_MODE", "Warn"}},
			{{"BLOBBY_DOWNLOAD_URL", "ftp://example.org"}},
			{{"BLOBBY_DOWNLOAD_URL", "example.org/last-ned"}}})
	{
		VersionPolicy p = before;
		std::vector<std::string> errors;
		BOOST_CHECK(!loadVersionPolicy(VersionPolicyFile(), envOf(env), p, errors));
		BOOST_CHECK(!errors.empty());
		BOOST_CHECK(p.minimum == before.minimum);
	}
}

BOOST_AUTO_TEST_SUITE_END()
