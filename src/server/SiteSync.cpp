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

#include "SiteSync.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "tinyxml2.h"

#include "SiteConfig.h"
#include "base64.h"

using nlohmann::json;

namespace
{
	// largest answers that are read
	const std::size_t MAX_SYNC_BYTES = 1024 * 1024;
	const std::size_t MAX_SPONSORS_BYTES = 8 * 1024 * 1024;
	const std::size_t MAX_ANSWER_BYTES = 64 * 1024;
	// largest outbox file that is read
	const std::size_t MAX_OUTBOX_BYTES = 16 * 1024 * 1024;
	// largest logo, a 160 by 40 BMP with alpha is about 26 KB
	const std::size_t MAX_LOGO_BYTES = 256 * 1024;
	const unsigned LIVE_TIMEOUT_MS = 2000;

	bool isInt(const json& value, long long min, long long max)
	{
		if (!value.is_number_integer())
			return false;
		const long long v = value.get<long long>();
		return v >= min && v <= max;
	}

	// an integer field of an object, or fallback when it is missing or not an integer in range
	int intField(const json& object, const char* name, int fallback, long long min = -2147483647LL - 1, long long max = 2147483647LL)
	{
		auto found = object.find(name);
		if (found == object.end() || !isInt(*found, min, max))
			return fallback;
		return found->get<int>();
	}

	std::string stringField(const json& object, const char* name)
	{
		auto found = object.find(name);
		if (found == object.end() || !found->is_string())
			return std::string();
		return found->get<std::string>();
	}

	bool parseObject(const std::string& text, json& value)
	{
		value = json::parse(text, nullptr, false);
		return !value.is_discarded() && value.is_object();
	}

	// the nextGame of each match in a sync
	std::map<int, int> syncNextGames(const std::string& syncJson)
	{
		std::map<int, int> result;
		json sync;
		if (!parseObject(syncJson, sync) || !sync.contains("matches") || !sync["matches"].is_array())
			return result;
		for (const auto& m : sync["matches"])
		{
			if (m.is_object() && isInt(m.value("matchId", json()), 1, 2147483647))
				result[m["matchId"].get<int>()] = intField(m, "nextGame", 1, 1);
		}
		return result;
	}

	std::string nowText()
	{
		return formatUtcTime(std::chrono::system_clock::now());
	}

	bool isHash(const std::string& text)
	{
		if (text.size() != 64)
			return false;
		for (char c : text)
		{
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		}
		return true;
	}

	bool fileExists(const std::string& path)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		return static_cast<bool>(file);
	}

	OutboxEntry entryFromJson(const json& line, bool& ok)
	{
		OutboxEntry entry;
		ok = false;
		if (!line.is_object())
			return entry;
		auto seq = line.find("seq");
		auto body = line.find("body");
		if (seq == line.end() || !seq->is_number_unsigned() || body == line.end() || !body->is_object())
			return entry;
		entry.seq = seq->get<unsigned long long>();
		entry.kind = stringField(line, "kind");
		entry.path = stringField(line, "path");
		entry.matchId = intField(line, "matchId", 0, 1);
		entry.body = body->dump();
		if (entry.kind == "game")
		{
			entry.game = intField(*body, "game", 0, 1);
			entry.finished = stringField(*body, "status") == "finished";
			entry.winnerId = intField(*body, "winnerId", 0, 1);
		}
		ok = entry.seq > 0 && entry.matchId > 0 && !entry.path.empty()
			&& (entry.kind == "connected" || (entry.kind == "game" && entry.game > 0));
		return entry;
	}

	json entryToJson(const OutboxEntry& entry)
	{
		json line;
		line["seq"] = entry.seq;
		line["kind"] = entry.kind;
		line["matchId"] = entry.matchId;
		line["path"] = entry.path;
		line["body"] = json::parse(entry.body);
		return line;
	}
}

// - - - - - - - - - - - - - - - - - -
// 			outbox
// - - - - - - - - - - - - - - - - - -

HttpClient::~HttpClient() = default;

Outbox::Outbox(std::string path, LogFunction log)
: mPath(std::move(path))
, mLog(std::move(log))
{
}

