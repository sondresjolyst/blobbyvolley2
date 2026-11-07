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

#include <cerrno>
#include <cstdio>
#include <fstream>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
	std::string lower(std::string text)
	{
		for (auto& c : text)
		{
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		return text;
	}

	bool isHostChar(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
	}

	bool isPort(const std::string& text)
	{
		if (text.empty() || text.size() > 5)
			return false;
		long value = 0;
		for (char c : text)
		{
			if (c < '0' || c > '9')
				return false;
			value = value * 10 + (c - '0');
		}
		return value >= 1 && value <= 65535;
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
	const std::string scheme = lower(url.substr(0, schemeEnd));
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
		const std::string h = lower(host);
		const bool local = h == "localhost" || h == "127.0.0.1" || h == "[::1]";
		if (!allowHttpLocalhost || !local)
		{
			error = "the site address must start with https://, plain http would send the token unprotected";
			return false;
		}
	}

	origin = scheme + "://" + lower(host) + (port.empty() ? std::string() : ":" + port);
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
		const std::string value = lower(mode);
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

std::string pathInDataDir(const std::string& dir, const std::string& path)
{
	if (dir.empty() || path.empty())
		return path;
	const bool absolute = path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':');
	if (absolute)
		return path;
	const char last = dir[dir.size() - 1];
	if (last == '/' || last == '\\')
		return dir + path;
	return dir + "/" + path;
}

bool makeFolder(const std::string& path)
{
	if (path.empty())
		return true;
#ifdef _WIN32
	if (_mkdir(path.c_str()) == 0 || errno == EEXIST)
		return true;
#else
	// server data is only for the server user
	if (mkdir(path.c_str(), 0700) == 0 || errno == EEXIST)
		return true;
#endif
	return false;
}

bool writeFileAtomically(const std::string& path, const std::string& content, std::string& error)
{
	const std::string temp = path + ".tmp";
#ifdef _WIN32
	FILE* file = std::fopen(temp.c_str(), "wb");
#else
	// a temporary file left by a crash is replaced by a new one that only the server user can read
	std::remove(temp.c_str());
	const int descriptor = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
	FILE* file = descriptor >= 0 ? fdopen(descriptor, "wb") : nullptr;
	if (descriptor >= 0 && !file)
		close(descriptor);
#endif
	if (!file)
	{
		error = "cannot write " + temp;
		return false;
	}
	bool ok = std::fwrite(content.data(), 1, content.size(), file) == content.size();
	ok = std::fflush(file) == 0 && ok;
	// the data must be on disk before the rename makes it visible
#ifdef _WIN32
	ok = _commit(_fileno(file)) == 0 && ok;
#else
	ok = fsync(fileno(file)) == 0 && ok;
#endif
	ok = std::fclose(file) == 0 && ok;
	if (!ok)
	{
		std::remove(temp.c_str());
		error = "cannot write " + temp;
		return false;
	}

#ifdef _WIN32
	// a reader may hold the old file open for a moment, so try a few times
	for (int attempt = 0; attempt < 5; ++attempt)
	{
		if (MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return true;
		Sleep(20);
	}
#else
	if (std::rename(temp.c_str(), path.c_str()) == 0)
		return true;
#endif
	std::remove(temp.c_str());
	error = "cannot rename " + temp + " to " + path;
	return false;
}

bool removeFolder(const std::string& path)
{
#ifdef _WIN32
	return _rmdir(path.c_str()) == 0;
#else
	return rmdir(path.c_str()) == 0;
#endif
}

bool renameFile(const std::string& from, const std::string& to)
{
#ifdef _WIN32
	return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool listFolder(const std::string& path, std::vector<std::string>& names)
{
	names.clear();
#ifdef _WIN32
	WIN32_FIND_DATAA found;
	HANDLE handle = FindFirstFileA((path + "/*").c_str(), &found);
	if (handle == INVALID_HANDLE_VALUE)
		return false;
	do
	{
		const std::string name = found.cFileName;
		if (name != "." && name != "..")
			names.push_back(name);
	} while (FindNextFileA(handle, &found));
	FindClose(handle);
	return true;
#else
	DIR* dir = opendir(path.c_str());
	if (!dir)
		return false;
	while (dirent* entry = readdir(dir))
	{
		const std::string name = entry->d_name;
		if (name != "." && name != "..")
			names.push_back(name);
	}
	closedir(dir);
	return true;
#endif
}

bool readWholeFile(const std::string& path, std::size_t maxBytes, std::string& content)
{
	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file)
		return false;
	std::string data;
	char buffer[4096];
	while (file)
	{
		file.read(buffer, sizeof(buffer));
		data.append(buffer, static_cast<std::size_t>(file.gcount()));
		if (data.size() > maxBytes)
			return false;
	}
	if (file.bad())
		return false;
	content.swap(data);
	return true;
}
