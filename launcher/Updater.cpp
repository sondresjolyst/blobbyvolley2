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

#include "Updater.h"

#ifdef _WIN32
#include <windows.h>
#include "Utf8.h"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "FsUtil.h"
#include "InstallDir.h"
#include "PackageExtract.h"
#include "Sha256.h"

namespace
{
	const char* const KEYS_VERSION_FILE = "keys-version";
	const uint64_t MAX_SIGNED_FILE_BYTES = 256 * 1024;
	const uint64_t MAX_SIGNATURE_BYTES = 1024;

	UpdateResult failure(UpdateResult result, const std::string& error)
	{
		result.status = UpdateStatus::FAILED;
		result.error = error;
		return result;
	}

	AppVersion newestInstalled(const std::string& installDir)
	{
		const auto installed = listInstalledVersions(installDir);
		return installed.empty() ? AppVersion() : installed[0].version;
	}

	Fetcher::Result fetchSmall(Fetcher& fetcher, const std::string& url, uint64_t maxBytes, std::string& body,
							   std::string& error)
	{
		return fetcher.fetch(url, Fetcher::Kind::SMALL, maxBytes, body, error, ProgressFunction());
	}
}

Fetcher::~Fetcher() = default;

uint64_t readKeysVersion(const std::string& stateDir)
{
	std::string text;
	if (stateDir.empty() || !fsu::readFile(fsu::join(stateDir, KEYS_VERSION_FILE), 64, text))
		return 0;
	uint64_t value = 0;
	for (char c : text)
	{
		if (c == '\n' || c == '\r')
			break;
		if (c < '0' || c > '9' || value > 100000000000ull)
			return 0;
		value = value * 10 + (uint64_t)(c - '0');
	}
	return value;
}

bool writeKeysVersion(const std::string& stateDir, uint64_t version)
{
	return !stateDir.empty() && fsu::writeFile(fsu::join(stateDir, KEYS_VERSION_FILE), std::to_string(version) + "\n");
}

UpdateResult checkForUpdate(Fetcher& fetcher, const UpdaterConfig& config)
{
	UpdateResult result;
	result.installed = newestInstalled(config.installDir);
	PackageFormat format;
	if (!packageFormatOf(config.platform, format))
		return failure(result, "there are no updates for this system");

	std::string keysJson, keysSig, manifestJson, manifestSig, error;
	const std::string prefix = config.latestPrefix;
	for (auto file : {std::make_pair("keys.json", &keysJson), std::make_pair("keys.json.sig", &keysSig),
					  std::make_pair("latest.json", &manifestJson), std::make_pair("latest.json.sig", &manifestSig)})
	{
		const uint64_t limit = std::string(file.first).find(".sig") != std::string::npos ? MAX_SIGNATURE_BYTES : MAX_SIGNED_FILE_BYTES;
		const Fetcher::Result fetched = fetchSmall(fetcher, prefix + file.first, limit, *file.second, error);
		if (fetched == Fetcher::Result::CANCELLED)
		{
			result.status = UpdateStatus::CANCELLED;
			return result;
		}
		if (fetched != Fetcher::Result::OK)
			return failure(result, "could not get " + std::string(file.first) + ": " + error);
	}

	const uint64_t highest = readKeysVersion(config.stateDir);
	TrustedKeys keys;
	UpdateFileError checked = verifyKeysFile(keysJson, keysSig, config.roots, highest, keys);
	if (checked != UpdateFileError::NONE)
		return failure(result, "keys.json was refused: " + describeUpdateFileError(checked));
	if (keys.version > highest)
		writeKeysVersion(config.stateDir, keys.version);

	UpdateManifest manifest;
	checked = verifyManifest(manifestJson, manifestSig, keys, manifest);
	if (checked == UpdateFileError::UNKNOWN_SCHEMA)
		return failure(result, "this launcher is too old to read the update information");
	if (checked != UpdateFileError::NONE)
		return failure(result, "latest.json was refused: " + describeUpdateFileError(checked));
	result.latest = manifest.version;

	if (!(result.installed < manifest.version))
	{
		result.status = UpdateStatus::UP_TO_DATE;
		return result;
	}
	if (!findPackage(manifest, config.platform, result.package))
		return failure(result, "version " + manifest.version.toString() + " has no usable package for this system");
	result.status = UpdateStatus::AVAILABLE;
	return result;
}

