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

#include <boost/test/unit_test.hpp>

#include <map>
#include <memory>

#include <archive.h>
#include <archive_entry.h>

#include "FsUtil.h"
#include "InstallDir.h"
#include "JoinLink.h"
#include "LauncherLink.h"
#include "PackageExtract.h"
#include "Sha256.h"
#include "TestDir.h"
#include "UpdateTestKeys.h"
#include "Updater.h"

namespace
{
	AppVersion version(uint32_t major, uint32_t minor, uint32_t patch)
	{
		AppVersion v;
		v.major = major;
		v.minor = minor;
		v.patch = patch;
		return v;
	}

	/// a new empty folder below the test folder
	std::string freshDir(const std::string& name)
	{
		const std::string path = testPath(name);
		fsu::removeTree(path);
		BOOST_REQUIRE(fsu::makeDirectory(path));
		return path;
	}

	void writeFile(const std::string& path, const std::string& content)
	{
		BOOST_REQUIRE(fsu::writeFile(path, content));
	}

	/// a version folder, complete when the marker is written
	void makeAppFolder(const std::string& installDir, const std::string& versionText, bool complete = true)
	{
		const std::string folder = fsu::join(installDir, "app-" + versionText);
		BOOST_REQUIRE(fsu::makeDirectory(folder));
		writeFile(fsu::join(folder, gameExecutableName()), "game " + versionText);
		if (complete)
			writeFile(fsu::join(folder, COMPLETE_MARKER), "");
	}

	struct ArchiveFile
	{
		std::string path;
		std::string content;
		/// AE_IFREG, AE_IFDIR or AE_IFLNK
		unsigned type;
		int mode;
	};

	ArchiveFile file(const std::string& path, const std::string& content, int mode = 0644)
	{
		return ArchiveFile{path, content, AE_IFREG, mode};
	}

	ArchiveFile folder(const std::string& path)
	{
		return ArchiveFile{path, std::string(), AE_IFDIR, 0755};
	}

	/// a package in memory, made with libarchive like zip and tar make them
	std::string makeArchive(PackageFormat format, const std::vector<ArchiveFile>& files)
	{
		std::unique_ptr<archive, int (*)(archive*)> writer(archive_write_new(), archive_write_free);
		if (format == PackageFormat::ZIP)
		{
			archive_write_set_format_zip(writer.get());
		}
		else
		{
			archive_write_add_filter_gzip(writer.get());
			archive_write_set_format_pax_restricted(writer.get());
		}
		std::vector<char> buffer(4 * 1024 * 1024);
		size_t used = 0;
		BOOST_REQUIRE(archive_write_open_memory(writer.get(), buffer.data(), buffer.size(), &used) == ARCHIVE_OK);
		for (const auto& item : files)
		{
			archive_entry* entry = archive_entry_new();
			archive_entry_set_pathname(entry, item.path.c_str());
			archive_entry_set_filetype(entry, item.type);
			archive_entry_set_perm(entry, item.mode);
			if (item.type == AE_IFLNK)
				archive_entry_set_symlink(entry, item.content.c_str());
			archive_entry_set_size(entry, item.type == AE_IFREG ? (la_int64_t)item.content.size() : 0);
			BOOST_REQUIRE(archive_write_header(writer.get(), entry) == ARCHIVE_OK);
			if (item.type == AE_IFREG && !item.content.empty())
				archive_write_data(writer.get(), item.content.data(), item.content.size());
			archive_entry_free(entry);
		}
		BOOST_REQUIRE(archive_write_close(writer.get()) == ARCHIVE_OK);
		return std::string(buffer.data(), used);
	}

	/// the files of a package of the new layout
	std::vector<ArchiveFile> packageFiles(const std::string& versionText, bool withMarker = true)
	{
		const std::string root = "blobbyvolley2-tournament-" + versionText + "-test/";
		const std::string app = root + "app-" + versionText + "/";
		std::vector<ArchiveFile> files = {
			folder(root),
			file(root + gameExecutableName(), "launcher", 0755),
			folder(app),
			file(app + gameExecutableName(), "game " + versionText, 0755),
			folder(app + "data/"),
			file(app + "data/rules.zip", std::string(5000, 'r')),
		};
		if (withMarker)
			files.push_back(file(app + COMPLETE_MARKER, ""));
		return files;
	}

