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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Sha256.h"
#include "SponsorSync.h"
#include "Tournament.h"

/// One request to the tournament site.
struct HttpRequest
{
	bool post = false;
	/// Path below the site origin, such as /api/server/sync.
	std::string path;
	/// JSON body of a POST.
	std::string body;
	/// Largest answer that is read. A larger answer fails.
	std::size_t maxBytes = 64 * 1024;
	unsigned timeoutMs = 10000;
};

/// The answer of the site. status is 0 when no answer came, then error says why.
struct HttpResponse
{
	long status = 0;
	std::string body;
	std::string error;
};

/// Sends requests to the tournament site. The real client uses libcurl, tests use a fake.
class HttpClient
{
	public:
		virtual ~HttpClient();
		virtual HttpResponse send(const HttpRequest& request) = 0;

		/// A running request stops soon after this flag turns true. nullptr never stops.
		void setCancelFlag(const std::atomic<bool>* cancel) { mCancel = cancel; }

		bool cancelled() const { return mCancel && mCancel->load(); }

	private:
		const std::atomic<bool>* mCancel = nullptr;
};

/// A report that waits to be sent to the site.
struct OutboxEntry
{
	/// Grows with each report, also across restarts, so it gives the order.
	unsigned long long seq = 0;
	/// connected or game
	std::string kind;
	int matchId = 0;
	/// Game number of a game report, 0 otherwise.
	int game = 0;
	/// True for a finished game, false for an abandoned one.
	bool finished = false;
	int winnerId = 0;
	std::string path;
	/// JSON body
	std::string body;
	/// The site took this game, but no sync has shown it yet. It is not sent again.
	bool delivered = false;
};

/*! \class Outbox
	\brief Reports on disk that wait for an answer of the site.
	\details The file has one JSON object per line and is written again as a whole after each
	change, under a temporary name that is then renamed over the old file. An empty path keeps
	the reports in memory only. The outbox is safe to use from two threads. A game the site took
	stays as delivered until a sync shows it, so the players file counts it also after a restart.
*/
class Outbox
{
	public:
		typedef std::function<void(bool problem, const std::string&)> LogFunction;

		/// \param maxBytes largest file that load() reads
		explicit Outbox(std::string path, LogFunction log = LogFunction(), std::size_t maxBytes = 16 * 1024 * 1024);

		/// Reads the file. A line that cannot be read is skipped and the file is kept as path.bad.
		/// A file that cannot be read at all or is too large is renamed to path.bad before anything is saved.
		void load();

		/// Adds a report that both players of a match are connected.
		void addConnected(int matchId, int player1, int player2);
		/// Adds a game report.
		void addGame(const GameReport& report);

		/// All reports in order, delivered games too.
		std::vector<OutboxEntry> entries() const;
		/// Calls use with all reports while no report can be added or removed.
		void withEntries(const std::function<void(const std::vector<OutboxEntry>&)>& use) const;
		/// The reports that may be sent now: per match only the first that is not delivered,
		/// since a match sends one report at a time.
		std::vector<OutboxEntry> sendable() const;
		/// Removes a report that got a final answer.
		void remove(unsigned long long seq);
		/// Keeps a game the site took as delivered until a sync shows it.
		void markDelivered(unsigned long long seq);
		/// Removes the delivered games that a sync shows. nextGames maps each listed match to its nextGame.
		/// A game of a match that is not listed is removed too. Returns true when something was removed.
		bool removeSynced(const std::map<int, int>& nextGames);

		/// Number of reports that wait to be sent, without delivered games.
		std::size_t size() const;
		/// Grows with each change.
		unsigned long long generation() const;
		/// The same, for use inside withEntries().
		unsigned long long generationLocked() const { return mGeneration; }

	private:
		void add(OutboxEntry entry);
		// writes the file, needs the lock
		void save();

		std::string mPath;
		LogFunction mLog;
		std::size_t mMaxBytes;
		mutable std::mutex mMutex;
		std::vector<OutboxEntry> mEntries;
		unsigned long long mNextSeq = 1;
		unsigned long long mGeneration = 0;
};

