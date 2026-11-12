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

#include "FsUtil.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <climits>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include "Utf8.h"

namespace fsu
{

std::string join(const std::string& a, const std::string& b)
{
	if (a.empty())
		return b;
	const char last = a[a.size() - 1];
	if (last == '/' || last == '\\')
		return a + b;
	return a + "/" + b;
}

std::string parent(const std::string& path)
{
	const std::size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos)
		return std::string(".");
	if (slash == 0)
		return path.substr(0, 1);
	return path.substr(0, slash);
}

#ifdef _WIN32

namespace
{
	DWORD attributes(const std::string& path)
	{
		return GetFileAttributesW(widen(path).c_str());
	}
}

bool isFile(const std::string& path)
{
	const DWORD found = attributes(path);
	return found != INVALID_FILE_ATTRIBUTES && !(found & FILE_ATTRIBUTE_DIRECTORY);
}

bool isDirectory(const std::string& path)
{
	const DWORD found = attributes(path);
	return found != INVALID_FILE_ATTRIBUTES && (found & FILE_ATTRIBUTE_DIRECTORY);
}

bool makeDirectory(const std::string& path)
{
	return CreateDirectoryW(widen(path).c_str(), nullptr) || isDirectory(path);
}

bool list(const std::string& path, std::vector<std::string>& names)
{
	names.clear();
	WIN32_FIND_DATAW found;
	HANDLE handle = FindFirstFileW(widen(join(path, "*")).c_str(), &found);
	if (handle == INVALID_HANDLE_VALUE)
		return false;
	do
	{
		const std::string name = narrow(found.cFileName);
		if (name != "." && name != "..")
			names.push_back(name);
	} while (FindNextFileW(handle, &found));
	FindClose(handle);
	return true;
}

bool rename(const std::string& from, const std::string& to)
{
	return MoveFileExW(widen(from).c_str(), widen(to).c_str(), MOVEFILE_WRITE_THROUGH) != 0;
}

bool removeFile(const std::string& path)
{
	const std::wstring wide = widen(path);
	SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_NORMAL);
	return DeleteFileW(wide.c_str()) != 0 || !isFile(path);
}

bool removeTree(const std::string& path)
{
	const DWORD found = attributes(path);
	if (found == INVALID_FILE_ATTRIBUTES)
		return true;
	// a link or junction is removed itself, never what it points to
	if ((found & FILE_ATTRIBUTE_DIRECTORY) && !(found & FILE_ATTRIBUTE_REPARSE_POINT))
	{
		std::vector<std::string> names;
		if (list(path, names))
		{
			for (const auto& name : names)
				removeTree(join(path, name));
		}
		SetFileAttributesW(widen(path).c_str(), FILE_ATTRIBUTE_NORMAL);
		return RemoveDirectoryW(widen(path).c_str()) != 0;
	}
	if (found & FILE_ATTRIBUTE_DIRECTORY)
		return RemoveDirectoryW(widen(path).c_str()) != 0;
	return removeFile(path);
}

bool readFile(const std::string& path, std::size_t maxBytes, std::string& content)
{
	FILE* file = _wfopen(widen(path).c_str(), L"rb");
	if (!file)
		return false;
	std::string data;
	char buffer[16384];
	bool ok = true;
	for (;;)
	{
		const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file);
		data.append(buffer, read);
		if (data.size() > maxBytes)
		{
			ok = false;
			break;
		}
		if (read < sizeof(buffer))
		{
			ok = !std::ferror(file);
			break;
		}
	}
	std::fclose(file);
	if (ok)
		content.swap(data);
	return ok;
}

