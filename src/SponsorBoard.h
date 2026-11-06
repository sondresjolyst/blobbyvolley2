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
	/// The board sits behind the net at the horizon, below the score and above the players.
	constexpr int BOARD_LEFT = 0;
	constexpr int BOARD_WIDTH = 800;
	constexpr int BOARD_TOP = 322;
	constexpr int BOARD_HEIGHT = 56;
	constexpr int PANEL_TOP = BOARD_TOP + 6;
	constexpr int PANEL_HEIGHT = BOARD_HEIGHT - 12;

	/// The part of the net pole that is drawn again on top of the board.
	constexpr int ROD_LEFT = 400 - 7;
	constexpr int ROD_WIDTH = 14;

	/// Space between two logos and scroll speed in pixels per second.
	constexpr int LOGO_GAP = 40;
	constexpr int SCROLL_SPEED = 45;

	/// Checks that data holds an uncompressed 24-bit BMP within the size limits
	/// whose pixel data fits inside the buffer. On failure, error gets the reason if given.
	bool validateBmp(const char* data, std::size_t size, std::string* error = nullptr);

	struct Placement
	{
		std::size_t logo;	///< index into the logo list
		int x;				///< left edge in game coordinates
	};

	/// Computes which logos are visible and where, for logos of the given widths,
	/// at the given time in milliseconds. The time comes from the render path,
	/// so the board scrolls with the clock and not with the simulation.
	std::vector<Placement> layout(const std::vector<int>& widths, std::uint32_t timeMs);

	/// Alpha of the dark LED grid laid over the panel at a panel pixel. Repeats every 2 pixels.
	std::uint8_t ledMaskAlpha(int x, int y);

	/// Reads all BMP files in the given PhysFS directory, sorted by name.
	/// Files that are too large, unreadable or not valid logos are skipped with a warning.
	std::vector<std::vector<char>> readLogoFiles(const std::string& directory = "sponsors");
}
