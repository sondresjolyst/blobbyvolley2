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
#include "RenderManager.h"

/* includes */
#include "FileRead.h"
#include "Blood.h"
#include "IUserConfigReader.h"
#include "FontIndex.h"

#include <algorithm>
#include <iostream>

/* implementation */

RenderManager::~RenderManager() = default;

RenderManager::RenderManager() :
	mBloodMgr(new BloodManager(IUserConfigReader::createUserConfigReader("config.xml")->getBool("blood")))
{
	mMouseMarkerPosition = -100.0;
}

BloodManager& RenderManager::getBlood() {
	return *mBloodMgr;
}

SDL_Surface* RenderManager::highlightSurface(SDL_Surface* surface, int luminance)
{
	SDL_Surface *newSurface = createEmptySurface(surface->w, surface->h);
	SDL_SetColorKey(surface, SDL_FALSE, 0);
	SDL_BlitSurface(surface, nullptr, newSurface, nullptr);
	SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(newSurface->format, 0, 0, 0));

	Uint8 alpha;
	SDL_GetSurfaceAlphaMod(surface, &alpha);
	SDL_SetSurfaceAlphaMod(newSurface, alpha);
	SDL_SetColorKey(newSurface, SDL_TRUE, SDL_MapRGB(newSurface->format, 0, 0, 0));

	SDL_LockSurface(newSurface);
	for (int y = 0; y < surface->h; ++y)
	{
		for (int x = 0; x < surface->w; ++x)
		{
			// this seems overly complicated:
			// aren't we just calculating newSurface->pixels + (y * newSurface->w + x)
			SDL_Color* pixel = &( ((SDL_Color*)newSurface->pixels)[y * newSurface->w + x] );
			Uint32 colorKey;
			SDL_GetColorKey(surface, &colorKey);
			// we index newSurface->pixels once as Uint32[] and once as SDL_Color[], so these must have the same size
			static_assert( sizeof(Uint32) == sizeof(SDL_Color), "Uint32 must have the same size as SDL_Color" );

			if (colorKey != ((Uint32*)newSurface->pixels)[y * newSurface->w +x])
			{
				pixel->r = pixel->r + luminance > 255 ? 255 : pixel->r + luminance;
				pixel->g = pixel->g + luminance > 255 ? 255 : pixel->g + luminance;
				pixel->b = pixel->b + luminance > 255 ? 255 : pixel->b + luminance;
			}
		}
	}
	SDL_UnlockSurface(newSurface);
	// no DisplayFormatAlpha, because of problems with
	// OpenGL RenderManager
	return newSurface;
}

SDL_Surface* RenderManager::loadSurface(const std::string& filename)
{
	FileRead file(filename);
	int fileLength = file.length();

	// just read the whole file
	auto fileContent = file.readRawBytes(fileLength);

	SDL_RWops* rwops = SDL_RWFromMem(fileContent.data(), fileLength);
	SDL_Surface* newSurface = SDL_LoadBMP_RW(rwops , 1);

	if (!newSurface)
		BOOST_THROW_EXCEPTION ( FileLoadException(filename) );

	return newSurface;
}


SDL_Surface* RenderManager::createEmptySurface(unsigned int width, unsigned int height)
{
	SDL_Surface* newSurface = SDL_CreateRGBSurface(0, width, height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00000000);

	return newSurface;
}

int RenderManager::getNextFontIndex(std::string::const_iterator& iter, std::string::const_iterator end)
{
	return ::getNextFontIndex(iter, end);
}

void RenderManager::setMouseMarker(float position)
{
	mMouseMarkerPosition = position;
}

SDL_Rect RenderManager::blobRect(const Vector2& position)
{
	SDL_Rect rect = {
		(short)(lround(position.x) - 37),
		(short)(lround(position.y) - 44),
		75,
		89
	};

	return rect;
}

SDL_Rect RenderManager::ballRect(const Vector2& position)
{
	SDL_Rect rect = {
		(short)(lround(position.x) - 32),
		(short)(lround(position.y) - 32),
		64,
		64
	};
	return rect;
}

Vector2 RenderManager::ballShadowPosition(const Vector2& position)
{
	return {
		position.x + (500.f - position.y) / 4.f + 16.f,
		500.f - (500.f - position.y) / 16.f - 10.f
	};
}

SDL_Rect RenderManager::ballShadowRect(const Vector2& position)
{
	SDL_Rect rect = {
		short(lround(position.x) - 64),
		short(lround(position.y) - 16),
		69,
		17
	};

	return rect;
}

Vector2 RenderManager::blobShadowPosition(const Vector2& position)
{
	return {
		position.x + (500.f - position.y) / 4 + 16.f,
		500.f - (500.f - position.y) / 16.f - 10.f
	};
}

SDL_Rect RenderManager::blobShadowRect(const Vector2& position)
{
	SDL_Rect rect = {
		short(lround(position.x) - 64),
		short(lround(position.y) - 16),
		128,
		32
	};
	return rect;
}