void Outbox::load()
{
	std::lock_guard<std::mutex> lock(mMutex);
	if (mPath.empty())
		return;
	std::string content;
	if (!readWholeFile(mPath, MAX_OUTBOX_BYTES, content))
	{
		if (fileExists(mPath) && mLog)
			mLog(true, "cannot read the outbox " + mPath + ", it is larger than 16 MB or not readable");
		return;
	}

	std::istringstream lines(content);
	std::string text;
	int bad = 0;
	std::vector<OutboxEntry> loaded;
	while (std::getline(lines, text))
	{
		if (!text.empty() && text.back() == '\r')
			text.pop_back();
		if (text.empty())
			continue;
		bool ok = false;
		OutboxEntry entry = entryFromJson(json::parse(text, nullptr, false), ok);
		if (ok)
			loaded.push_back(entry);
		else
			++bad;
	}
	std::sort(loaded.begin(), loaded.end(), [](const OutboxEntry& a, const OutboxEntry& b){ return a.seq < b.seq; });
	mEntries.swap(loaded);
	for (const auto& e : mEntries)
		mNextSeq = std::max(mNextSeq, e.seq + 1);
	++mGeneration;

	if (bad > 0)
	{
		std::string error;
		writeFileAtomically(mPath + ".bad", content, error);
		if (mLog)
			mLog(true, "skipped " + std::to_string(bad) + " lines of the outbox that cannot be read, the file is kept as "
				+ mPath + ".bad");
		save();
	}
	if (mLog && !mEntries.empty())
		mLog(false, std::to_string(mEntries.size()) + " reports wait in the outbox");
}

void Outbox::addConnected(int matchId, int player1, int player2)
{
	OutboxEntry entry;
	entry.kind = "connected";
	entry.matchId = matchId;
	entry.path = "/api/server/matches/" + std::to_string(matchId) + "/connected";
	json body;
	body["playerIds"] = json::array({player1, player2});
	entry.body = body.dump();
	add(std::move(entry));
}

void Outbox::addGame(const GameReport& report)
{
	OutboxEntry entry;
	entry.kind = "game";
	entry.matchId = report.matchId;
	entry.game = report.game;
	entry.finished = report.finished;
	entry.winnerId = report.finished ? report.winnerId : 0;
	entry.path = "/api/server/games";
	entry.body = gameReportJson(report);
	add(std::move(entry));
}

void Outbox::add(OutboxEntry entry)
{
	std::lock_guard<std::mutex> lock(mMutex);
	entry.seq = mNextSeq++;
	mEntries.push_back(std::move(entry));
	++mGeneration;
	save();
}

std::vector<OutboxEntry> Outbox::entries() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mEntries;
}

std::vector<OutboxEntry> Outbox::sendable() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	std::vector<OutboxEntry> result;
	std::set<int> seen;
	for (const auto& e : mEntries)
	{
		if (seen.insert(e.matchId).second)
			result.push_back(e);
	}
	return result;
}

void Outbox::remove(unsigned long long seq)
{
	std::lock_guard<std::mutex> lock(mMutex);
	auto it = std::find_if(mEntries.begin(), mEntries.end(), [seq](const OutboxEntry& e){ return e.seq == seq; });
	if (it == mEntries.end())
		return;
	mEntries.erase(it);
	++mGeneration;
	save();
}

std::size_t Outbox::size() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mEntries.size();
}

unsigned long long Outbox::generation() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mGeneration;
}

void Outbox::save()
{
	if (mPath.empty())
		return;
	std::string content;
	for (const auto& e : mEntries)
		content += entryToJson(e).dump() + "\n";
	std::string error;
	if (!writeFileAtomically(mPath, content, error) && mLog)
		mLog(true, "cannot save the outbox: " + error + ", the reports stay in memory");
}

// - - - - - - - - - - - - - - - - - -
// 			answers
// - - - - - - - - - - - - - - - - - -

