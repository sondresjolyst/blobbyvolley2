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

#include "Updater.h"

/*! Downloads over https with libcurl and checks the certificates.
	\details On Windows the certificates come from the Windows store, elsewhere from the CA bundle of
	the system. Only https is allowed, also for redirects.
*/
class CurlFetcher : public Fetcher
{
	public:
		CurlFetcher();
		~CurlFetcher() override;
		CurlFetcher(const CurlFetcher&) = delete;
		CurlFetcher& operator=(const CurlFetcher&) = delete;

		Result fetch(const std::string& url, Kind kind, uint64_t maxBytes, std::string& body, std::string& error,
					 const ProgressFunction& progress) override;

	private:
		void* mCurl;
};
