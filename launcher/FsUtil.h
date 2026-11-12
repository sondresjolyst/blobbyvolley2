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

// File functions of the launcher. All paths are UTF-8, also on Windows, where the wide
// functions of the system are used, so a folder with a name like Bjørn works.
namespace fsu
{
	/// a and b joined with a slash
	std::string join(const std::string& a, const std::string& b);
	/// The folder part of a path, without the last slash.
	std::string parent(const std::string& path);

	bool isFile(const std::string& path);
	bool isDirectory(const std::string& path);

	/// Makes a folder. True when it is there afterwards.
	bool makeDirectory(const std::string& path);

	/// The names in a folder, without . and .. False when the folder cannot be read.
	bool list(const std::string& path, std::vector<std::string>& names);

	/// Renames a file or folder. It fails when the target exists.
	bool rename(const std::string& from, const std::string& to);

	bool removeFile(const std::string& path);

	/// Removes a folder and everything in it. True when it is gone afterwards.
	bool removeTree(const std::string& path);

	/// Reads a whole file of at most maxBytes.
	bool readFile(const std::string& path, std::size_t maxBytes, std::string& content);

	/// Writes a whole file under a temporary name and renames it into place.
	bool writeFile(const std::string& path, const std::string& content);

	/// True when the launcher can make and remove a file in the folder.
	bool isWritableDirectory(const std::string& path);

	/// The path of the running program, empty when it cannot be found.
	std::string executablePath();
}
