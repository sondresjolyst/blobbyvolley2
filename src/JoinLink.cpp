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

/* header include */
#include "JoinLink.h"

/* includes */
#include "StringUtil.h"
#include "TournamentProtocol.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/* implementation */
namespace
{
	const char* const SCHEME = "blobby://";
	const char* const ACTION = "join";

	bool startsWithNoCase(const std::string& text, std::size_t pos, const std::string& prefix)
	{
		if (text.size() < pos + prefix.size())
			return false;
		for (std::size_t i = 0; i < prefix.size(); ++i)
		{
			if (lowerAscii(text[pos + i]) != lowerAscii(prefix[i]))
				return false;
		}
		return true;
	}

	bool isDigit(char c)
	{
		return c >= '0' && c <= '9';
	}

	bool isLabelChar(char c)
	{
		return (c >= 'a' && c <= 'z') || isDigit(c) || c == '-';
	}

	/// A lower case host name: labels of 1 to 63 letters, digits and hyphens, separated by dots,
	/// that do not start or end with a hyphen. The same rule as the site uses.
	bool isHostName(const std::string& host)
	{
		if (host.empty() || host.size() > MAX_JOIN_LINK_HOST_BYTES)
			return false;
		std::size_t start = 0;
		while (true)
		{
			std::size_t end = host.find('.', start);
			if (end == std::string::npos)
				end = host.size();
			std::size_t length = end - start;
			if (length < 1 || length > 63)
				return false;
			if (host[start] == '-' || host[end - 1] == '-')
				return false;
			for (std::size_t i = start; i < end; ++i)
			{
				if (!isLabelChar(host[i]))
					return false;
			}
			if (end == host.size())
				return true;
			start = end + 1;
		}
	}

	/// A host name, or a dotted IPv4 address when it starts with a digit.
	/// RakNet passes a host that starts with 0, 1 or 2 to inet_addr without a lookup, and inet_addr
	/// also reads hex, octal and short forms such as 0x7f000001, 0177.0.0.1 and 127.1.
	/// So a host that starts with a digit must be four decimal parts, and no label may look like hex.
	bool isWellFormedHost(const std::string& host)
	{
		if (!isHostName(host))
			return false;

		std::size_t label = 0;
		while (label < host.size())
		{
			if (host.compare(label, 2, "0x") == 0)
				return false;
			std::size_t dot = host.find('.', label);
			if (dot == std::string::npos)
				break;
			label = dot + 1;
		}

		if (!isDigit(host[0]))
			return true;
		for (char c : host)
		{
			if (!isDigit(c) && c != '.')
				return false;
		}

		int parts = 0;
		std::size_t start = 0;
		while (start <= host.size())
		{
			std::size_t end = host.find('.', start);
			if (end == std::string::npos)
				end = host.size();
			std::string part = host.substr(start, end - start);
			if (part.size() > 3 || (part.size() > 1 && part[0] == '0'))
				return false;
			if (std::stoi(part) > 255)
				return false;
			++parts;
			start = end + 1;
		}
		return parts == 4;
	}
}

bool joinNeedsConfirmation(JoinLinkSource source)
{
	return source != JoinLinkSource::IN_GAME;
}

bool looksLikeJoinLink(const std::string& argument)
{
	return startsWithNoCase(argument, 0, "blobby:");
}

