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

#include <fstream>
#include <sstream>

#include "UpdateManifest.h"
#include "UpdateTestKeys.h"

namespace
{
	const std::string SHA = "57e706b53d7778ad7d12266973d74305fd88af8be4c36d7caabb7af2b4290d64";
	const std::string URL_030 = "https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/v0.3.0/"
		"blobbyvolley2-tournament-0.3.0-windows-x64.zip";

	AppVersion version(uint32_t major, uint32_t minor, uint32_t patch)
	{
		AppVersion v;
		v.major = major;
		v.minor = minor;
		v.patch = patch;
		return v;
	}

	std::string readSourceFile(const std::string& path)
	{
		std::ifstream file(std::string(BLOBBY_TEST_SOURCE_DIR) + "/" + path, std::ios::binary);
		std::ostringstream content;
		content << file.rdbuf();
		return content.str();
	}

	struct Keys
	{
		TestKey rootA{1};
		TestKey rootB{2};
		TestKey otherRoot{3};
		TestKey signing{4};
		TestKey second{5};
		TestKey stranger{6};

		std::vector<PublicKey> roots() const
		{
			return {rootA.publicKey, rootB.publicKey};
		}

		TrustedKeys trusted() const
		{
			TrustedKeys keys;
			keys.version = 1;
			keys.active.push_back(signing.publicKey);
			return keys;
		}
	};
}

BOOST_AUTO_TEST_SUITE( update_manifest )

BOOST_AUTO_TEST_CASE( versions_compare_part_by_part )
{
	BOOST_CHECK(version(0, 2, 0) < version(0, 3, 0));
	BOOST_CHECK(version(0, 9, 9) < version(0, 10, 0));
	BOOST_CHECK(version(0, 3, 0) < version(1, 0, 0));
	BOOST_CHECK(version(0, 3, 1) < version(0, 3, 2));
	BOOST_CHECK(!(version(0, 3, 0) < version(0, 3, 0)));
	BOOST_CHECK(!(version(0, 3, 1) < version(0, 3, 0)));
	BOOST_CHECK(version(0, 3, 0) == version(0, 3, 0));
}

BOOST_AUTO_TEST_CASE( versions_are_read_strictly )
{
	AppVersion read;
	BOOST_CHECK(parseAppVersion("0.3.0", read));
	BOOST_CHECK(read == version(0, 3, 0));
	BOOST_CHECK(parseAppVersion("12.345.678901", read));
	BOOST_CHECK_EQUAL(read.toString(), "12.345.678901");
	AppVersion untouched = version(9, 9, 9);
	for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.x", "v1.2.3", "1.2.3 ", "1..3", "1234567.0.0", "-1.2.3"})
	{
		BOOST_CHECK_MESSAGE(!parseAppVersion(bad, untouched), bad);
		BOOST_CHECK(untouched == version(9, 9, 9));
	}
}