SiteAnswer classifySiteAnswer(const std::string& kind, const HttpResponse& response, std::string& detail)
{
	if (response.status == 0)
	{
		detail = response.error.empty() ? std::string("no answer") : response.error;
		return SiteAnswer::RETRY;
	}
	if (response.status == 401 || response.status == 503)
	{
		json body;
		if (parseObject(response.body, body) && body.contains("error") && body["error"].is_string())
			detail = body["error"].get<std::string>();
		else
			detail = "HTTP " + std::to_string(response.status);
		// a 503 without the not-configured error is an outage, not a missing token
		if (response.status == 503 && detail != "not-configured")
			return SiteAnswer::RETRY;
		return SiteAnswer::AUTH;
	}
	if (response.status == 400 || response.status == 404)
	{
		json body;
		detail = "HTTP " + std::to_string(response.status);
		if (parseObject(response.body, body) && body.contains("field") && body["field"].is_string())
			detail += ", invalid " + body["field"].get<std::string>();
		return SiteAnswer::REFUSED;
	}
	if (response.status != 200)
	{
		detail = "HTTP " + std::to_string(response.status);
		return SiteAnswer::RETRY;
	}

	// a 200 is final, whatever it says
	json body;
	if (!parseObject(response.body, body) || !body.contains("status") || !body["status"].is_string())
	{
		detail = "an answer that cannot be read";
		return SiteAnswer::REFUSED;
	}
	const std::string status = body["status"].get<std::string>();
	if (status == "refused")
	{
		detail = "refused";
		if (body.contains("reason") && body["reason"].is_string())
			detail += " " + body["reason"].get<std::string>();
		return SiteAnswer::REFUSED;
	}
	detail = status;
	if (kind == "connected" && (status == "started" || status == "already-started"))
		return SiteAnswer::DONE;
	if (kind == "game" && (status == "stored" || status == "duplicate" || status == "game-closed"))
		return SiteAnswer::DONE;
	detail = "unknown status " + status;
	return SiteAnswer::REFUSED;
}

// - - - - - - - - - - - - - - - - - -
// 			sync
// - - - - - - - - - - - - - - - - - -