void RenderManager::setTitle(const std::string& title)
{
	SDL_SetWindowTitle(mWindow, title.c_str());
}

SDL_Window* RenderManager::getWindow()
{
	return mWindow;
}

Color RenderManager::getOscillationColor() const
{
	float time = float(SDL_GetTicks()) / 1000.f;

	return {
		int((std::sin(time*1.5) + 1.0) * 128),
		int((std::sin(time*2.5) + 1.0) * 128),
		int((std::sin(time*3.5) + 1.0) * 128)
	};
}

void RenderManager::setSponsorLogos(const std::vector<std::vector<char>>& bmpFiles)
{
	std::vector<SDL_Surface*> surfaces;
	std::vector<int> widths;

	for(const auto& file : bmpFiles)
	{
		if(surfaces.size() >= sponsors::MAX_LOGOS)
			break;

		std::string error;
		if(!sponsors::validateBmp(file.data(), file.size(), &error))
		{
			std::cerr << "Warning: skipping sponsor logo: " << error << std::endl;
			continue;
		}

		SDL_RWops* rwops = SDL_RWFromConstMem(file.data(), int(file.size()));
		SDL_Surface* surface = rwops ? SDL_LoadBMP_RW(rwops, 1) : nullptr;
		if(!surface)
		{
			std::cerr << "Warning: skipping sponsor logo: " << SDL_GetError() << std::endl;
			continue;
		}

		SDL_Surface* logo = prepareSponsorLogo(surface);
		SDL_FreeSurface(surface);
		if(!logo)
			continue;

		surfaces.push_back(logo);
		widths.push_back(logo->w);
	}

	// an empty list hides the board and frees the old logos
	mSponsorBoard.setLogoWidths(widths);
	replaceSponsorSurfaces(std::move(surfaces));
}

SDL_Surface* RenderManager::prepareSponsorLogo(SDL_Surface* source)
{
	// Scale to the panel height with a box filter and give it the LED look.
	// This happens once here, so both renderers draw the same pixels 1:1.
	SDL_Surface* src = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_ARGB8888, 0);
	if(!src)
		return nullptr;

	const int dw = sponsors::scaledWidth(src->w, src->h);
	const int dh = sponsors::LOGO_HEIGHT;
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_ARGB8888);
	if(!dst)
	{
		SDL_FreeSurface(src);
		return nullptr;
	}

	SDL_LockSurface(src);
	SDL_LockSurface(dst);
	for(int y = 0; y < dh; ++y)
	{
		const int y0 = y * src->h / dh;
		const int y1 = std::max(y0 + 1, (y + 1) * src->h / dh);
		auto* out = (Uint32*)((Uint8*)dst->pixels + y * dst->pitch);
		for(int x = 0; x < dw; ++x)
		{
			const int x0 = x * src->w / dw;
			const int x1 = std::max(x0 + 1, (x + 1) * src->w / dw);
			// average with alpha weights so transparent pixels do not darken the edges
			int r = 0, g = 0, b = 0, a = 0, n = 0;
			for(int sy = y0; sy < y1; ++sy)
			{
				const auto* in = (const Uint32*)((const Uint8*)src->pixels + sy * src->pitch);
				for(int sx = x0; sx < x1; ++sx)
				{
					const int pa = (in[sx] >> 24) & 0xFF;
					r += ((in[sx] >> 16) & 0xFF) * pa;
					g += ((in[sx] >> 8) & 0xFF) * pa;
					b += (in[sx] & 0xFF) * pa;
					a += pa;
					++n;
				}
			}
			auto rr = Uint8(a ? r / a : 0);
			auto gg = Uint8(a ? g / a : 0);
			auto bb = Uint8(a ? b / a : 0);
			const auto aa = Uint8(a / n);
			// the LED look only matters where the logo is visible
			if(aa)
				sponsors::ledFilter(rr, gg, bb);
			out[x] = (Uint32(aa) << 24) | (Uint32(rr) << 16) | (Uint32(gg) << 8) | Uint32(bb);
		}
	}
	SDL_UnlockSurface(dst);
	SDL_UnlockSurface(src);
	SDL_FreeSurface(src);
	return dst;
}

void RenderManager::showSponsors(bool show)
{
	mSponsorBoard.setVisible(show);
}

void RenderManager::replaceSponsorSurfaces(std::vector<SDL_Surface*> surfaces)
{
	for(auto* surface : surfaces)
		SDL_FreeSurface(surface);
}

void RenderManager::setSponsorBoardStyle(sponsors::BoardStyle style)
{
	mSponsorBoard.setStyle(style);
}

const sponsors::BoardLook& RenderManager::sponsorLook() const
{
	return sponsors::boardLook(mSponsorBoard.style());
}

bool RenderManager::sponsorBoardActive() const
{
	return mSponsorBoard.active();
}

std::vector<sponsors::Placement> RenderManager::sponsorLayout() const
{
	return mSponsorBoard.placements(SDL_GetTicks());
}
