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

#pragma once

#include "State.h"
#include "NetworkMessage.h"

#include <vector>
#include <list>
#include <future>
#include <functional>
#include <string>
#include <utility>

class RakClient;
class RakServer;
class DuelMatch;
class NetworkGame;

/*! \class NetworkSearchState
	\brief Base class for search states
	\details This class provides a search interface.
			Derived classes provide the job that downloads the
			server list, and the code that reads and pings the
			servers. The job runs on a detached search thread and
			only uses the network. Files, the state and all
			RakNet calls stay on the main thread.
*/
class NetworkSearchState : public State
{
public:
	NetworkSearchState();
	~NetworkSearchState() override;

	void step_impl() override;
	// onlinegames connect to the masterserver
	// LAN games send a broadcast to local network
	void searchServers();

	const char* getStateName() const override;
protected:
	typedef std::vector< std::pair<std::string, int> > ServerAddressList;

	std::vector<ServerInfo> mScannedServers;
	std::unique_ptr<RakClient> mPingClient;

private:
	// returns the job that runs on the search thread. It must not touch the state, files or other globals.
	virtual std::function<std::string()> getSearchJob() const = 0;
	// reads the server addresses from the job result. An empty list keeps the current server list.
	virtual ServerAddressList findServers(const std::string& searchResult) const = 0;
	// pings the found servers
	virtual void pingServers(const ServerAddressList& servers) = 0;

	typedef std::list<RakClient*> ClientList;

	ClientList mQueryClients;
	RakClient* mDirectConnectClient = nullptr;

	std::future<std::string> mSearchJob;
	// a search was requested while another one was running
	bool mSearchRequested = false;

	unsigned mSelectedServer;
	bool mDisplayInfo;
	bool mEnteringServer;
	bool mDisplayUpdateNotification;

	std::unique_ptr<ServerInfo> mHostedServer;

	std::string mEnteredServer;
	unsigned mServerBoxPosition;
};

/*! \class OnlineSearchState
	\brief State for online server search screen.
*/
class OnlineSearchState : public NetworkSearchState
{
public:
	OnlineSearchState();
	~OnlineSearchState() override = default;
	const char* getStateName() const override;

private:
	std::function<std::string()> getSearchJob() const override;
	ServerAddressList findServers(const std::string& serverListXml) const override;
	void pingServers(const ServerAddressList& servers) override;
	// downloads the server list, runs on the search thread
	static std::string downloadServerList();
};


/*! \class LANSearchState
	\brief State for LAN game search screen.
*/
class LANSearchState : public NetworkSearchState
{
public:
	LANSearchState();
	~LANSearchState() override = default;
	const char* getStateName() const override;

private:
	std::function<std::string()> getSearchJob() const override;
	ServerAddressList findServers(const std::string& searchResult) const override;
	void pingServers(const ServerAddressList& servers) override;
};