bool buildPlayersFile(const std::string& syncJson, const std::vector<OutboxEntry>& pending,
					std::string& xml, std::string& syncVersion, std::vector<std::string>& warnings,
					std::string& error)
{
	json sync;
	if (!parseObject(syncJson, sync))
	{
		error = "the sync is not a JSON object";
		return false;
	}
	auto version = sync.find("syncVersion");
	auto players = sync.find("players");
	auto matches = sync.find("matches");
	if (version == sync.end() || !version->is_string() || version->get<std::string>().empty())
	{
		error = "the sync has no syncVersion";
		return false;
	}
	if (players == sync.end() || !players->is_array() || matches == sync.end() || !matches->is_array())
	{
		error = "the sync has no players or matches list";
		return false;
	}

	tinyxml2::XMLDocument doc;
	doc.InsertEndChild(doc.NewDeclaration("xml version=\"1.0\" encoding=\"utf-8\""));
	tinyxml2::XMLElement* root = doc.NewElement("tournament");
	doc.InsertEndChild(root);

	int index = 0;
	for (const auto& p : *players)
	{
		++index;
		if (!p.is_object() || !isInt(p.value("playerId", json()), 1, 2147483647)
			|| !p.contains("nickname") || !p["nickname"].is_string() || !p.contains("code") || !p["code"].is_string())
		{
			warnings.push_back("player " + std::to_string(index) + " of the sync is not complete, skipped");
			continue;
		}
		tinyxml2::XMLElement* element = doc.NewElement("player");
		element->SetAttribute("id", p["playerId"].get<int>());
		element->SetAttribute("nickname", p["nickname"].get<std::string>().c_str());
		element->SetAttribute("code", p["code"].get<std::string>().c_str());
		root->InsertEndChild(element);
	}

	index = 0;
	for (const auto& m : *matches)
	{
		++index;
		const std::string where = "match " + std::to_string(index) + " of the sync";
		if (!m.is_object() || !isInt(m.value("matchId", json()), 1, 2147483647))
		{
			warnings.push_back(where + " has no matchId, skipped");
			continue;
		}
		const int matchId = m["matchId"].get<int>();
		auto pl = m.find("players");
		if (pl == m.end() || !pl->is_array() || pl->size() != 2 || !(*pl)[0].is_object() || !(*pl)[1].is_object()
			|| !isInt((*pl)[0].value("playerId", json()), 1, 2147483647)
			|| !isInt((*pl)[1].value("playerId", json()), 1, 2147483647))
		{
			warnings.push_back("match " + std::to_string(matchId) + " of the sync has no two players, skipped");
			continue;
		}
		const int player1 = (*pl)[0]["playerId"].get<int>();
		const int player2 = (*pl)[1]["playerId"].get<int>();

		int nextGame = intField(m, "nextGame", 1, 1);
		bool awaiting = m.contains("awaitingDecision") && m["awaitingDecision"].is_boolean() && m["awaitingDecision"].get<bool>();
		int wins1 = intField(m, "wins1", -1, 0);
		int wins2 = intField(m, "wins2", -1, 0);

		// reports the site does not have yet count as if it had them
		for (const auto& e : pending)
		{
			if (e.kind != "game" || e.matchId != matchId || e.game < nextGame)
				continue;
			nextGame = e.game + 1;
			if (e.finished)
			{
				awaiting = false;
				if (e.winnerId == player1 && wins1 >= 0)
					++wins1;
				else if (e.winnerId == player2 && wins2 >= 0)
					++wins2;
			}
			else
			{
				awaiting = true;
			}
		}

		tinyxml2::XMLElement* element = doc.NewElement("match");
		element->SetAttribute("id", matchId);
		element->SetAttribute("player1", player1);
		element->SetAttribute("player2", player2);
		const std::string rule = stringField(m, "ruleFile");
		if (!rule.empty())
			element->SetAttribute("ruleFile", rule.c_str());
		if (isInt(m.value("scoreToWin", json()), -2147483647LL, 2147483647LL))
			element->SetAttribute("scoreToWin", m["scoreToWin"].get<int>());
		if (isInt(m.value("speed", json()), -2147483647LL, 2147483647LL))
			element->SetAttribute("speed", m["speed"].get<int>());
		if (isInt(m.value("bestOf", json()), -2147483647LL, 2147483647LL))
			element->SetAttribute("bestOf", m["bestOf"].get<int>());
		element->SetAttribute("nextGame", nextGame);
		element->SetAttribute("awaitingDecision", awaiting);
		if (wins1 >= 0)
			element->SetAttribute("wins1", wins1);
		if (wins2 >= 0)
			element->SetAttribute("wins2", wins2);
		if (isInt(m.value("nextGameDelay", json()), -2147483647LL, 2147483647LL))
			element->SetAttribute("nextGameDelay", m["nextGameDelay"].get<int>());
		root->InsertEndChild(element);
	}

	tinyxml2::XMLPrinter printer;
	doc.Print(&printer);
	xml = printer.CStr();
	syncVersion = version->get<std::string>();
	return true;
}

std::string syncSponsorsVersion(const std::string& syncJson)
{
	json sync;
	if (!parseObject(syncJson, sync))
		return std::string();
	return stringField(sync, "sponsorsVersion");
}

// - - - - - - - - - - - - - - - - - -
// 			sponsors
// - - - - - - - - - - - - - - - - - -

std::string storedSponsorsVersion(const std::string& folder)
{
	std::string content;
	json board;
	if (!readWholeFile(pathInDataDir(folder, "board.json"), MAX_ANSWER_BYTES, content) || !parseObject(content, board))
		return std::string();
	return stringField(board, "sponsorsVersion");
}

