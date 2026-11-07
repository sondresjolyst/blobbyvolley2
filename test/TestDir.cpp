#include "TestDir.h"

#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <process.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
	std::string systemTempDir()
	{
#ifdef _WIN32
		char buffer[MAX_PATH + 1];
		DWORD length = GetTempPathA(sizeof(buffer), buffer);
		std::string dir = (length > 0 && length < sizeof(buffer)) ? std::string(buffer, length) : std::string(".");
#else
		const char* env = std::getenv("TMPDIR");
		std::string dir = (env && *env) ? std::string(env) : std::string("/tmp");
#endif
		while (dir.size() > 1 && (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\'))
			dir.erase(dir.size() - 1);
		return dir;
	}

	int processId()
	{
#ifdef _WIN32
		return _getpid();
#else
		return static_cast<int>(getpid());
#endif
	}

	bool makeDir(const std::string& path)
	{
#ifdef _WIN32
		return _mkdir(path.c_str()) == 0;
#else
		return mkdir(path.c_str(), 0700) == 0;
#endif
	}

	// removes a folder and everything in it
	void removeTree(const std::string& path)
	{
#ifdef _WIN32
		WIN32_FIND_DATAA found;
		HANDLE handle = FindFirstFileA((path + "\\*").c_str(), &found);
		if (handle != INVALID_HANDLE_VALUE)
		{
			do
			{
				const std::string name = found.cFileName;
				if (name == "." || name == "..")
					continue;
				const std::string child = path + "\\" + name;
				if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					removeTree(child);
				else
				{
					SetFileAttributesA(child.c_str(), FILE_ATTRIBUTE_NORMAL);
					DeleteFileA(child.c_str());
				}
			} while (FindNextFileA(handle, &found));
			FindClose(handle);
		}
		RemoveDirectoryA(path.c_str());
#else
		if (DIR* dir = opendir(path.c_str()))
		{
			while (dirent* entry = readdir(dir))
			{
				const std::string name = entry->d_name;
				if (name == "." || name == "..")
					continue;
				const std::string child = path + "/" + name;
				struct stat info;
				if (lstat(child.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
					removeTree(child);
				else
					unlink(child.c_str());
			}
			closedir(dir);
		}
		rmdir(path.c_str());
#endif
	}

	std::string makeTestDir()
	{
		const std::string dir = systemTempDir() + "/bt" + std::to_string(processId());
		// a folder left by an earlier run with the same process id is cleared first
		removeTree(dir);
		if (!makeDir(dir))
			throw std::runtime_error("cannot make the test folder " + dir);
		return dir;
	}

	struct TestDirFixture
	{
		TestDirFixture()
		{
			testDir();
		}

		~TestDirFixture()
		{
			removeTree(testDir());
		}
	};
}

const std::string& testDir()
{
	static const std::string dir = makeTestDir();
	return dir;
}

std::string testPath(const std::string& name)
{
	return testDir() + "/" + name;
}

BOOST_TEST_GLOBAL_FIXTURE(TestDirFixture);
