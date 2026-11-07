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
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "raknet/NetworkTypes.h"
#include "MatchAssignments.h"
#include "ServerLog.h"
#include "TournamentProtocol.h"

class NetworkPlayer;

/// Largest players file the loader reads.
constexpr std::size_t MAX_PLAYERS_FILE_BYTES = 1024 * 1024;
/// A join code that moved to a new connection stays there at least this long.
constexpr unsigned long long JOIN_CODE_HANDOVER_COOLDOWN_MS = 10000;

/// A short part of a name or code that is safe to log. It never holds the whole code.
std::string joinCodeLogPrefix(const std::string& raw);

/// One player of the tournament.
struct JoinCodeEntry
{
	std::string code;
	std::string nickname;
	/// Player id on the tournament site, 0 when the file does not give one.
	int playerId = 0;
};

/*! \class JoinCodeTable
	\brief Maps join codes to tournament players.
	\details The table is read from an XML file of this form:
	\code
	<tournament>
		<player id="12" nickname="Ola" code="K7QM2XPR4T"/>
	</tournament>
	\endcode
	Entries with a bad code or nickname are skipped. A code that appears more than
	once is dropped for every entry that uses it, since it is not clear who it belongs to.
	The same goes for a player id.
	Other elements below the root are ignored, so later versions can add to the file.
	The match elements of the same file are read by parseMatchAssignments().
*/
class JoinCodeTable
{
	public:
		/// Parses the XML text. Returns false and sets error when the text is not a players file.
		/// Problems with single entries are added to warnings.
		bool parse(const std::string& xml, std::vector<std::string>& warnings, std::string& error);

		/// Looks up a normalized code. Returns nullptr for an unknown code.
		const JoinCodeEntry* find(const std::string& code) const;

		/// Looks up a player by the player id of the site. Returns nullptr for an unknown id or 0.
		const JoinCodeEntry* findById(int playerId) const;

		/// All player ids in the table, without 0.
		std::set<int> playerIds() const;

		std::size_t size() const;

	private:
		std::map<std::string, JoinCodeEntry> mEntries;
};

/// Reads a whole file. Returns false and sets error when it cannot be read or is too large.
bool readJoinCodeFile(const std::string& path, std::string& content, std::string& error);

/*! \class JoinCodeGate
	\brief Admits players to a tournament server by join code.
	\details Owns the current table, reloads it when the file changes and remembers
	which codes are in use by connected players. A failed reload keeps the last good table.
*/
class JoinCodeGate
{
	public:
		enum class Result
		{
			ACCEPTED,
			UNKNOWN_CODE,
			CODE_IN_USE
		};

		/// \param path players file. A relative path is relative to the working directory.
		/// \param reload_interval_ms how often poll() checks the file
		explicit JoinCodeGate(std::string path, unsigned reload_interval_ms = 2000);

		void setLogFunction(ServerLog log);

		/// Reads the file now if its content changed since the last read.
		/// Returns true if a new table is in use.
		bool reload();

		/// Calls reload() when the reload interval has passed. now_ms is any monotonic time in milliseconds.
		void poll(unsigned long long now_ms);

		/// Uses the given XML text as the file content. Returns true if a new table is in use.
		bool applyContent(const std::string& xml);

		/// Checks the join code in name and, if it is accepted, marks it as in use by player.
		Result admit(const PlayerID& player, const std::string& name, std::string& nickname);

		/// Checks the name of player as a join code and replaces it with the nickname when accepted.
		Result admit(NetworkPlayer& player);

		enum class Handover
		{
			/// Nobody else holds a known code, admit decides.
			NONE,
			/// holder has the code and has to leave before player is admitted.
			HAND_OVER,
			/// The code moved less than JOIN_CODE_HANDOVER_COOLDOWN_MS ago, refuse player.
			TOO_SOON,
			/// The connection that holds the code cannot leave now, refuse player.
			HOLDER_BUSY
		};

		/// Tells whether the connection that holds a code may lose it now.
		typedef std::function<bool(const PlayerID&)> HolderCheck;

		/// Decides whether the known join code in name moves from the connection that holds it to player.
		/// HAND_OVER sets holder and starts the cooldown of the code. now_ms is the clock poll() uses.
		/// holderCanLeave may keep the code with its holder, an empty check always lets it go.
		Handover checkHandover(const PlayerID& player, const std::string& name, unsigned long long now_ms,
							const HolderCheck& holderCanLeave, PlayerID& holder);

		/// Frees the code of a player that left. Does nothing for an unknown player.
		void release(const PlayerID& player);

		const JoinCodeTable& getTable() const;
		const std::string& getPath() const;

		/// Matches of the players file that was read last.
		const std::vector<MatchAssignment>& getMatches() const;
		/// Grows by one each time a new players file is in use.
		unsigned getGeneration() const;

		/// Player id of a connected player, 0 when the player has no code or no id.
		int playerIdOf(const PlayerID& player) const;
		/// Finds the connection of the player with this player id. Returns false when the player is not connected.
		bool findConnection(int playerId, PlayerID& player) const;

	private:
		void log(bool problem, const std::string& message) const;

		std::string mPath;
		unsigned mReloadIntervalMs;
		unsigned long long mLastPollMs = 0;
		bool mPolled = false;

		JoinCodeTable mTable;
		std::vector<MatchAssignment> mMatches;
		unsigned mGeneration = 0;
		std::string mLastContent;
		std::string mLastError;
		bool mHasContent = false;

		std::map<std::string, PlayerID> mCodeOwners;
		// when each code last moved to a new connection
		std::map<std::string, unsigned long long> mHandedOverAt;
		std::map<PlayerID, std::string> mPlayerCodes;

		ServerLog mLog;
};