/// What to do with a report after an answer of the site.
enum class SiteAnswer
{
	/// The site has the report, remove it.
	DONE,
	/// The site will never take the report, log it and remove it.
	REFUSED,
	/// The site is not reachable or failed, send the same report again later.
	RETRY,
	/// The token is wrong or not made yet, keep the report and try again much later.
	AUTH
};

/// Decides what an answer means for a report of this kind, connected or game.
/// detail gets the status or the reason the site gave.
SiteAnswer classifySiteAnswer(const std::string& kind, const HttpResponse& response, std::string& detail);

/// Text from the site made safe to log: control characters become '?' and it is cut to 200 bytes.
std::string siteTextForLog(const std::string& text);

/// True if path can go after the site origin, that is it starts with '/'.
bool isSitePath(const std::string& path);

/// The only path an outbox report of this kind and match goes to, empty for an unknown kind.
std::string outboxPath(const std::string& kind, int matchId);

/*! Turns the sync of the site into a players file for JoinCodeGate and TournamentDirector.
	\details Pending game reports that the site does not have yet count as if it had them,
	so a restart during an outage does not play a game again. Returns false and sets error
	when the text is not a sync. Single bad entries are skipped and added to warnings.
*/
bool buildPlayersFile(const std::string& syncJson, const std::vector<OutboxEntry>& pending,
					std::string& xml, std::string& syncVersion, std::vector<std::string>& warnings,
					std::string& error);

/// The nextGame of each match in a sync.
std::map<int, int> syncNextGames(const std::string& syncJson);

/*! Stores the sponsors of the site below a folder, so they survive an outage.
	\details Logos go to logos/<hash>.bmp and the list to board.json, which is written last.
	board.json holds sponsorsVersion, gameBoard and the sponsors in order with name and logo file.
	A logo whose SHA-256 differs from its hash, or that the game could not show, is skipped.
	Logo files that board.json no longer names are removed.
	The server reads these files with loadStoredSponsors and sends them to the players.
*/
bool storeSponsors(const std::string& folder, const std::string& sponsorsJson, std::string& version,
				std::vector<std::string>& warnings, std::string& error);

/// The sponsorsVersion of a stored board.json, empty when there is none.
std::string storedSponsorsVersion(const std::string& folder);

/*! Reads a board that storeSponsors stored, or a board.json written by hand in the same format.
	\details Each sponsor names its logo as logos/<hash>.bmp. A logo that does not match its hash,
	that the game could not show or that goes past the limits of sponsors::limitBoard is skipped
	and added to warnings. Returns false and sets error when board.json cannot be read or a logo
	it names is missing, which happens for a moment while the sync replaces the board.
*/
bool loadStoredSponsors(const std::string& folder, sponsors::Board& board,
						std::vector<std::string>& warnings, std::string& error);

/// Reads the stored board again when board.json changes, for DedicatedServer::setSponsorSource.
class StoredSponsorBoard
{
	public:
		typedef std::function<void(bool problem, const std::string&)> LogFunction;

		explicit StoredSponsorBoard(std::string folder, LogFunction log = LogFunction());

		/// True and the board when board.json changed and was read. A board that could not be read
		/// is tried again on the next call.
		bool poll(sponsors::Board& board);

	private:
		std::string mFolder;
		LogFunction mLog;
		std::string mContent;
		bool mLoaded = false;
		std::string mLastError;
};

/// Live score of a running game.
struct LiveScore
{
	int matchId = 0;
	int game = 0;
	int player1 = 0;
	int points1 = 0;
	int player2 = 0;
	int points2 = 0;
};

/// Where the site link keeps its files.
struct SitePaths
{
	/// Players file that the sync writes.
	std::string playersFile;
	/// The last sync as the site sent it, so a restart during an outage still knows the matches. Empty to not keep it.
	std::string syncFile;
	/// Reports the site refused, one JSON object per line. Empty to only log them.
	std::string refusedFile;
	/// Folder for the sponsors. Empty to not fetch them.
	std::string sponsorsFolder;
};

/// Timing of the site link, in milliseconds.
struct SiteTiming
{
	unsigned syncInterval = 3000;
	unsigned sponsorsInterval = 60000;
	unsigned liveInterval = 1000;
	unsigned firstRetry = 1000;
	unsigned maxRetry = 30000;
	unsigned authRetry = 30000;
};