bool storeSponsors(const std::string& folder, const std::string& sponsorsJson, std::string& version,
				std::vector<std::string>& warnings, std::string& error)
{
	static const std::set<std::string> STYLES{"off", "led", "printed", "wood", "slim", "padded"};

	json answer;
	if (!parseObject(sponsorsJson, answer))
	{
		error = "the sponsors answer is not a JSON object";
		return false;
	}
	const std::string newVersion = stringField(answer, "sponsorsVersion");
	const std::string style = stringField(answer, "gameBoard");
	auto list = answer.find("sponsors");
	if (newVersion.empty() || STYLES.count(style) == 0 || list == answer.end() || !list->is_array())
	{
		error = "the sponsors answer has no sponsorsVersion, a known gameBoard or a sponsors list";
		return false;
	}

	const std::string logos = pathInDataDir(folder, "logos");
	if (!makeFolder(folder) || !makeFolder(logos))
	{
		error = "cannot make the folder " + logos;
		return false;
	}

	json board;
	board["sponsorsVersion"] = newVersion;
	board["gameBoard"] = style;
	board["sponsors"] = json::array();
	int index = 0;
	for (const auto& s : *list)
	{
		++index;
		const std::string where = "sponsor " + std::to_string(index);
		if (!s.is_object() || !s.contains("logo") || !s["logo"].is_object())
		{
			warnings.push_back(where + " has no logo, skipped");
			continue;
		}
		const json& logo = s["logo"];
		const std::string hash = stringField(logo, "hash");
		if (stringField(logo, "format") != "bmp" || !isHash(hash))
		{
			warnings.push_back(where + " has no BMP logo with a valid hash, skipped");
			continue;
		}
		const std::string file = "logos/" + hash + ".bmp";
		const std::string path = pathInDataDir(folder, file);
		if (!fileExists(path))
		{
			const std::string data = stringField(logo, "data");
			if (data.size() > MAX_LOGO_BYTES * 4 / 3 + 4)
			{
				warnings.push_back(where + " has a logo that is too large, skipped");
				continue;
			}
			std::vector<uint8_t> bytes = decode(data);
			if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M')
			{
				warnings.push_back(where + " has a logo that is not a BMP, skipped");
				continue;
			}
			std::string writeError;
			if (!writeFileAtomically(path, std::string(bytes.begin(), bytes.end()), writeError))
			{
				error = writeError;
				return false;
			}
		}
		json entry;
		entry["sponsorId"] = intField(s, "sponsorId", 0);
		entry["name"] = stringField(s, "name");
		entry["logo"] = file;
		entry["width"] = intField(logo, "width", 0, 0, 10000);
		entry["height"] = intField(logo, "height", 0, 0, 10000);
		board["sponsors"].push_back(entry);
	}

	// the list goes last, so it only names logos that are on disk
	if (!writeFileAtomically(pathInDataDir(folder, "board.json"), board.dump(1, '\t') + "\n", error))
		return false;
	version = newVersion;
	return true;
}

// - - - - - - - - - - - - - - - - - -
// 			site link
// - - - - - - - - - - - - - - - - - -

SiteLink::SiteLink(HttpClient& http, Outbox& outbox, SitePaths paths, LogFunction log, SiteTiming timing)
: mHttp(http)
, mOutbox(outbox)
, mPaths(std::move(paths))
, mLog(std::move(log))
, mTiming(timing)
{
}

unsigned SiteLink::nextRetry(unsigned current) const
{
	if (current == 0)
		return mTiming.firstRetry;
	return std::min(mTiming.maxRetry, current * 2);
}

void SiteLink::reachable(bool ok, const std::string& what, bool token)
{
	// log when the site goes down or comes back, not every failed request of an outage
	if (ok)
	{
		if (mDown && mLog)
			mLog(false, "the tournament site answers again");
		mDown = false;
		mLastProblem.clear();
		return;
	}
	if (mLog && (!mDown || (token && what != mLastProblem)))
	{
		if (token)
			mLog(true, what + ", the reports wait until then");
		else
			mLog(true, "the tournament site cannot be reached: " + what + ", the reports wait and are sent when it is back");
	}
	mDown = true;
	mLastProblem = what;
}

void SiteLink::step(unsigned long long now_ms)
{
	if (!mStarted)
	{
		mStarted = true;
		loadSavedSync();
	}
	if (now_ms >= mSyncAt)
		sync(now_ms);
	if (!mPaths.sponsorsFolder.empty() && now_ms >= mSponsorsAt)
		sponsors(now_ms);
	if (now_ms >= mOutboxAt && mOutbox.size() > 0)
		sendOutbox(now_ms);
	// the players file also reflects the reports that wait in the outbox
	if (!mSyncJson.empty() && mOutbox.generation() != mWrittenGeneration)
		writePlayersFile();
	sendLive(now_ms);
}

