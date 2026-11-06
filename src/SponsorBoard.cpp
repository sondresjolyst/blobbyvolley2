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

namespace sponsors
{

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

std::vector<Placement> layout(const std::vector<int>& widths, std::uint32_t timeMs)
{
	std::vector<Placement> result;

	long long cycle = 0;
	for(int w : widths)
		cycle += std::max(w, 0) + LOGO_GAP;
	if(widths.empty())
		return result;

	const long long scrolled = (long long)timeMs * SCROLL_SPEED / 1000;
	long long x = PANEL_LEFT - scrolled % cycle;

	// every step moves right by at least LOGO_GAP, so this ends
	for(std::size_t i = 0; x < PANEL_LEFT + PANEL_WIDTH; i = (i + 1) % widths.size())
	{
		const int w = std::max(widths[i], 0);
		if(x + w > PANEL_LEFT)
			result.push_back(Placement{i, int(x)});
		x += w + LOGO_GAP;
	}
	return result;
}

bool clipToPanel(int x, int width, int& skip, int& visible)
{
	const int left = std::max(x, PANEL_LEFT);
	const int right = std::min(x + width, PANEL_LEFT + PANEL_WIDTH);
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

const std::vector<Fill>& boardBody()
{
	static const std::vector<Fill> body = [] {
		std::vector<Fill> fills;
		const int left = BOARD_LEFT;
		const int width = BOARD_WIDTH;
		const int bottom = BOARD_BOTTOM;
		const int top = BOARD_TOP;

		// Cast shadow, a sheared strip from the base going right and slightly back,
		// 1.1 x the height sideways and 0.1 x the height back. Most of it lies behind the board.
		const int shadowDx = BOARD_HEIGHT * 11 / 10;
		const int shadowDy = std::max(2, BOARD_HEIGHT / 10);
		for(int r = 0; r < shadowDy; ++r)
		{
			const int x0 = left + shadowDx * r / shadowDy;
			const int x1 = BOARD_RIGHT + shadowDx * (r + 1) / shadowDy;
			fills.push_back(Fill{x0, bottom - 1 - r, x1 - x0, 1, 80, 84, 78, 255, Blend::MULTIPLY});
			// slightly soft outer edge like the palm shadows
			fills.push_back(Fill{x1, bottom - 1 - r, 2, 1, 150, 152, 148, 255, Blend::MULTIPLY});
		}

		// contact darkening in front of the base, small because the front is sunlit
		fills.push_back(Fill{left - 2, bottom, width + 4, 1, 120, 122, 118, 255, Blend::MULTIPLY});
		fills.push_back(Fill{left - 3, bottom + 1, width + 6, 1, 190, 190, 186, 255, Blend::MULTIPLY});
		fills.push_back(Fill{left - 3, bottom + 2, width + 6, 1, 225, 225, 222, 255, Blend::MULTIPLY});

		// top face, seen from slightly above
		for(int row = 0; row < TOP_FACE; ++row)
		{
			const auto shade = std::uint8_t(150 - row * 10);
			fills.push_back(Fill{left + 2 - row, top + row, width - 4 + 2 * row, 1,
				shade, std::uint8_t(shade + 2), std::uint8_t(shade + 10), 255, Blend::SOLID});
		}

		// metal front, a little wider towards the bottom, lit from above
		for(int row = TOP_FACE; row < BOARD_HEIGHT; ++row)
		{
			const int grow = (row - TOP_FACE) / 6;
			const auto shade = std::uint8_t(92 - row * 2);
			fills.push_back(Fill{left - grow, top + row, width + 2 * grow, 1,
				shade, std::uint8_t(shade + 3), std::uint8_t(shade + 9), 255, Blend::SOLID});
		}

		// dark LED panel, a little lighter at the top
		for(int row = 0; row < PANEL_HEIGHT; ++row)
		{
			const auto shade = std::uint8_t(14 - row / 2);
			fills.push_back(Fill{PANEL_LEFT, PANEL_TOP + row, PANEL_WIDTH, 1,
				shade, std::uint8_t(shade + 1), std::uint8_t(shade + 3), 255, Blend::SOLID});
		}
		return fills;
	}();
	return body;
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
