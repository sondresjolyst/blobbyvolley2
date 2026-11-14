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

#include <cstdint>
#include <string>

#include "UpdateManifest.h"

/// Limits against archives that unpack to far more than they look like.
struct ExtractLimits
{
	uint64_t maxEntries = 20000;
	uint64_t maxTotalBytes = 2048ull * 1024 * 1024;
};

/*! Unpacks a package that is held in memory into the folder destination, which must exist.
	\details Only plain files and folders are unpacked. A link, a device, an absolute path, a path
	with .. or a backslash fails the whole package. Files keep only their execute bit.
	Returns false and sets error when it fails. destination may then hold part of the files.
*/
bool extractPackage(const std::string& data, PackageFormat format, const std::string& destination, std::string& error,
					const ExtractLimits& limits = ExtractLimits());

/// Checks a path inside an archive: relative, with / between the parts, no empty, . or .. part,
/// and no backslash, colon or control character. A slash at the end is allowed.
/// On Windows it also rejects the parts that isUnsafeWindowsName finds.
bool isSafeArchivePath(const std::string& path);

/// Checks one part of a path for a name Windows does not allow: a device name such as CON, NUL,
/// COM1 or LPT1 in any case and with or without an extension, or a name that ends with a dot or a space.
bool isUnsafeWindowsName(const std::string& part);
