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

#include "ProgressWindow.h"

#include <SDL.h>

namespace
{
	const int WIDTH = 480;
	const int HEIGHT = 90;
}

ProgressWindow::ProgressWindow() = default;

ProgressWindow::~ProgressWindow()
{
	close();
}

bool ProgressWindow::open(const std::string& status)
{
	if (mWindow)
	{
		show(status, -1);
		return true;
	}
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
		return false;
	mSdlStarted = true;
	mWindow = SDL_CreateWindow(status.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
	if (!mWindow)
	{
		close();
		return false;
	}
	mRenderer = SDL_CreateRenderer(mWindow, -1, 0);
	if (!mRenderer)
		mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_SOFTWARE);
	mSkipped = false;
	show(status, -1);
	return true;
}

void ProgressWindow::close()
{
	if (mRenderer)
		SDL_DestroyRenderer(mRenderer);
	if (mWindow)
		SDL_DestroyWindow(mWindow);
	mRenderer = nullptr;
	mWindow = nullptr;
	if (mSdlStarted)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	mSdlStarted = false;
}

void ProgressWindow::show(const std::string& status, double fraction)
{
	mFraction = fraction;
	if (status != mStatus && mWindow)
	{
		mStatus = status;
		SDL_SetWindowTitle(mWindow, (status + "   (Esc skips)").c_str());
	}
	draw();
}

bool ProgressWindow::keepGoing()
{
	if (!mWindow)
		return !mSkipped;
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
			mSkipped = true;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
			mSkipped = true;
	}
	// at most 30 frames a second
	const uint32_t now = SDL_GetTicks();
	if (now - mLastDraw > 33)
		draw();
	return !mSkipped;
}

void ProgressWindow::draw()
{
	if (!mRenderer)
		return;
	mLastDraw = SDL_GetTicks();
	SDL_SetRenderDrawColor(mRenderer, 24, 32, 64, 255);
	SDL_RenderClear(mRenderer);
	const SDL_Rect frame = {20, 30, WIDTH - 40, 30};
	SDL_SetRenderDrawColor(mRenderer, 220, 220, 220, 255);
	SDL_RenderDrawRect(mRenderer, &frame);
	SDL_SetRenderDrawColor(mRenderer, 255, 196, 0, 255);
	const int inner = frame.w - 4;
	if (mFraction >= 0)
	{
		const double clamped = mFraction > 1 ? 1 : mFraction;
		const SDL_Rect bar = {frame.x + 2, frame.y + 2, (int)(inner * clamped), frame.h - 4};
		SDL_RenderFillRect(mRenderer, &bar);
	}
	else
	{
		// a block that moves back and forth while the size is not known
		const int block = inner / 5;
		const int span = inner - block;
		const int step = (int)((mLastDraw / 4) % (uint32_t)(2 * span));
		const int x = step < span ? step : 2 * span - step;
		const SDL_Rect bar = {frame.x + 2 + x, frame.y + 2, block, frame.h - 4};
		SDL_RenderFillRect(mRenderer, &bar);
	}
	SDL_RenderPresent(mRenderer);
}
