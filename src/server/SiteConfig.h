/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)

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

#include <functional>
#include <string>
#include <vector>

/// Tournament settings as the server config file gives them.
struct SiteConfigFile
{
	bool tournamentMode = false;
	std::string siteUrl;
	std::string siteToken;
	std::string dataDir;
};

/// Tournament settings after the environment and the config file are combined.
struct SiteConfig
{
	bool tournamentMode = false;
	/// Origin of the tournament site, such as https://example.org. Empty when the server does not talk to a site.
	std::string siteUrl;
	/// Token for the game server API of the site. Never log it.
	std::string siteToken;
	/// Folder for the players file, the results, the outbox and the sponsors. Empty means the working directory.
	std::string dataDir;
	/// Extra certificates to trust, for a test site or a private CA. Empty uses only the system certificates.
	std::string caFile;
	/// Allows plain http for localhost, only for tests.
	bool allowHttpLocalhost = false;

	bool usesSite() const { return tournamentMode && !siteUrl.empty(); }
};

/// Reads an environment variable. Returns nullptr when it is not set.
typedef std::function<const char*(const char*)> EnvironmentFunction;

/// Checks the address of the tournament site and returns its origin without a trailing slash.
/// Only https is allowed. With allowHttpLocalhost, http is also allowed for localhost, 127.0.0.1 and [::1].
/// The address may not hold a user, a path other than /, a query or a fragment.
bool checkSiteUrl(const std::string& url, bool allowHttpLocalhost, std::string& origin, std::string& error);

/// True if a token can be sent in a header. It must be 16 to 512 printable characters without spaces.
bool isValidSiteToken(const std::string& token);

/*! Combines the environment and the config file. The environment comes first.
	\details The variables are BLOBBY_TOURNAMENT (1 or 0), BLOBBY_SITE_URL, BLOBBY_SITE_TOKEN,
	BLOBBY_DATA_DIR, BLOBBY_SITE_CA_FILE and BLOBBY_SITE_ALLOW_HTTP (1 allows http for localhost, for tests).
	Returns false and adds to errors when the settings cannot work. The errors never hold the token.
*/
bool loadSiteConfig(const SiteConfigFile& file, const EnvironmentFunction& env, SiteConfig& config,
					std::vector<std::string>& errors);

/// Reads a UDP port. Only the digits of a number from 1 to 65535 are allowed, without a sign, spaces or a leading zero.
bool parsePort(const std::string& text, int& port);

/*! Picks the UDP port the server listens on. BLOBBY_PORT comes before the port from the config file.
	\details Returns false and sets error when BLOBBY_PORT is set but is not a valid port.
	An empty or unset BLOBBY_PORT keeps filePort.
*/
bool loadServerPort(int filePort, const EnvironmentFunction& env, int& port, std::string& error);
