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

#include "SiteConfig.h"

#include "StringUtil.h"

namespace
{
	bool isHostChar(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
	}

	bool isPort(const std::string& text)
	{
		uint16_t port = 0;
		return parsePort(text, port, true);
	}

	// a value from the environment, or nullptr when it is not set or empty
	const char* fromEnv(const EnvironmentFunction& env, const char* name)
	{
		if (!env)
			return nullptr;
		const char* value = env(name);
		if (!value || !*value)
			return nullptr;
		return value;
	}
}

bool checkSiteUrl(const std::string& url, bool allowHttpLocalhost, std::string& origin, std::string& error)
{
	for (char c : url)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if (u <= 0x20 || u >= 0x7F)
		{
			error = "the site address may not hold spaces or control characters";
			return false;
		}
	}

	const std::size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos)
	{
		error = "the site address must start with https://";
		return false;
	}
	const std::string scheme = lowerAscii(url.substr(0, schemeEnd));
	if (scheme != "https" && scheme != "http")
	{
		error = "the site address must start with https://";
		return false;
	}

	const std::string rest = url.substr(schemeEnd + 3);
	const std::size_t authorityEnd = rest.find_first_of("/?#");
	const std::string authority = rest.substr(0, authorityEnd);
	const std::string tail = authorityEnd == std::string::npos ? std::string() : rest.substr(authorityEnd);
	if (!tail.empty() && tail != "/")
	{
		error = "the site address may not have a path, a query or a fragment";
		return false;
	}
	if (authority.find('@') != std::string::npos)
	{
		error = "the site address may not hold a user name";
		return false;
	}

	std::string host;
	std::string port;
	if (!authority.empty() && authority[0] == '[')
	{
		const std::size_t close = authority.find(']');
		if (close == std::string::npos || close == 1)
		{
			error = "the site address has an invalid host";
			return false;
		}
		host = authority.substr(0, close + 1);
		for (std::size_t i = 1; i < close; ++i)
		{
			char c = authority[i];
			bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.';
			if (!ok)
			{
				error = "the site address has an invalid host";
				return false;
			}
		}
		const std::string after = authority.substr(close + 1);
		if (!after.empty())
		{
			if (after[0] != ':' || !isPort(after.substr(1)))
			{
				error = "the site address has an invalid port";
				return false;
			}
			port = after.substr(1);
		}
	}
	else
	{
		const std::size_t colon = authority.find(':');
		host = authority.substr(0, colon);
		if (colon != std::string::npos)
		{
			port = authority.substr(colon + 1);
			if (!isPort(port))
			{
				error = "the site address has an invalid port";
				return false;
			}
		}
		if (host.empty() || host.front() == '.' || host.front() == '-' || host.back() == '-')
		{
			error = "the site address has an invalid host";
			return false;
		}
		for (char c : host)
		{
			if (!isHostChar(c))
			{
				error = "the site address has an invalid host";
				return false;
			}
		}
	}

	if (scheme == "http")
	{
		const std::string h = lowerAscii(host);
		const bool local = h == "localhost" || h == "127.0.0.1" || h == "[::1]";
		if (!allowHttpLocalhost || !local)
		{
			error = "the site address must start with https://, plain http would send the token unprotected";
			return false;
		}
	}

	origin = scheme + "://" + lowerAscii(host) + (port.empty() ? std::string() : ":" + port);
	return true;
}

bool isValidSiteToken(const std::string& token)
{
	if (token.size() < 16 || token.size() > 512)
		return false;
	for (char c : token)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if (u <= 0x20 || u >= 0x7F)
			return false;
	}
	return true;
}

bool loadSiteConfig(const SiteConfigFile& file, const EnvironmentFunction& env, SiteConfig& config,
					std::vector<std::string>& errors)
{
	SiteConfig result;
	const std::size_t errorsBefore = errors.size();

	result.tournamentMode = file.tournamentMode;
	if (const char* mode = fromEnv(env, "BLOBBY_TOURNAMENT"))
	{
		const std::string value = lowerAscii(mode);
		if (value == "1" || value == "true")
			result.tournamentMode = true;
		else if (value == "0" || value == "false")
			result.tournamentMode = false;
		else
			errors.push_back("BLOBBY_TOURNAMENT must be 1 or 0");
	}

	const char* url = fromEnv(env, "BLOBBY_SITE_URL");
	const std::string rawUrl = url ? std::string(url) : file.siteUrl;
	const char* token = fromEnv(env, "BLOBBY_SITE_TOKEN");
	result.siteToken = token ? std::string(token) : file.siteToken;
	const char* dataDir = fromEnv(env, "BLOBBY_DATA_DIR");
	result.dataDir = dataDir ? std::string(dataDir) : file.dataDir;
	if (const char* ca = fromEnv(env, "BLOBBY_SITE_CA_FILE"))
		result.caFile = ca;
	if (const char* allow = fromEnv(env, "BLOBBY_SITE_ALLOW_HTTP"))
		result.allowHttpLocalhost = std::string(allow) == "1";

	if (result.tournamentMode && !rawUrl.empty())
	{
		std::string origin;
		std::string error;
		if (checkSiteUrl(rawUrl, result.allowHttpLocalhost, origin, error))
			result.siteUrl = origin;
		else
			errors.push_back("BLOBBY_SITE_URL: " + error);

		if (result.siteToken.empty())
			errors.push_back("BLOBBY_SITE_TOKEN is not set, make a token on the admin page of the site");
		else if (!isValidSiteToken(result.siteToken))
			errors.push_back("BLOBBY_SITE_TOKEN must be 16 to 512 printable characters without spaces");
	}

	config = result;
	return errors.size() == errorsBefore;
}

bool parsePort(const std::string& text, int& port)
{
	if (text.empty() || text.size() > 5 || text[0] == '0')
		return false;
	long value = 0;
	for (char c : text)
	{
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
	}
	if (value < 1 || value > 65535)
		return false;
	port = static_cast<int>(value);
	return true;
}

bool loadServerPort(int filePort, const EnvironmentFunction& env, int& port, std::string& error)
{
	const char* value = fromEnv(env, "BLOBBY_PORT");
	if (!value)
	{
		port = filePort;
		return true;
	}
	if (!parsePort(value, port))
	{
		error = "BLOBBY_PORT must be a number from 1 to 65535";
		return false;
	}
	return true;
}

bool loadMaxTraining(const EnvironmentFunction& env, unsigned defaultValue, unsigned maxValue, unsigned& value,
					std::string& error)
{
	const char* text = fromEnv(env, "BLOBBY_MAX_TRAINING");
	if (!text)
	{
		value = defaultValue;
		return true;
	}
	// only digits, and no leading zero except for 0 itself
	const std::string digits = text;
	bool ok = digits.size() <= 4 && (digits == "0" || digits[0] != '0');
	unsigned read = 0;
	for (char c : digits)
	{
		if (c < '0' || c > '9')
			ok = false;
		else
			read = read * 10 + (unsigned)(c - '0');
	}
	if (!ok || read > maxValue)
	{
		error = "BLOBBY_MAX_TRAINING must be a number from 0 to " + std::to_string(maxValue);
		return false;
	}
	value = read;
	return true;
}
