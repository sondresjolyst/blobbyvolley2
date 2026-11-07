/*=============================================================================
Blobby Volley 2
Copyright (C) 2022 Daniel Knobe (daniel-knobe@web.de)
Copyright (C) 2022 Erik Schultheis (erik-schultheis@freenet.de)

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

#include <memory>

#include "SponsorBoard.h"

class State;
class RakClient;
class SoundManager;
class InputManager;
class IUserConfigReader;
class IMGUI;
class RenderManager;
struct SDL_Window;

namespace sponsors
{
	class FileLogoCache;
	class BoardReceiver;
}

class BlobbyApp {
	public:
		explicit BlobbyApp(std::unique_ptr<State> initState, const IUserConfigReader& config);

		void step();

		void switchToState(std::unique_ptr<State> newState);

		/// get the currently active state
		State& getCurrentState() const;

		/// get the sound manager
		SoundManager& getSoundManager() const;

		/// get the input manager
		InputManager& getInputManager() const;

		/// gets the gui manager
		IMGUI& getIMGUI() const;

		/// gets the name of the currently active state
		const char* getCurrenStateName() const;

		/// gets the render manager
		RenderManager& getRenderManager() const;

		/// Gets the game window
		SDL_Window* getWindow() const;

		void setupRenderManager(const IUserConfigReader& config);

		/// Handles an ID_SPONSORS packet of a tournament server. client is the connection it came on,
		/// so the board goes back to the local logos once that connection is gone.
		void handleSponsorPacket(const unsigned char* data, unsigned length, const std::shared_ptr<RakClient>& client);

		~BlobbyApp();

	private:
		// shows the board of the server when there is one, the local logos otherwise
		void applySponsors();

		// sponsor board of a tournament server, kept in the user folder
		std::unique_ptr<sponsors::FileLogoCache> mSponsorCache;
		std::unique_ptr<sponsors::BoardReceiver> mSponsorReceiver;
		std::weak_ptr<RakClient> mSponsorConnection;
		bool mServerSponsors = false;
		// the style from the options, for the local logos
		sponsors::BoardStyle mLocalSponsorStyle = sponsors::BoardStyle::LED;

		// We keep two references to states: One for the currently active one, and one
		// that is set to the new state if we want to switch states. This is because we
		// want to make sure that the current state, which called switchState, is not
		// destroyed while its `step` member function is still running.
		std::unique_ptr<State> mCurrentState;
		std::unique_ptr<State> mStateToSwitchTo;

		std::unique_ptr<SoundManager> mSoundManager;
		std::unique_ptr<IMGUI> mIMGUI;
		std::unique_ptr<InputManager> mInputMgr;
		std::unique_ptr<RenderManager> mRenderMgr;
};