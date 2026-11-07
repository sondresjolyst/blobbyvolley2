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
/* header include */
#include "BlobbyApp.h"

/* includes */
#include <string>
#include <cassert>
#include <iostream>

#include "raknet/BitStream.h"
#include "raknet/RakClient.h"

#include "state/State.h"
#include "SoundManager.h"
#include "BlobbyDebug.h"
#include "IUserConfigReader.h"
#include "IMGUI.h"
#include "FileSystem.h"
#include "SponsorBoard.h"
#include "SponsorSync.h"

/* implementation */
void BlobbyApp::switchToState(std::unique_ptr<State> newState)
{
	assert(newState != nullptr);
	mStateToSwitchTo = std::move(newState);
	mStateToSwitchTo->setApp(this);
}

State& BlobbyApp::getCurrentState() const
{
	return *mCurrentState;
}

const char* BlobbyApp::getCurrenStateName() const
{
	return mCurrentState->getStateName();
}

void BlobbyApp::step()
{
	// perform a state step
	mCurrentState->step_impl();

	// check if we should switch to a new state
	if( mStateToSwitchTo != nullptr )
	{
		// maybe this debug statuses will help pinpointing crashes faster
		DEBUG_STATUS( std::string("switching to state ") + mStateToSwitchTo->getStateName());

		// if yes, set that new state
		// use swap so the states do not get destroyed here
		mCurrentState = std::move( mStateToSwitchTo );
		mIMGUI->resetSelection();
		mCurrentState->init();
	}

	// the connection to the tournament server is gone, so the local logos come back
	if( mSponsorConnection.expired() && (mServerSponsors || mSponsorReceiver->ready()) )
	{
		mSponsorReceiver->reset();
		mServerSponsors = false;
		applySponsors();
	}
}

void BlobbyApp::handleSponsorPacket(const unsigned char* data, unsigned length, const std::shared_ptr<RakClient>& client)
{
	// a board from another connection starts over
	if( mSponsorConnection.lock() != client )
	{
		mSponsorReceiver->reset();
		mSponsorConnection = client;
	}

	RakNet::BitStream stream(const_cast<unsigned char*>(data), length, false);
	stream.IgnoreBytes(1);	// ID_SPONSORS
	RakNet::BitStream answer;
	switch( mSponsorReceiver->handle(stream, answer) )
	{
		case sponsors::BoardReceiver::Result::REQUEST:
			client->Send(&answer, LOW_PRIORITY, RELIABLE_ORDERED, sponsors::NETWORK_CHANNEL);
			break;
		case sponsors::BoardReceiver::Result::READY:
			mServerSponsors = true;
			applySponsors();
			break;
		case sponsors::BoardReceiver::Result::NOTHING:
			break;
	}
	if( !mSponsorReceiver->problem().empty() )
		std::cerr << "Warning: " << mSponsorReceiver->problem() << std::endl;
}

void BlobbyApp::applySponsors()
{
	if( mServerSponsors && mSponsorReceiver->ready() )
	{
		mRenderMgr->setSponsorBoardStyle(mSponsorReceiver->style());
		mRenderMgr->setSponsorLogos(mSponsorReceiver->logoFiles());
	}
	else
	{
		mRenderMgr->setSponsorBoardStyle(mLocalSponsorStyle);
		mRenderMgr->setSponsorLogos(sponsors::readLogoFiles());
	}
}

BlobbyApp::BlobbyApp(std::unique_ptr<State> initState, const IUserConfigReader& config) :
	mSponsorCache( new sponsors::FileLogoCache() ),
	mCurrentState(std::move(initState)),
	mSoundManager( new SoundManager ),
	mInputMgr( new InputManager(this) )
{
	mSponsorReceiver.reset(new sponsors::BoardReceiver(*mSponsorCache));
	mCurrentState->setApp(this);

	mSoundManager->setVolume(config.getFloat("global_volume"));
	mSoundManager->setMute(config.getBool("mute"));
	/// \todo play sound is misleading. what we actually want to do is load the sound
	mSoundManager->playSound(SoundManager::IMPACT, 0.0);
	mSoundManager->playSound(SoundManager::WHISTLE, 0.0);

	mIMGUI.reset(new IMGUI(mInputMgr.get()));
	mIMGUI->setTextMgr(config.getString("language"));

	setupRenderManager(config);

	// a state that is switched to gets init() in step(), the first state gets it here
	mCurrentState->init();
}

BlobbyApp::~BlobbyApp() = default;

SoundManager& BlobbyApp::getSoundManager() const
{
	return *mSoundManager;
}

InputManager& BlobbyApp::getInputManager() const {
	return *mInputMgr;
}

IMGUI& BlobbyApp::getIMGUI() const
{
	return *mIMGUI;
}

SDL_Window* BlobbyApp::getWindow() const
{
	return mRenderMgr->getWindow();
}

RenderManager& BlobbyApp::getRenderManager() const
{
	return *mRenderMgr;
}

void BlobbyApp::setupRenderManager(const IUserConfigReader& config) {
	std::string device_name = config.getString("device");
	if(device_name == "SDL")
		mRenderMgr = RenderManager::createRenderManagerSDL();
	else if (device_name == "OpenGL")
		mRenderMgr = RenderManager::createRenderManagerGL2D();
	else if (device_name == "Null")
		mRenderMgr = RenderManager::createRenderManagerNull();
	else
	{
		std::cerr << "Warning: Unknown renderer selected!";
		std::cerr << "Falling back to SDL" << std::endl;
		mRenderMgr = RenderManager::createRenderManagerSDL();
	}

	bool fullscreen = config.getBool("fullscreen");
	bool shadows = config.getBool("show_shadow");
	mRenderMgr->init(BASE_RESOLUTION_X, BASE_RESOLUTION_Y, fullscreen);
	mRenderMgr->showShadow(shadows);
	mRenderMgr->showSponsors(config.getBool("show_sponsors", true));
	// for local testing, a tournament server sends its own style
	mLocalSponsorStyle = sponsors::parseBoardStyle(config.getString("sponsor_board_style", "led"));
	// a new renderer gets the board of the server again
	applySponsors();

	std::string bg = std::string("backgrounds/") + config.getString("background");
	if ( FileSystem::getSingleton().exists(bg) )
		mRenderMgr->setBackground(bg);
}