void SiteLink::sync(unsigned long long now_ms)
{
	HttpRequest request;
	request.path = "/api/server/sync";
	request.maxBytes = MAX_SYNC_BYTES;
	HttpResponse response = mHttp.send(request);

	if (response.status != 200)
	{
		std::string detail;
		SiteAnswer answer = classifySiteAnswer("sync", response, detail);
		if (answer == SiteAnswer::AUTH)
		{
			const std::string what = detail == "not-configured"
				? "the site has no game server token yet, make one on the admin page"
				: "the site refused the token (" + detail + "), check BLOBBY_SITE_TOKEN";
			reachable(false, what, true);
			mSyncAt = now_ms + mTiming.authRetry;
			return;
		}
		reachable(false, "sync failed with " + detail);
		mSyncRetry = nextRetry(mSyncRetry);
		mSyncAt = now_ms + mSyncRetry;
		return;
	}

	std::string xml;
	std::string version;
	std::vector<std::string> warnings;
	std::string error;
	if (!buildPlayersFile(response.body, std::vector<OutboxEntry>(), xml, version, warnings, error))
	{
		reachable(false, "the sync cannot be read, " + error);
		mSyncRetry = nextRetry(mSyncRetry);
		mSyncAt = now_ms + mSyncRetry;
		return;
	}
	reachable(true, "");
	mSyncRetry = 0;
	mSyncAt = now_ms + mTiming.syncInterval;

	// a delivered game counts until the sync has moved past it or the match left the list
	const std::size_t deliveredBefore = mDelivered.size();
	const std::map<int, int> nextGames = syncNextGames(response.body);
	mDelivered.erase(std::remove_if(mDelivered.begin(), mDelivered.end(), [&](const OutboxEntry& e){
		auto found = nextGames.find(e.matchId);
		return e.kind != "game" || found == nextGames.end() || found->second > e.game;
	}), mDelivered.end());
	const bool deliveredChanged = mDelivered.size() != deliveredBefore;

	// the sponsors are read again when the sync says they changed
	const std::string sponsorsVersion = syncSponsorsVersion(response.body);
	if (!sponsorsVersion.empty() && mSponsorsLoaded && sponsorsVersion != mSponsorsVersion)
		mSponsorsAt = now_ms;

	if (version == mSyncVersion && mWritten && mOutbox.generation() == mWrittenGeneration && !deliveredChanged)
		return;
	if (version != mSyncVersion)
	{
		if (mLog)
		{
			for (const auto& w : warnings)
				mLog(true, w);
		}
		// kept on disk, so a restart during an outage still knows the matches
		if (!mPaths.syncFile.empty() && !writeFileAtomically(mPaths.syncFile, response.body, error) && mLog)
			mLog(true, "cannot save the sync: " + error);
	}
	mSyncJson = response.body;
	mSyncVersion = version;
	writePlayersFile();
}

void SiteLink::loadSavedSync()
{
	std::string saved;
	std::string xml;
	std::string version;
	std::vector<std::string> warnings;
	std::string error;
	if (mPaths.syncFile.empty() || !readWholeFile(mPaths.syncFile, MAX_SYNC_BYTES, saved)
		|| !buildPlayersFile(saved, std::vector<OutboxEntry>(), xml, version, warnings, error))
		return;
	mSyncJson = saved;
	mSyncVersion = version;
}

void SiteLink::writePlayersFile()
{
	const unsigned long long generation = mOutbox.generation();
	std::string xml;
	std::string version;
	std::vector<std::string> warnings;
	std::string error;
	// reports the site took after the last sync still count, until a newer sync has them
	std::vector<OutboxEntry> pending = mDelivered;
	for (const auto& e : mOutbox.entries())
		pending.push_back(e);
	if (!buildPlayersFile(mSyncJson, pending, xml, version, warnings, error))
		return;
	if (!mWritten || xml != mWrittenXml)
	{
		if (!writeFileAtomically(mPaths.playersFile, xml, error))
		{
			// log once, the next round tries again
			if (mLog && error != mWriteError)
				mLog(true, "cannot write the players file: " + error);
			mWriteError = error;
			return;
		}
		mWriteError.clear();
		if (mLog && version != mWrittenVersion)
			mLog(false, "the sync " + version + " is written to " + mPaths.playersFile);
	}
	mWritten = true;
	mWrittenXml = xml;
	mWrittenVersion = version;
	mWrittenGeneration = generation;
}

