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
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "SponsorBoard.h"

namespace RakNet
{
	class BitStream;
}

/// Sends the sponsor board of a tournament server to its players over the game connection.
/// The server announces the board style and the hashes of the logos. The player asks for the logos
/// it has not cached, and the server sends each of them once. Only players that announced
/// tournament support get these packets, so casual servers and older games see no change.
namespace sponsors
{
	/// Largest sum of the logo files of one board. A logo that appears twice counts once.
	constexpr std::size_t MAX_TOTAL_SIZE = 768 * 1024;
	/// Ordering channel of the sponsor packets, so logos do not hold up game packets.
	constexpr char NETWORK_CHANNEL = 1;

	/// Second byte of an ID_SPONSORS packet.
	enum class SponsorPacketType : unsigned char
	{
		BOARD,		///< server to player: style and logo hashes
		REQUEST,	///< player to server: logos it needs
		LOGO		///< server to player: one logo file
	};

	struct LogoFile
	{
		std::string hash;	///< SHA-256 of data as lower case hex
		std::string data;	///< the BMP file
	};

	/// A board as a tournament server sends it.
	struct Board
	{
		BoardStyle style = BoardStyle::LED;
		/// in board order, the same logo may appear more than once
		std::vector<LogoFile> logos;
	};

	/// SHA-256 of the style name and the logo hashes in order.
	std::string boardVersion(const std::string& styleName, const std::vector<std::string>& hashes);
	std::string boardVersion(const Board& board);

	/// True if data is a logo the game can show and its SHA-256 is hash.
	bool checkLogo(const std::string& hash, const std::string& data, std::string* error = nullptr);

	/// Drops logos that fail checkLogo, logos after MAX_LOGOS and logos that would go past MAX_TOTAL_SIZE.
	/// Adds a warning for each.
	void limitBoard(Board& board, std::vector<std::string>& warnings);

	/// What the announcement of a board holds.
	struct Announcement
	{
		struct Entry
		{
			std::string hash;
			std::uint32_t size;
		};
		std::string version;
		std::string styleName;
		std::vector<Entry> logos;

		BoardStyle style() const { return parseBoardStyle(styleName); }
		/// the different hashes in board order
		std::vector<std::string> uniqueHashes() const;
	};

	/// Reads the type of an ID_SPONSORS packet whose message id was read.
	bool readPacketType(RakNet::BitStream& stream, SponsorPacketType& type);

	/// Writes a whole BOARD packet.
	void writeAnnouncement(RakNet::BitStream& stream, const Board& board, const std::string& version);
	/// Reads a BOARD packet after its type. False when it is malformed or over the limits.
	bool readAnnouncement(RakNet::BitStream& stream, Announcement& announcement);

	/// Writes a whole REQUEST packet.
	void writeRequest(RakNet::BitStream& stream, const std::string& version, const std::vector<std::string>& hashes);
	/// Reads a REQUEST packet after its type. False when it is malformed or asks for too many logos.
	bool readRequest(RakNet::BitStream& stream, std::string& version, std::vector<std::string>& hashes);

	/// Writes a whole LOGO packet.
	void writeLogo(RakNet::BitStream& stream, const LogoFile& logo);
	/// Reads a LOGO packet after its type. False when it is malformed or too large. The hash is not checked here.
	bool readLogo(RakNet::BitStream& stream, LogoFile& logo);

	/// The board on the server side.
	class BoardSender
	{
		public:
			void setBoard(Board board);
			bool hasBoard() const { return mHasBoard; }
			const std::string& version() const { return mVersion; }
			const Board& board() const { return mBoard; }

			void writeAnnouncement(RakNet::BitStream& stream) const;

			/// The logos a REQUEST packet asks for, read after its type. A request for another version
			/// gets nothing, since a new announcement is on its way. sent holds the logos this player
			/// got for the current version, so each one is sent at most once.
			std::vector<const LogoFile*> answer(RakNet::BitStream& request, std::set<std::string>& sent) const;

		private:
			bool mHasBoard = false;
			Board mBoard;
			std::string mVersion;
	};

	/// Where a player keeps the logos it got from a server, so they survive a restart.
	class LogoCache
	{
		public:
			virtual ~LogoCache() = default;
			/// false when the logo is not cached
			virtual bool load(const std::string& hash, std::string& data) = 0;
			virtual void store(const std::string& hash, const std::string& data) = 0;
			/// removes the cached logos that are not in keep
			virtual void keepOnly(const std::set<std::string>& keep) = 0;
	};

	/// Keeps the logos as <directory>/<hash>.bmp in the PhysFS write folder, the user folder of the game.
	class FileLogoCache : public LogoCache
	{
		public:
			explicit FileLogoCache(std::string directory = "sponsors/cache");
			bool load(const std::string& hash, std::string& data) override;
			void store(const std::string& hash, const std::string& data) override;
			void keepOnly(const std::set<std::string>& keep) override;

		private:
			std::string path(const std::string& hash) const;
			std::string mDirectory;
	};

	/// The board on the player side. It takes logos from the cache and asks the server for the others.
	class BoardReceiver
	{
		public:
			enum class Result
			{
				NOTHING,	///< nothing to do
				REQUEST,	///< send the answer to the server
				READY		///< the board is complete, show it
			};

			explicit BoardReceiver(LogoCache& cache);

			/// Handles an ID_SPONSORS packet whose message id was read. For REQUEST, answer holds the packet to send.
			/// A board with the version of the current one is ignored.
			Result handle(RakNet::BitStream& packet, RakNet::BitStream& answer);

			bool ready() const { return mReady; }
			BoardStyle style() const { return mAnnouncement.style(); }
			/// the logo files in board order, once ready
			std::vector<std::vector<char>> logoFiles() const;
			/// a problem with the last packet, for the log
			const std::string& problem() const { return mProblem; }

			void reset();

		private:
			Result announce(RakNet::BitStream& packet, RakNet::BitStream& answer);
			Result receiveLogo(RakNet::BitStream& packet);
			Result finish();

			LogoCache& mCache;
			bool mAnnounced = false;
			bool mReady = false;
			Announcement mAnnouncement;
			std::map<std::string, std::string> mLogos;
			std::string mProblem;
	};
}
