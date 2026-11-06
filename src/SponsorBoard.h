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
#include <utility>
#include <vector>

/// Renderer independent parts of the scrolling sponsor board in the match background.
/// The board is drawing only. Nothing here touches the simulation.
namespace sponsors
{
	/// Limits for a single logo file. Logos are BMP files of about 32 px height, either 24-bit
	/// or 32-bit with an alpha channel (BITMAPV4HEADER or BITMAPV5HEADER with BI_BITFIELDS).
	constexpr int MAX_LOGO_WIDTH = 160;
	constexpr int MAX_LOGO_HEIGHT = 40;
	/// 14 byte file header, 124 byte BITMAPV5HEADER, 4 bytes per pixel, and some room for extra header data
	constexpr std::size_t MAX_FILE_SIZE = 14 + 124 + MAX_LOGO_WIDTH * MAX_LOGO_HEIGHT * 4 + 1024;
	constexpr std::size_t MAX_LOGOS = 32;

	/// Board geometry in the 800x600 game coordinates.
	/// A dark LED board with a metal frame stands on the sand behind the court, between the
	/// palm trunks, so nothing in the background art should be in front of it.
	/// It sits below the score and above the players and is drawn behind everything else.
	constexpr int BOARD_LEFT = 96;
	constexpr int BOARD_RIGHT = 572;
	constexpr int BOARD_WIDTH = BOARD_RIGHT - BOARD_LEFT;
	constexpr int BOARD_BOTTOM = 348;
	constexpr int BOARD_HEIGHT = 30;
	constexpr int BOARD_TOP = BOARD_BOTTOM - BOARD_HEIGHT;
	/// rows of the top face, seen from slightly above
	constexpr int TOP_FACE = 3;
	constexpr int PANEL_LEFT = BOARD_LEFT + 3;
	constexpr int PANEL_WIDTH = BOARD_WIDTH - 6;
	constexpr int PANEL_TOP = BOARD_TOP + 5;
	constexpr int PANEL_HEIGHT = 22;
	/// everything the board draws lies between AREA_TOP and AREA_TOP + AREA_HEIGHT
	constexpr int AREA_TOP = BOARD_TOP - 6;
	constexpr int AREA_HEIGHT = BOARD_BOTTOM + 3 - AREA_TOP;

	/// Logos are scaled to this height on the panel.
	constexpr int LOGO_HEIGHT = PANEL_HEIGHT;

	/// The part of the net pole that is drawn again on top of the board.
	constexpr int ROD_LEFT = 400 - 7;
	constexpr int ROD_WIDTH = 14;

	/// Space between two logos and scroll speed in pixels per second.
	constexpr int LOGO_GAP = 36;
	constexpr int SCROLL_SPEED = 30;

	/// How a fill is combined with what is already drawn.
	enum class Blend
	{
		SOLID,		///< replaces the pixels
		ALPHA,		///< blended with alpha a
		MULTIPLY	///< multiplies the pixels with the colour, for shadows on the sand
	};

	/// A filled rectangle of the board.
	struct Fill
	{
		int x, y, w, h;
		std::uint8_t r, g, b, a;
		Blend blend;
	};

	/// A strip of the background copied to another place with alpha, for sand drifting over the base.
	struct BackgroundCopy
	{
		int srcX, srcY, w, h, dstX, dstY;
		std::uint8_t alpha;
	};

	/// Board styles. The site sends them as off, led, printed, wood, slim and padded.
	enum class BoardStyle
	{
		OFF,		///< no board
		LED,		///< dark LED panel in a metal frame, the default
		PRINTED,	///< white printed panels in a thin grey frame
		WOOD,		///< wooden beach bar board with posts and light panels
		SLIM,		///< slim glossy LED without a frame
		PADDED		///< padded board with rounded edges, like court padding
	};

	/// Parses the style name sent by the site. Unknown names give LED.
	BoardStyle parseBoardStyle(const std::string& name);
	/// The name of a style as the site sends it.
	const char* boardStyleName(BoardStyle style);

