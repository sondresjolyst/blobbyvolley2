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

#include <cstddef>
#include <string>
#include <vector>

/// Puts a relative path below dir. An absolute path or an empty dir leaves the path as it is.
std::string pathInDataDir(const std::string& dir, const std::string& path);

/// Makes a folder if it is not there. Returns false when it cannot be made.
bool makeFolder(const std::string& path);

/// Writes a whole file under a temporary name and renames it over the old file,
/// so a reader never sees half a file. Returns false and sets error when it fails.
bool writeFileAtomically(const std::string& path, const std::string& content, std::string& error);

/// Adds a line and a line break to the end of a file, and makes the file if it is not there.
/// Returns false when it cannot be written.
bool appendLine(const std::string& path, const std::string& line);

/// Removes an empty folder. Returns false when it fails.
bool removeFolder(const std::string& path);

/// Renames a file, replacing a file of the new name. Returns false when it fails.
bool renameFile(const std::string& from, const std::string& to);

/// Lists the file names in a folder, without . and .. Returns false when the folder cannot be read.
bool listFolder(const std::string& path, std::vector<std::string>& names);

/// Why readWholeFile failed.
enum class ReadFileError
{
	NONE,
	CANNOT_OPEN,
	TOO_LARGE,
	CANNOT_READ
};

/// Reads a whole file of at most maxBytes. Returns false when it cannot be read or is larger.
/// Then error, if given, tells why.
bool readWholeFile(const std::string& path, std::size_t maxBytes, std::string& content, ReadFileError* error = nullptr);
