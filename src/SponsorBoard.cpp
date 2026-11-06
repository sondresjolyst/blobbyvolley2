/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

/* header include */
#include "SponsorBoard.h"

/* includes */
#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>

#include "FileRead.h"
#include "FileSystem.h"

/* implementation */
namespace
{
	std::uint32_t readU32(const char* data, std::size_t offset)
	{
		const auto* p = reinterpret_cast<const unsigned char*>(data + offset);
		return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
	}

	std::uint16_t readU16(const char* data, std::size_t offset)
	{
		const auto* p = reinterpret_cast<const unsigned char*>(data + offset);
		return std::uint16_t(p[0] | (p[1] << 8));
	}

	bool fail(std::string* error, const char* reason)
	{
		if(error)
			*error = reason;
		return false;
	}

	bool hasBmpExtension(const std::string& name)
	{
		if(name.size() < 4)
			return false;
		std::string ext = name.substr(name.size() - 4);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		return ext == ".bmp";
	}
}

namespace
{
	using sponsors::Fill;
	using sponsors::Blend;

	std::uint8_t clampByte(int v)
	{
		return std::uint8_t(std::max(0, std::min(255, v)));
	}

	void solid(std::vector<Fill>& fills, int x, int y, int w, int h, int r, int g, int b)
	{
		fills.push_back(Fill{x, y, w, h, clampByte(r), clampByte(g), clampByte(b), 255, Blend::SOLID});
	}

	void alpha(std::vector<Fill>& fills, int x, int y, int w, int h, int r, int g, int b, int a)
	{
		fills.push_back(Fill{x, y, w, h, clampByte(r), clampByte(g), clampByte(b), clampByte(a), Blend::ALPHA});
	}

	void multiply(std::vector<Fill>& fills, int x, int y, int w, int h, int r, int g, int b)
	{
		fills.push_back(Fill{x, y, w, h, clampByte(r), clampByte(g), clampByte(b), 255, Blend::MULTIPLY});
	}

	// cast shadow and contact darkening shared by all styles, for a board of the given height
	void groundShadow(std::vector<Fill>& fills, int height)
	{
		using namespace sponsors;
		const int left = BOARD_LEFT, width = BOARD_WIDTH, bottom = BOARD_BOTTOM;

		// a sheared strip from the base going right and slightly back,
		// 1.1 x the height sideways and 0.1 x the height back. Most of it lies behind the board.
		const int shadowDx = height * 11 / 10;
		const int shadowDy = std::max(2, height / 10);
		for(int r = 0; r < shadowDy; ++r)
		{
			const int x0 = left + shadowDx * r / shadowDy;
			const int x1 = BOARD_RIGHT + shadowDx * (r + 1) / shadowDy;
			multiply(fills, x0, bottom - 1 - r, x1 - x0, 1, 80, 84, 78);
			// slightly soft outer edge like the palm shadows
			multiply(fills, x1, bottom - 1 - r, 2, 1, 150, 152, 148);
		}

		// contact darkening in front of the base, small because the front is sunlit
		multiply(fills, left - 2, bottom, width + 4, 1, 120, 122, 118);
		multiply(fills, left - 3, bottom + 1, width + 6, 1, 190, 190, 186);
		multiply(fills, left - 3, bottom + 2, width + 6, 1, 225, 225, 222);
	}

	// seams between equal panels, every 119 px
	template<class F>
	void forSeams(int from, int to, F f)
	{
		for(int x = from + 119; x < to - 10; x += 119)
			f(x);
	}

	sponsors::BoardLook makeLed()
	{
		using namespace sponsors;
		BoardLook look{};
		const int left = BOARD_LEFT, width = BOARD_WIDTH, height = BOARD_HEIGHT, top = BOARD_TOP;
		groundShadow(look.under, height);

		// top face, seen from slightly above
		for(int row = 0; row < TOP_FACE; ++row)
			solid(look.under, left + 2 - row, top + row, width - 4 + 2 * row, 1, 150 - row * 10, 152 - row * 10, 160 - row * 10);

		// metal front, a little wider towards the bottom, lit from above
		for(int row = TOP_FACE; row < height; ++row)
		{
			const int grow = (row - TOP_FACE) / 6;
			const int shade = 92 - row * 2;
			solid(look.under, left - grow, top + row, width + 2 * grow, 1, shade, shade + 3, shade + 9);
		}

		// dark LED panel, a little lighter at the top
		look.panelX = PANEL_LEFT;
		look.panelY = PANEL_TOP;
		look.panelW = PANEL_WIDTH;
		look.panelH = PANEL_HEIGHT;
		for(int row = 0; row < look.panelH; ++row)
			solid(look.under, look.panelX, look.panelY + row, look.panelW, 1, 14 - row / 2, 15 - row / 2, 17 - row / 2);
		look.ledGrid = true;
		return look;
	}