	/// Everything a renderer needs to draw one board style.
	struct BoardLook
	{
		std::vector<Fill> under;	///< cast shadow, contact darkening and the board, drawn before the logos
		std::vector<Fill> over;		///< parts in front of the logos, such as seams and posts
		int panelX, panelY, panelW, panelH;	///< where the logos scroll
		bool ledGrid;				///< whether the LED grid is laid over the panel
	};

	/// The look of a style. All styles share the placement, the shadow, the contact line and the sand drift.
	/// The shadow follows the palm shadows in the default background: the sun is front left,
	/// so it falls right and slightly back, at about 0.31 of the sand colour.
	const BoardLook& boardLook(BoardStyle style);

	/// Sand drifting over the bottom edge. Drawn after the logos.
	const std::vector<BackgroundCopy>& sandDrift();

	/// The contact line where the board meets the sand. Drawn last.
	const std::vector<Fill>& contactLine();

	/// Width of a logo of the given size after scaling it to LOGO_HEIGHT.
	int scaledWidth(int width, int height);

	/// Dims and desaturates a logo pixel a little so it looks lit by LEDs.
	void ledFilter(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b);

	/// Checks that data holds an uncompressed 24-bit BMP, or a 32-bit BI_BITFIELDS BMP with a V4 or V5
	/// header whose colour and alpha masks are distinct whole bytes, within the size limits and
	/// whose pixel data fits inside the buffer. On failure, error gets the reason if given.
	bool validateBmp(const char* data, std::size_t size, std::string* error = nullptr);

	struct Placement
	{
		std::size_t logo;	///< index into the logo list
		int x;				///< left edge in game coordinates, may lie left of the panel
	};

	/// The part of a logo at x with the given width that lies on the panel.
	/// Returns false when nothing is visible. skip is the number of columns cut off on the left.
	bool clipToPanel(int x, int width, int& skip, int& visible, int panelLeft = PANEL_LEFT, int panelWidth = PANEL_WIDTH);

	/// Computes which logos are visible on the panel and where, for scaled logos of the given widths,
	/// at the given time in milliseconds. The time comes from the render path,
	/// so the board scrolls with the clock and not with the simulation.
	std::vector<Placement> layout(const std::vector<int>& widths, std::uint32_t timeMs,
	                              int panelLeft = PANEL_LEFT, int panelWidth = PANEL_WIDTH);

	/// Alpha of the dark LED grid laid over the panel at a panel pixel. Repeats every 2 pixels.
	std::uint8_t ledMaskAlpha(int x, int y);

	/// What the renderers need to know about the loaded logos. An empty logo list hides the board,
	/// so replacing the logos with an empty list at runtime removes it cleanly.
	class BoardState
	{
		public:
			void setLogoWidths(std::vector<int> widths) { mWidths = std::move(widths); }
			void setVisible(bool visible) { mVisible = visible; }
			void setStyle(BoardStyle style) { mStyle = style; }
			BoardStyle style() const { return mStyle; }
			bool active() const { return mVisible && mStyle != BoardStyle::OFF && !mWidths.empty(); }
			std::size_t logoCount() const { return mWidths.size(); }
			/// visible logos at the given time, empty when the board is not active
			std::vector<Placement> placements(std::uint32_t timeMs) const
			{
				if(!active())
					return {};
				const BoardLook& look = boardLook(mStyle);
				return layout(mWidths, timeMs, look.panelX, look.panelW);
			}

		private:
			bool mVisible = false;
			BoardStyle mStyle = BoardStyle::LED;
			std::vector<int> mWidths;
	};

	/// Reads all BMP files in the given PhysFS directory, sorted by name.
	/// Files that are too large, unreadable or not valid logos are skipped with a warning.
	std::vector<std::vector<char>> readLogoFiles(const std::string& directory = "sponsors");
}