	/// Serves files from memory instead of the network.
	class MemoryFetcher : public Fetcher
	{
		public:
			std::map<std::string, std::string> files;
			std::vector<std::string> requested;
			bool cancelPackage = false;

			Result fetch(const std::string& url, Kind kind, uint64_t maxBytes, std::string& body, std::string& error,
						 const ProgressFunction& progress) override
			{
				requested.push_back(url);
				auto found = files.find(url);
				if (found == files.end())
				{
					error = "HTTP 404";
					return Result::FAILED;
				}
				if (found->second.size() > maxBytes)
				{
					error = "too large";
					return Result::FAILED;
				}
				if (kind == Kind::PACKAGE && progress && (!progress(found->second.size() / 2, found->second.size()) || cancelPackage))
					return Result::CANCELLED;
				body = found->second;
				return Result::OK;
			}
	};

	/// Serves the files of a release from a local folder, by the last part of the link.
	class FolderFetcher : public Fetcher
	{
		public:
			explicit FolderFetcher(std::string folder) : mFolder(std::move(folder)) {}

			Result fetch(const std::string& url, Kind, uint64_t maxBytes, std::string& body, std::string& error,
						 const ProgressFunction& progress) override
			{
				const std::string name = url.substr(url.find_last_of('/') + 1);
				std::string content;
				if (!fsu::readFile(fsu::join(mFolder, name), (std::size_t)maxBytes, content))
				{
					error = "cannot read " + name;
					return Result::FAILED;
				}
				if (progress && !progress(content.size(), content.size()))
					return Result::CANCELLED;
				body.swap(content);
				return Result::OK;
			}

		private:
			std::string mFolder;
	};

	const std::string PREFIX = "https://updates.test/latest/";

	/// A fake release of version with a package in the given format, signed with test keys.
	struct FakeRelease
	{
		TestKey root{11};
		TestKey signing{12};
		MemoryFetcher fetcher;
		UpdaterConfig config;
		std::string package;

		FakeRelease(const std::string& installDir, const std::string& stateDir, const std::string& versionText,
					PackageFormat format = PackageFormat::TAR_GZ)
		{
			config.installDir = installDir;
			config.stateDir = stateDir;
			config.platform = format == PackageFormat::ZIP ? "windows-x64" : "linux-x64";
			config.roots = {root.publicKey};
			config.latestPrefix = PREFIX;
			setKeys(1);
			setPackage(versionText, makeArchive(format, packageFiles(versionText)));
		}

		void setKeys(uint64_t keysVersion)
		{
			const std::string keys = makeKeysJson(keysVersion, {&signing}, {});
			fetcher.files[PREFIX + "keys.json"] = keys;
			fetcher.files[PREFIX + "keys.json.sig"] = root.sign(keys);
		}

		std::string packageUrl(const std::string& versionText) const
		{
			return std::string(RELEASE_DOWNLOAD_PREFIX) + "v" + versionText + "/blobbyvolley2-tournament-" + versionText
				+ "-" + config.platform + (config.platform == "linux-x64" ? ".tar.gz" : ".zip");
		}

		void setPackage(const std::string& versionText, const std::string& data, const std::string& url = std::string(),
						const std::string& sha = std::string())
		{
			package = data;
			const std::string link = url.empty() ? packageUrl(versionText) : url;
			setManifest(makeManifestJson(versionText, config.platform, link, data.size(), sha.empty() ? sha256Hex(data) : sha));
			fetcher.files[link] = data;
		}

		void setManifest(const std::string& manifest)
		{
			fetcher.files[PREFIX + "latest.json"] = manifest;
			fetcher.files[PREFIX + "latest.json.sig"] = signing.sign(manifest);
		}

		UpdateResult update()
		{
			UpdateResult check = checkForUpdate(fetcher, config);
			if (check.status != UpdateStatus::AVAILABLE)
				return check;
			return installUpdate(fetcher, config, check, [](uint64_t, uint64_t) { return true; });
		}
	};

