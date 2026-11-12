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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Checks of the signed update files, as doc/update-manifest.md describes them.

/// A game version such as 0.3.0.
struct AppVersion
{
	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;

	std::string toString() const;
};

bool operator<(const AppVersion& a, const AppVersion& b);
bool operator==(const AppVersion& a, const AppVersion& b);

/// Reads a version like 1.2.3. Each part has one to six digits and no leading zero.
bool parseAppVersion(const std::string& text, AppVersion& version);

typedef std::array<uint8_t, 32> PublicKey;
typedef std::array<uint8_t, 64> Signature;

/// Reads 64 lowercase hex digits into a key.
bool parsePublicKeyHex(const std::string& hex, PublicKey& key);

/// Reads the base64 text of a .sig file: 88 characters with padding, with whitespace allowed around it.
bool parseSignature(const std::string& text, Signature& signature);

/// True when signature is a valid Ed25519 signature of message by key, with SHA-512 as in RFC 8032.
bool checkEd25519(const std::string& message, const Signature& signature, const PublicKey& key);

/// The two root keys built into the launcher. They sign keys.json.
std::vector<PublicKey> officialRootKeys();

enum class UpdateFileError
{
	NONE,
	/// The signature file is not 64 bytes of base64.
	BAD_SIGNATURE_TEXT,
	/// No trusted key made the signature.
	BAD_SIGNATURE,
	/// Signed, but not the JSON the launcher expects.
	BAD_FORMAT,
	/// A schema number this launcher does not know. The file is ignored.
	UNKNOWN_SCHEMA,
	/// keys.json is older than one this launcher has seen.
	OLD_KEYS
};

std::string describeUpdateFileError(UpdateFileError error);

/// Highest keys.json version the launcher accepts and stores. A higher one is refused as damaged.
constexpr uint64_t MAX_KEYS_VERSION = 1000000000;

/// The signing keys that a root key vouched for.
struct TrustedKeys
{
	uint64_t version = 0;
	/// signing_keys without the revoked ones
	std::vector<PublicKey> active;
};

/*! Checks keys.json and keys.json.sig.
	\details The signature must come from one of roots. The version must be a whole number from 1 to
	MAX_KEYS_VERSION and must not be lower than highestSeen. The caller remembers keys.version as the new highest version.
*/
UpdateFileError verifyKeysFile(const std::string& json, const std::string& signatureText,
							   const std::vector<PublicKey>& roots, uint64_t highestSeen, TrustedKeys& keys);

/// One package in latest.json
struct PlatformPackage
{
	std::string name;
	std::string url;
	uint64_t size = 0;
	std::string sha256;
};

/// latest.json
struct UpdateManifest
{
	AppVersion version;
	std::string tag;
	std::vector<PlatformPackage> platforms;
};

/// Checks latest.json and latest.json.sig with the active signing keys, and reads it only after that.
UpdateFileError verifyManifest(const std::string& json, const std::string& signatureText, const TrustedKeys& keys,
							   UpdateManifest& manifest);

/// Release downloads start with this. The launcher downloads nothing else.
extern const char* const RELEASE_DOWNLOAD_PREFIX;
/// The signed files of the newest release are below this.
extern const char* const LATEST_RELEASE_PREFIX;
/// Where players get the game by hand.
extern const char* const DOWNLOAD_PAGE_URL;

/// Largest package the launcher downloads.
constexpr uint64_t MAX_PACKAGE_BYTES = 512ull * 1024 * 1024;

/// The archive type of the packages of a platform: zip for Windows and macOS, tar.gz for Linux.
enum class PackageFormat
{
	ZIP,
	TAR_GZ
};

/// False when the platform is not one the release builds.
bool packageFormatOf(const std::string& platform, PackageFormat& format);

/*! True for the download link of a package of the release tag.
	\details It is RELEASE_DOWNLOAD_PREFIX, the tag, a slash and a file name of letters, digits, dots,
	dashes and underscores that ends with the extension of the platform.
*/
bool isAllowedPackageUrl(const std::string& url, const std::string& tag, const std::string& platform);

/*! Finds the package for platform in the manifest and checks it.
	\details The tag must be v and the version, the link must pass isAllowedPackageUrl, the size must be
	between 1 and MAX_PACKAGE_BYTES and the SHA-256 must be 64 lowercase hex digits.
*/
bool findPackage(const UpdateManifest& manifest, const std::string& platform, PlatformPackage& package);

/// The platform name of this build, such as windows-x64. Empty when releases have no package for it.
std::string currentPlatform();