	sponsors::BoardLook makePrinted()
	{
		using namespace sponsors;
		BoardLook look{};
		const int left = BOARD_LEFT, width = BOARD_WIDTH, height = BOARD_HEIGHT, top = BOARD_TOP;
		groundShadow(look.under, height);

		for(int row = 0; row < 2; ++row)
			solid(look.under, left + 1 - row, top + row, width - 2 + 2 * row, 1, 205 - row * 15, 205 - row * 15, 210 - row * 15);
		solid(look.under, left, top + 2, width, height - 2, 140, 142, 148);

		look.panelX = left + 1;
		look.panelY = top + 3;
		look.panelW = width - 2;
		look.panelH = height - 4;
		for(int row = 0; row < look.panelH; ++row)
			solid(look.under, look.panelX, look.panelY + row, look.panelW, 1, 248 - row * 2, 248 - row * 2, 246 - row * 2);

		const BoardLook* self = &look;
		forSeams(left, BOARD_RIGHT, [&](int x) { solid(look.over, x, self->panelY, 1, self->panelH, 170, 172, 178); });
		look.ledGrid = false;
		return look;
	}

	void woodPost(std::vector<Fill>& fills, int x, int top, int height, bool cap)
	{
		const int postW = 6;
		solid(fills, x, top - 5, postW, height + 5, 104, 66, 34);
		solid(fills, x, top - 5, 2, height + 5, 132, 88, 48);
		if(cap)
			solid(fills, x, top - 6, postW, 1, 150, 104, 60);
	}

	sponsors::BoardLook makeWood()
	{
		using namespace sponsors;
		BoardLook look{};
		const int left = BOARD_LEFT, width = BOARD_WIDTH, height = BOARD_HEIGHT, top = BOARD_TOP;
		const int centerPost = left + width / 2 - 3;
		groundShadow(look.under, height);

		woodPost(look.under, left - 2, top, height, true);
		woodPost(look.under, centerPost, top, height, true);
		woodPost(look.under, BOARD_RIGHT - 4, top, height, true);

		// planks above and below the panel, a darker groove every 8 rows
		for(int row = 0; row < height; ++row)
		{
			if(row >= 4 && row < height - 4)
				continue;
			const int shade = 150 - row * 2 - ((row % 8 == 7) ? 28 : 0);
			solid(look.under, left + 4, top + row, width - 8, 1, shade, shade * 64 / 100, shade * 34 / 100);
		}
		solid(look.under, left + 4, top + 4, 4, height - 8, 120, 78, 40);
		solid(look.under, BOARD_RIGHT - 8, top + 4, 4, height - 8, 120, 78, 40);

		look.panelX = left + 8;
		look.panelY = top + 4;
		look.panelW = width - 16;
		look.panelH = height - 8;
		for(int row = 0; row < look.panelH; ++row)
			solid(look.under, look.panelX, look.panelY + row, look.panelW, 1, 238 - row, 228 - row, 204 - row);

		// the middle post stands in front of the panel
		woodPost(look.over, centerPost, top, height, true);
		look.ledGrid = false;
		return look;
	}

	sponsors::BoardLook makeSlim()
	{
		using namespace sponsors;
		BoardLook look{};
		const int left = BOARD_LEFT, width = BOARD_WIDTH, height = 26, bottom = BOARD_BOTTOM;
		const int top = bottom - height;
		groundShadow(look.under, height);

		look.panelX = left;
		look.panelY = top;
		look.panelW = width;
		look.panelH = height;
		for(int row = 0; row < height; ++row)
			solid(look.under, left, top + row, width, 1, 10 - row / 3, 10 - row / 3, 12 - row / 3);

		// glossy reflection strip on top and a thin edge at the base
		solid(look.over, left, top, width, 1, 150, 155, 165);
		alpha(look.over, left, top + 1, width, 1, 255, 255, 255, 46);
		alpha(look.over, left, top + 2, width, 1, 255, 255, 255, 30);
		alpha(look.over, left, top + 3, width, 1, 255, 255, 255, 16);
		solid(look.over, left, bottom - 1, width, 1, 40, 42, 46);
		look.ledGrid = true;
		return look;
	}

