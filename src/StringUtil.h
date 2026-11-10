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

/// Turns A to Z into a to z and leaves every other byte as it is.
char lowerAscii(char c);
std::string lowerAscii(std::string text);

/// Removes the given characters from both ends of text.
std::string trim(const std::string& text, const char* chars = " \t\r\n");

/// Reads a port of 1 to 65535 written in decimal digits only.
/// Leading zeros are refused unless allowLeadingZeros is set.
bool parsePort(const std::string& text, uint16_t& port, bool allowLeadingZeros = false);
