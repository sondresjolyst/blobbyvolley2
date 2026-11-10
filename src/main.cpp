/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)
Changed by Sjølyst Innovation AS, 2026

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

/* includes */
#include <ctime>
#include <cstring>
#include <sstream>
#include <atomic>
#include <thread>
#include <iostream>

#include <SDL.h>

#include "Global.h"

#ifdef __SWITCH__
	#include <switch.h>
#endif

#include "RenderManager.h"
#include "SoundManager.h"
#include "InputManager.h"
#include "TextManager.h"
#include "UserConfig.h"
#include "IMGUI.h"
#include "SpeedController.h"
#include "Blood.h"
#include "FileSystem.h"
#include "state/State.h"
#include "state/LobbyStates.h"
#include "BlobbyApp.h"
#include "JoinLink.h"

// this global allows the host game thread to be killed
extern std::atomic<bool> gKillHostThread;
extern std::shared_ptr<std::thread> gHostedServerThread;

/* implementation */

void deinit()
{
	SDL_Quit();

#if (defined __SWITCH__) && (defined DEBUG)
	socketExit();
#endif
}

void setupPHYSFS()
{
	FileSystem& fs = FileSystem::getSingleton();
	
	// Game should be playable out of the source package on all
	// relevant platforms.

	// List all the subdirectories / zip archives that we want to read from
	std::vector<std::string> subdirs = {"gfx", "sounds", "scripts", "backgrounds", "rules"};

	// set write dir
	std::string writeDir = fs.getPrefDir();
	fs.setWriteDir(writeDir);
	for(const auto& sub_dir : subdirs) {
		fs.probeDir(sub_dir);
	}
	fs.probeDir("replays");
	fs.probeDir("sponsors");

	// set read dir (files in datafolder next to working dir)
	{
		std::string dataDir = "data";
		fs.addToSearchPath(dataDir);
		for(const auto& archive : subdirs) {
			fs.addToSearchPath(fs.join(dataDir, archive + ".zip"));
		}
	}

	// set read dir (files next to application)
	{
		std::string dataDir = fs.getBaseDir();
		fs.addToSearchPath(dataDir);
		for(const auto& archive : subdirs) {
			fs.addToSearchPath(fs.join(dataDir, archive + ".zip"));
		}
	}

	// set read dir (data folder next to application). A join link starts the game
	// in another working directory, so the data folder above is not found then.
	{
		std::string dataDir = fs.join(fs.getBaseDir(), "data");
		fs.addToSearchPath(dataDir);
		for(const auto& archive : subdirs) {
			fs.addToSearchPath(fs.join(dataDir, archive + ".zip"));
		}
	}

	// set read dir (files inside MacOS bundle)
	#if (defined BUILD_MACOS_BUNDLE)
	{
		std::string dataDir = fs.join(fs.getBaseDir(), std::string("Contents") + 
		                                               fs.getDirSeparator() +
		                                               std::string("Resources"));
		fs.addToSearchPath(dataDir);
		for(const auto& archive : subdirs) {
			fs.addToSearchPath(fs.join(dataDir, archive + ".zip"));
		}
	}
	#endif

	// set read dir (unix-like when installed)
	#ifdef BLOBBY_DATA_DIR
		fs.addToSearchPath(BLOBBY_DATA_DIR);
		for(const auto& archive : subdirs) {
			fs.addToSearchPath(fs.join(BLOBBY_DATA_DIR, archive + ".zip"));
		}
	#endif
}

namespace
{
	const char* const REGISTER_FLAG = "--register-link-handler";
	const char* const UNREGISTER_FLAG = "--unregister-link-handler";

	/// What the command line asks for.
	struct StartRequest
	{
		bool hasLink = false;
		JoinLink link;
		/// Set when a link was given but cannot be used.
		std::string linkError;
	};

	/// Looks for a join link on the command line. Other arguments are ignored, as before.
	/// A link must be the only argument, so nothing can be added next to it.
	StartRequest readStartRequest(int argc, char* argv[])
	{
		StartRequest request;
		bool link_seen = false;
		for (int i = 1; i < argc; ++i)
		{
			if (looksLikeJoinLink(argv[i]))
				link_seen = true;
		}
		if (!link_seen)
			return request;

		if (argc != 2)
		{
			request.linkError = "A join link must be the only argument.";
			return request;
		}

		JoinLinkError error = parseJoinLink(argv[1], request.link);
		if (error != JoinLinkError::NONE)
		{
			request.linkError = describeJoinLinkError(error);
			return request;
		}
		request.hasLink = true;
		return request;
	}

