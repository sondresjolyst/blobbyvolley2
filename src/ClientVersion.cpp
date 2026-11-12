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

/* header include */
#include "ClientVersion.h"

/* includes */
#include <tuple>

#include "ForkVersion.h"
#include "NetworkMessage.h"
#include "raknet/BitStream.h"

/* implementation */
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

	void writeVersion(RakNet::BitStream& stream, const ForkVersion& version)
	{
		stream.Write(version.major);
		stream.Write(version.minor);
		stream.Write(version.patch);
	}

	bool readVersion(RakNet::BitStream& stream, ForkVersion& version)
	{
		ForkVersion read;
		if (!stream.Read(read.major) || !stream.Read(read.minor) || !stream.Read(read.patch))
			return false;
		version = read;
		return true;
	}
}

std::string ForkVersion::toString() const
{
	return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

bool operator<(const ForkVersion& a, const ForkVersion& b)
{
	return std::tie(a.major, a.minor, a.patch) < std::tie(b.major, b.minor, b.patch);
}

bool operator==(const ForkVersion& a, const ForkVersion& b)
{
	return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

ForkVersion currentForkVersion()
{
	ForkVersion version;
	version.major = BLOBBY_FORK_VERSION_MAJOR;
	version.minor = BLOBBY_FORK_VERSION_MINOR;
	version.patch = BLOBBY_FORK_VERSION_PATCH;
	return version;
}

bool parseForkVersion(const std::string& text, ForkVersion& version)
{
	const auto first = text.find('.');
	if (first == std::string::npos)
		return false;
	const auto second = text.find('.', first + 1);
	if (second == std::string::npos)
		return false;
	ForkVersion read;
	if (!parsePart(text.substr(0, first), read.major)
		|| !parsePart(text.substr(first + 1, second - first - 1), read.minor)
		|| !parsePart(text.substr(second + 1), read.patch))
		return false;
	version = read;
	return true;
}

bool isDownloadUrl(const std::string& url)
{
	if (url.size() > MAX_DOWNLOAD_URL_LENGTH)
		return false;
	std::string rest;
	if (url.compare(0, 8, "https://") == 0)
		rest = url.substr(8);
	else if (url.compare(0, 7, "http://") == 0)
		rest = url.substr(7);
	else
		return false;
	// a host has to follow the scheme
	if (rest.empty() || rest[0] == '/' || rest[0] == '?' || rest[0] == '#')
		return false;
	for (char c : url)
	{
		if (c <= ' ' || c > '~')
			return false;
	}
	return true;
}

void writeClientVersion(RakNet::BitStream& stream, const ForkVersion& version)
{
	stream.Write(CLIENT_VERSION_MARKER);
	writeVersion(stream, version);
}

bool readClientVersion(RakNet::BitStream& stream, ForkVersion& version)
{
	if (stream.GetNumberOfUnreadBits() < 128)
		return false;
	uint32_t marker = 0;
	if (!stream.Read(marker) || marker != CLIENT_VERSION_MARKER)
		return false;
	return readVersion(stream, version);
}

bool parseMinClientMode(const std::string& text, MinClientMode& mode)
{
	if (text == "warn")
		mode = MinClientMode::WARN;
	else if (text == "reject")
		mode = MinClientMode::REJECT;
	else
		return false;
	return true;
}

VersionNotice checkClientVersion(const VersionPolicy& policy, bool hasVersion, const ForkVersion& client)
{
	if (policy.mode == MinClientMode::OFF)
		return VersionNotice::NONE;
	if (hasVersion && !(client < policy.minimum))
		return VersionNotice::NONE;
	return policy.mode == MinClientMode::REJECT ? VersionNotice::UPDATE_REQUIRED : VersionNotice::UPDATE_AVAILABLE;
}

void makeServerVersionPacket(RakNet::BitStream& stream, const ServerVersionData& data)
{
	stream.Write((unsigned char)ID_SERVER_VERSION);
	writeVersion(stream, data.server);
	stream.Write((unsigned char)data.notice);
	writeVersion(stream, data.minimum);
	// a link that a client would not open is not sent
	const std::string url = isDownloadUrl(data.downloadUrl) ? data.downloadUrl : std::string();
	stream.Write((unsigned char)url.size());
	stream.Write(url.data(), url.size());
}

bool readServerVersionPacket(RakNet::BitStream& stream, ServerVersionData& data)
{
	unsigned char id = 0;
	if (!stream.Read(id) || id != ID_SERVER_VERSION)
		return false;
	ServerVersionData read;
	unsigned char notice = 0;
	if (!readVersion(stream, read.server) || !stream.Read(notice) || !readVersion(stream, read.minimum))
		return false;
	if (notice > (unsigned char)VersionNotice::UPDATE_REQUIRED)
		return false;
	read.notice = (VersionNotice)notice;
	unsigned char length = 0;
	if (!stream.Read(length))
		return false;
	std::string url(length, '\0');
	if (length > 0 && !stream.Read(&url[0], length))
		return false;
	if (isDownloadUrl(url))
		read.downloadUrl = url;
	// fields that a newer server adds after these are left for a newer client
	data = read;
	return true;
}
