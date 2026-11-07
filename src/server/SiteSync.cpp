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
#include "SponsorBoard.h"
#include "base64.h"

using nlohmann::json;

namespace
{
	// largest answers that are read
	const std::size_t MAX_SYNC_BYTES = 1024 * 1024;
	const std::size_t MAX_SPONSORS_BYTES = 8 * 1024 * 1024;
	const std::size_t MAX_ANSWER_BYTES = 64 * 1024;
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
		auto delivered = line.find("delivered");
		entry.delivered = delivered != line.end() && delivered->is_boolean() && delivered->get<bool>();
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
		if (entry.delivered)
			line["delivered"] = true;
		return line;
	}
}

// - - - - - - - - - - - - - - - - - -
// 			outbox
// - - - - - - - - - - - - - - - - - -

HttpClient::~HttpClient() = default;

Outbox::Outbox(std::string path, LogFunction log, std::size_t maxBytes)
: mPath(std::move(path))
, mLog(std::move(log))
, mMaxBytes(maxBytes)
{
}

void Outbox::load()
{
	std::lock_guard<std::mutex> lock(mMutex);
	if (mPath.empty())
		return;
	std::string content;
	if (!readWholeFile(mPath, mMaxBytes, content))
	{
		// move it away before anything is saved, so the reports in it are not overwritten
		if (fileExists(mPath))
		{
			const bool moved = renameFile(mPath, mPath + ".bad");
			if (mLog)
				mLog(true, "cannot read the outbox " + mPath + ", it is too large or not readable. "
					+ (moved ? "It is kept as " + mPath + ".bad, send its reports by hand"
							: std::string("It could not be moved, new reports stay in memory")));
			if (!moved)
				mPath.clear();
		}
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
		mLog(false, std::to_string(mEntries.size()) + " reports wait in the outbox or for a sync that shows them");
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

void Outbox::withEntries(const std::function<void(const std::vector<OutboxEntry>&)>& use) const
{
	std::lock_guard<std::mutex> lock(mMutex);
	use(mEntries);
}

std::vector<OutboxEntry> Outbox::sendable() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	std::vector<OutboxEntry> result;
	std::set<int> seen;
	for (const auto& e : mEntries)
	{
		if (!e.delivered && seen.insert(e.matchId).second)
			result.push_back(e);
	}
	return result;
}

void Outbox::markDelivered(unsigned long long seq)
{
	std::lock_guard<std::mutex> lock(mMutex);
	auto it = std::find_if(mEntries.begin(), mEntries.end(), [seq](const OutboxEntry& e){ return e.seq == seq; });
	if (it == mEntries.end())
		return;
	// only a game changes the players file
	if (it->kind == "game")
		it->delivered = true;
	else
		mEntries.erase(it);
	++mGeneration;
	save();
}

bool Outbox::removeSynced(const std::map<int, int>& nextGames)
{
	std::lock_guard<std::mutex> lock(mMutex);
	const std::size_t before = mEntries.size();
	mEntries.erase(std::remove_if(mEntries.begin(), mEntries.end(), [&](const OutboxEntry& e){
		if (!e.delivered)
			return false;
		auto found = nextGames.find(e.matchId);
		return found == nextGames.end() || found->second > e.game;
	}), mEntries.end());
	if (mEntries.size() == before)
		return false;
	++mGeneration;
	save();
	return true;
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
	return static_cast<std::size_t>(std::count_if(mEntries.begin(), mEntries.end(),
		[](const OutboxEntry& e){ return !e.delivered; }));
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
		// probably not the site, such as a proxy page, so try again later
		detail = "an answer that cannot be read";
		return SiteAnswer::RETRY;
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

std::string sha256Hex(const std::string& data)
{
	static const std::uint32_t K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
	std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	auto rotr = [](std::uint32_t x, int n){ return (x >> n) | (x << (32 - n)); };

	std::string msg = data;
	const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
	msg.push_back(static_cast<char>(0x80));
	while (msg.size() % 64 != 56)
		msg.push_back(0);
	for (int i = 7; i >= 0; --i)
		msg.push_back(static_cast<char>((bits >> (8 * i)) & 0xff));

	for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64)
	{
		std::uint32_t w[64];
		for (int i = 0; i < 16; ++i)
		{
			const unsigned char* b = reinterpret_cast<const unsigned char*>(msg.data() + chunk + 4 * i);
			w[i] = (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | std::uint32_t(b[3]);
		}
		for (int i = 16; i < 64; ++i)
		{
			const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
		for (int i = 0; i < 64; ++i)
		{
			const std::uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
			const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			k = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
		h[5] += f;
		h[6] += g;
		h[7] += k;
	}

	static const char* const HEX = "0123456789abcdef";
	std::string out;
	for (std::uint32_t v : h)
	{
		for (int i = 28; i >= 0; i -= 4)
			out.push_back(HEX[(v >> i) & 0xf]);
	}
	return out;
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
			if (data.size() > sponsors::MAX_FILE_SIZE * 4 / 3 + 4)
			{
				warnings.push_back(where + " has a logo that is too large, skipped");
				continue;
			}
			std::vector<uint8_t> decoded = decode(data);
			const std::string bytes(decoded.begin(), decoded.end());
			if (sha256Hex(bytes) != hash)
			{
				warnings.push_back(where + " has a logo that does not match its hash, skipped");
				continue;
			}
			// the same check as the game, so only a logo it can show is kept
			std::string bmpError;
			if (!sponsors::validateBmp(bytes.data(), bytes.size(), &bmpError))
			{
				warnings.push_back(where + " has a logo the game cannot show: " + bmpError + ", skipped");
				continue;
			}
			std::string writeError;
			if (!writeFileAtomically(path, bytes, writeError))
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

	// logos that the list no longer names
	std::set<std::string> kept;
	for (const auto& entry : board["sponsors"])
		kept.insert(entry["logo"].get<std::string>().substr(6));
	std::vector<std::string> names;
	if (listFolder(logos, names))
	{
		for (const auto& name : names)
		{
			const bool logoFile = name.size() == 68 && isHash(name.substr(0, 64)) && name.substr(64) == ".bmp";
			if (logoFile && kept.count(name) == 0)
				std::remove(pathInDataDir(logos, name).c_str());
		}
	}
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
	// the players file also reflects the reports in the outbox, and a failed write is tried again
	auto writeIfNeeded = [this](){
		if (!mSyncJson.empty() && (!mWritten || mSyncVersion != mWrittenVersion || mOutbox.generation() != mWrittenGeneration))
			writePlayersFile();
	};
	writeIfNeeded();
	if (!stopping() && now_ms >= mSyncAt)
		sync(now_ms);
	if (!stopping() && !mPaths.sponsorsFolder.empty() && now_ms >= mSponsorsAt)
		sponsors(now_ms);
	if (!stopping() && now_ms >= mOutboxAt && mOutbox.size() > 0)
		sendOutbox(now_ms);
	writeIfNeeded();
	if (!stopping())
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
	mOutbox.removeSynced(syncNextGames(response.body));

	if (version != mSyncVersion && mLog)
	{
		for (const auto& w : warnings)
			mLog(true, w);
	}
	// kept on disk, so a restart during an outage still knows the matches
	if (!mPaths.syncFile.empty() && version != mSavedSyncVersion)
	{
		if (writeFileAtomically(mPaths.syncFile, response.body, error))
			mSavedSyncVersion = version;
		else if (mLog)
			mLog(true, "cannot save the sync: " + error);
	}
	mSyncJson = response.body;
	mSyncVersion = version;
	// the version only counts as written once the players file is on disk
	if (version == mWrittenVersion && mWritten && mOutbox.generation() == mWrittenGeneration)
		return;
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
	mSavedSyncVersion = version;
}

bool SiteLink::writePlayersFile()
{
	bool ok = false;
	// no report is added or removed meanwhile, so the file matches the outbox it was made from.
	// This only writes a local file, the lock is never held during a request.
	mOutbox.withEntries([&](const std::vector<OutboxEntry>& entries){
		const unsigned long long generation = mOutbox.generationLocked();
		std::string xml;
		std::string version;
		std::vector<std::string> warnings;
		std::string error;
		if (!buildPlayersFile(mSyncJson, entries, xml, version, warnings, error))
			return;
		if (!mWritten || xml != mWrittenXml)
		{
			if (!writeFileAtomically(mPaths.playersFile, xml, error))
			{
				// log once, the next round tries again
				if (mLog && error != mWriteError)
					mLog(true, "cannot write the players file: " + error + ", trying again");
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
		ok = true;
	});
	return ok;
}

void SiteLink::sponsors(unsigned long long now_ms)
{
	if (!mSponsorsLoaded)
	{
		mSponsorsVersion = storedSponsorsVersion(mPaths.sponsorsFolder);
		mSponsorsLoaded = true;
	}
	// sponsors are read once a minute, the answer says whether they changed
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
		if (stopping())
			return;
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
				// a closed game did not count on the site, a stored game counts until a sync shows it
				if (detail == "game-closed")
					mOutbox.remove(entry.seq);
				else
					mOutbox.markDelivered(entry.seq);
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
		if (stopping())
			return;
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
	mLogLine = log;
	mHttp->setCancelFlag(&mStopping);
	mLink.reset(new SiteLink(*mHttp, *mOutbox, std::move(paths), std::move(log)));
	mLink->setStopFlag(&mStopping);
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
	// a running request is cut off, its report stays in the outbox
	mStopping = true;
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
	unsigned failures = 0;
	while (mRunning)
	{
		const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		std::string problem;
		try
		{
			mLink->step(static_cast<unsigned long long>(now) + 1);
		}
		catch (const std::exception& e)
		{
			problem = e.what();
		}
		catch (...)
		{
			problem = "an unknown error";
		}

		unsigned pause = 200;
		if (problem.empty())
		{
			failures = 0;
		}
		else
		{
			// keep going, but slower, so a repeating error does not spin
			++failures;
			pause = std::min(30000u, 1000u << std::min(failures, 5u));
			if (mLogLine)
				mLogLine(true, "the tournament site link failed with " + problem + ", trying again in "
					+ std::to_string(pause / 1000) + " s");
		}

		std::unique_lock<std::mutex> lock(mWakeMutex);
		mWakeUp.wait_for(lock, std::chrono::milliseconds(pause), [&](){ return (mWoken && failures == 0) || !mRunning; });
		mWoken = false;
	}
}

void SiteReporter::matchConnected(int matchId, int player1, int player2)
{
	// the outbox first, it is what reaches the site
	mOutbox->addConnected(matchId, player1, player2);
	if (mInner)
		mInner->matchConnected(matchId, player1, player2);
	wake();
}

void SiteReporter::gameEnded(const GameReport& report)
{
	// the outbox first, it is what reaches the site
	mOutbox->addGame(report);
	mLink->clearLiveScore(report.matchId);
	if (mInner)
		mInner->gameEnded(report);
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