void SiteLink::sponsors(unsigned long long now_ms)
{
	if (!mSponsorsLoaded)
	{
		mSponsorsVersion = storedSponsorsVersion(mPaths.sponsorsFolder);
		mSponsorsLoaded = true;
	}
	// no sponsors while the site is down, the sync finds out when it is back
	if (mDown)
	{
		mSponsorsAt = mSyncAt;
		return;
	}

	HttpRequest request;
	request.path = "/api/server/sponsors";
	request.maxBytes = MAX_SPONSORS_BYTES;
	request.timeoutMs = 20000;
	HttpResponse response = mHttp.send(request);
	mSponsorsAt = now_ms + mTiming.sponsorsInterval;
	if (response.status != 200)
	{
		if (mLog)
			mLog(true, "cannot read the sponsors: " + (response.status == 0 ? response.error : "HTTP " + std::to_string(response.status)));
		return;
	}

	json answer;
	if (parseObject(response.body, answer) && stringField(answer, "sponsorsVersion") == mSponsorsVersion
		&& !mSponsorsVersion.empty())
		return;

	std::string version;
	std::vector<std::string> warnings;
	std::string error;
	if (!storeSponsors(mPaths.sponsorsFolder, response.body, version, warnings, error))
	{
		if (mLog)
			mLog(true, "cannot store the sponsors: " + error);
		return;
	}
	if (mLog)
	{
		for (const auto& w : warnings)
			mLog(true, w);
		mLog(false, "the sponsors " + version + " are stored in " + mPaths.sponsorsFolder);
	}
	mSponsorsVersion = version;
}

void SiteLink::sendOutbox(unsigned long long now_ms)
{
	// one report per match at a time, in order. The next one goes in the next round.
	for (const OutboxEntry& entry : mOutbox.sendable())
	{
		HttpRequest request;
		request.post = true;
		request.path = entry.path;
		request.body = entry.body;
		request.maxBytes = MAX_ANSWER_BYTES;
		HttpResponse response = mHttp.send(request);

		std::string detail;
		const SiteAnswer answer = classifySiteAnswer(entry.kind, response, detail);
		std::string what = "match " + std::to_string(entry.matchId);
		if (entry.kind == "game")
			what += " game " + std::to_string(entry.game);
		else
			what += " connected";

		switch (answer)
		{
			case SiteAnswer::DONE:
				reachable(true, "");
				mOutboxRetry = 0;
				// a closed game did not count on the site
				if (detail != "game-closed")
					mDelivered.push_back(entry);
				mOutbox.remove(entry.seq);
				// read the sync again soon, so the players file catches up
				mSyncAt = std::min(mSyncAt, now_ms + mTiming.firstRetry);
				if (mLog)
					mLog(false, "the site took the report of " + what + ": " + detail);
				break;
			case SiteAnswer::REFUSED:
				reachable(true, "");
				mOutboxRetry = 0;
				mOutbox.remove(entry.seq);
				logRefused(entry, detail);
				if (mLog)
					mLog(true, "the site refused the report of " + what + ": " + detail + ", it is not sent again");
				break;
			case SiteAnswer::AUTH:
				reachable(false, detail == "not-configured"
					? "the site has no game server token yet, make one on the admin page"
					: "the site refused the token (" + detail + "), check BLOBBY_SITE_TOKEN", true);
				mOutboxAt = now_ms + mTiming.authRetry;
				return;
			case SiteAnswer::RETRY:
				reachable(false, "sending " + what + " failed with " + detail);
				mOutboxRetry = nextRetry(mOutboxRetry);
				mOutboxAt = now_ms + mOutboxRetry;
				return;
		}
	}
	mOutboxAt = now_ms;
}

void SiteLink::logRefused(const OutboxEntry& entry, const std::string& reason)
{
	if (mPaths.refusedFile.empty())
		return;
	json line;
	line["at"] = nowText();
	line["reason"] = reason;
	line["kind"] = entry.kind;
	line["path"] = entry.path;
	line["body"] = json::parse(entry.body, nullptr, false);
	std::ofstream file(mPaths.refusedFile, std::ios::out | std::ios::app | std::ios::binary);
	if (file)
		file << line.dump() << "\n";
}

