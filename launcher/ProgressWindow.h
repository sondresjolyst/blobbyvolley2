/*=============================================================================
Blobby Volley 2
Copyright (C) 2026 Sjølyst Innovation AS

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

#include <cstdint>
#include <string>

struct SDL_Window;
struct SDL_Renderer;

/// A small window with the status in its title and a progress bar. Escape or closing it skips the update.
class ProgressWindow
{
	public:
		ProgressWindow();
		~ProgressWindow();
		ProgressWindow(const ProgressWindow&) = delete;
		ProgressWindow& operator=(const ProgressWindow&) = delete;

		/// Opens the window. False when there is no screen, then the update runs without it.
		bool open(const std::string& status);
		void close();

		/// fraction from 0 to 1, or below 0 when the size is not known
		void show(const std::string& status, double fraction);

		/// Handles the window events and draws. False once the player skipped.
		bool keepGoing();

		/// The window, for message boxes that belong to it. May be nullptr.
		SDL_Window* window() const { return mWindow; }

	private:
		void draw();

		SDL_Window* mWindow = nullptr;
		SDL_Renderer* mRenderer = nullptr;
		std::string mStatus;
		double mFraction = -1;
		bool mSkipped = false;
		uint32_t mLastDraw = 0;
		bool mSdlStarted = false;
};
