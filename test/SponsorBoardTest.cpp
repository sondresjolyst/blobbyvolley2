#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>
#include "SponsorBoard.h"

namespace
{
	void putU16(std::vector<char>& d, std::size_t at, std::uint16_t v)
	{
		d[at] = char(v & 0xFF);
		d[at + 1] = char(v >> 8);
	}

	void putU32(std::vector<char>& d, std::size_t at, std::uint32_t v)
	{
		for(int i = 0; i < 4; ++i)
			d[at + i] = char((v >> (8 * i)) & 0xFF);
	}

	// builds an uncompressed BMP with a BITMAPINFOHEADER
	std::vector<char> makeBmp(std::int32_t width, std::int32_t height, std::uint16_t bpp = 24)
	{
		// pixel data size, kept small for the invalid sizes the tests use
		auto clampSize = [](std::int32_t v) -> std::uint32_t {
			if(v < 0)
				v = v < -1000 ? 1 : -v;
			return std::uint32_t(std::min<std::int32_t>(std::max<std::int32_t>(v, 1), 1000));
		};
		const std::uint32_t absHeight = clampSize(height);
		const std::uint32_t stride = (clampSize(width) * bpp / 8 + 3) / 4 * 4;
		const std::uint32_t size = 54 + stride * absHeight;
		std::vector<char> d(size, 0);
		d[0] = 'B';
		d[1] = 'M';
		putU32(d, 2, size);
		putU32(d, 10, 54);
		putU32(d, 14, 40);
		putU32(d, 18, width);
		putU32(d, 22, height);
		putU16(d, 26, 1);
		putU16(d, 28, bpp);
		putU32(d, 30, 0);
		return d;
	}

	bool valid(const std::vector<char>& d)
	{
		return sponsors::validateBmp(d.data(), d.size());
	}
}

BOOST_AUTO_TEST_SUITE(SponsorBoardTest)

BOOST_AUTO_TEST_CASE( sponsor_bmp_accepts_logo_format )
{
	BOOST_CHECK(valid(makeBmp(160, 32)));
	BOOST_CHECK(valid(makeBmp(97, 32)));
	BOOST_CHECK(valid(makeBmp(1, 1)));
	BOOST_CHECK(valid(makeBmp(120, -32)));
	BOOST_CHECK(valid(makeBmp(sponsors::MAX_LOGO_WIDTH, sponsors::MAX_LOGO_HEIGHT)));
}

BOOST_AUTO_TEST_CASE( sponsor_bmp_rejects_bad_files )
{
	BOOST_CHECK(!sponsors::validateBmp(nullptr, 0));
	BOOST_CHECK(!valid(std::vector<char>(10, 'B')));
	BOOST_CHECK(!valid(makeBmp(161, 32)));
	BOOST_CHECK(!valid(makeBmp(160, sponsors::MAX_LOGO_HEIGHT + 1)));
	BOOST_CHECK(!valid(makeBmp(0, 32)));
	BOOST_CHECK(!valid(makeBmp(-5, 32)));
	BOOST_CHECK(!valid(makeBmp(100, 0)));
	BOOST_CHECK(!valid(makeBmp(100, -2147483647 - 1)));
	BOOST_CHECK(!valid(makeBmp(100, 32, 32)));
	BOOST_CHECK(!valid(makeBmp(100, 32, 8)));

	auto wrongMagic = makeBmp(100, 32);
	wrongMagic[0] = 'P';
	BOOST_CHECK(!valid(wrongMagic));

	auto compressed = makeBmp(100, 32);
	putU32(compressed, 30, 1);
	BOOST_CHECK(!valid(compressed));

	auto truncated = makeBmp(100, 32);
	truncated.resize(truncated.size() - 1);
	BOOST_CHECK(!valid(truncated));

	auto badOffset = makeBmp(100, 32);
	putU32(badOffset, 10, 0xFFFFFFF0u);
	BOOST_CHECK(!valid(badOffset));

	auto badInfo = makeBmp(100, 32);
	putU32(badInfo, 14, 0xFFFFFFF0u);
	BOOST_CHECK(!valid(badInfo));

	std::vector<char> huge = makeBmp(100, 32);
	huge.resize(sponsors::MAX_FILE_SIZE + 1);
	BOOST_CHECK(!valid(huge));
}

