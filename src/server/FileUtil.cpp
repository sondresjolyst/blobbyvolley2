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

#include "FileUtil.h"

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
	// renames from to to and replaces a file of that name, trying up to attempts times
	bool replaceFile(const std::string& from, const std::string& to, int attempts)
	{
#ifdef _WIN32
		for (int attempt = 0; attempt < attempts; ++attempt)
		{
			if (attempt > 0)
				Sleep(20);
			if (MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				return true;
		}
		return false;
#else
		(void)attempts;
		return std::rename(from.c_str(), to.c_str()) == 0;
#endif
	}
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

	// on Windows a reader may hold the old file open for a moment, so try a few times
	if (replaceFile(temp, path, 5))
		return true;
	std::remove(temp.c_str());
	error = "cannot rename " + temp + " to " + path;
	return false;
}

bool appendLine(const std::string& path, const std::string& line)
{
	std::ofstream file(path, std::ios::out | std::ios::app | std::ios::binary);
	if (file)
		file << line << "\n";
	return static_cast<bool>(file);
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
	return replaceFile(from, to, 1);
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

bool readWholeFile(const std::string& path, std::size_t maxBytes, std::string& content, ReadFileError* error)
{
	ReadFileError ignored;
	ReadFileError& why = error ? *error : ignored;
	why = ReadFileError::NONE;

	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file)
	{
		why = ReadFileError::CANNOT_OPEN;
		return false;
	}
	std::string data;
	char buffer[4096];
	while (file)
	{
		file.read(buffer, sizeof(buffer));
		data.append(buffer, static_cast<std::size_t>(file.gcount()));
		if (data.size() > maxBytes)
		{
			why = ReadFileError::TOO_LARGE;
			return false;
		}
	}
	if (file.bad())
	{
		why = ReadFileError::CANNOT_READ;
		return false;
	}
	content.swap(data);
	return true;
}