	sponsors::BoardLook makePadded()
	{
		using namespace sponsors;
		BoardLook look{};
		const int left = BOARD_LEFT, width = BOARD_WIDTH, height = BOARD_HEIGHT, top = BOARD_TOP;
		groundShadow(look.under, height);

		// rounded ends, lighter at the top and darker at the bottom
		const int inset[] = {5, 3, 2, 1, 1};
		for(int row = 0; row < height; ++row)
		{
			int in = 0;
			if(row < 5)
				in = inset[row];
			if(height - 1 - row < 5)
				in = inset[height - 1 - row];
			const int light = row < height / 3 ? 30 - row * 2 : (row > height * 2 / 3 ? -(row - height * 2 / 3) * 3 : 10);
			solid(look.under, left + in, top + row, width - 2 * in, 1, 28 + light, 78 + light, 156 + light);
		}
		forSeams(left, BOARD_RIGHT, [&](int x) {
			solid(look.under, x, top + 2, 1, height - 4, 16, 50, 110);
			solid(look.under, x + 1, top + 2, 1, height - 4, 60, 110, 190);
		});

		look.panelX = left + 6;
		look.panelY = top + 4;
		look.panelW = width - 12;
		look.panelH = height - 8;
		look.ledGrid = false;
		return look;
	}
}

