/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)
Changed by Sjølyst Innovation AS, 2026

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
#include "FontIndex.h"

/* includes */
#include <string>
#ifdef DEBUG
#include <iostream>
#endif

/* implementation */
#define INVALID_FONT_INDEX -1

int getNextFontIndex(std::string::const_iterator& iter, std::string::const_iterator end)
{
	int index = INVALID_FONT_INDEX;
	char testChar = *iter;
	++iter;
	if (testChar >= '0' && testChar <= '9')
		index = testChar - '0';
	else if (testChar >= 'a' && testChar <= 'z')
		index = testChar - 'a' + 10;
	else if (testChar >= 'A' && testChar <= 'Z')
		index = testChar - 'A' + 10;
	else if (testChar == '.')
		index = 36;
	else if (testChar == '!')
		index = 37;
	else if (testChar == '(')
		index = 38;
	else if (testChar == ')')
		index = 39;
	else if (testChar == '\'')
		index = 44;
	else if (testChar == ':')
		index = 45;
	else if (testChar == ';')
		index = 46;
	else if (testChar == '?')
		index = FONT_INDEX_UNKNOWN;
	else if (testChar == ',')
		index = 48;
	else if (testChar == '/')
		index = 49;
	else if (testChar == '_')
		index = 50;
	else if (testChar == ' ')
		index = 51;
	else if (testChar == '-')
		index = 52;
	else if (testChar == '%')
		index = 53;
	else if (testChar == '+')
		index = 54;
	else if (testChar == std::string("¿")[0]) // UTF-8 escape (c2xx)
	{
		if (iter == end)
			return FONT_INDEX_UNKNOWN;
		testChar = *iter;
		++iter;
		if (testChar == std::string("¿")[1])
			index = 56;
	}
	else if (testChar == std::string("👁")[0]) // UTF-8 escape (f0xxxxxx)
	{
		if (iter == end)
			return FONT_INDEX_UNKNOWN;
		testChar = *iter;
		++iter;
		if (testChar == std::string("👁")[1]) // UTF-8 escape (xx9fxxxx)
		{
			if (iter == end)
				return FONT_INDEX_UNKNOWN;
			testChar = *iter;
			++iter;
			if (testChar == std::string("👁")[2]) // UTF-8 escape (xxxx91xx)
			{
				if (iter == end)
					return FONT_INDEX_UNKNOWN;
				testChar = *iter;
				++iter;
				if (testChar == std::string("👁")[3]) // UTF-8 escape (xxxxxx81)
				{
					index = 57;
				}
			}
			else if (testChar == std::string("🔒")[2]) // UTF-8 escape (xxxx94xx)
			{
				if (iter == end)
					return FONT_INDEX_UNKNOWN;
				testChar = *iter;
				++iter;
				if (testChar == std::string("🔒")[3]) // UTF-8 escape (xxxxxx92)
				{
					index = 58;
				}
			}
		}
	}
	else if (testChar == std::string("ß")[0]) // UTF-8 escape (c3xx)
	{
		if (iter == end)
			return FONT_INDEX_UNKNOWN;
		testChar = *iter;
		++iter;
		if (testChar == std::string("ß")[1])
			index = 40;
		else if (testChar == std::string("ä")[1])
			index = 41;
		else if (testChar == std::string("ö")[1])
			index = 42;
		else if (testChar == std::string("ü")[1])
			index = 43;
		else if (testChar == std::string("ì")[1])
			index = 55;
		else if (testChar == std::string("Ä")[1])
			index = 41;
		else if (testChar == std::string("Ö")[1])
			index = 42;
		else if (testChar == std::string("Ü")[1])
			index = 43;
		else if (testChar == std::string("Ì")[1])
			index = 55;
		else if ((testChar >= std::string("à")[1]) && (testChar <= std::string("å")[1]))
			index = 10; // Map to A
		else if ((testChar >= std::string("À")[1]) && (testChar <= std::string("Å")[1]))
			index = 10; // Map to A
		else if ((testChar >= std::string("è")[1]) && (testChar <= std::string("ë")[1]))
			index = 14; // Map to E
		else if ((testChar >= std::string("ò")[1]) && (testChar <= std::string("õ")[1]))
			index = 24; // Map to O
		else if ((testChar >= std::string("Ò")[1]) && (testChar <= std::string("Õ")[1]))
			index = 24; // Map to O
		else if (testChar == std::string("ý")[1])
			index = 34; // Map to Y
		else if ((testChar >= std::string("ì")[1]) && (testChar <= std::string("ï")[1]))
			index = 18; // Map to I
		else if (testChar == std::string("ñ")[1])
			index = 23; // Map to N
		else if (testChar == std::string("Ñ")[1])
			index = 23; // Map to N
		else if (testChar == std::string("ç")[1])
			index = 12; // Map to C
		else if (testChar == std::string("Ç")[1])
			index = 12; // Map to C
	}
	else if (testChar == std::string("Ć")[0]) // UTF-8 escape (c4xx)
	{
		if (iter == end)
			return FONT_INDEX_UNKNOWN;
		testChar = *iter;
		++iter;
		if ((testChar >= std::string("Ć")[1]) && (testChar <= std::string("č")[1]))
			index = 12; // Map to C
		else if ((testChar >= std::string("Ē")[1]) && (testChar <= std::string("ě")[1]))
			index = 14; // Map to E
	}
	else if (testChar == std::string("Ś")[0]) // UTF-8 escape (c5xx)
	{
		if (iter == end)
			return FONT_INDEX_UNKNOWN;
		testChar = *iter;
		++iter;
		if ((testChar >= std::string("Ŕ")[1]) && (testChar <= std::string("ř")[1]))
			index = 27; // Map to R
		else if ((testChar >= std::string("Ś")[1]) && (testChar <= std::string("š")[1]))
			index = 28; // Map to S
		else if ((testChar >= std::string("Ũ")[1]) && (testChar <= std::string("ų")[1]))
			index = 30; // Map to U
		else if ((testChar >= std::string("Ŷ")[1]) && (testChar <= std::string("Ÿ")[1]))
			index = 34; // Map to Y
		else if ((testChar >= std::string("Ź")[1]) && (testChar <= std::string("ž")[1]))
			index = 35; // Map to Z
	}

	if (index == INVALID_FONT_INDEX)
	{
#ifdef DEBUG
		std::cerr << "Warning: Missing font for character code " << testChar << std::endl;
#endif
		index = FONT_INDEX_UNKNOWN;
	}

	return index;
}
