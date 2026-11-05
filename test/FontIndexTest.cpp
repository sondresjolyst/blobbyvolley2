#include <boost/test/unit_test.hpp>

#include <string>
#include "FontIndex.h"

namespace
{
	// returns the index of the first character and checks how far the iterator moved
	int firstIndex(const std::string& text, size_t& consumed)
	{
		auto iter = text.cbegin();
		int index = getNextFontIndex(iter, text.cend());
		consumed = iter - text.cbegin();
		return index;
	}
}

BOOST_AUTO_TEST_SUITE(FontIndexTest)

BOOST_AUTO_TEST_CASE( font_index_truncated_characters )
{
	const char* truncated[] = { "\xC2", "\xC3", "\xC4", "\xC5", "\xF0", "\xF0\x9F", "\xF0\x9F\x91", "\xF0\x9F\x94" };
	for(const char* text : truncated)
	{
		std::string str(text);
		size_t consumed = 0;
		BOOST_CHECK_EQUAL(firstIndex(str, consumed), FONT_INDEX_UNKNOWN);
		BOOST_CHECK(consumed <= str.size());
	}
}

BOOST_AUTO_TEST_CASE( font_index_valid_characters )
{
	struct Case { const char* text; int index; };
	const Case cases[] = {
		{ "0", 0 }, { "a", 10 }, { "Z", 35 }, { ".", 36 }, { "?", 47 },
		{ "\xC3\x9F", 40 }, { "\xC3\xA4", 41 }, { "\xC3\xB6", 42 }, { "\xC3\xBC", 43 },
		{ "\xC2\xBF", 56 },
		{ "\xF0\x9F\x91\x81", 57 }, { "\xF0\x9F\x94\x92", 58 },
		{ "\xC4\x86", 12 }, { "\xC5\x9A", 28 }
	};
	for(const Case& c : cases)
	{
		std::string str(c.text);
		size_t consumed = 0;
		BOOST_CHECK_EQUAL(firstIndex(str, consumed), c.index);
		BOOST_CHECK_EQUAL(consumed, str.size());
	}
}

BOOST_AUTO_TEST_SUITE_END()