namespace sponsors
{

BoardStyle parseBoardStyle(const std::string& name)
{
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	const auto first = lower.find_first_not_of(" \t\r\n");
	const auto last = lower.find_last_not_of(" \t\r\n");
	lower = first == std::string::npos ? std::string() : lower.substr(first, last - first + 1);

	for(BoardStyle style : {BoardStyle::OFF, BoardStyle::LED, BoardStyle::PRINTED, BoardStyle::WOOD, BoardStyle::SLIM, BoardStyle::PADDED})
	{
		if(lower == boardStyleName(style))
			return style;
	}
	return BoardStyle::LED;
}

const char* boardStyleName(BoardStyle style)
{
	switch(style)
	{
		case BoardStyle::OFF: return "off";
		case BoardStyle::LED: return "led";
		case BoardStyle::PRINTED: return "printed";
		case BoardStyle::WOOD: return "wood";
		case BoardStyle::SLIM: return "slim";
		case BoardStyle::PADDED: return "padded";
	}
	return "led";
}

const BoardLook& boardLook(BoardStyle style)
{
	static const BoardLook led = makeLed();
	static const BoardLook printed = makePrinted();
	static const BoardLook wood = makeWood();
	static const BoardLook slim = makeSlim();
	static const BoardLook padded = makePadded();
	switch(style)
	{
		case BoardStyle::PRINTED: return printed;
		case BoardStyle::WOOD: return wood;
		case BoardStyle::SLIM: return slim;
		case BoardStyle::PADDED: return padded;
		default: return led;
	}
}


bool validateBmp(const char* data, std::size_t size, std::string* error)
{
	// 14 byte file header and at least a 40 byte BITMAPINFOHEADER
	constexpr std::size_t FILE_HEADER = 14;
	constexpr std::size_t MIN_HEADERS = FILE_HEADER + 40;

	if(!data || size < MIN_HEADERS)
		return fail(error, "file too small");
	if(size > MAX_FILE_SIZE)
		return fail(error, "file too large");
	if(data[0] != 'B' || data[1] != 'M')
		return fail(error, "not a BMP file");

	const std::uint32_t pixelOffset = readU32(data, 10);
	const std::uint32_t infoSize = readU32(data, 14);
	if(infoSize < 40 || infoSize > size - FILE_HEADER)
		return fail(error, "bad header size");

	const auto width = std::int32_t(readU32(data, 18));
	const auto rawHeight = std::int32_t(readU32(data, 22));
	if(width < 1 || width > MAX_LOGO_WIDTH)
		return fail(error, "bad width");
	if(rawHeight == 0 || rawHeight < -MAX_LOGO_HEIGHT || rawHeight > MAX_LOGO_HEIGHT)
		return fail(error, "bad height");
	const std::uint32_t height = rawHeight < 0 ? std::uint32_t(-rawHeight) : std::uint32_t(rawHeight);

	if(readU16(data, 26) != 1)
		return fail(error, "bad plane count");
	if(readU16(data, 28) != 24)
		return fail(error, "not 24 bits per pixel");
	if(readU32(data, 30) != 0)
		return fail(error, "compressed BMP");

	// rows are padded to 4 bytes, all values here are small so nothing overflows
	const std::uint32_t stride = (std::uint32_t(width) * 3 + 3) / 4 * 4;
	if(pixelOffset < FILE_HEADER + infoSize || pixelOffset > size)
		return fail(error, "bad pixel offset");
	if(stride * height > size - pixelOffset)
		return fail(error, "pixel data truncated");

	return true;
}

std::vector<Placement> layout(const std::vector<int>& widths, std::uint32_t timeMs, int panelLeft, int panelWidth)
{
	std::vector<Placement> result;

	long long cycle = 0;
	for(int w : widths)
		cycle += std::max(w, 0) + LOGO_GAP;
	if(widths.empty())
		return result;

	const long long scrolled = (long long)timeMs * SCROLL_SPEED / 1000;
	long long x = panelLeft - scrolled % cycle;

	// every step moves right by at least LOGO_GAP, so this ends
	for(std::size_t i = 0; x < panelLeft + panelWidth; i = (i + 1) % widths.size())
	{
		const int w = std::max(widths[i], 0);
		if(x + w > panelLeft)
			result.push_back(Placement{i, int(x)});
		x += w + LOGO_GAP;
	}
	return result;
}

bool clipToPanel(int x, int width, int& skip, int& visible, int panelLeft, int panelWidth)
{
	const int left = std::max(x, panelLeft);
	const int right = std::min(x + width, panelLeft + panelWidth);
	skip = left - x;
	visible = right - left;
	return visible > 0;
}

std::uint8_t ledMaskAlpha(int x, int y)
{
	if(y & 1)
		return 80;
	if(x & 1)
		return 40;
	return 0;
}

const std::vector<BackgroundCopy>& sandDrift()
{
	static const std::vector<BackgroundCopy> drift = [] {
		std::vector<BackgroundCopy> copies;
		for(int x = BOARD_LEFT - 4; x < BOARD_RIGHT; x += 5)
		{
			// a fixed pseudo random depth of 2 to 5 rows per strip
			const unsigned hash = unsigned(x) * 2654435761u;
			const int depth = 2 + int((hash >> 13) % 4);
			for(int k = 0; k < depth; ++k)
				copies.push_back(BackgroundCopy{x, BOARD_BOTTOM + 3 + k, 5, 1, x, BOARD_BOTTOM - 1 - k, std::uint8_t(230 - k * 50)});
		}
		return copies;
	}();
	return drift;
}

const std::vector<Fill>& contactLine()
{
	static const std::vector<Fill> line = {
		Fill{BOARD_LEFT - 1, BOARD_BOTTOM - 1, BOARD_WIDTH + 2, 1, 30, 26, 12, 150, Blend::ALPHA}
	};
	return line;
}

int scaledWidth(int width, int height)
{
	if(width <= 0 || height <= 0)
		return 1;
	return std::max(1, (width * LOGO_HEIGHT + height / 2) / height);
}

void ledFilter(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
{
	// mix a quarter of the grey value in and dim to 85 percent
	const int lum = (r * 30 + g * 59 + b * 11) / 100;
	auto mix = [lum](std::uint8_t c) { return std::uint8_t((c * 3 + lum) / 4 * 85 / 100); };
	r = mix(r);
	g = mix(g);
	b = mix(b);
}

std::vector<std::vector<char>> readLogoFiles(const std::string& directory)
{
	std::vector<std::vector<char>> files;

	FileSystem& fs = FileSystem::getSingleton();
	if(!fs.isDirectory(directory))
		return files;

	std::vector<std::string> names = fs.enumerateFiles(directory, "", true);
	std::sort(names.begin(), names.end());

	for(const auto& name : names)
	{
		if(files.size() >= MAX_LOGOS)
		{
			std::cerr << "Warning: only the first " << MAX_LOGOS << " sponsor logos are used" << std::endl;
			break;
		}
		if(!hasBmpExtension(name))
			continue;

		const std::string path = directory + "/" + name;
		try
		{
			FileRead file(path);
			const std::uint32_t length = file.length();
			if(length > MAX_FILE_SIZE)
			{
				std::cerr << "Warning: skipping sponsor logo " << path << ": file too large" << std::endl;
				continue;
			}

			std::vector<char> content = file.readRawBytes(length);
			std::string error;
			if(!validateBmp(content.data(), content.size(), &error))
			{
				std::cerr << "Warning: skipping sponsor logo " << path << ": " << error << std::endl;
				continue;
			}
			files.push_back(std::move(content));
		}
		catch(const std::exception& e)
		{
			std::cerr << "Warning: skipping sponsor logo " << path << ": " << e.what() << std::endl;
		}
	}

	return files;
}

}
