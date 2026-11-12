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

#include "CurlFetcher.h"

#include <curl/curl.h>

#include "ForkVersion.h"
#include "FsUtil.h"

namespace
{
	struct Transfer
	{
		std::string* body;
		uint64_t maxBytes;
		bool tooLarge;
		const ProgressFunction* progress;
		bool cancelled;
	};

	size_t writeBody(char* data, size_t size, size_t count, void* user)
	{
		Transfer& transfer = *static_cast<Transfer*>(user);
		const size_t bytes = size * count;
		if (transfer.body->size() + bytes > transfer.maxBytes)
		{
			transfer.tooLarge = true;
			return 0;
		}
		transfer.body->append(data, bytes);
		return bytes;
	}

	int progressCallback(void* user, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t)
	{
		Transfer& transfer = *static_cast<Transfer*>(user);
		if (transfer.progress && *transfer.progress && !(*transfer.progress)((uint64_t)done, (uint64_t)total))
		{
			transfer.cancelled = true;
			return 1;
		}
		return 0;
	}

#if !defined(_WIN32)
	/// The CA bundle of the system. A libcurl from another distribution may look in the wrong place.
	std::string systemCaBundle()
	{
		for (const char* path : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
								 "/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cacert.pem",
								 "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", "/etc/ssl/cert.pem"})
		{
			if (fsu::isFile(path))
				return path;
		}
		return std::string();
	}
#endif
}

CurlFetcher::CurlFetcher()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	mCurl = curl_easy_init();
}

CurlFetcher::~CurlFetcher()
{
	if (mCurl)
		curl_easy_cleanup(static_cast<CURL*>(mCurl));
	curl_global_cleanup();
}

Fetcher::Result CurlFetcher::fetch(const std::string& url, Kind kind, uint64_t maxBytes, std::string& body,
								   std::string& error, const ProgressFunction& progress)
{
	CURL* curl = static_cast<CURL*>(mCurl);
	if (!curl)
	{
		error = "libcurl did not start";
		return Result::FAILED;
	}
	curl_easy_reset(curl);

	std::string received;
	Transfer transfer{&received, maxBytes, false, &progress, false};
	char errorText[CURL_ERROR_SIZE] = {0};

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorText);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ("blobby-launcher/" BLOBBY_FORK_VERSION));
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef _WIN32
	// MSYS2 builds libcurl with OpenSSL and a CA file that players do not have, so use the Windows store
	curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
	curl_easy_setopt(curl, CURLOPT_CAPATH, nullptr);
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#else
	const std::string bundle = systemCaBundle();
	if (!bundle.empty())
		curl_easy_setopt(curl, CURLOPT_CAINFO, bundle.c_str());
#endif
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
	if (kind == Kind::SMALL)
	{
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
	}
	else
	{
		// a slow line is fine, a stalled one is not
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);

	const CURLcode result = curl_easy_perform(curl);
	if (transfer.cancelled)
		return Result::CANCELLED;
	if (transfer.tooLarge)
	{
		error = "the file is larger than expected";
		return Result::FAILED;
	}
	if (result != CURLE_OK)
	{
		error = errorText[0] ? std::string(errorText) : std::string(curl_easy_strerror(result));
		return Result::FAILED;
	}
	body.swap(received);
	return Result::OK;
}