void SiteLink::setLiveScore(const LiveScore& score)
{
	std::lock_guard<std::mutex> lock(mLiveMutex);
	mLive[score.matchId] = score;
}

void SiteLink::clearLiveScore(int matchId)
{
	std::lock_guard<std::mutex> lock(mLiveMutex);
	mLive.erase(matchId);
}

void SiteLink::sendLive(unsigned long long now_ms)
{
	std::vector<LiveScore> due;
	{
		std::lock_guard<std::mutex> lock(mLiveMutex);
		for (auto it = mLive.begin(); it != mLive.end(); )
		{
			auto sent = mLiveSentAt.find(it->first);
			if (sent != mLiveSentAt.end() && now_ms < sent->second + mTiming.liveInterval)
			{
				++it;
				continue;
			}
			due.push_back(it->second);
			it = mLive.erase(it);
		}
	}

	for (const LiveScore& score : due)
	{
		mLiveSentAt[score.matchId] = now_ms;
		// best effort, a live score is dropped while the site is down
		if (mDown)
			continue;
		json body;
		body["matchId"] = score.matchId;
		body["game"] = score.game;
		body["points"] = json::array({
			json{{"playerId", score.player1}, {"points", score.points1}},
			json{{"playerId", score.player2}, {"points", score.points2}}});
		HttpRequest request;
		request.post = true;
		request.path = "/api/server/live";
		request.body = body.dump();
		request.timeoutMs = LIVE_TIMEOUT_MS;
		mHttp.send(request);
	}
}

// - - - - - - - - - - - - - - - - - -
// 			reporter
// - - - - - - - - - - - - - - - - - -

SiteReporter::SiteReporter(std::shared_ptr<TournamentEvents> inner, std::unique_ptr<HttpClient> http,
						std::unique_ptr<Outbox> outbox, SitePaths paths, SiteLink::LogFunction log)
: mInner(std::move(inner))
, mHttp(std::move(http))
, mOutbox(std::move(outbox))
{
	mLink.reset(new SiteLink(*mHttp, *mOutbox, std::move(paths), std::move(log)));
}

SiteReporter::~SiteReporter()
{
	stop();
}

void SiteReporter::start()
{
	if (mRunning)
		return;
	mRunning = true;
	mThread = std::thread([this](){ run(); });
}

void SiteReporter::stop()
{
	if (!mThread.joinable())
		return;
	mRunning = false;
	wake();
	mThread.join();
}

void SiteReporter::wake()
{
	{
		std::lock_guard<std::mutex> lock(mWakeMutex);
		mWoken = true;
	}
	mWakeUp.notify_one();
}

void SiteReporter::run()
{
	auto start = std::chrono::steady_clock::now();
	while (mRunning)
	{
		const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		mLink->step(static_cast<unsigned long long>(now) + 1);

		std::unique_lock<std::mutex> lock(mWakeMutex);
		mWakeUp.wait_for(lock, std::chrono::milliseconds(200), [this](){ return mWoken || !mRunning; });
		mWoken = false;
	}
}

void SiteReporter::matchConnected(int matchId, int player1, int player2)
{
	if (mInner)
		mInner->matchConnected(matchId, player1, player2);
	mOutbox->addConnected(matchId, player1, player2);
	wake();
}

void SiteReporter::gameEnded(const GameReport& report)
{
	if (mInner)
		mInner->gameEnded(report);
	mLink->clearLiveScore(report.matchId);
	mOutbox->addGame(report);
	wake();
}

void SiteReporter::seriesEnded(const SeriesResult& result)
{
	// the site decides the series from the games, so this is only logged
	if (mInner)
		mInner->seriesEnded(result);
}

void SiteReporter::liveScore(int matchId, int game, int player1, int points1, int player2, int points2)
{
	if (mInner)
		mInner->liveScore(matchId, game, player1, points1, player2, points2);
	LiveScore score;
	score.matchId = matchId;
	score.game = game;
	score.player1 = player1;
	score.points1 = points1;
	score.player2 = player2;
	score.points2 = points2;
	mLink->setLiveScore(score);
}
