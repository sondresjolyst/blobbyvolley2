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

#include <string>
#include <vector>

/*! Starts program in the folder workDir with the arguments and waits until it ends.
	\details Returns the exit code of the program, or -1 and an error when it cannot start.
	The program ends too when the launcher is stopped or killed. On Linux and macOS a SIGTERM,
	SIGINT or SIGHUP to the launcher is passed on to the program.
*/
int runAndWait(const std::string& program, const std::vector<std::string>& args, const std::string& workDir,
			   std::string& error);

/// Sets an environment variable for the programs the launcher starts. An empty value removes it.
void setEnvironment(const std::string& name, const std::string& value);

/// The command line that Windows programs read back as these arguments.
std::string windowsCommandLine(const std::string& program, const std::vector<std::string>& args);
