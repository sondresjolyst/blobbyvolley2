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
#include <string>

namespace RakNet
{
	class BitStream;
}

/// A fork version from version.txt, such as 0.2.0. It is not the network protocol version.
struct ForkVersion
{
	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;

	std::string toString() const;
};

bool operator<(const ForkVersion& a, const ForkVersion& b);
bool operator==(const ForkVersion& a, const ForkVersion& b);

/// The version this program was built as.
ForkVersion currentForkVersion();

/// Reads a version like 1.2.3. Each part has one to six digits and no leading zero.
bool parseForkVersion(const std::string& text, ForkVersion& version);

/// True for an http or https address of at most MAX_DOWNLOAD_URL_LENGTH printable ASCII characters
/// without spaces, with a host after the scheme.
bool isDownloadUrl(const std::string& url);
constexpr std::size_t MAX_DOWNLOAD_URL_LENGTH = 200;

/// True for a download link the game opens: a download link with https, no user, no port and no backslash,
/// whose lowercase host is julalan.no or a subdomain of it, or github.com with a path below
/// /sondresjolyst/blobbyvolley2-tournament/. Anyone can run a server, so other links are only shown as text.
bool isOfficialDownloadUrl(const std::string& url);

/// Marks the client version at the end of ID_ENTER_SERVER, after the features. The bytes spell BVER.
constexpr uint32_t CLIENT_VERSION_MARKER = 0x52455642;

/// Adds the client version to an ID_ENTER_SERVER packet, after the features.
void writeClientVersion(RakNet::BitStream& stream, const ForkVersion& version);
/// Reads the client version of an ID_ENTER_SERVER packet whose features were read. False when the client sent none.
bool readClientVersion(RakNet::BitStream& stream, ForkVersion& version);

/// What the server tells a client about its version
enum class VersionNotice : unsigned char
{
	NONE,
	UPDATE_AVAILABLE,
	UPDATE_REQUIRED
};

/// The version settings of a server
struct VersionPolicy
{
	/// min_client_version of the config, 0.0.0 when not set. It can only raise the derived minimum.
	ForkVersion minimum;
	/// page where players get the newest game, empty means no link
	std::string downloadUrl;
};

/*! The oldest game the server lets in.
	\details A feature release raises the minor version and a fix raises the patch, so a game needs at least the
	major and minor version of the server. The configured minimum can raise that limit but never lower it.
*/
ForkVersion minimumClientVersion(const VersionPolicy& policy, const ForkVersion& server);

/// The version a game that sends no version counts as. Every such game is 0.2.0 or older, and the older
/// games of the fork cannot be told apart from it.
ForkVersion versionOfGameWithoutVersion();

/*! The notice for a client.
	\details A client that sent no version counts as versionOfGameWithoutVersion(). A client older than minimum
	is turned away.
	A client that may play but is older than the server is told that a new version is out.
*/
VersionNotice checkClientVersion(const ForkVersion& minimum, const ForkVersion& server, bool hasVersion,
								 const ForkVersion& client);

/// The fields of an ID_SERVER_VERSION packet
struct ServerVersionData
{
	ForkVersion server;
	VersionNotice notice = VersionNotice::NONE;
	/// 0.0.0 when the server has no minimum
	ForkVersion minimum;
	/// empty when the server gave no link or the link is not an http or https address
	std::string downloadUrl;
};

void makeServerVersionPacket(RakNet::BitStream& stream, const ServerVersionData& data);
/// Reads a whole ID_SERVER_VERSION packet, from the packet type on. False when a field is missing or
/// the notice is unknown. A download link that is not an http or https address is dropped.
bool readServerVersionPacket(RakNet::BitStream& stream, ServerVersionData& data);
