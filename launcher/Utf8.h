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

#ifdef _WIN32
#include <string>
#include <windows.h>

/// UTF-8 to UTF-16 for the wide functions of Windows.
inline std::wstring widen(const std::string& text)
{
	if (text.empty())
		return std::wstring();
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
	std::wstring wide(length > 0 ? length : 0, L'\0');
	if (length > 0)
		MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &wide[0], length);
	return wide;
}

/// UTF-16 from Windows to UTF-8.
inline std::string narrow(const std::wstring& wide)
{
	if (wide.empty())
		return std::string();
	const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
	std::string text(length > 0 ? length : 0, '\0');
	if (length > 0)
		WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &text[0], length, nullptr, nullptr);
	return text;
}
#endif
