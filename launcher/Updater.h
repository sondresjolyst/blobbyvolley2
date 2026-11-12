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
#include <functional>
#include <string>
#include <vector>

#include "UpdateManifest.h"

/// Called while a download runs. Returns false to stop it.
typedef std::function<bool(uint64_t done, uint64_t total)> ProgressFunction;

/// Downloads files. The launcher uses libcurl, the tests read local files.
class Fetcher
{
	public:
		virtual ~Fetcher();

		enum class Kind
		{
			/// a small signed file, with a short timeout so offline play is not delayed
			SMALL,
			/// the package, which may take a while on a slow line
			PACKAGE
		};

		enum class Result
		{
			OK,
			FAILED,
			CANCELLED
		};

		/// Downloads url into body. It fails when the file is larger than maxBytes.
		virtual Result fetch(const std::string& url, Kind kind, uint64_t maxBytes, std::string& body, std::string& error,
							 const ProgressFunction& progress) = 0;
};

struct UpdaterConfig
{
	/// the folder of the launcher, which holds the version folders
	std::string installDir;
	/// the launcher folder in the settings folder, empty when there is none
	std::string stateDir;
	/// such as windows-x64
	std::string platform;
	/// the keys that may sign keys.json
	std::vector<PublicKey> roots;
	/// where keys.json and latest.json are, with a slash at the end
	std::string latestPrefix = LATEST_RELEASE_PREFIX;
};

enum class UpdateStatus
{
	/// the newest version is installed
	UP_TO_DATE,
	/// a newer version can be installed
	AVAILABLE,
	/// the newer version was installed
	INSTALLED,
	FAILED,
	CANCELLED
};

struct UpdateResult
{
	UpdateStatus status = UpdateStatus::FAILED;
	/// for a player, set when the status is FAILED
	std::string error;
	/// the newest complete version folder before the update, 0.0.0 without one
	AppVersion installed;
	/// the version of the manifest
	AppVersion latest;
	PlatformPackage package;
};

/// The highest version of keys.json that the launcher has seen, 0 when none.
uint64_t readKeysVersion(const std::string& stateDir);
bool writeKeysVersion(const std::string& stateDir, uint64_t version);

/*! Fetches and checks keys.json, latest.json and their signatures.
	\details It remembers a newer keys version in stateDir. The status is AVAILABLE only for a version
	that is newer than every complete version folder and that has a package for the platform.
*/
UpdateResult checkForUpdate(Fetcher& fetcher, const UpdaterConfig& config);

/*! Downloads and installs the package that checkForUpdate found.
	\details It checks the size and SHA-256, unpacks into a staging folder in installDir and renames the
	new version folder into place. It installs nothing that is not newer than the installed versions.
	Another launcher that updates the same folder at the same time makes it fail.
*/
UpdateResult installUpdate(Fetcher& fetcher, const UpdaterConfig& config, const UpdateResult& available,
						   const ProgressFunction& progress);

/// Holds a lock on the install folder while it lives, so two launchers do not update it at once.
class InstallLock
{
	public:
		InstallLock();
		~InstallLock();
		InstallLock(const InstallLock&) = delete;
		InstallLock& operator=(const InstallLock&) = delete;

		/// False when another launcher holds the lock or the folder is not writable.
		bool acquire(const std::string& installDir);

	private:
#ifdef _WIN32
		void* mHandle;
#else
		int mDescriptor;
#endif
};