	std::vector<std::string> installedVersions(const std::string& installDir)
	{
		std::vector<std::string> names;
		for (const auto& item : listInstalledVersions(installDir))
			names.push_back(item.version.toString());
		return names;
	}

	bool hasLeftovers(const std::string& installDir)
	{
		std::vector<std::string> names;
		fsu::list(installDir, names);
		for (const auto& name : names)
		{
			if (name.compare(0, 9, ".staging-") == 0 || name.compare(0, 7, ".trash-") == 0)
				return true;
		}
		return false;
	}
}

BOOST_AUTO_TEST_SUITE( launcher )

BOOST_AUTO_TEST_CASE( version_folders_are_named_after_the_version )
{
	BOOST_CHECK_EQUAL(appFolderName(version(0, 3, 0)), "app-0.3.0");
	AppVersion read;
	BOOST_CHECK(parseAppFolderName("app-0.10.2", read));
	BOOST_CHECK(read == version(0, 10, 2));
	BOOST_CHECK(!parseAppFolderName("app-0.10", read));
	BOOST_CHECK(!parseAppFolderName("App-0.1.0", read));
	BOOST_CHECK(!parseAppFolderName(".staging-app-0.1.0", read));
}

BOOST_AUTO_TEST_CASE( only_complete_folders_count )
{
	const std::string dir = freshDir("installed");
	makeAppFolder(dir, "0.1.0");
	makeAppFolder(dir, "0.10.0");
	makeAppFolder(dir, "0.2.0");
	makeAppFolder(dir, "0.11.0", false);
	BOOST_REQUIRE(fsu::makeDirectory(fsu::join(dir, "app-latest")));
	BOOST_REQUIRE(fsu::makeDirectory(fsu::join(dir, "data")));
	const std::vector<std::string> expected = {"0.10.0", "0.2.0", "0.1.0"};
	BOOST_CHECK(installedVersions(dir) == expected);
	BOOST_CHECK(listInstalledVersions(testPath("missing")).empty());
}

BOOST_AUTO_TEST_CASE( the_newest_version_that_did_not_fail_starts )
{
	std::vector<InstalledVersion> installed(3);
	installed[0].version = version(0, 4, 0);
	installed[1].version = version(0, 3, 0);
	installed[2].version = version(0, 2, 0);

	StartHistory history;
	BOOST_CHECK_EQUAL(pickVersionToStart(installed, history), 0);
	// the first start of a new version
	history.tried.insert("0.4.0");
	history.started.insert("0.3.0");
	history.tried.insert("0.3.0");
	BOOST_CHECK_EQUAL(pickVersionToStart(installed, history), 1);
	// it ran once, so it is fine
	history.started.insert("0.4.0");
	BOOST_CHECK_EQUAL(pickVersionToStart(installed, history), 0);

	StartHistory allFailed;
	allFailed.tried = {"0.4.0", "0.3.0", "0.2.0"};
	BOOST_CHECK_EQUAL(pickVersionToStart(installed, allFailed), 0);
	StartHistory twoFailed;
	twoFailed.tried = {"0.4.0", "0.3.0"};
	BOOST_CHECK_EQUAL(pickVersionToStart(installed, twoFailed), 2);
	BOOST_CHECK_EQUAL(pickVersionToStart(std::vector<InstalledVersion>(), history), -1);
}

BOOST_AUTO_TEST_CASE( start_markers_are_read_from_the_state_folder )
{
	const std::string state = freshDir("state-markers");
	markTried(state, version(0, 3, 0));
	markTried(state, version(0, 4, 0));
	writeFile(fsu::join(state, startedMarkerName("0.3.0")), "");
	writeFile(fsu::join(state, "tried-junk"), "");
	const StartHistory history = readStartHistory(state);
	BOOST_CHECK(!history.failed(version(0, 3, 0)));
	BOOST_CHECK(history.failed(version(0, 4, 0)));
	BOOST_CHECK(!history.failed(version(0, 5, 0)));
	BOOST_CHECK_EQUAL(history.tried.size(), 2u);
	BOOST_CHECK(readStartHistory("").tried.empty());
}

