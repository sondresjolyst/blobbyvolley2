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

#include "ChildProcess.h"

#ifdef _WIN32
#include <windows.h>
#include "Utf8.h"
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

std::string windowsCommandLine(const std::string& program, const std::vector<std::string>& args)
{
	// the rules of CommandLineToArgvW and the C runtime
	std::string line;
	std::vector<std::string> all;
	all.push_back(program);
	all.insert(all.end(), args.begin(), args.end());
	for (std::size_t n = 0; n < all.size(); ++n)
	{
		const std::string& arg = all[n];
		if (n > 0)
			line += ' ';
		if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos)
		{
			line += arg;
			continue;
		}
		line += '"';
		std::size_t backslashes = 0;
		for (char c : arg)
		{
			if (c == '\\')
			{
				++backslashes;
				continue;
			}
			if (c == '"')
				line.append(backslashes * 2 + 1, '\\');
			else
				line.append(backslashes, '\\');
			backslashes = 0;
			line += c;
		}
		line.append(backslashes * 2, '\\');
		line += '"';
	}
	return line;
}

#ifdef _WIN32

void setEnvironment(const std::string& name, const std::string& value)
{
	SetEnvironmentVariableW(widen(name).c_str(), value.empty() ? nullptr : widen(value).c_str());
}

int runAndWait(const std::string& program, const std::vector<std::string>& args, const std::string& workDir,
			   std::string& error)
{
	std::wstring commandLine = widen(windowsCommandLine(program, args));
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (!CreateProcessW(widen(program).c_str(), &commandLine[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
						workDir.empty() ? nullptr : widen(workDir).c_str(), &startup, &process))
	{
		error = "cannot start " + program + " (error " + std::to_string(GetLastError()) + ")";
		return -1;
	}

	// the game ends with the launcher, but programs that the game opens, such as the browser, stay
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (job)
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
		if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
			|| !AssignProcessToJobObject(job, process.hProcess))
		{
			CloseHandle(job);
			job = nullptr;
		}
	}
	ResumeThread(process.hThread);
	CloseHandle(process.hThread);

	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(process.hProcess, &code);
	CloseHandle(process.hProcess);
	if (job)
		CloseHandle(job);
	return (int)code;
}

#else

namespace
{
	volatile sig_atomic_t gChild = 0;

	void forwardSignal(int signal)
	{
		if (gChild > 0)
			kill((pid_t)gChild, signal);
	}
}

void setEnvironment(const std::string& name, const std::string& value)
{
	if (value.empty())
		unsetenv(name.c_str());
	else
		setenv(name.c_str(), value.c_str(), 1);
}

int runAndWait(const std::string& program, const std::vector<std::string>& args, const std::string& workDir,
			   std::string& error)
{
	std::vector<char*> argv;
	argv.push_back(const_cast<char*>(program.c_str()));
	for (const auto& arg : args)
		argv.push_back(const_cast<char*>(arg.c_str()));
	argv.push_back(nullptr);

	struct sigaction action;
	std::memset(&action, 0, sizeof(action));
	action.sa_handler = forwardSignal;
	sigemptyset(&action.sa_mask);
	for (int signal : {SIGTERM, SIGINT, SIGHUP})
		sigaction(signal, &action, nullptr);

	const pid_t parent = getpid();
	const pid_t child = fork();
	if (child < 0)
	{
		error = std::string("cannot start ") + program + ": " + std::strerror(errno);
		return -1;
	}
	if (child == 0)
	{
		struct sigaction plain;
		std::memset(&plain, 0, sizeof(plain));
		plain.sa_handler = SIG_DFL;
		for (int signal : {SIGTERM, SIGINT, SIGHUP})
			sigaction(signal, &plain, nullptr);
#ifdef __linux__
		// the game ends when the launcher is killed
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		if (getppid() != parent)
			_exit(1);
#else
		(void)parent;
#endif
		if (!workDir.empty() && chdir(workDir.c_str()) != 0)
			_exit(127);
		execv(program.c_str(), argv.data());
		_exit(127);
	}
	gChild = child;

	int status = 0;
	while (waitpid(child, &status, 0) < 0)
	{
		if (errno != EINTR)
		{
			gChild = 0;
			error = std::string("lost the game process: ") + std::strerror(errno);
			return -1;
		}
	}
	gChild = 0;
	if (WIFEXITED(status))
	{
		if (WEXITSTATUS(status) == 127)
			error = "cannot start " + program;
		return WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}

#endif
