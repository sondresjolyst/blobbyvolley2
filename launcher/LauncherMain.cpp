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

// The launcher that players start. See doc/launcher.md.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#include <physfs.h>

#include "CurlFetcher.h"
#include "ForkVersion.h"
#include "FsUtil.h"
#include "InstallDir.h"
#include "LauncherLink.h"
#include "ChildProcess.h"
#include "ProgressWindow.h"
#include "Updater.h"

namespace
{
	const char* const TITLE = BLOBBY_FORK_NAME;
	/// Set to 1 to start the game without looking for updates, as the package checks in CI do.
	const char* const NO_UPDATE_ENV = "BLOBBY_LAUNCHER_NO_UPDATE";

	std::string gLogPath;
	std::string gLog;

	/// Writes a line to stderr and to launcher.log in the settings folder, which is rewritten at each start.
	void logLine(const std::string& line)
	{
		std::cerr << line << std::endl;
		gLog += line + "\n";
		if (gLog.size() > 64 * 1024)
			gLog.erase(0, gLog.size() - 32 * 1024);
		if (!gLogPath.empty())
			fsu::writeFile(gLogPath, gLog);
	}

	/// Shows a message with buttons and returns the index of the button, or -1.
	int ask(SDL_Window* parent, Uint32 flags, const std::string& message, const std::vector<std::string>& buttons)
	{
		std::vector<SDL_MessageBoxButtonData> data;
		for (std::size_t i = 0; i < buttons.size(); ++i)
		{
			SDL_MessageBoxButtonData button = {};
			button.buttonid = (int)i;
			button.text = buttons[i].c_str();
			if (i == 0)
				button.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
			if (i + 1 == buttons.size())
				button.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			data.push_back(button);
		}
		SDL_MessageBoxData box = {};
		box.flags = flags;
		box.window = parent;
		box.title = TITLE;
		box.message = message.c_str();
		box.numbuttons = (int)data.size();
		box.buttons = data.data();
		int chosen = -1;
		if (SDL_ShowMessageBox(&box, &chosen) != 0)
		{
			logLine(std::string("cannot show a message: ") + SDL_GetError());
			return -1;
		}
		return chosen;
	}

	void openDownloadPage()
	{
		logLine(std::string("opening ") + DOWNLOAD_PAGE_URL);
#if SDL_VERSION_ATLEAST(2, 0, 14)
		if (SDL_OpenURL(DOWNLOAD_PAGE_URL) != 0)
			logLine(std::string("cannot open the browser: ") + SDL_GetError());
#endif
	}

	/// What happens after the player asked the game to update.
	enum class AfterRequest
	{
		START_GAME,
		QUIT
	};

	class Launcher
	{
		public:
			Launcher(const std::string& self, const std::string& stateDir)
				: mSelf(self), mInstallDir(fsu::parent(self)), mStateDir(stateDir)
			{
				const char* off = std::getenv(NO_UPDATE_ENV);
				mUpdatesOff = off && *off && std::string(off) != "0";
				mConfig.installDir = mInstallDir;
				mConfig.stateDir = mStateDir;
				mConfig.platform = currentPlatform();
				mConfig.roots = officialRootKeys();

				if (isTranslocated(mInstallDir))
					mWhyNotInstall = "macOS runs it from a temporary copy. Move the game to another folder first.";
				else if (!fsu::isWritableDirectory(mInstallDir))
					mWhyNotInstall = "its folder is read-only.";
				else if (mConfig.platform.empty())
					mWhyNotInstall = "there are no packages for this system.";
				else if (mStateDir.empty())
					mWhyNotInstall = "the settings folder cannot be found.";
			}

			bool checksForUpdates() const
			{
				return !mUpdatesOff && !mConfig.platform.empty();
			}

			bool canInstall() const
			{
				return checksForUpdates() && mWhyNotInstall.empty();
			}

			const std::string& installDir() const { return mInstallDir; }
			const std::string& self() const { return mSelf; }