JoinLinkError parseJoinLink(const std::string& text, JoinLink& link)
{
	if (text.size() > MAX_JOIN_LINK_BYTES)
		return JoinLinkError::TOO_LONG;

	// printable ASCII only. This also refuses spaces, quotes are refused below.
	for (char c : text)
	{
		if (c < 0x21 || c > 0x7E)
			return JoinLinkError::BAD_CHARACTER;
	}

	const std::string scheme = SCHEME;
	const std::string action = ACTION;
	if (!startsWithNoCase(text, 0, scheme) || !startsWithNoCase(text, scheme.size(), action))
		return JoinLinkError::NOT_A_JOIN_LINK;

	std::size_t pos = scheme.size() + action.size();
	if (pos < text.size() && text[pos] == '/')
		++pos;
	if (pos >= text.size() || text[pos] != '?')
		return JoinLinkError::NOT_A_JOIN_LINK;
	++pos;

	std::string query = text.substr(pos);
	if (!query.empty() && query.back() == '/')
		query.pop_back();

	std::string server;
	std::string code;
	bool has_server = false;
	bool has_code = false;
	int count = 0;
	std::size_t start = 0;
	while (start <= query.size())
	{
		std::size_t end = query.find('&', start);
		if (end == std::string::npos)
			end = query.size();
		std::string parameter = query.substr(start, end - start);
		++count;

		std::size_t eq = parameter.find('=');
		if (eq == std::string::npos)
			return JoinLinkError::BAD_PARAMETERS;
		std::string name = parameter.substr(0, eq);
		std::string value = parameter.substr(eq + 1);
		if (name == "server" && !has_server)
		{
			server = value;
			has_server = true;
		}
		else if (name == "code" && !has_code)
		{
			code = value;
			has_code = true;
		}
		else
		{
			return JoinLinkError::BAD_PARAMETERS;
		}
		start = end + 1;
	}
	if (count != 2 || !has_server || !has_code)
		return JoinLinkError::BAD_PARAMETERS;

	std::size_t colon = server.find(':');
	if (colon == std::string::npos)
		return JoinLinkError::BAD_PORT;

	std::string host = lowerAscii(server.substr(0, colon));
	if (!isWellFormedHost(host))
		return JoinLinkError::BAD_HOST;

	uint16_t port = 0;
	if (!parsePort(server.substr(colon + 1), port))
		return JoinLinkError::BAD_PORT;

	// normalizeJoinCode trims spaces, but the link cannot hold any
	std::string normalized = normalizeJoinCode(code);
	if (normalized.empty() || normalized.size() != code.size())
		return JoinLinkError::BAD_CODE;

	link.host = host;
	link.port = port;
	link.code = normalized;
	return JoinLinkError::NONE;
}

JoinLinkError joinLinkFromText(const std::string& text, JoinLink& link)
{
	if (text.size() > MAX_JOIN_LINK_TEXT_BYTES)
		return JoinLinkError::TOO_LONG;

	std::string trimmed = trim(text);
	if (!looksLikeJoinLink(trimmed))
		return JoinLinkError::NO_LINK;
	return parseJoinLink(trimmed, link);
}

JoinLinkError joinLinkFromField(const std::string& text, JoinLink& link)
{
	return parseJoinLink(trim(text), link);
}

std::string joinLinkPrefill(const std::string& clipboard)
{
	JoinLink link;
	if (joinLinkFromText(clipboard, link) != JoinLinkError::NONE)
		return "";
	return trim(clipboard);
}

std::string describeJoinLinkError(JoinLinkError error)
{
	switch (error)
	{
		case JoinLinkError::NONE:
			return "The join link is valid.";
		case JoinLinkError::TOO_LONG:
			return "The join link is too long.";
		case JoinLinkError::BAD_CHARACTER:
			return "The join link holds a character that is not allowed.";
		case JoinLinkError::NOT_A_JOIN_LINK:
			return "This is not a join link. A join link starts with blobby://join?";
		case JoinLinkError::BAD_PARAMETERS:
			return "The join link must have exactly one server and one code.";
		case JoinLinkError::BAD_HOST:
			return "The server name in the join link is not valid.";
		case JoinLinkError::BAD_PORT:
			return "The server port in the join link is not valid.";
		case JoinLinkError::BAD_CODE:
			return "The join code in the join link is not valid.";
		case JoinLinkError::NO_LINK:
			return "There is no join link in the clipboard.";
	}
	return "The join link is not valid.";
}

const char* const LINK_HANDLER_KEY = "Software\\Classes\\blobby";