BOOST_AUTO_TEST_CASE( old_versions_are_removed_and_the_previous_one_stays )
{
	const std::string dir = freshDir("cleanup");
	for (const char* v : {"0.1.0", "0.2.0", "0.3.0", "0.4.0"})
		makeAppFolder(dir, v);
	BOOST_REQUIRE(fsu::makeDirectory(fsu::join(dir, ".staging-app-0.5.0")));
	writeFile(fsu::join(dir, ".staging-app-0.5.0/x"), "half");
	writeFile(fsu::join(dir, "settings.txt"), "kept");

	removeOldVersions(dir, listInstalledVersions(dir), 0);
	std::vector<std::string> expected = {"0.4.0", "0.3.0"};
	BOOST_CHECK(installedVersions(dir) == expected);
	BOOST_CHECK(!hasLeftovers(dir));
	BOOST_CHECK(fsu::isFile(fsu::join(dir, "settings.txt")));

	// a rollback keeps the version it starts
	makeAppFolder(dir, "0.2.0");
	removeOldVersions(dir, listInstalledVersions(dir), 2);
	expected = {"0.4.0", "0.3.0", "0.2.0"};
	BOOST_CHECK(installedVersions(dir) == expected);
}

BOOST_AUTO_TEST_CASE( translocated_apps_are_found )
{
	BOOST_CHECK(isTranslocated("/private/var/folders/x/T/AppTranslocation/ABC/d/Blobby.app/Contents/MacOS"));
	BOOST_CHECK(!isTranslocated("/Applications/Blobby.app/Contents/MacOS"));
}

BOOST_AUTO_TEST_CASE( archive_paths_are_checked )
{
	BOOST_CHECK(isSafeArchivePath("pkg/app-0.3.0/blobby"));
	BOOST_CHECK(isSafeArchivePath("pkg/app-0.3.0/"));
	for (const char* bad : {"", "/etc/passwd", "../x", "pkg/../../x", "pkg/./x", "pkg//x", "pkg\\x", "C:/x", "c:x",
							"pkg/a\nb", "..", "."})
	{
		BOOST_CHECK_MESSAGE(!isSafeArchivePath(bad), bad);
	}
}

BOOST_AUTO_TEST_CASE( packages_unpack_with_their_folders )
{
	for (PackageFormat format : {PackageFormat::ZIP, PackageFormat::TAR_GZ})
	{
		const std::string dir = freshDir(format == PackageFormat::ZIP ? "unpack-zip" : "unpack-tar");
		std::string error;
		BOOST_REQUIRE_MESSAGE(extractPackage(makeArchive(format, packageFiles("0.3.0")), format, dir, error), error);
		const std::string app = fsu::join(dir, "blobbyvolley2-tournament-0.3.0-test/app-0.3.0");
		std::string content;
		BOOST_REQUIRE(fsu::readFile(fsu::join(app, gameExecutableName()), 100, content));
		BOOST_CHECK_EQUAL(content, "game 0.3.0");
		BOOST_REQUIRE(fsu::readFile(fsu::join(app, "data/rules.zip"), 10000, content));
		BOOST_CHECK_EQUAL(content.size(), 5000u);
		BOOST_CHECK(fsu::isFile(fsu::join(app, COMPLETE_MARKER)));
	}
}

BOOST_AUTO_TEST_CASE( dangerous_archives_are_refused )
{
	const std::vector<std::vector<ArchiveFile>> bad = {
		{file("pkg/../evil.txt", "x")},
		{file("/tmp/evil.txt", "x")},
		{file("pkg\\..\\evil.txt", "x")},
		{folder("pkg/"), ArchiveFile{"pkg/link", "/etc", AE_IFLNK, 0777}},
		{},
	};
	for (std::size_t i = 0; i < bad.size(); ++i)
	{
		const std::string dir = freshDir("refused");
		std::string error;
		BOOST_CHECK_MESSAGE(!extractPackage(makeArchive(PackageFormat::TAR_GZ, bad[i]), PackageFormat::TAR_GZ, dir, error),
							"archive " << i);
		BOOST_CHECK(!error.empty());
		BOOST_CHECK(!fsu::isFile(testPath("evil.txt")));
	}
	std::string error;
	const std::string dir = freshDir("refused");
	BOOST_CHECK(!extractPackage("not an archive", PackageFormat::ZIP, dir, error));

	// more data than the limit
	ExtractLimits limits;
	limits.maxTotalBytes = 1000;
	BOOST_CHECK(!extractPackage(makeArchive(PackageFormat::ZIP, packageFiles("0.3.0")), PackageFormat::ZIP, dir, error, limits));
	limits = ExtractLimits();
	limits.maxEntries = 2;
	BOOST_CHECK(!extractPackage(makeArchive(PackageFormat::ZIP, packageFiles("0.3.0")), PackageFormat::ZIP, freshDir("refused2"), error, limits));
}

