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

#include "UpdateManifest.h"

#include <algorithm>
#include <tuple>

#include <nlohmann/json.hpp>

#include "monocypher-ed25519.h"
#include "Sha256.h"

const char* const RELEASE_DOWNLOAD_PREFIX = "https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/";
const char* const LATEST_RELEASE_PREFIX = "https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/latest/download/";
const char* const DOWNLOAD_PAGE_URL = "https://julalan.no/last-ned";

namespace
{
	bool parsePart(const std::string& text, uint32_t& value)
	{
		if (text.empty() || text.size() > 6 || (text.size() > 1 && text[0] == '0'))
			return false;
		uint32_t read = 0;
		for (char c : text)
		{
			if (c < '0' || c > '9')
				return false;
			read = read * 10 + (uint32_t)(c - '0');
		}
		value = read;
		return true;
	}

	int hexValue(char c)
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		return -1;
	}

	int base64Value(char c)
	{
		if (c >= 'A' && c <= 'Z')
			return c - 'A';
		if (c >= 'a' && c <= 'z')
			return c - 'a' + 26;
		if (c >= '0' && c <= '9')
			return c - '0' + 52;
		if (c == '+')
			return 62;
		if (c == '/')
			return 63;
		return -1;
	}

	bool isSpace(char c)
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	// strict standard base64 with padding
	bool decodeBase64(const std::string& text, std::vector<uint8_t>& out)
	{
		if (text.empty() || text.size() % 4 != 0)
			return false;
		std::size_t padding = 0;
		if (text[text.size() - 1] == '=')
			++padding;
		if (text[text.size() - 2] == '=')
			++padding;
		std::vector<uint8_t> bytes;
		uint32_t buffer = 0;
		int bits = 0;
		for (std::size_t i = 0; i < text.size() - padding; ++i)
		{
			int value = base64Value(text[i]);
			if (value < 0)
				return false;
			buffer = (buffer << 6) | (uint32_t)value;
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				bytes.push_back((uint8_t)((buffer >> bits) & 0xFF));
			}
		}
		// the bits left over must be zero, so each signature has only one text
		if ((buffer & ((1u << bits) - 1)) != 0)
			return false;
		out.swap(bytes);
		return true;
	}

	bool readKeyList(const nlohmann::json& list, std::vector<PublicKey>& keys)
	{
		if (!list.is_array())
			return false;
		for (const auto& item : list)
		{
			if (!item.is_string())
				return false;
			PublicKey key;
			if (!parsePublicKeyHex(item.get<std::string>(), key))
				return false;
			keys.push_back(key);
		}
		return true;
	}

	bool signedByAny(const std::string& message, const Signature& signature, const std::vector<PublicKey>& keys)
	{
		for (const auto& key : keys)
		{
			if (checkEd25519(message, signature, key))
				return true;
		}
		return false;
	}

	bool readSchema(const nlohmann::json& document, UpdateFileError& error)
	{
		if (!document.is_object())
		{
			error = UpdateFileError::BAD_FORMAT;
			return false;
		}
		auto schema = document.find("schema");
		if (schema == document.end() || !schema->is_number_unsigned())
		{
			error = UpdateFileError::BAD_FORMAT;
			return false;
		}
		if (schema->get<uint64_t>() != 1)
		{
			error = UpdateFileError::UNKNOWN_SCHEMA;
			return false;
		}
		return true;
	}

	bool isFileNameCharacter(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
	}

	bool endsWith(const std::string& text, const std::string& end)
	{
		return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
	}
}