UpdateResult installUpdate(Fetcher& fetcher, const UpdaterConfig& config, const UpdateResult& available,
						   const ProgressFunction& progress)
{
	UpdateResult result = available;
	PackageFormat format;
	if (available.status != UpdateStatus::AVAILABLE || !packageFormatOf(config.platform, format)
		|| !isAllowedPackageUrl(available.package.url, "v" + available.latest.toString(), config.platform))
		return failure(result, "there is no update to install");

	InstallLock lock;
	if (!lock.acquire(config.installDir))
		return failure(result, "another copy of the game is updating this folder");
	// never go back to an older or the same version
	result.installed = newestInstalled(config.installDir);
	if (!(result.installed < available.latest))
	{
		result.status = UpdateStatus::UP_TO_DATE;
		return result;
	}

	std::string package, error;
	const Fetcher::Result fetched = fetcher.fetch(available.package.url, Fetcher::Kind::PACKAGE, available.package.size,
												  package, error, progress);
	if (fetched == Fetcher::Result::CANCELLED)
	{
		result.status = UpdateStatus::CANCELLED;
		return result;
	}
	if (fetched != Fetcher::Result::OK)
		return failure(result, "the download failed: " + error);
	if (package.size() != available.package.size)
		return failure(result, "the download has the wrong size");
	if (sha256Hex(package) != available.package.sha256)
		return failure(result, "the download does not match the update information");

	const std::string folderName = appFolderName(available.latest);
	const std::string staging = fsu::join(config.installDir, ".staging-" + folderName);
	fsu::removeTree(staging);
	if (!fsu::makeDirectory(staging))
		return failure(result, "cannot write to the game folder");

	if (!extractPackage(package, format, staging, error))
	{
		fsu::removeTree(staging);
		return failure(result, "cannot unpack the update: " + error);
	}
	package.clear();

	// the package holds one folder, and the version folder is in it
	std::vector<std::string> names;
	fsu::list(staging, names);
	const std::string root = names.size() == 1 ? fsu::join(staging, names[0]) : std::string();
	const std::string unpacked = root.empty() ? std::string() : fsu::join(root, folderName);
	if (unpacked.empty() || !fsu::isFile(fsu::join(unpacked, COMPLETE_MARKER))
		|| !fsu::isFile(fsu::join(unpacked, gameExecutableName())))
	{
		fsu::removeTree(staging);
		return failure(result, "the update does not hold " + folderName);
	}

	// a folder of that name without the marker is what an old failed install left
	const std::string target = fsu::join(config.installDir, folderName);
	fsu::removeTree(target);
	if (!fsu::rename(unpacked, target))
	{
		fsu::removeTree(staging);
		return failure(result, "cannot move the update into the game folder");
	}
	fsu::removeTree(staging);
	result.status = UpdateStatus::INSTALLED;
	return result;
}

#ifdef _WIN32

InstallLock::InstallLock() : mHandle(INVALID_HANDLE_VALUE)
{
}

InstallLock::~InstallLock()
{
	if (mHandle != INVALID_HANDLE_VALUE)
		CloseHandle((HANDLE)mHandle);
}

bool InstallLock::acquire(const std::string& installDir)
{
	if (mHandle != INVALID_HANDLE_VALUE)
		return true;
	// no sharing, so a second launcher cannot open it until this one closes it or ends
	HANDLE handle = CreateFileW(widen(fsu::join(installDir, ".update-lock")).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
								nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return false;
	mHandle = handle;
	return true;
}

#else

InstallLock::InstallLock() : mDescriptor(-1)
{
}

InstallLock::~InstallLock()
{
	if (mDescriptor >= 0)
		close(mDescriptor);
}

bool InstallLock::acquire(const std::string& installDir)
{
	if (mDescriptor >= 0)
		return true;
	const int descriptor = open(fsu::join(installDir, ".update-lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (descriptor < 0)
		return false;
	if (flock(descriptor, LOCK_EX | LOCK_NB) != 0)
	{
		close(descriptor);
		return false;
	}
	mDescriptor = descriptor;
	return true;
}

#endif