BOOST_AUTO_TEST_CASE( an_install_of_the_previous_version_is_updated )
{
	for (PackageFormat format : {PackageFormat::TAR_GZ, PackageFormat::ZIP})
	{
		const std::string install = freshDir("e2e-install");
		const std::string state = freshDir("e2e-state");
		makeAppFolder(install, "0.2.0");
		FakeRelease release(install, state, "0.3.0", format);
		// the release files lie in a local folder, as a release would hold them
		const std::string releaseDir = freshDir("e2e-release");
		for (const auto& item : release.fetcher.files)
			writeFile(fsu::join(releaseDir, item.first.substr(item.first.find_last_of('/') + 1)), item.second);
		FolderFetcher fetcher(releaseDir);

		UpdateResult check = checkForUpdate(fetcher, release.config);
		BOOST_REQUIRE_MESSAGE(check.status == UpdateStatus::AVAILABLE, check.error);
		BOOST_CHECK(check.installed == version(0, 2, 0));
		BOOST_CHECK(check.latest == version(0, 3, 0));
		BOOST_CHECK_EQUAL(readKeysVersion(state), 1u);

		int progressCalls = 0;
		UpdateResult done = installUpdate(fetcher, release.config, check, [&](uint64_t, uint64_t) { ++progressCalls; return true; });
		BOOST_REQUIRE_MESSAGE(done.status == UpdateStatus::INSTALLED, done.error);
		BOOST_CHECK_GT(progressCalls, 0);
		const std::vector<std::string> expected = {"0.3.0", "0.2.0"};
		BOOST_CHECK(installedVersions(install) == expected);
		std::string content;
		BOOST_REQUIRE(fsu::readFile(fsu::join(install, "app-0.3.0/" + gameExecutableName()), 100, content));
		BOOST_CHECK_EQUAL(content, "game 0.3.0");
		BOOST_CHECK(!hasLeftovers(install));
		// the launcher of the package is not copied over the running one
		BOOST_CHECK(!fsu::isFile(fsu::join(install, gameExecutableName())));

		// the next check finds nothing newer
		BOOST_CHECK(checkForUpdate(fetcher, release.config).status == UpdateStatus::UP_TO_DATE);
		// and the launcher starts the new version
		const auto installed = listInstalledVersions(install);
		BOOST_CHECK_EQUAL(pickVersionToStart(installed, readStartHistory(state)), 0);

		// only the release files are asked for
		BOOST_REQUIRE(release.update().status == UpdateStatus::UP_TO_DATE);
		for (const auto& url : release.fetcher.requested)
		{
			BOOST_CHECK_MESSAGE(url.compare(0, PREFIX.size(), PREFIX) == 0
				|| url.compare(0, std::string(RELEASE_DOWNLOAD_PREFIX).size(), RELEASE_DOWNLOAD_PREFIX) == 0, url);
		}
	}
}