std::vector<RegistryValue> linkHandlerRegistryValues(const std::string& exe_path)
{
	if (exe_path.empty())
		return {};
	for (char c : exe_path)
	{
		unsigned char byte = static_cast<unsigned char>(c);
		if (byte == '"' || byte < 0x20 || byte == 0x7F)
			return {};
	}

	const std::string quoted = "\"" + exe_path + "\"";
	return {
		{"", "", "URL:Blobby Volley 2 Tournament join link"},
		{"", "URL Protocol", ""},
		{"DefaultIcon", "", quoted + ",0"},
		{"shell\\open\\command", "", quoted + " \"%1\""},
	};
}

#ifdef _WIN32
namespace
{
	std::wstring widen(const std::string& text)
	{
		if (text.empty())
			return std::wstring();
		int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
		if (size <= 0)
			return std::wstring();
		std::wstring result(static_cast<std::size_t>(size), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), &result[0], size);
		return result;
	}

	std::string narrow(const std::wstring& text)
	{
		if (text.empty())
			return std::string();
		int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (size <= 0)
			return std::string();
		std::string result(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], size, nullptr, nullptr);
		return result;
	}

	bool runningExePath(std::string& path)
	{
		// started by the launcher, links must open the launcher, since the version folder changes with each update
		DWORD launcherLength = GetEnvironmentVariableW(L"BLOBBY_LAUNCHER", nullptr, 0);
		if (launcherLength > 1)
		{
			std::wstring launcher(launcherLength, L'\0');
			launcherLength = GetEnvironmentVariableW(L"BLOBBY_LAUNCHER", &launcher[0], launcherLength);
			launcher.resize(launcherLength);
			const DWORD attributes = GetFileAttributesW(launcher.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				path = narrow(launcher);
				return !path.empty();
			}
		}

		std::wstring buffer(32768, L'\0');
		DWORD length = GetModuleFileNameW(nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size())
			return false;
		buffer.resize(length);
		path = narrow(buffer);
		return !path.empty();
	}
}

bool registerLinkHandler(std::string& error)
{
	std::string exe_path;
	if (!runningExePath(exe_path))
	{
		error = "Could not find the path of the game.";
		return false;
	}

	std::vector<RegistryValue> values = linkHandlerRegistryValues(exe_path);
	if (values.empty())
	{
		error = "The path of the game cannot be used in a link handler: " + exe_path;
		return false;
	}

	const std::string root = LINK_HANDLER_KEY;
	for (const auto& value : values)
	{
		std::wstring key = widen(value.subkey.empty() ? root : root + "\\" + value.subkey);
		HKEY handle = nullptr;
		LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
		                              KEY_SET_VALUE, nullptr, &handle, nullptr);
		if (result != ERROR_SUCCESS)
		{
			error = "Could not create the registry key HKEY_CURRENT_USER\\" + narrow(key) + ".";
			return false;
		}

		std::wstring name = widen(value.name);
		std::wstring data = widen(value.data);
		DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
		result = RegSetValueExW(handle, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
		                        reinterpret_cast<const BYTE*>(data.c_str()), bytes);
		RegCloseKey(handle);
		if (result != ERROR_SUCCESS)
		{
			error = "Could not write the registry key HKEY_CURRENT_USER\\" + narrow(key) + ".";
			return false;
		}
	}
	return true;
}

bool unregisterLinkHandler(std::string& error)
{
	std::wstring key = widen(LINK_HANDLER_KEY);
	LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
	if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND)
	{
		result = RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
		if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND)
			return true;
	}
	error = "Could not remove the registry key HKEY_CURRENT_USER\\" + std::string(LINK_HANDLER_KEY) + ".";
	return false;
}
#else
bool registerLinkHandler(std::string& error)
{
	error = "Join links can only be registered on Windows.";
	return false;
}

bool unregisterLinkHandler(std::string& error)
{
	error = "Join links can only be registered on Windows.";
	return false;
}
#endif