/// Pause after the given number of failed steps in a row, from firstRetry doubling up to maxRetry.
unsigned siteFailurePause(const SiteTiming& timing, unsigned failures);

/*! \class SiteLink
	\brief Talks to the tournament site: reads the sync, sends the reports of the outbox and the live score.
	\details step() does the work that is due and blocks while a request runs. SiteReporter
	calls it from its own thread. The outbox and the live scores may change from another thread.
*/
class SiteLink
{
	public:
		typedef std::function<void(bool problem, const std::string&)> LogFunction;

		SiteLink(HttpClient& http, Outbox& outbox, SitePaths paths, LogFunction log, SiteTiming timing = SiteTiming());

		/// Does what is due. now_ms is any monotonic time in milliseconds.
		void step(unsigned long long now_ms);

		/// When the flag turns true, step() returns before the next request.
		void setStopFlag(const std::atomic<bool>* stop) { mStop = stop; }

		/// Remembers the newest score of a game. It is sent at most once per liveInterval and never queued.
		void setLiveScore(const LiveScore& score);
		/// Forgets the live score of a match whose game ended.
		void clearLiveScore(int matchId);

		/// True while the last request to the site failed.
		bool isSiteDown() const { return mDown; }

	private:
		void sync(unsigned long long now_ms);
		void sponsors(unsigned long long now_ms);
		void sendOutbox(unsigned long long now_ms);
		void sendLive(unsigned long long now_ms);
		bool writePlayersFile();
		bool stopping() const { return mStop && mStop->load(); }
		void reachable(bool ok, const std::string& what, bool token = false);
		void loadSavedSync();
		void logRefused(const OutboxEntry& entry, const std::string& reason);
		unsigned nextRetry(unsigned current) const;

		HttpClient& mHttp;
		Outbox& mOutbox;
		SitePaths mPaths;
		LogFunction mLog;
		SiteTiming mTiming;

		const std::atomic<bool>* mStop = nullptr;
		bool mStarted = false;
		bool mDown = false;
		std::string mLastProblem;

		unsigned long long mSyncAt = 0;
		unsigned mSyncRetry = 0;
		std::string mSyncJson;
		std::string mSyncVersion;
		std::string mWrittenVersion;
		unsigned long long mWrittenGeneration = 0;
		bool mWritten = false;
		std::string mWrittenXml;
		std::string mWriteError;
		std::string mSavedSyncVersion;

		unsigned long long mSponsorsAt = 0;
		std::string mSponsorsVersion;
		bool mSponsorsLoaded = false;

		unsigned long long mOutboxAt = 0;
		unsigned mOutboxRetry = 0;

		std::mutex mLiveMutex;
		std::map<int, LiveScore> mLive;
		std::map<int, unsigned long long> mLiveSentAt;
};

/*! \class SiteReporter
	\brief Sends tournament events to the site from a thread of its own.
	\details Every event also goes to the inner events, which log it and write the results file.
	Reports are on disk in the outbox before this returns, so the main loop never waits for the site.
*/
class SiteReporter : public TournamentEvents
{
	public:
		SiteReporter(std::shared_ptr<TournamentEvents> inner, std::unique_ptr<HttpClient> http,
					std::unique_ptr<Outbox> outbox, SitePaths paths, SiteLink::LogFunction log);
		~SiteReporter() override;

		/// Starts the thread.
		void start();
		/// Stops the thread. A request that runs ends first.
		void stop();

		void matchConnected(int matchId, int player1, int player2) override;
		void gameEnded(const GameReport& report) override;
		void seriesEnded(const SeriesResult& result) override;
		void liveScore(int matchId, int game, int player1, int points1, int player2, int points2) override;

	private:
		void run();
		void wake();
		SiteLink::LogFunction mLogLine;

		std::shared_ptr<TournamentEvents> mInner;
		std::unique_ptr<HttpClient> mHttp;
		std::unique_ptr<Outbox> mOutbox;
		std::unique_ptr<SiteLink> mLink;

		std::mutex mWakeMutex;
		std::condition_variable mWakeUp;
		bool mWoken = false;
		std::atomic<bool> mRunning{false};
		std::atomic<bool> mStopping{false};
		std::thread mThread;
};
