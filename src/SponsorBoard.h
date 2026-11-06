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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Renderer independent parts of the scrolling sponsor board in the match background.
/// The board is drawing only. Nothing here touches the simulation.
namespace sponsors
{
	/// Limits for a single logo file. Logos are 24-bit BMP files of about 32 px height.
	constexpr int MAX_LOGO_WIDTH = 160;
	constexpr int MAX_LOGO_HEIGHT = 40;
	constexpr std::size_t MAX_FILE_SIZE = 64 * 1024;
	constexpr std::size_t MAX_LOGOS = 32;

	/// Board geometry in the 800x600 game coordinates.
	/// A low LED board stands on the sand behind the court, just below the waterline.
	/// It sits below the score and above the players and is drawn behind everything else.
	constexpr int BOARD_LEFT = 16;
	constexpr int BOARD_WIDTH = 768;
	constexpr int BOARD_TOP = 314;
	constexpr int BOARD_HEIGHT = 24;
	constexpr int FRAME = 2;
	constexpr int PANEL_LEFT = BOARD_LEFT + FRAME;
	constexpr int PANEL_WIDTH = BOARD_WIDTH - 2 * FRAME;
	constexpr int PANEL_TOP = BOARD_TOP + FRAME;
	constexpr int PANEL_HEIGHT = BOARD_HEIGHT - 2 * FRAME;
	constexpr int LEG_HEIGHT = 5;
	constexpr int SHADOW_HEIGHT = 8;
	/// everything the board draws, including legs and shadow, lies in this band
	constexpr int AREA_HEIGHT = BOARD_HEIGHT + SHADOW_HEIGHT;

	/// Logos are scaled to this height on the panel.
	constexpr int LOGO_HEIGHT = 16;

	/// The part of the net pole that is drawn again on top of the board.
	constexpr int ROD_LEFT = 400 - 7;
	constexpr int ROD_WIDTH = 14;

	/// Space between two logos and scroll speed in pixels per second.
	constexpr int LOGO_GAP = 36;
	constexpr int SCROLL_SPEED = 40;

	/// A filled rectangle of the board body. Alpha below 255 means blended.
	struct Fill
	{
		int x, y, w, h;
		std::uint8_t r, g, b, a;
	};

	/// Shadow, legs, frame and panel, in drawing order. Both renderers draw the same list.
	const std::vector<Fill>& boardBody();

	/// Width of a logo of the given size after scaling it to LOGO_HEIGHT.
	int scaledWidth(int width, int height);

	/// Dims and desaturates a logo pixel a little so it looks lit by LEDs.
	void ledFilter(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b);

	/// Checks that data holds an uncompressed 24-bit BMP within the size limits
	/// whose pixel data fits inside the buffer. On failure, error gets the reason if given.
	bool validateBmp(const char* data, std::size_t size, std::string* error = nullptr);

	struct Placement
	{
		std::size_t logo;	///< index into the logo list
		int x;				///< left edge in game coordinates, may lie left of the panel
	};

	/// The part of a logo at x with the given width that lies on the panel.
	/// Returns false when nothing is visible. skip is the number of columns cut off on the left.
	bool clipToPanel(int x, int width, int& skip, int& visible);

	/// Computes which logos are visible on the panel and where, for scaled logos of the given widths,
	/// at the given time in milliseconds. The time comes from the render path,
	/// so the board scrolls with the clock and not with the simulation.
	std::vector<Placement> layout(const std::vector<int>& widths, std::uint32_t timeMs);

	/// Alpha of the dark LED grid laid over the panel at a panel pixel. Repeats every 2 pixels.
	std::uint8_t ledMaskAlpha(int x, int y);

	/// Reads all BMP files in the given PhysFS directory, sorted by name.
	/// Files that are too large, unreadable or not valid logos are skipped with a warning.
	std::vector<std::vector<char>> readLogoFiles(const std::string& directory = "sponsors");
}
