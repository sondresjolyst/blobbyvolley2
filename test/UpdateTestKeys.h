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

#include "monocypher-ed25519.h"
#include "UpdateManifest.h"

// Throwaway Ed25519 keys for the update tests. They are made from a fixed seed in each run and
// are never trusted by a real launcher.
struct TestKey
{
	std::array<uint8_t, 64> secret;
	PublicKey publicKey;

	explicit TestKey(uint8_t seedByte)
	{
		uint8_t seed[32];
		for (int i = 0; i < 32; ++i)
			seed[i] = (uint8_t)(seedByte + i * 7);
		crypto_ed25519_key_pair(secret.data(), publicKey.data(), seed);
	}

	std::string hex() const
	{
		static const char digits[] = "0123456789abcdef";
		std::string text;
		for (uint8_t byte : publicKey)
		{
			text += digits[byte >> 4];
			text += digits[byte & 15];
		}
		return text;
	}

	/// The text of a .sig file for message: base64 of the raw signature.
	std::string sign(const std::string& message) const
	{
		uint8_t signature[64];
		crypto_ed25519_sign(signature, secret.data(), reinterpret_cast<const uint8_t*>(message.data()), message.size());
		return base64(signature, sizeof(signature));
	}

	static std::string base64(const uint8_t* data, std::size_t size)
	{
		static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string text;
		for (std::size_t i = 0; i < size; i += 3)
		{
			uint32_t block = (uint32_t)data[i] << 16;
			if (i + 1 < size)
				block |= (uint32_t)data[i + 1] << 8;
			if (i + 2 < size)
				block |= data[i + 2];
			text += table[(block >> 18) & 63];
			text += table[(block >> 12) & 63];
			text += i + 1 < size ? table[(block >> 6) & 63] : '=';
			text += i + 2 < size ? table[block & 63] : '=';
		}
		return text;
	}
};

/// keys.json with the given version, signing keys and revoked keys.
inline std::string makeKeysJson(uint64_t version, const std::vector<const TestKey*>& signing,
								const std::vector<const TestKey*>& revoked, int schema = 1)
{
	std::string text = "{\n  \"schema\": " + std::to_string(schema) + ",\n  \"version\": " + std::to_string(version)
		+ ",\n  \"signing_keys\": [";
	for (std::size_t i = 0; i < signing.size(); ++i)
		text += (i ? ", \"" : "\"") + signing[i]->hex() + "\"";
	text += "],\n  \"revoked\": [";
	for (std::size_t i = 0; i < revoked.size(); ++i)
		text += (i ? ", \"" : "\"") + revoked[i]->hex() + "\"";
	text += "]\n}\n";
	return text;
}

/// latest.json for one release with one package
inline std::string makeManifestJson(const std::string& version, const std::string& platform, const std::string& url,
									uint64_t size, const std::string& sha256, int schema = 1)
{
	return "{\"schema\":" + std::to_string(schema) + ",\"version\":\"" + version + "\",\"tag\":\"v" + version
		+ "\",\"released\":\"2026-11-10T17:10:32Z\",\"platforms\":[{\"name\":\"" + platform + "\",\"url\":\"" + url
		+ "\",\"size\":" + std::to_string(size) + ",\"sha256\":\"" + sha256 + "\"}]}\n";
}