BOOST_AUTO_TEST_CASE( an_equal_or_older_version_is_never_installed )
{
	const std::string install = freshDir("e2e-older");
	const std::string state = freshDir("e2e-older-state");
	makeAppFolder(install, "0.3.0");
	FakeRelease same(install, state, "0.3.0");
	BOOST_CHECK(same.update().status == UpdateStatus::UP_TO_DATE);
	FakeRelease older(install, state, "0.2.5");
	BOOST_CHECK(older.update().status == UpdateStatus::UP_TO_DATE);
	const std::vector<std::string> expected = {"0.3.0"};
	BOOST_CHECK(installedVersions(install) == expected);

	// an install that was checked before a newer version arrived does not go back either
	FakeRelease newer(install, state, "0.4.0");
	UpdateResult check = checkForUpdate(newer.fetcher, newer.config);
	BOOST_REQUIRE(check.status == UpdateStatus::AVAILABLE);
	makeAppFolder(install, "0.4.0");
	BOOST_CHECK(installUpdate(newer.fetcher, newer.config, check, ProgressFunction()).status == UpdateStatus::UP_TO_DATE);
}

BOOST_AUTO_TEST_CASE( bad_updates_change_nothing )
{
	struct Case
	{
		const char* name;
		std::function<void(FakeRelease&)> spoil;
	};
	const std::vector<Case> cases = {
		{"wrong hash", [](FakeRelease& r) { r.setPackage("0.3.0", r.package, std::string(), std::string(64, 'a')); }},
		{"changed package", [](FakeRelease& r) { std::string url = r.packageUrl("0.3.0"); r.fetcher.files[url][10] ^= 1; }},
		{"missing marker", [](FakeRelease& r) { r.setPackage("0.3.0", makeArchive(PackageFormat::TAR_GZ, packageFiles("0.3.0", false))); }},
		{"wrong version folder", [](FakeRelease& r) { r.setPackage("0.3.0", makeArchive(PackageFormat::TAR_GZ, packageFiles("0.3.1"))); }},
		{"link to another site", [](FakeRelease& r) { r.setPackage("0.3.0", r.package, "https://example.com/v0.3.0/a.tar.gz"); }},
		{"link to another release", [](FakeRelease& r) { r.setPackage("0.3.0", r.package, std::string(RELEASE_DOWNLOAD_PREFIX) + "v0.2.0/a.tar.gz"); }},
		{"manifest signed by a root key", [](FakeRelease& r) {
			const std::string m = r.fetcher.files[PREFIX + "latest.json"];
			r.fetcher.files[PREFIX + "latest.json.sig"] = r.root.sign(m); }},
		{"keys signed by a stranger", [](FakeRelease& r) {
			TestKey stranger(99);
			r.fetcher.files[PREFIX + "keys.json.sig"] = stranger.sign(r.fetcher.files[PREFIX + "keys.json"]); }},
		{"revoked signing key", [](FakeRelease& r) {
			const std::string keys = makeKeysJson(2, {&r.signing}, {&r.signing});
			r.fetcher.files[PREFIX + "keys.json"] = keys;
			r.fetcher.files[PREFIX + "keys.json.sig"] = r.root.sign(keys); }},
		{"older keys", [](FakeRelease& r) { writeKeysVersion(r.config.stateDir, 7); }},
		{"offline", [](FakeRelease& r) { r.fetcher.files.clear(); }},
		{"missing package", [](FakeRelease& r) { r.fetcher.files.erase(r.packageUrl("0.3.0")); }},
	};
	for (const auto& item : cases)
	{
		const std::string install = freshDir("e2e-bad");
		const std::string state = freshDir("e2e-bad-state");
		makeAppFolder(install, "0.2.0");
		FakeRelease release(install, state, "0.3.0");
		item.spoil(release);
		const UpdateResult result = release.update();
		BOOST_CHECK_MESSAGE(result.status == UpdateStatus::FAILED, item.name);
		BOOST_CHECK_MESSAGE(!result.error.empty(), item.name);
		const std::vector<std::string> expected = {"0.2.0"};
		BOOST_CHECK_MESSAGE(installedVersions(install) == expected, item.name);
		BOOST_CHECK_MESSAGE(!fsu::isDirectory(fsu::join(install, "app-0.3.0")), item.name);
		BOOST_CHECK_MESSAGE(!hasLeftovers(install), item.name);
	}
}