	/// Handles the flags that register or remove the link handler. Returns true if one was given.
	bool handleLinkHandlerFlag(int argc, char* argv[], int& exit_code)
	{
		if (argc != 2)
			return false;
		std::string flag = argv[1];
		std::string error;
		if (flag == REGISTER_FLAG)
		{
			if (registerLinkHandler(error))
			{
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, AppTitle,
					"Join links now open this copy of Blobby Volley 2. If you move the game, run this again.", nullptr);
				exit_code = EXIT_SUCCESS;
			}
			else
			{
				std::cerr << error << std::endl;
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, AppTitle, error.c_str(), nullptr);
				exit_code = EXIT_FAILURE;
			}
			return true;
		}
		if (flag == UNREGISTER_FLAG)
		{
			if (unregisterLinkHandler(error))
			{
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, AppTitle,
					"Join links no longer open Blobby Volley 2.", nullptr);
				exit_code = EXIT_SUCCESS;
			}
			else
			{
				std::cerr << error << std::endl;
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, AppTitle, error.c_str(), nullptr);
				exit_code = EXIT_FAILURE;
			}
			return true;
		}
		return false;
	}
}

int main(int argc, char* argv[])
{
	{
		int exit_code = EXIT_SUCCESS;
		if (handleLinkHandlerFlag(argc, argv, exit_code))
			return exit_code;
	}

#if (defined __SWITCH__) && (defined DEBUG)
	socketInitializeDefault();   
	nxlinkStdio();
#endif

	DEBUG_STATUS("started main");

	FileSystem filesys(argv[0]);
	setupPHYSFS();

	DEBUG_STATUS("physfs initialised");

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);


	DEBUG_STATUS("SDL initialised");

	atexit(SDL_Quit);
	atexit([](){gKillHostThread=true; if(gHostedServerThread) gHostedServerThread->join();});
	srand(SDL_GetTicks());

	// Test Version Startup Warning
	#ifdef TEST_VERSION
	struct tm* ptm;
	time_t time = std::time(0);
	ptm = gmtime ( &time );

	if( ptm->tm_year > (2015-1900) || ptm->tm_mon >= 12 )
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TEST VERISON OUTDATED",
									(std::string("This is a test version of ") + AppTitle + " which expired on "
									"1.12.2015. Please visit https://blobbyvolley.de for a newer version").c_str(), 0);
		return -1;
	}

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "TEST VERISON WARNING",
								(std::string("This is a test version of ") + AppTitle + " for testing only.\n"
								"It might be unstable and/or incompatible to the current release. "
								"Use of this version is limited to 1.12.2015.\n"
								"Visit https://blobbyvolley.de for more information or bug reporting.").c_str(), 0);
	#endif

	try
	{
		UserConfig gameConfig;
		gameConfig.loadFile("config.xml");

		SpeedController scontroller(gameConfig.getFloat("gamefps"));
		SpeedController::setMainInstance(&scontroller);
		scontroller.setDrawFPS(gameConfig.getBool("showfps"));

		int running = 1;

		DEBUG_STATUS("starting mainloop");

		StartRequest request = readStartRequest(argc, argv);
		if (!request.linkError.empty())
		{
			std::cerr << "Join link not used: " << request.linkError << std::endl;
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, AppTitle,
				(request.linkError + "\nGet a new link from the tournament site. The game opens the main menu.").c_str(), nullptr);
		}

		std::unique_ptr<State> first_state;
		if (request.hasLink)
			first_state.reset(joinFromLink(request.link, JoinLinkSource::EXTERNAL, nullptr));
		if (!first_state)
			first_state.reset(new MainMenuState());

		BlobbyApp app( std::move(first_state), gameConfig );

		while (running)
		{
			app.getInputManager().updateInput();
			running = app.getInputManager().running();

			app.getIMGUI().begin();
			app.step();
			//draw FPS:
			static int lastfps = 0;
			static int lastlag = -1;
			if (scontroller.getDrawFPS())
			{
				// We need to ensure that the title bar is only set
				// when the framerate changed, because setting the
				// title can be quite resource intensive on some
				// windows manager, like for example metacity.
				// we only update lag information if lag changed at least by
				// 5 ms, for the same reason.
				int newfps = scontroller.getFPS();
				if (newfps != lastfps || std::abs(CURRENT_NETWORK_LAG - lastlag) > 4)
				{
					std::stringstream tmp;
					tmp << AppTitle << "  FPS: " << newfps;
					if( CURRENT_NETWORK_LAG != -1)
						tmp << "  LAG: " << CURRENT_NETWORK_LAG;
					app.getRenderManager().setTitle(tmp.str());
					lastlag = CURRENT_NETWORK_LAG;
				}
				lastfps = newfps;
			}
			// Dirty workarround for hiding fps in title
			if (!scontroller.getDrawFPS() && (lastfps != -1))
			{
				std::stringstream tmp;
				tmp << AppTitle;
				app.getRenderManager().setTitle(tmp.str());

				lastfps = -1;
			}

			if (!scontroller.doFramedrop())
			{
				app.getIMGUI().end(app.getRenderManager());
				app.getRenderManager().getBlood().step(app.getRenderManager());
				app.getRenderManager().refresh();
			}
			scontroller.update();
		}
	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", (std::string("An error occurred, blobby will close: ") + e.what()).c_str(), nullptr);
		SDL_Quit();
#if (defined __SWITCH__) && (defined DEBUG)
	socketExit();
#endif
		exit (EXIT_FAILURE);
	}
	deinit();
	exit(EXIT_SUCCESS);
}
