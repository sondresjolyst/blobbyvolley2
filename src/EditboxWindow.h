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

/// The part of the text an edit box shows. All values count bytes.
struct EditboxWindow
{
	std::size_t first;  ///< index of the first byte shown
	std::size_t count;  ///< number of bytes shown
	std::size_t cursor; ///< cursor, moved inside the text
};

/// Picks the part of text a box of length characters shows, so that the cursor stays inside the box.
/// Text that fits the box is shown whole. The window never starts or ends inside a UTF-8 sequence.
EditboxWindow editboxWindow(const std::string& text, std::size_t cursor, std::size_t length);

/// Tells whether a box of length characters takes one more byte.
/// A maxLength of 0 means the text may be as long as the box.
bool editboxTakesInput(std::size_t textLength, std::size_t length, std::size_t maxLength);
