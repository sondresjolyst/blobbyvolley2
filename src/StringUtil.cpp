/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)

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

/* header include */
#include "StringUtil.h"

/* implementation */
char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowerAscii(std::string text)
{
	for (auto& c : text)
		c = lowerAscii(c);
	return text;
}

std::string trim(const std::string& text, const char* chars)
{
	std::size_t begin = text.find_first_not_of(chars);
	if (begin == std::string::npos)
		return std::string();
	std::size_t end = text.find_last_not_of(chars);
	return text.substr(begin, end - begin + 1);
}

bool parsePort(const std::string& text, uint16_t& port, bool allowLeadingZeros)
{
	if (text.empty() || text.size() > 5 || (!allowLeadingZeros && text[0] == '0'))
		return false;
	unsigned long value = 0;
	for (char c : text)
	{
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + static_cast<unsigned long>(c - '0');
	}
	if (value < 1 || value > 65535)
		return false;
	port = static_cast<uint16_t>(value);
	return true;
}