BOOST_AUTO_TEST_CASE( signatures_match_rfc_8032 )
{
	// test 1 of RFC 8032, section 7.1, so the launcher uses SHA-512 Ed25519 and not the BLAKE2b variant
	PublicKey key;
	BOOST_REQUIRE(parsePublicKeyHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", key));
	const std::string hex = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
	std::vector<uint8_t> raw;
	for (std::size_t i = 0; i < hex.size(); i += 2)
		raw.push_back((uint8_t)std::stoi(hex.substr(i, 2), nullptr, 16));
	Signature signature;
	BOOST_REQUIRE(parseSignature(TestKey::base64(raw.data(), raw.size()), signature));
	BOOST_CHECK(checkEd25519("", signature, key));
	BOOST_CHECK(!checkEd25519("x", signature, key));
}

BOOST_AUTO_TEST_CASE( signature_text_is_strict_base64 )
{
	TestKey key(9);
	const std::string text = key.sign("hello");
	BOOST_CHECK_EQUAL(text.size(), 88u);
	Signature signature;
	BOOST_CHECK(parseSignature(text, signature));
	BOOST_CHECK(parseSignature(" \n" + text + "\r\n", signature));
	BOOST_CHECK(checkEd25519("hello", signature, key.publicKey));
	BOOST_CHECK(!parseSignature(text.substr(0, 87), signature));
	BOOST_CHECK(!parseSignature(text + "A", signature));
	BOOST_CHECK(!parseSignature(text.substr(0, 40) + "*" + text.substr(41), signature));
	BOOST_CHECK(!parseSignature(text.substr(0, 40) + " " + text.substr(41), signature));
	// the bits after the last byte must be zero
	std::string loose = text;
	loose[85] = loose[85] == 'A' ? 'B' : 'A';
	Signature other;
	if (parseSignature(loose, other))
		BOOST_CHECK(other != signature);
	BOOST_CHECK(!parseSignature(std::string(88, '='), signature));
}

BOOST_AUTO_TEST_CASE( public_keys_are_lowercase_hex )
{
	PublicKey key;
	BOOST_CHECK(parsePublicKeyHex(std::string(64, 'a'), key));
	BOOST_CHECK(!parsePublicKeyHex(std::string(64, 'A'), key));
	BOOST_CHECK(!parsePublicKeyHex(std::string(63, 'a'), key));
	BOOST_CHECK(!parsePublicKeyHex(std::string(66, 'a'), key));
	BOOST_CHECK(!parsePublicKeyHex(std::string(64, 'g'), key));
	BOOST_CHECK_EQUAL(officialRootKeys().size(), 2u);
}

BOOST_AUTO_TEST_CASE( good_keys_and_manifest_pass )
{
	Keys k;
	const std::string keysJson = makeKeysJson(3, {&k.signing, &k.second}, {});
	TrustedKeys trusted;
	BOOST_REQUIRE(verifyKeysFile(keysJson, k.rootB.sign(keysJson), k.roots(), 2, trusted) == UpdateFileError::NONE);
	BOOST_CHECK_EQUAL(trusted.version, 3u);
	BOOST_CHECK_EQUAL(trusted.active.size(), 2u);

	const std::string manifestJson = makeManifestJson("0.3.0", "windows-x64", URL_030, 1234, SHA);
	UpdateManifest manifest;
	BOOST_REQUIRE(verifyManifest(manifestJson, k.second.sign(manifestJson), trusted, manifest) == UpdateFileError::NONE);
	BOOST_CHECK(manifest.version == version(0, 3, 0));
	BOOST_CHECK_EQUAL(manifest.tag, "v0.3.0");
	PlatformPackage package;
	BOOST_REQUIRE(findPackage(manifest, "windows-x64", package));
	BOOST_CHECK_EQUAL(package.url, URL_030);
	BOOST_CHECK_EQUAL(package.size, 1234u);
	BOOST_CHECK_EQUAL(package.sha256, SHA);
	BOOST_CHECK(!findPackage(manifest, "linux-x64", package));
}

BOOST_AUTO_TEST_CASE( changed_bytes_break_the_signature )
{
	Keys k;
	const std::string keysJson = makeKeysJson(1, {&k.signing}, {});
	const std::string keysSig = k.rootA.sign(keysJson);
	std::string changed = keysJson;
	changed[changed.size() - 2] = ' ';
	TrustedKeys trusted;
	BOOST_CHECK(verifyKeysFile(changed, keysSig, k.roots(), 0, trusted) == UpdateFileError::BAD_SIGNATURE);
	BOOST_CHECK(verifyKeysFile(keysJson + "\n", keysSig, k.roots(), 0, trusted) == UpdateFileError::BAD_SIGNATURE);
	BOOST_CHECK(verifyKeysFile(keysJson, "not base64", k.roots(), 0, trusted) == UpdateFileError::BAD_SIGNATURE_TEXT);

	const std::string manifestJson = makeManifestJson("0.3.0", "windows-x64", URL_030, 1234, SHA);
	const std::string manifestSig = k.signing.sign(manifestJson);
	UpdateManifest manifest;
	const std::string otherSize = makeManifestJson("0.3.0", "windows-x64", URL_030, 1235, SHA);
	BOOST_CHECK(verifyManifest(otherSize, manifestSig, k.trusted(), manifest) == UpdateFileError::BAD_SIGNATURE);
	BOOST_CHECK(verifyManifest(manifestJson, manifestSig, k.trusted(), manifest) == UpdateFileError::NONE);
}

BOOST_AUTO_TEST_CASE( keys_signed_by_an_unknown_root_are_refused )
{
	Keys k;
	const std::string keysJson = makeKeysJson(1, {&k.signing}, {});
	TrustedKeys trusted;
	BOOST_CHECK(verifyKeysFile(keysJson, k.otherRoot.sign(keysJson), k.roots(), 0, trusted) == UpdateFileError::BAD_SIGNATURE);
	// a signing key cannot sign the key list either
	BOOST_CHECK(verifyKeysFile(keysJson, k.signing.sign(keysJson), k.roots(), 0, trusted) == UpdateFileError::BAD_SIGNATURE);
	// and the test roots are not the real ones
	BOOST_CHECK(verifyKeysFile(keysJson, k.rootA.sign(keysJson), officialRootKeys(), 0, trusted) == UpdateFileError::BAD_SIGNATURE);
}

BOOST_AUTO_TEST_CASE( a_manifest_needs_an_active_signing_key )
{
	Keys k;
	const std::string manifestJson = makeManifestJson("0.3.0", "windows-x64", URL_030, 1234, SHA);
	UpdateManifest manifest;

	// a key that keys.json does not list
	BOOST_CHECK(verifyManifest(manifestJson, k.stranger.sign(manifestJson), k.trusted(), manifest) == UpdateFileError::BAD_SIGNATURE);
	// a root key is not a signing key
	BOOST_CHECK(verifyManifest(manifestJson, k.rootA.sign(manifestJson), k.trusted(), manifest) == UpdateFileError::BAD_SIGNATURE);

	// a revoked key, even when it is still in signing_keys
	const std::string keysJson = makeKeysJson(2, {&k.signing, &k.second}, {&k.signing});
	TrustedKeys trusted;
	BOOST_REQUIRE(verifyKeysFile(keysJson, k.rootA.sign(keysJson), k.roots(), 1, trusted) == UpdateFileError::NONE);
	BOOST_CHECK_EQUAL(trusted.active.size(), 1u);
	BOOST_CHECK(verifyManifest(manifestJson, k.signing.sign(manifestJson), trusted, manifest) == UpdateFileError::BAD_SIGNATURE);
	BOOST_CHECK(verifyManifest(manifestJson, k.second.sign(manifestJson), trusted, manifest) == UpdateFileError::NONE);
}

BOOST_AUTO_TEST_CASE( an_older_key_list_is_refused )
{
	Keys k;
	const std::string oldKeys = makeKeysJson(1, {&k.signing}, {});
	TrustedKeys trusted;
	BOOST_CHECK(verifyKeysFile(oldKeys, k.rootA.sign(oldKeys), k.roots(), 2, trusted) == UpdateFileError::OLD_KEYS);
	BOOST_CHECK(verifyKeysFile(oldKeys, k.rootA.sign(oldKeys), k.roots(), 1, trusted) == UpdateFileError::NONE);
	BOOST_CHECK(verifyKeysFile(oldKeys, k.rootA.sign(oldKeys), k.roots(), 0, trusted) == UpdateFileError::NONE);
	const std::string zero = makeKeysJson(0, {&k.signing}, {});
	BOOST_CHECK(verifyKeysFile(zero, k.rootA.sign(zero), k.roots(), 0, trusted) == UpdateFileError::BAD_FORMAT);
}

BOOST_AUTO_TEST_CASE( unknown_schemas_and_bad_fields_are_refused )
{
	Keys k;
	TrustedKeys trusted;
	const std::string newKeys = makeKeysJson(1, {&k.signing}, {}, 2);
	BOOST_CHECK(verifyKeysFile(newKeys, k.rootA.sign(newKeys), k.roots(), 0, trusted) == UpdateFileError::UNKNOWN_SCHEMA);
	for (const std::string& bad : {std::string("[]"), std::string("{\"schema\":1}"), std::string("not json"),
		std::string("{\"schema\":1,\"version\":1,\"signing_keys\":[\"AB\"],\"revoked\":[]}"),
		std::string("{\"schema\":1,\"version\":-1,\"signing_keys\":[],\"revoked\":[]}"),
		std::string("{\"schema\":1,\"version\":\"1\",\"signing_keys\":[],\"revoked\":[]}"),
		std::string("{\"schema\":1,\"version\":1,\"signing_keys\":[]}")})
	{
		BOOST_CHECK_MESSAGE(verifyKeysFile(bad, k.rootA.sign(bad), k.roots(), 0, trusted) == UpdateFileError::BAD_FORMAT, bad);
	}

	UpdateManifest manifest;
	const std::string newManifest = makeManifestJson("0.3.0", "windows-x64", URL_030, 1234, SHA, 2);
	BOOST_CHECK(verifyManifest(newManifest, k.signing.sign(newManifest), k.trusted(), manifest) == UpdateFileError::UNKNOWN_SCHEMA);
	for (const std::string& bad : {makeManifestJson("0.3", "windows-x64", URL_030, 1234, SHA),
		std::string("{\"schema\":1,\"version\":\"0.3.0\",\"tag\":\"v0.3.0\",\"platforms\":[{\"name\":\"windows-x64\"}]}"),
		std::string("{\"schema\":1,\"version\":\"0.3.0\",\"tag\":\"v0.3.0\",\"platforms\":{}}")})
	{
		BOOST_CHECK_MESSAGE(verifyManifest(bad, k.signing.sign(bad), k.trusted(), manifest) == UpdateFileError::BAD_FORMAT, bad);
	}
}

BOOST_AUTO_TEST_CASE( package_links_are_pinned_to_the_release )
{
	BOOST_CHECK(isAllowedPackageUrl(URL_030, "v0.3.0", "windows-x64"));
	BOOST_CHECK(isAllowedPackageUrl("https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/v0.3.0/"
		"blobbyvolley2-tournament-0.3.0-linux-x64.tar.gz", "v0.3.0", "linux-x64"));
	const std::string base = "https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/";
	for (const std::string& bad : {
		std::string("http://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/v0.3.0/a.zip"),
		std::string("https://api.github.com/repos/sondresjolyst/blobbyvolley2-tournament/releases/assets/1"),
		std::string("https://github.com/someone/blobbyvolley2-tournament/releases/download/v0.3.0/a.zip"),
		std::string("https://github.com.evil.example/sondresjolyst/blobbyvolley2-tournament/releases/download/v0.3.0/a.zip"),
		std::string("https://user@github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/v0.3.0/a.zip"),
		std::string("https://github.com:443/sondresjolyst/blobbyvolley2-tournament/releases/download/v0.3.0/a.zip"),
		base + "v0.2.0/a.zip",
		base + "v0.3.0/../v0.2.0/a.zip",
		base + "v0.3.0/sub/a.zip",
		base + "v0.3.0/a.zip?x=1",
		base + "v0.3.0/a.zip#x",
		base + "v0.3.0/%2e%2e.zip",
		base + "v0.3.0/a\\b.zip",
		base + "v0.3.0/.zip",
		base + "v0.3.0/a.tar.gz",
		base + "v0.3.0/a.exe",
		base + "v0.3.0/",
		base + "v0.3.0/a b.zip",
		base + "v0.3.0/a.zip ",
	})
	{
		BOOST_CHECK_MESSAGE(!isAllowedPackageUrl(bad, "v0.3.0", "windows-x64"), bad);
	}
	BOOST_CHECK(!isAllowedPackageUrl(base + "../x/a.zip", "..", "windows-x64"));
	BOOST_CHECK(!isAllowedPackageUrl(URL_030, "v0.3.0", "solaris-sparc"));
}

BOOST_AUTO_TEST_CASE( packages_need_a_sane_size_hash_and_tag )
{
	UpdateManifest manifest;
	manifest.version = version(0, 3, 0);
	manifest.tag = "v0.3.0";
	PlatformPackage item;
	item.name = "windows-x64";
	item.url = URL_030;
	item.size = 1234;
	item.sha256 = SHA;
	manifest.platforms.push_back(item);
	PlatformPackage found;
	BOOST_CHECK(findPackage(manifest, "windows-x64", found));

	UpdateManifest bad = manifest;
	bad.platforms[0].size = 0;
	BOOST_CHECK(!findPackage(bad, "windows-x64", found));
	bad = manifest;
	bad.platforms[0].size = MAX_PACKAGE_BYTES + 1;
	BOOST_CHECK(!findPackage(bad, "windows-x64", found));
	bad = manifest;
	bad.platforms[0].sha256 = std::string(64, 'A');
	BOOST_CHECK(!findPackage(bad, "windows-x64", found));
	bad = manifest;
	bad.tag = "v0.3.1";
	BOOST_CHECK(!findPackage(bad, "windows-x64", found));
	bad = manifest;
	bad.version = version(0, 3, 1);
	BOOST_CHECK(!findPackage(bad, "windows-x64", found));
}

BOOST_AUTO_TEST_CASE( the_key_list_in_the_repository_is_signed_by_a_root_key )
{
	// the files that make-update-manifest.sh copies to each release, signed with OpenSSL
	const std::string json = readSourceFile(".github/update-keys/keys.json");
	const std::string signature = readSourceFile(".github/update-keys/keys.json.sig");
	BOOST_REQUIRE(!json.empty());
	TrustedKeys trusted;
	BOOST_CHECK(verifyKeysFile(json, signature, officialRootKeys(), 0, trusted) == UpdateFileError::NONE);
	BOOST_CHECK(!trusted.active.empty());
}

BOOST_AUTO_TEST_CASE( the_manifest_script_output_verifies )
{
	// made by test/update-manifest/regenerate.sh, which runs make-update-manifest.sh with TestKey(4)
	const std::string json = readSourceFile("test/update-manifest/latest.json");
	const std::string signature = readSourceFile("test/update-manifest/latest.json.sig");
	BOOST_REQUIRE(!json.empty());
	BOOST_CHECK_EQUAL(json.find('\r'), std::string::npos);
	Keys k;
	UpdateManifest manifest;
	BOOST_REQUIRE(verifyManifest(json, signature, k.trusted(), manifest) == UpdateFileError::NONE);
	BOOST_CHECK(manifest.version == version(0, 3, 0));
	BOOST_CHECK_EQUAL(manifest.tag, "v0.3.0");
	BOOST_REQUIRE_EQUAL(manifest.platforms.size(), 2u);
	BOOST_CHECK_EQUAL(manifest.platforms[0].name, "windows-x64");
	BOOST_CHECK_EQUAL(manifest.platforms[0].size, 16u);
	BOOST_CHECK_EQUAL(manifest.platforms[1].name, "linux-x64");
}

BOOST_AUTO_TEST_SUITE_END()
