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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Longest join link the game reads, in bytes.
constexpr std::size_t MAX_JOIN_LINK_BYTES = 300;
/// Longest host name in a join link. The client keeps the host in a 64 byte buffer.
constexpr std::size_t MAX_JOIN_LINK_HOST_BYTES = 63;
/// Longest clipboard text the game looks at for a join link, in bytes.
constexpr std::size_t MAX_JOIN_LINK_TEXT_BYTES = 4096;

/// A join link from the tournament site, blobby://join?server=<host>:<port>&code=<code>.
struct JoinLink
{
	/// Lower case host name or IPv4 address.
	std::string host;
	uint16_t port = 0;
	/// Upper case join code.
	std::string code;
};

enum class JoinLinkError
{
	NONE,
	TOO_LONG,
	BAD_CHARACTER,
	NOT_A_JOIN_LINK,
	BAD_PARAMETERS,
	BAD_HOST,
	BAD_PORT,
	BAD_CODE,
	/// The text holds no join link at all.
	NO_LINK
};

/// Where a join link came from.
enum class JoinLinkSource
{
	/// The player typed or pasted the link into the join link field and picked join.
	IN_GAME,
	/// The command line or the blobby:// link handler, where the player did not pick join in the game.
	EXTERNAL
};

/// True if the game must ask the player before it joins the server of a link from this source.
bool joinNeedsConfirmation(JoinLinkSource source);

/// True if the argument looks like it is meant as a blobby:// link, so the game should treat it as one.
bool looksLikeJoinLink(const std::string& argument);

/// Reads a join link. Returns NONE and fills link on success. On an error link is left unchanged.
/// Only the exact form the site builds is accepted, with these allowances: the scheme and the word
/// join in any case, a slash after join or at the very end, the two parameters in either order,
/// and the host and code in any case.
JoinLinkError parseJoinLink(const std::string& text, JoinLink& link);

/// Reads a join link from pasted text, such as the clipboard. Spaces, tabs and line breaks around
/// the link are removed. Text that is empty or does not start with blobby: gives NO_LINK.
/// Otherwise the same checks as parseJoinLink apply.
JoinLinkError joinLinkFromText(const std::string& text, JoinLink& link);

/// Reads a join link the player typed or pasted into the join link field. Spaces, tabs and line
/// breaks around the link are removed, then the checks of parseJoinLink apply.
JoinLinkError joinLinkFromField(const std::string& text, JoinLink& link);

/// The text the join link field starts with: the trimmed clipboard text if it holds a valid
/// join link, otherwise an empty string.
std::string joinLinkPrefill(const std::string& clipboard);

/// A sentence that tells the player what is wrong with a link.
std::string describeJoinLinkError(JoinLinkError error);

/// One registry value for the blobby:// link handler.
struct RegistryValue
{
	/// Key below the root key, empty for the root key itself.
	std::string subkey;
	/// Value name, empty for the default value.
	std::string name;
	std::string data;
};

/// Key below HKEY_CURRENT_USER that holds the blobby:// link handler of the current user.
extern const char* const LINK_HANDLER_KEY;

/// The values that register exe_path as the handler of blobby:// links.
/// Returns an empty list when exe_path is empty or holds a quote or a control character,
/// since it could not be quoted safely in the command.
std::vector<RegistryValue> linkHandlerRegistryValues(const std::string& exe_path);

/// Registers the running program as handler of blobby:// links for the current user, below
/// HKEY_CURRENT_USER\Software\Classes. Needs no admin rights. Returns false and sets error on failure.
/// Only works on Windows.
bool registerLinkHandler(std::string& error);

/// Removes the blobby:// link handler of the current user. Returns true if it is gone afterwards.
bool unregisterLinkHandler(std::string& error);
