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

#include <string>

#include "SiteSync.h"

/*! \class CurlHttpClient
	\brief Sends requests to the tournament site with libcurl.
	\details Certificates are always checked and redirects are not followed, so the token only
	goes to the configured site. Plain http is only used when allowHttp is set, for a test site
	on localhost. Use one client from one thread.
*/
class CurlHttpClient : public HttpClient
{
	public:
		/// \param origin site origin, checked by checkSiteUrl()
		/// \param caFile extra certificates to trust, empty for the system certificates only
		CurlHttpClient(std::string origin, std::string token, std::string caFile, bool allowHttp);
		~CurlHttpClient() override;

		HttpResponse send(const HttpRequest& request) override;

		/// Sets up libcurl. Call it once on the main thread before any thread uses a client.
		static bool globalInit();

	private:
		std::string mOrigin;
		std::string mAuthorization;
		std::string mCaFile;
		bool mAllowHttp;
		// CURL handle, kept for connection reuse
		void* mHandle = nullptr;
};