std::string AppVersion::toString() const
{
	return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

bool operator<(const AppVersion& a, const AppVersion& b)
{
	return std::tie(a.major, a.minor, a.patch) < std::tie(b.major, b.minor, b.patch);
}

bool operator==(const AppVersion& a, const AppVersion& b)
{
	return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

bool parseAppVersion(const std::string& text, AppVersion& version)
{
	const auto first = text.find('.');
	if (first == std::string::npos)
		return false;
	const auto second = text.find('.', first + 1);
	if (second == std::string::npos)
		return false;
	AppVersion read;
	if (!parsePart(text.substr(0, first), read.major)
		|| !parsePart(text.substr(first + 1, second - first - 1), read.minor)
		|| !parsePart(text.substr(second + 1), read.patch))
		return false;
	version = read;
	return true;
}

bool parsePublicKeyHex(const std::string& hex, PublicKey& key)
{
	if (hex.size() != 64)
		return false;
	PublicKey read;
	for (std::size_t i = 0; i < read.size(); ++i)
	{
		const int high = hexValue(hex[2 * i]);
		const int low = hexValue(hex[2 * i + 1]);
		if (high < 0 || low < 0)
			return false;
		read[i] = (uint8_t)(high * 16 + low);
	}
	key = read;
	return true;
}

bool parseSignature(const std::string& text, Signature& signature)
{
	std::size_t begin = 0;
	std::size_t end = text.size();
	while (begin < end && isSpace(text[begin]))
		++begin;
	while (end > begin && isSpace(text[end - 1]))
		--end;
	if (end - begin != 88)
		return false;
	std::vector<uint8_t> bytes;
	if (!decodeBase64(text.substr(begin, end - begin), bytes) || bytes.size() != signature.size())
		return false;
	std::copy(bytes.begin(), bytes.end(), signature.begin());
	return true;
}

bool checkEd25519(const std::string& message, const Signature& signature, const PublicKey& key)
{
	return crypto_ed25519_check(signature.data(), key.data(), reinterpret_cast<const uint8_t*>(message.data()),
								message.size()) == 0;
}

std::vector<PublicKey> officialRootKeys()
{
	static const char* const hex[] = {
		// root key A
		"468de0d785b9dba3f513e7a254ff013344ce1949671e2fbb551f7034b037d4b8",
		// root key B
		"9be3b51ab954ed1d675c8a8de274a0cb9739ac52da84307a94ab19432738d386",
	};
	std::vector<PublicKey> keys;
	for (const char* text : hex)
	{
		PublicKey key;
		if (parsePublicKeyHex(text, key))
			keys.push_back(key);
	}
	return keys;
}

std::string describeUpdateFileError(UpdateFileError error)
{
	switch (error)
	{
		case UpdateFileError::NONE:
			return "no error";
		case UpdateFileError::BAD_SIGNATURE_TEXT:
			return "the signature file is damaged";
		case UpdateFileError::BAD_SIGNATURE:
			return "the signature is not valid";
		case UpdateFileError::BAD_FORMAT:
			return "the update information is damaged";
		case UpdateFileError::UNKNOWN_SCHEMA:
			return "the update information is for a newer launcher";
		case UpdateFileError::OLD_KEYS:
			return "the key list is older than one seen before";
	}
	return "unknown error";
}

UpdateFileError verifyKeysFile(const std::string& json, const std::string& signatureText,
							   const std::vector<PublicKey>& roots, uint64_t highestSeen, TrustedKeys& keys)
{
	Signature signature;
	if (!parseSignature(signatureText, signature))
		return UpdateFileError::BAD_SIGNATURE_TEXT;
	if (!signedByAny(json, signature, roots))
		return UpdateFileError::BAD_SIGNATURE;

	const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
	if (document.is_discarded())
		return UpdateFileError::BAD_FORMAT;
	UpdateFileError error = UpdateFileError::NONE;
	if (!readSchema(document, error))
		return error;

	auto version = document.find("version");
	auto signing = document.find("signing_keys");
	auto revoked = document.find("revoked");
	if (version == document.end() || !version->is_number_unsigned() || signing == document.end()
		|| revoked == document.end())
		return UpdateFileError::BAD_FORMAT;
	std::vector<PublicKey> signingKeys;
	std::vector<PublicKey> revokedKeys;
	if (!readKeyList(*signing, signingKeys) || !readKeyList(*revoked, revokedKeys))
		return UpdateFileError::BAD_FORMAT;
	const uint64_t number = version->get<uint64_t>();
	if (number == 0)
		return UpdateFileError::BAD_FORMAT;
	if (number < highestSeen)
		return UpdateFileError::OLD_KEYS;

	TrustedKeys read;
	read.version = number;
	for (const auto& key : signingKeys)
	{
		if (std::find(revokedKeys.begin(), revokedKeys.end(), key) == revokedKeys.end())
			read.active.push_back(key);
	}
	keys = read;
	return UpdateFileError::NONE;
}

UpdateFileError verifyManifest(const std::string& json, const std::string& signatureText, const TrustedKeys& keys,
							   UpdateManifest& manifest)
{
	Signature signature;
	if (!parseSignature(signatureText, signature))
		return UpdateFileError::BAD_SIGNATURE_TEXT;
	if (!signedByAny(json, signature, keys.active))
		return UpdateFileError::BAD_SIGNATURE;

	const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
	if (document.is_discarded())
		return UpdateFileError::BAD_FORMAT;
	UpdateFileError error = UpdateFileError::NONE;
	if (!readSchema(document, error))
		return error;

	auto version = document.find("version");
	auto tag = document.find("tag");
	auto platforms = document.find("platforms");
	if (version == document.end() || !version->is_string() || tag == document.end() || !tag->is_string()
		|| platforms == document.end() || !platforms->is_array())
		return UpdateFileError::BAD_FORMAT;

	UpdateManifest read;
	if (!parseAppVersion(version->get<std::string>(), read.version))
		return UpdateFileError::BAD_FORMAT;
	read.tag = tag->get<std::string>();
	for (const auto& item : *platforms)
	{
		if (!item.is_object())
			return UpdateFileError::BAD_FORMAT;
		auto name = item.find("name");
		auto url = item.find("url");
		auto size = item.find("size");
		auto sha256 = item.find("sha256");
		if (name == item.end() || !name->is_string() || url == item.end() || !url->is_string() || size == item.end()
			|| !size->is_number_unsigned() || sha256 == item.end() || !sha256->is_string())
			return UpdateFileError::BAD_FORMAT;
		PlatformPackage package;
		package.name = name->get<std::string>();
		package.url = url->get<std::string>();
		package.size = size->get<uint64_t>();
		package.sha256 = sha256->get<std::string>();
		read.platforms.push_back(package);
	}
	manifest = read;
	return UpdateFileError::NONE;
}

bool packageFormatOf(const std::string& platform, PackageFormat& format)
{
	if (platform == "windows-x64" || platform == "macos-arm64" || platform == "macos-x64")
	{
		format = PackageFormat::ZIP;
		return true;
	}
	if (platform == "linux-x64")
	{
		format = PackageFormat::TAR_GZ;
		return true;
	}
	return false;
}

bool isAllowedPackageUrl(const std::string& url, const std::string& tag, const std::string& platform)
{
	PackageFormat format;
	if (!packageFormatOf(platform, format))
		return false;
	const std::string prefix = std::string(RELEASE_DOWNLOAD_PREFIX) + tag + "/";
	if (tag.empty() || url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0)
		return false;
	// the tag itself must not hold anything that changes the path
	if (tag[0] == '.' || tag.find("..") != std::string::npos)
		return false;
	for (char c : tag)
	{
		if (!isFileNameCharacter(c))
			return false;
	}
	const std::string file = url.substr(prefix.size());
	if (file.size() > 200 || file[0] == '.')
		return false;
	for (char c : file)
	{
		if (!isFileNameCharacter(c))
			return false;
	}
	return endsWith(file, format == PackageFormat::ZIP ? ".zip" : ".tar.gz");
}

bool findPackage(const UpdateManifest& manifest, const std::string& platform, PlatformPackage& package)
{
	if (manifest.tag != "v" + manifest.version.toString())
		return false;
	for (const auto& item : manifest.platforms)
	{
		if (item.name != platform)
			continue;
		if (!isAllowedPackageUrl(item.url, manifest.tag, platform))
			return false;
		if (item.size == 0 || item.size > MAX_PACKAGE_BYTES || !isSha256Hex(item.sha256))
			return false;
		package = item;
		return true;
	}
	return false;
}

std::string currentPlatform()
{
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
	return "windows-x64";
#elif defined(__linux__) && defined(__x86_64__)
	return "linux-x64";
#elif defined(__APPLE__) && defined(__aarch64__)
	return "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
	return "macos-x64";
#else
	return std::string();
#endif
}
