#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

#include "StringUtil.h"

BOOST_AUTO_TEST_SUITE( string_util )

BOOST_AUTO_TEST_CASE( port_reads_1_to_65535 )
{
	uint16_t port = 0;
	BOOST_CHECK(parsePort("1", port));
	BOOST_CHECK_EQUAL(port, 1);
	BOOST_CHECK(parsePort("1234", port));
	BOOST_CHECK_EQUAL(port, 1234);
	BOOST_CHECK(parsePort("65535", port));
	BOOST_CHECK_EQUAL(port, 65535);
}

BOOST_AUTO_TEST_CASE( port_refuses_bad_text_and_keeps_the_old_value )
{
	uint16_t port = 7;
	for (const char* text : {"", "0", "65536", "99999", "123456", "-1", "+1", " 1", "1 ", "12a", "0x10"})
	{
		BOOST_CHECK_MESSAGE(!parsePort(text, port), text);
		BOOST_CHECK_EQUAL(port, 7);
	}
}

BOOST_AUTO_TEST_CASE( port_refuses_leading_zeros_unless_allowed )
{
	uint16_t port = 7;
	BOOST_CHECK(!parsePort("01", port));
	BOOST_CHECK(!parsePort("00080", port));
	BOOST_CHECK_EQUAL(port, 7);

	BOOST_CHECK(parsePort("01", port, true));
	BOOST_CHECK_EQUAL(port, 1);
	BOOST_CHECK(parsePort("00080", port, true));
	BOOST_CHECK_EQUAL(port, 80);
	BOOST_CHECK(!parsePort("00", port, true));
	BOOST_CHECK(!parsePort("000000", port, true));
}

BOOST_AUTO_TEST_SUITE_END()
