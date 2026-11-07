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

#include "CurlHttpClient.h"

#include <utility>

#include <curl/curl.h>

namespace
{
	const long CONNECT_TIMEOUT_MS = 5000;

	struct Download
	{
		std::string body;
		std::size_t maxBytes;
		bool tooLarge;
	};

	size_t collect(char* data, size_t size, size_t count, void* user)
	{
		Download* download = static_cast<Download*>(user);
		const size_t bytes = size * count;
		if (download->body.size() + bytes > download->maxBytes)
		{
			download->tooLarge = true;
			// a short count makes libcurl stop the transfer
			return 0;
		}
		download->body.append(data, bytes);
		return bytes;
	}
}

bool CurlHttpClient::globalInit()
{
	static const bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
	return ok;
}

CurlHttpClient::CurlHttpClient(std::string origin, std::string token, std::string caFile, bool allowHttp)
: mOrigin(std::move(origin))
, mAuthorization("Authorization: Bearer " + token)
, mCaFile(std::move(caFile))
, mAllowHttp(allowHttp)
{
	globalInit();
}

CurlHttpClient::~CurlHttpClient()
{
	if (mHandle)
		curl_easy_cleanup(static_cast<CURL*>(mHandle));
}

HttpResponse CurlHttpClient::send(const HttpRequest& request)
{
	HttpResponse response;
	if (!mHandle)
		mHandle = curl_easy_init();
	CURL* curl = static_cast<CURL*>(mHandle);
	if (!curl)
	{
		response.error = "libcurl could not be started";
		return response;
	}
	curl_easy_reset(curl);

	const std::string url = mOrigin + request.path;
	Download download{std::string(), request.maxBytes, false};

	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, mAuthorization.c_str());
	headers = curl_slist_append(headers, "Accept: application/json");
	if (request.post)
		headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	// only the scheme of the site, and never a redirect that could take the token elsewhere
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, mAllowHttp ? "http,https" : "https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, mAllowHttp ? (CURLPROTO_HTTP | CURLPROTO_HTTPS) : CURLPROTO_HTTPS);
#endif
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	if (!mCaFile.empty())
		curl_easy_setopt(curl, CURLOPT_CAINFO, mCaFile.c_str());
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
	// the Windows package has no CA bundle, so use the certificates of Windows
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, CONNECT_TIMEOUT_MS);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)request.timeoutMs);
	curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, (long)request.maxBytes);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "blobby-server");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
	if (request.post)
	{
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)request.body.size());
	}
	else
	{
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	}

	const CURLcode result = curl_easy_perform(curl);
	curl_slist_free_all(headers);

	if (result != CURLE_OK)
	{
		if (download.tooLarge || result == CURLE_FILESIZE_EXCEEDED)
			response.error = "the answer is larger than " + std::to_string(request.maxBytes) + " bytes";
		else
			response.error = curl_easy_strerror(result);
		return response;
	}

	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	response.status = status;
	response.body.swap(download.body);
	return response;
}