			/// The quiet check at each start. It shows a window only when it found a newer version.
			void updateAtStart()
			{
				if (!checksForUpdates())
					return;
				UpdateResult check = checkForUpdate(fetcher(), mConfig);
				logResult("update check", check);
				if (check.status == UpdateStatus::FAILED)
				{
					// offline play is normal, only a launcher that cannot update safely tells the player
					if (check.useDownloadPage)
						notifyOnce("damaged-state", "This launcher cannot check for updates safely, because " + check.error
							+ ".\nGet new versions from the download page.");
					return;
				}
				if (check.status != UpdateStatus::AVAILABLE)
					return;
				if (!canInstall())
				{
					notifyOnce(check.latest.toString(), "Version " + check.latest.toString() + " of " + TITLE
						+ " is out. This copy cannot update itself, because " + mWhyNotInstall
						+ "\nGet the new version from the download page.");
					return;
				}
				ProgressWindow window;
				const UpdateResult done = install(window, check);
				if (done.status == UpdateStatus::FAILED)
				{
					ask(window.window(), SDL_MESSAGEBOX_WARNING, "The update to version " + check.latest.toString()
						+ " did not work: " + done.error + ".\nThe game starts with the version you have.", {"OK"});
				}
			}

			/// The player picked Update now in the game.
			AfterRequest updateOnRequest()
			{
				for (;;)
				{
					std::string message;
					if (!canInstall())
					{
						const std::string why = mUpdatesOff ? "updates are turned off." : mWhyNotInstall;
						message = "This copy of the game cannot update itself, because " + why
							+ "\nGet the new version from the download page.";
						const int choice = ask(nullptr, SDL_MESSAGEBOX_WARNING, message, {"Download page", "Back to the game"});
						if (choice == 0)
						{
							openDownloadPage();
							return AfterRequest::QUIT;
						}
						return AfterRequest::START_GAME;
					}

					ProgressWindow window;
					window.open("Looking for a new version");
					window.keepGoing();
					UpdateResult check = checkForUpdate(fetcher(), mConfig);
					logResult("requested update check", check);
					if (check.status == UpdateStatus::AVAILABLE)
						check = install(window, check);
					if (check.status == UpdateStatus::INSTALLED || check.status == UpdateStatus::CANCELLED)
						return AfterRequest::START_GAME;

					if (check.status == UpdateStatus::UP_TO_DATE)
						message = "No newer version was found. You have version " + check.installed.toString()
							+ ", the newest one for this system.";
					else
						message = "The update did not work: " + check.error + ".";
					const int choice = ask(window.window(), SDL_MESSAGEBOX_WARNING, message,
										   {"Try again", "Download page", "Back to the game"});
					window.close();
					if (choice == 0)
						continue;
					if (choice == 1)
					{
						openDownloadPage();
						return AfterRequest::QUIT;
					}
					return AfterRequest::START_GAME;
				}
			}

		private:
			Fetcher& fetcher()
			{
				if (!mFetcher)
					mFetcher.reset(new CurlFetcher());
				return *mFetcher;
			}

			UpdateResult install(ProgressWindow& window, const UpdateResult& check)
			{
				const std::string status = "Updating to version " + check.latest.toString();
				window.open(status);
				const UpdateResult done = installUpdate(fetcher(), mConfig, check, [&](uint64_t got, uint64_t total) {
					window.show(status, total > 0 ? (double)got / (double)total : -1.0);
					return window.keepGoing();
				});
				logResult("install", done);
				window.show("Starting the game", 1.0);
				return done;
			}

			/// Shows a message about version once, so the player is not asked at every start.
			void notifyOnce(const std::string& key, const std::string& message)
			{
				const std::string path = fsu::join(mStateDir, "notified");
				std::string seen;
				if (!mStateDir.empty() && fsu::readFile(path, 256, seen) && seen == key)
					return;
				if (!mStateDir.empty())
					fsu::writeFile(path, key);
				if (ask(nullptr, SDL_MESSAGEBOX_INFORMATION, message, {"Download page", "Not now"}) == 0)
					openDownloadPage();
			}

			void logResult(const std::string& what, const UpdateResult& result)
			{
				static const char* const names[] = {"up to date", "available", "installed", "failed", "cancelled"};
				std::string line = what + ": " + names[(int)result.status] + ", installed " + result.installed.toString()
					+ ", latest " + result.latest.toString();
				if (!result.error.empty())
					line += ", " + result.error;
				logLine(line);
			}

