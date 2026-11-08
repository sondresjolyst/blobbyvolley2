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

#include "EditboxWindow.h"

namespace
{
	bool isContinuationByte(char c)
	{
		return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
	}
}

EditboxWindow editboxWindow(const std::string& text, std::size_t cursor, std::size_t length)
{
	if (cursor > text.size())
		cursor = text.size();

	std::size_t first = cursor > length ? cursor - length : 0;
	while (first < cursor && isContinuationByte(text[first]))
		++first;

	std::size_t end = first + length < text.size() ? first + length : text.size();
	while (end > first && end < text.size() && isContinuationByte(text[end]))
		--end;

	return EditboxWindow{first, end - first, cursor};
}

bool editboxTakesInput(std::size_t textLength, std::size_t length, std::size_t maxLength)
{
	return textLength < (maxLength == 0 ? length : maxLength);
}