BOOST_AUTO_TEST_CASE( sponsor_layout_scrolls_with_time )
{
	BOOST_CHECK(sponsors::layout({}, 1234).empty());

	const std::vector<int> widths = {100, 160};
	auto start = sponsors::layout(widths, 0);
	BOOST_REQUIRE(!start.empty());
	BOOST_CHECK_EQUAL(start[0].logo, 0u);
	BOOST_CHECK_EQUAL(start[0].x, sponsors::PANEL_LEFT);
	BOOST_CHECK_EQUAL(start[1].logo, 1u);
	BOOST_CHECK_EQUAL(start[1].x, sponsors::PANEL_LEFT + 100 + sponsors::LOGO_GAP);

	// one second later everything moved left by the scroll speed
	auto later = sponsors::layout(widths, 1000);
	BOOST_REQUIRE(!later.empty());
	BOOST_CHECK_EQUAL(later[0].x, sponsors::PANEL_LEFT - sponsors::SCROLL_SPEED);

	// after a full cycle the board looks the same
	const int cycle = 100 + 160 + 2 * sponsors::LOGO_GAP;
	auto wrapped = sponsors::layout(widths, (std::uint32_t(cycle) * 1000 + sponsors::SCROLL_SPEED - 1) / sponsors::SCROLL_SPEED);
	BOOST_REQUIRE_EQUAL(wrapped.size(), start.size());
	for(std::size_t i = 0; i < start.size(); ++i)
	{
		BOOST_CHECK_EQUAL(wrapped[i].logo, start[i].logo);
		BOOST_CHECK_EQUAL(wrapped[i].x, start[i].x);
	}
}

BOOST_AUTO_TEST_CASE( sponsor_layout_fills_board )
{
	// a single small logo repeats across the whole board
	for(std::uint32_t t : {0u, 777u, 4294967295u})
	{
		auto placements = sponsors::layout({32}, t);
		BOOST_REQUIRE(!placements.empty());
		BOOST_CHECK(placements.front().x <= sponsors::PANEL_LEFT + sponsors::LOGO_GAP);
		BOOST_CHECK(placements.back().x + 32 + sponsors::LOGO_GAP >= sponsors::PANEL_LEFT + sponsors::PANEL_WIDTH);
		for(const auto& p : placements)
		{
			BOOST_CHECK(p.x + 32 > sponsors::PANEL_LEFT);
			BOOST_CHECK(p.x < sponsors::PANEL_LEFT + sponsors::PANEL_WIDTH);
		}
	}
}

BOOST_AUTO_TEST_CASE( sponsor_logo_clipping_and_scaling )
{
	int skip = 0, visible = 0;
	BOOST_CHECK(sponsors::clipToPanel(sponsors::PANEL_LEFT - 10, 50, skip, visible));
	BOOST_CHECK_EQUAL(skip, 10);
	BOOST_CHECK_EQUAL(visible, 40);
	BOOST_CHECK(sponsors::clipToPanel(sponsors::PANEL_LEFT + sponsors::PANEL_WIDTH - 20, 50, skip, visible));
	BOOST_CHECK_EQUAL(skip, 0);
	BOOST_CHECK_EQUAL(visible, 20);
	BOOST_CHECK(!sponsors::clipToPanel(sponsors::PANEL_LEFT - 50, 50, skip, visible));

	BOOST_CHECK_EQUAL(sponsors::scaledWidth(160, 32), 5 * sponsors::LOGO_HEIGHT);
	BOOST_CHECK_EQUAL(sponsors::scaledWidth(1, 40), 1);
	BOOST_CHECK(sponsors::LOGO_HEIGHT <= sponsors::PANEL_HEIGHT);
}

BOOST_AUTO_TEST_SUITE_END()
