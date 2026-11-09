#include <boost/test/unit_test.hpp>

#include <string>

#include "JoinLink.h"

namespace
{
	JoinLinkError parse(const std::string& text)
	{
		JoinLink link;
		return parseJoinLink(text, link);
	}
}

BOOST_AUTO_TEST_SUITE(join_link)

BOOST_AUTO_TEST_CASE(reads_the_link_the_site_builds)
{
	// the form of joinLink() in src/lib/players/join-code.ts of the site
	JoinLink link;
	BOOST_REQUIRE(parseJoinLink("blobby://join?server=lan.example.no:1234&code=K7QM2XPR4T", link) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(link.host, "lan.example.no");
	BOOST_CHECK_EQUAL(link.port, 1234);
	BOOST_CHECK_EQUAL(link.code, "K7QM2XPR4T");
}

BOOST_AUTO_TEST_CASE(reads_ipv4_hosts_and_port_limits)
{
	JoinLink link;
	BOOST_REQUIRE(parseJoinLink("blobby://join?server=192.168.1.20:1&code=B9HNZ3WD6F", link) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(link.host, "192.168.1.20");
	BOOST_CHECK_EQUAL(link.port, 1);
	BOOST_REQUIRE(parseJoinLink("blobby://join?server=10.0.0.1:65535&code=B9HNZ3WD6F", link) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(link.port, 65535);
	BOOST_REQUIRE(parseJoinLink("blobby://join?server=localhost:1234&code=B9HNZ3WD6F", link) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(link.host, "localhost");
}

BOOST_AUTO_TEST_CASE(allows_case_order_and_trailing_slashes)
{
	JoinLink link;
	BOOST_REQUIRE(parseJoinLink("BLOBBY://Join?code=k7qm2xpr4t&server=LAN.Example.NO:1234", link) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(link.host, "lan.example.no");
	BOOST_CHECK_EQUAL(link.code, "K7QM2XPR4T");
	BOOST_CHECK(parse("blobby://join/?server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::NONE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4T/") == JoinLinkError::NONE);
}

BOOST_AUTO_TEST_CASE(leaves_the_link_alone_on_error)
{
	JoinLink link;
	link.host = "before";
	BOOST_CHECK(parseJoinLink("blobby://join?server=lan:0&code=K7QM2XPR4T", link) == JoinLinkError::BAD_PORT);
	BOOST_CHECK_EQUAL(link.host, "before");
	BOOST_CHECK_EQUAL(link.port, 0);
	BOOST_CHECK(link.code.empty());
}

BOOST_AUTO_TEST_CASE(refuses_other_forms)
{
	BOOST_CHECK(parse("") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby:") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby://") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby://join") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby://lan:1234/K7QM2XPR4T") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby://joinx?server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby://join//?server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby:join?server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("http://join?server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(parse("blobby://join#server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::NOT_A_JOIN_LINK);
}

BOOST_AUTO_TEST_CASE(refuses_bad_parameters)
{
	BOOST_CHECK(parse("blobby://join?") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?server=lan:1234") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?code=K7QM2XPR4T") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4T&") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&&code=K7QM2XPR4T") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4T&code=B9HNZ3WD6F") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4T&exec=calc") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?Server=lan:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_PARAMETERS);
	BOOST_CHECK(parse("blobby://join?server&code=K7QM2XPR4T") == JoinLinkError::BAD_PARAMETERS);
}

BOOST_AUTO_TEST_CASE(refuses_bad_hosts)
{
	BOOST_CHECK(parse("blobby://join?server=:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=-lan:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=lan-:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=lan..no:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=.lan:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=lan.:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=lan_1:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=user@lan:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=lan%2Eno:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=[::1]:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=::1:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=\"lan\":1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);

	// numbers and dots only must be an IPv4 address
	BOOST_CHECK(parse("blobby://join?server=256.1.1.1:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=1.2.3:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=1.2.3.4.5:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=01.2.3.4:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=1234:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);

	// inet_addr reads these as addresses, so a host that starts with a digit must be dotted decimal
	BOOST_CHECK(parse("blobby://join?server=0x7f000001:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=0x7f.1:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=0177.0.0.1:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=127.1:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=2fast.lan:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=9lives:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=1.2.3.4a:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=lan.0x7f:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=0X7F000001:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=fast2.lan:1234&code=K7QM2XPR4T") == JoinLinkError::NONE);
	BOOST_CHECK(parse("blobby://join?server=127.0.0.1:1234&code=K7QM2XPR4T") == JoinLinkError::NONE);
	BOOST_CHECK(parse("blobby://join?server=0.0.0.0:1234&code=K7QM2XPR4T") == JoinLinkError::NONE);

	// the client holds at most 63 bytes of host
	std::string longest(63, 'a');
	BOOST_CHECK(parse("blobby://join?server=" + longest + ":1234&code=K7QM2XPR4T") == JoinLinkError::NONE);
	BOOST_CHECK(parse("blobby://join?server=" + longest + "a:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
	BOOST_CHECK(parse("blobby://join?server=" + std::string(30, 'a') + "." + std::string(33, 'b') + ":1234&code=K7QM2XPR4T") == JoinLinkError::BAD_HOST);
}

BOOST_AUTO_TEST_CASE(refuses_bad_ports)
{
	BOOST_CHECK(parse("blobby://join?server=lan&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:0&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:01234&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:65536&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:99999&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:123456&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:-1&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:+80&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:12a&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
	BOOST_CHECK(parse("blobby://join?server=lan:12:34&code=K7QM2XPR4T") == JoinLinkError::BAD_PORT);
}

BOOST_AUTO_TEST_CASE(refuses_bad_codes)
{
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4TT") == JoinLinkError::BAD_CODE);
	// 0, O, 1, I and L are not in the alphabet
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR40") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4O") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR41") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4I") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4L") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4%") == JoinLinkError::BAD_CODE);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4=") == JoinLinkError::BAD_CODE);
}

BOOST_AUTO_TEST_CASE(refuses_odd_characters_and_length)
{
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4T ") == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2 XPR4T") == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4T\" --register-link-handler") == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(parse("blobby://join?server=lan:1234&code=K7QM2XPR4T\n") == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(parse("blobby://join?server=l\xC3\xA6n:1234&code=K7QM2XPR4T") == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(parse(std::string("blobby://join?server=lan:1234&code=K7QM2XPR4T\0", 46)) == JoinLinkError::BAD_CHARACTER);

	std::string long_link = "blobby://join?server=lan:1234&code=K7QM2XPR4T" + std::string(MAX_JOIN_LINK_BYTES, '/');
	BOOST_CHECK(parse(long_link) == JoinLinkError::TOO_LONG);
}

BOOST_AUTO_TEST_CASE(detects_link_arguments)
{
	BOOST_CHECK(looksLikeJoinLink("blobby://join?server=lan:1234&code=K7QM2XPR4T"));
	BOOST_CHECK(looksLikeJoinLink("BLOBBY:anything"));
	BOOST_CHECK(!looksLikeJoinLink("--register-link-handler"));
	BOOST_CHECK(!looksLikeJoinLink("blobby"));
	BOOST_CHECK(!looksLikeJoinLink(""));
}

BOOST_AUTO_TEST_CASE(only_external_links_ask_before_joining)
{
	BOOST_CHECK(!joinNeedsConfirmation(JoinLinkSource::IN_GAME));
	BOOST_CHECK(joinNeedsConfirmation(JoinLinkSource::EXTERNAL));
}

BOOST_AUTO_TEST_CASE(reads_pasted_text_with_space_around)
{
	JoinLink link;
	BOOST_REQUIRE(joinLinkFromText("  blobby://join?server=lan:1234&code=K7QM2XPR4T\r\n", link) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(link.host, "lan");
	BOOST_CHECK_EQUAL(link.port, 1234);
	BOOST_CHECK_EQUAL(link.code, "K7QM2XPR4T");

	JoinLink tabbed;
	BOOST_CHECK(joinLinkFromText("\t\nblobby://join?server=lan:1234&code=K7QM2XPR4T\n\n", tabbed) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(tabbed.code, "K7QM2XPR4T");
}

BOOST_AUTO_TEST_CASE(finds_no_link_in_empty_or_other_text)
{
	JoinLink link;
	BOOST_CHECK(joinLinkFromText("", link) == JoinLinkError::NO_LINK);
	BOOST_CHECK(joinLinkFromText(" \t\r\n ", link) == JoinLinkError::NO_LINK);
	BOOST_CHECK(joinLinkFromText("hello", link) == JoinLinkError::NO_LINK);
	BOOST_CHECK(joinLinkFromText("K7QM2XPR4T", link) == JoinLinkError::NO_LINK);
	BOOST_CHECK(joinLinkFromText("https://example.no/join?server=lan:1234&code=K7QM2XPR4T", link) == JoinLinkError::NO_LINK);
	BOOST_CHECK(joinLinkFromText("link: blobby://join?server=lan:1234&code=K7QM2XPR4T", link) == JoinLinkError::NO_LINK);
	BOOST_CHECK(link.host.empty());
	BOOST_CHECK(link.code.empty());
}

BOOST_AUTO_TEST_CASE(checks_pasted_links_like_command_line_links)
{
	JoinLink link;
	BOOST_CHECK(joinLinkFromText("blobby://join?server=lan:1234&code=K7QM2XPR4T and more", link) == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(joinLinkFromText("blobby://join?server=lan:1234&code=K7QM2XPR4T\nblobby://join?server=evil:1&code=K7QM2XPR4T", link) == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(joinLinkFromText("blobby://join?server=0x7f000001:1234&code=K7QM2XPR4T", link) == JoinLinkError::BAD_HOST);
	BOOST_CHECK(joinLinkFromText("blobby://join?server=lan:1234&code=K7QM2XPR4", link) == JoinLinkError::BAD_CODE);
	BOOST_CHECK(joinLinkFromText("blobby://join", link) == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(link.host.empty());
}

BOOST_AUTO_TEST_CASE(refuses_too_long_pasted_text)
{
	JoinLink link;
	std::string padded = std::string(MAX_JOIN_LINK_TEXT_BYTES - 100, ' ') + "blobby://join?server=lan:1234&code=K7QM2XPR4T";
	BOOST_CHECK(joinLinkFromText(padded, link) == JoinLinkError::NONE);

	std::string huge = std::string(MAX_JOIN_LINK_TEXT_BYTES, ' ') + "blobby://join?server=lan:1234&code=K7QM2XPR4T";
	BOOST_CHECK(joinLinkFromText(huge, link) == JoinLinkError::TOO_LONG);
	BOOST_CHECK(joinLinkFromText("blobby://join?server=lan:1234&code=K7QM2XPR4T" + std::string(MAX_JOIN_LINK_BYTES, '/'), link) == JoinLinkError::TOO_LONG);
}

BOOST_AUTO_TEST_CASE(reads_the_join_link_field)
{
	JoinLink link;
	BOOST_REQUIRE(joinLinkFromField(" blobby://join?server=lan.example.no:1234&code=k7qm2xpr4t\r\n", link) == JoinLinkError::NONE);
	BOOST_CHECK_EQUAL(link.host, "lan.example.no");
	BOOST_CHECK_EQUAL(link.port, 1234);
	BOOST_CHECK_EQUAL(link.code, "K7QM2XPR4T");

	// an empty field or other text is not a join link, with the message for that
	JoinLink other;
	BOOST_CHECK(joinLinkFromField("", other) == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(joinLinkFromField("   ", other) == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(joinLinkFromField("K7QM2XPR4T", other) == JoinLinkError::NOT_A_JOIN_LINK);
	BOOST_CHECK(joinLinkFromField("blobby://join?server=lan:1234&code=K7QM2XPR4", other) == JoinLinkError::BAD_CODE);
	BOOST_CHECK(joinLinkFromField("blobby://join?server=lan:1234&code=K7QM2 XPR4T", other) == JoinLinkError::BAD_CHARACTER);
	BOOST_CHECK(joinLinkFromField("blobby://join?server=lan:1234&code=K7QM2XPR4T" + std::string(MAX_JOIN_LINK_BYTES, '/'), other) == JoinLinkError::TOO_LONG);
	BOOST_CHECK(other.host.empty());
}

BOOST_AUTO_TEST_CASE(prefills_the_field_only_with_a_valid_link)
{
	BOOST_CHECK_EQUAL(joinLinkPrefill("blobby://join?server=lan:1234&code=K7QM2XPR4T"), "blobby://join?server=lan:1234&code=K7QM2XPR4T");
	BOOST_CHECK_EQUAL(joinLinkPrefill("\t blobby://join?server=lan:1234&code=K7QM2XPR4T\r\n"), "blobby://join?server=lan:1234&code=K7QM2XPR4T");

	BOOST_CHECK_EQUAL(joinLinkPrefill(""), "");
	BOOST_CHECK_EQUAL(joinLinkPrefill("hello"), "");
	BOOST_CHECK_EQUAL(joinLinkPrefill("blobby://join?server=lan:1234&code=K7QM2XPR4"), "");
	BOOST_CHECK_EQUAL(joinLinkPrefill("blobby://join?server=0x7f000001:1234&code=K7QM2XPR4T"), "");
	BOOST_CHECK_EQUAL(joinLinkPrefill(std::string(MAX_JOIN_LINK_TEXT_BYTES, ' ') + "blobby://join?server=lan:1234&code=K7QM2XPR4T"), "");
}

BOOST_AUTO_TEST_CASE(every_error_has_a_message)
{
	for (JoinLinkError error : {JoinLinkError::TOO_LONG, JoinLinkError::BAD_CHARACTER, JoinLinkError::NOT_A_JOIN_LINK,
	                            JoinLinkError::BAD_PARAMETERS, JoinLinkError::BAD_HOST, JoinLinkError::BAD_PORT,
	                            JoinLinkError::BAD_CODE, JoinLinkError::NO_LINK})
	{
		std::string message = describeJoinLinkError(error);
		BOOST_CHECK(!message.empty());
		BOOST_CHECK(message != describeJoinLinkError(JoinLinkError::NONE));
	}
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(link_handler_registry)

BOOST_AUTO_TEST_CASE(builds_the_values_for_a_path)
{
	std::vector<RegistryValue> values = linkHandlerRegistryValues("C:\\Games\\Blobby Volley\\blobby.exe");
	BOOST_REQUIRE_EQUAL(values.size(), 4u);

	BOOST_CHECK_EQUAL(values[0].subkey, "");
	BOOST_CHECK_EQUAL(values[0].name, "");
	BOOST_CHECK_EQUAL(values[0].data, "URL:Blobby Volley JulaLAN join link");

	BOOST_CHECK_EQUAL(values[1].subkey, "");
	BOOST_CHECK_EQUAL(values[1].name, "URL Protocol");
	BOOST_CHECK_EQUAL(values[1].data, "");

	BOOST_CHECK_EQUAL(values[2].subkey, "DefaultIcon");
	BOOST_CHECK_EQUAL(values[2].name, "");
	BOOST_CHECK_EQUAL(values[2].data, "\"C:\\Games\\Blobby Volley\\blobby.exe\",0");

	BOOST_CHECK_EQUAL(values[3].subkey, "shell\\open\\command");
	BOOST_CHECK_EQUAL(values[3].name, "");
	BOOST_CHECK_EQUAL(values[3].data, "\"C:\\Games\\Blobby Volley\\blobby.exe\" \"%1\"");
}

BOOST_AUTO_TEST_CASE(registers_below_the_user_classes)
{
	BOOST_CHECK_EQUAL(std::string(LINK_HANDLER_KEY), "Software\\Classes\\blobby");
}

BOOST_AUTO_TEST_CASE(keeps_non_ascii_paths)
{
	std::vector<RegistryValue> values = linkHandlerRegistryValues("C:\\Users\\Sj\xC3\xB8lyst\\blobby.exe");
	BOOST_REQUIRE_EQUAL(values.size(), 4u);
	BOOST_CHECK_EQUAL(values[3].data, "\"C:\\Users\\Sj\xC3\xB8lyst\\blobby.exe\" \"%1\"");
}

BOOST_AUTO_TEST_CASE(refuses_paths_that_cannot_be_quoted)
{
	BOOST_CHECK(linkHandlerRegistryValues("").empty());
	BOOST_CHECK(linkHandlerRegistryValues("C:\\a\"b\\blobby.exe").empty());
	BOOST_CHECK(linkHandlerRegistryValues("C:\\a\nb\\blobby.exe").empty());
	BOOST_CHECK(linkHandlerRegistryValues("C:\\a\tb\\blobby.exe").empty());
}

BOOST_AUTO_TEST_SUITE_END()