			std::string mSelf;
			std::string mInstallDir;
			std::string mStateDir;
			std::string mWhyNotInstall;
			bool mUpdatesOff = false;
			UpdaterConfig mConfig;
			std::unique_ptr<Fetcher> mFetcher;
	};

	bool isLinkHandlerFlag(const std::vector<std::string>& args)
	{
		return args.size() == 1 && (args[0] == "--register-link-handler" || args[0] == "--unregister-link-handler");
	}

	std::string findStateDir(const char* argv0)
	{
		if (!PHYSFS_init(argv0))
			return std::string();
		const char* pref = PHYSFS_getPrefDir(PREF_ORGANIZATION, PREF_APPLICATION);
		std::string dir = pref ? fsu::join(pref, LAUNCHER_STATE_FOLDER) : std::string();
		PHYSFS_deinit();
		if (!dir.empty() && !fsu::makeDirectory(dir))
			dir.clear();
		return dir;
	}
}

int main(int argc, char* argv[])
{
	std::vector<std::string> args(argv + 1, argv + argc);
	const std::string self = fsu::executablePath();
	if (self.empty())
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TITLE, "The launcher cannot find its own folder.", nullptr);
		return 1;
	}
	const std::string stateDir = findStateDir(argc > 0 ? argv[0] : nullptr);
	if (!stateDir.empty())
	{
		gLogPath = fsu::join(stateDir, "launcher.log");
		fsu::removeFile(gLogPath);
	}
	logLine(std::string("launcher ") + BLOBBY_FORK_VERSION + " in " + fsu::parent(self));

	Launcher launcher(self, stateDir);
	const std::string requestPath = stateDir.empty() ? std::string() : fsu::join(stateDir, UPDATE_REQUEST_FILE);
	if (!isLinkHandlerFlag(args))
		launcher.updateAtStart();

	for (;;)
	{
		const auto installed = listInstalledVersions(launcher.installDir());
		const StartHistory history = readStartHistory(stateDir);
		const int chosen = pickVersionToStart(installed, history);
		if (chosen < 0)
		{
			logLine("no complete version folder");
			if (ask(nullptr, SDL_MESSAGEBOX_ERROR, "The game files were not found next to the launcher.\n"
					"Download the game again from the download page.", {"Download page", "Close"}) == 0)
				openDownloadPage();
			return 1;
		}
		if (chosen > 0)
			logLine("version " + installed[0].version.toString() + " did not start before, so "
				+ installed[chosen].version.toString() + " starts instead");
		if (launcher.canInstall())
		{
			InstallLock lock;
			if (lock.acquire(launcher.installDir()))
				removeOldVersions(launcher.installDir(), installed, chosen);
		}

		const InstalledVersion& version = installed[chosen];
		markTried(stateDir, version.version);
		if (!requestPath.empty())
			fsu::removeFile(requestPath);
		setEnvironment(LAUNCHER_PATH_ENV, launcher.self());
		setEnvironment(LAUNCHER_UPDATES_ENV, launcher.canInstall() ? "1" : "");

		const std::string program = fsu::join(version.path, gameExecutableName());
		logLine("starting " + program);
		std::string error;
		const int code = runAndWait(program, args, version.path, error);
		if (!error.empty())
		{
			logLine(error);
			ask(nullptr, SDL_MESSAGEBOX_ERROR, "The game did not start: " + error, {"OK"});
			return 1;
		}
		logLine("the game ended with code " + std::to_string(code));

		std::string request;
		if (requestPath.empty() || !fsu::readFile(requestPath, MAX_UPDATE_REQUEST_BYTES, request))
			return code;
		fsu::removeFile(requestPath);
		const std::string link = readUpdateRequest(request);
		logLine("the game asked for an update" + (link.empty() ? std::string() : " and to join " + link));
		if (launcher.updateOnRequest() == AfterRequest::QUIT)
			return 0;
		args.clear();
		if (!link.empty())
			args.push_back(link);
	}
}