BOOST_AUTO_TEST_CASE( a_newer_key_list_is_remembered )
{
	const std::string install = freshDir("e2e-keys");
	const std::string state = freshDir("e2e-keys-state");
	makeAppFolder(install, "0.2.0");
	FakeRelease release(install, state, "0.3.0");
	release.setKeys(4);
	BOOST_CHECK(checkForUpdate(release.fetcher, release.config).status == UpdateStatus::AVAILABLE);
	BOOST_CHECK_EQUAL(readKeysVersion(state), 4u);
	// a replayed older key list is refused from now on
	release.setKeys(3);
	BOOST_CHECK(checkForUpdate(release.fetcher, release.config).status == UpdateStatus::FAILED);
	BOOST_CHECK_EQUAL(readKeysVersion(state), 4u);
}

BOOST_AUTO_TEST_CASE( a_cancelled_download_installs_nothing )
{
	const std::string install = freshDir("e2e-cancel");
	const std::string state = freshDir("e2e-cancel-state");
	makeAppFolder(install, "0.2.0");
	FakeRelease release(install, state, "0.3.0");
	UpdateResult check = checkForUpdate(release.fetcher, release.config);
	BOOST_REQUIRE(check.status == UpdateStatus::AVAILABLE);
	UpdateResult result = installUpdate(release.fetcher, release.config, check, [](uint64_t, uint64_t) { return false; });
	BOOST_CHECK(result.status == UpdateStatus::CANCELLED);
	BOOST_CHECK(!fsu::isDirectory(fsu::join(install, "app-0.3.0")));
}

BOOST_AUTO_TEST_CASE( a_second_launcher_cannot_update_at_the_same_time )
{
	const std::string install = freshDir("e2e-lock");
	const std::string state = freshDir("e2e-lock-state");
	makeAppFolder(install, "0.2.0");
	FakeRelease release(install, state, "0.3.0");
	UpdateResult check = checkForUpdate(release.fetcher, release.config);
	BOOST_REQUIRE(check.status == UpdateStatus::AVAILABLE);
	{
		InstallLock other;
		BOOST_REQUIRE(other.acquire(install));
		BOOST_CHECK(installUpdate(release.fetcher, release.config, check, ProgressFunction()).status == UpdateStatus::FAILED);
	}
	BOOST_CHECK(installUpdate(release.fetcher, release.config, check, ProgressFunction()).status == UpdateStatus::INSTALLED);
}

BOOST_AUTO_TEST_CASE( update_requests_carry_a_join_link )
{
	JoinLink link;
	link.host = "lan.example";
	link.port = 1234;
	link.code = "ABCDEFGHJK";
	const std::string text = formatJoinLink(link);
	BOOST_CHECK_EQUAL(text, "blobby://join?server=lan.example:1234&code=ABCDEFGHJK");
	JoinLink parsed;
	BOOST_CHECK(parseJoinLink(text, parsed) == JoinLinkError::NONE);

	BOOST_CHECK_EQUAL(readUpdateRequest(makeUpdateRequest(text)), text);
	BOOST_CHECK_EQUAL(readUpdateRequest(makeUpdateRequest("")), "");
	BOOST_CHECK_EQUAL(readUpdateRequest(makeUpdateRequest("https://example.com/")), "");
	BOOST_CHECK_EQUAL(readUpdateRequest(makeUpdateRequest("blobby://join --evil")), "");
	BOOST_CHECK_EQUAL(readUpdateRequest("link=" + text), "");
	BOOST_CHECK_EQUAL(readUpdateRequest(makeUpdateRequest(text + std::string(400, 'A'))), "");

	auto env = [](const char* name) -> const char* {
		const std::string key = name;
		if (key == LAUNCHER_PATH_ENV)
			return "C:/Games/Blobby/blobby.exe";
		if (key == LAUNCHER_UPDATES_ENV)
			return "1";
		return nullptr;
	};
	BOOST_CHECK(launcherCanUpdate(env));
	BOOST_CHECK_EQUAL(launcherPath(env), "C:/Games/Blobby/blobby.exe");
	BOOST_CHECK(!launcherCanUpdate([](const char*) -> const char* { return nullptr; }));
	BOOST_CHECK(!launcherCanUpdate([](const char* name) -> const char* {
		return std::string(name) == LAUNCHER_UPDATES_ENV ? "1" : nullptr; }));
}

BOOST_AUTO_TEST_SUITE_END()