bool writeFile(const std::string& path, const std::string& content)
{
	const std::string temp = path + ".tmp";
	FILE* file = _wfopen(widen(temp).c_str(), L"wb");
	if (!file)
		return false;
	bool ok = std::fwrite(content.data(), 1, content.size(), file) == content.size();
	ok = std::fclose(file) == 0 && ok;
	if (ok)
		ok = MoveFileExW(widen(temp).c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	if (!ok)
		removeFile(temp);
	return ok;
}

std::string executablePath()
{
	std::wstring buffer(MAX_PATH, L'\0');
	for (int attempt = 0; attempt < 6; ++attempt)
	{
		const DWORD length = GetModuleFileNameW(nullptr, &buffer[0], (DWORD)buffer.size());
		if (length == 0)
			return std::string();
		if (length < buffer.size())
		{
			buffer.resize(length);
			return narrow(buffer);
		}
		buffer.resize(buffer.size() * 2);
	}
	return std::string();
}

#else

bool isFile(const std::string& path)
{
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool isDirectory(const std::string& path)
{
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool makeDirectory(const std::string& path)
{
	return mkdir(path.c_str(), 0755) == 0 || isDirectory(path);
}

bool list(const std::string& path, std::vector<std::string>& names)
{
	names.clear();
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
}

bool rename(const std::string& from, const std::string& to)
{
	struct stat info;
	if (lstat(to.c_str(), &info) == 0)
		return false;
	return std::rename(from.c_str(), to.c_str()) == 0;
}

bool removeFile(const std::string& path)
{
	return unlink(path.c_str()) == 0 || errno == ENOENT;
}

bool removeTree(const std::string& path)
{
	struct stat info;
	if (lstat(path.c_str(), &info) != 0)
		return errno == ENOENT;
	// a link is removed itself, never what it points to
	if (S_ISDIR(info.st_mode))
	{
		std::vector<std::string> names;
		if (list(path, names))
		{
			for (const auto& name : names)
				removeTree(join(path, name));
		}
		return rmdir(path.c_str()) == 0;
	}
	return removeFile(path);
}

bool readFile(const std::string& path, std::size_t maxBytes, std::string& content)
{
	FILE* file = std::fopen(path.c_str(), "rb");
	if (!file)
		return false;
	std::string data;
	char buffer[16384];
	bool ok = true;
	for (;;)
	{
		const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file);
		data.append(buffer, read);
		if (data.size() > maxBytes)
		{
			ok = false;
			break;
		}
		if (read < sizeof(buffer))
		{
			ok = !std::ferror(file);
			break;
		}
	}
	std::fclose(file);
	if (ok)
		content.swap(data);
	return ok;
}

bool writeFile(const std::string& path, const std::string& content)
{
	const std::string temp = path + ".tmp";
	FILE* file = std::fopen(temp.c_str(), "wb");
	if (!file)
		return false;
	bool ok = std::fwrite(content.data(), 1, content.size(), file) == content.size();
	ok = std::fflush(file) == 0 && ok;
	ok = fsync(fileno(file)) == 0 && ok;
	ok = std::fclose(file) == 0 && ok;
	if (ok)
		ok = std::rename(temp.c_str(), path.c_str()) == 0;
	if (!ok)
		removeFile(temp);
	return ok;
}

std::string executablePath()
{
#ifdef __APPLE__
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string buffer(size, '\0');
	if (_NSGetExecutablePath(&buffer[0], &size) != 0)
		return std::string();
	buffer.resize(std::strlen(buffer.c_str()));
	char resolved[PATH_MAX];
	if (realpath(buffer.c_str(), resolved))
		return resolved;
	return buffer;
#else
	char buffer[PATH_MAX + 1];
	const ssize_t length = readlink("/proc/self/exe", buffer, PATH_MAX);
	if (length <= 0)
		return std::string();
	return std::string(buffer, (std::size_t)length);
#endif
}

#endif

bool isWritableDirectory(const std::string& path)
{
	if (!isDirectory(path))
		return false;
	const std::string probe = join(path, ".write-test");
	removeFile(probe);
	if (!writeFile(probe, "x"))
		return false;
	return removeFile(probe);
}

}
