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

#include "SponsorSync.h"

#include <algorithm>
#include <iostream>
#include <utility>

#include "raknet/BitStream.h"

#include "FileRead.h"
#include "FileSystem.h"
#include "FileWrite.h"
#include "NetworkMessage.h"
#include "Sha256.h"

namespace sponsors
{

namespace
{
	const int HASH_LENGTH = 64;
	const std::size_t MAX_STYLE_NAME = 32;

	void writeHash(RakNet::BitStream& stream, const std::string& hash)
	{
		std::string fixed = hash;
		fixed.resize(HASH_LENGTH, '0');
		stream.Write(fixed.data(), HASH_LENGTH);
	}

	bool readHash(RakNet::BitStream& stream, std::string& hash)
	{
		char buffer[HASH_LENGTH];
		if(!stream.Read(buffer, HASH_LENGTH))
			return false;
		hash.assign(buffer, HASH_LENGTH);
		return isSha256Hex(hash);
	}

	void writeHeader(RakNet::BitStream& stream, SponsorPacketType type)
	{
		stream.Write((unsigned char)ID_SPONSORS);
		stream.Write((unsigned char)type);
	}

	std::size_t unreadBytes(const RakNet::BitStream& stream)
	{
		const int bits = stream.GetNumberOfUnreadBits();
		return bits > 0 ? std::size_t(bits) / 8 : 0;
	}
}

std::string boardVersion(const std::string& styleName, const std::vector<std::string>& hashes)
{
	std::string text = styleName;
	for(const auto& hash : hashes)
		text += "\n" + hash;
	return sha256Hex(text);
}

std::string boardVersion(const Board& board)
{
	std::vector<std::string> hashes;
	for(const auto& logo : board.logos)
		hashes.push_back(logo.hash);
	return boardVersion(boardStyleName(board.style), hashes);
}

std::string logoFileName(const std::string& hash)
{
	return hash + ".bmp";
}

bool isLogoFileName(const std::string& name)
{
	return name.size() == std::size_t(HASH_LENGTH) + 4 && isSha256Hex(name.substr(0, HASH_LENGTH)) && name.compare(HASH_LENGTH, 4, ".bmp") == 0;
}

bool checkLogo(const std::string& hash, const std::string& data, std::string* error)
{
	if(!isSha256Hex(hash))
	{
		if(error)
			*error = "the hash is not a SHA-256";
		return false;
	}
	if(data.size() > MAX_FILE_SIZE)
	{
		if(error)
			*error = "the file is too large";
		return false;
	}
	if(sha256Hex(data) != hash)
	{
		if(error)
			*error = "the file does not match its hash";
		return false;
	}
	return validateBmp(data.data(), data.size(), error);
}

void limitBoard(Board& board, std::vector<std::string>& warnings)
{
	std::vector<LogoFile> kept;
	std::set<std::string> counted;
	std::size_t total = 0;
	int index = 0;
	for(auto& logo : board.logos)
	{
		++index;
		const std::string where = "logo " + std::to_string(index);
		if(kept.size() >= MAX_LOGOS)
		{
			warnings.push_back(where + " is skipped, a board shows at most " + std::to_string(MAX_LOGOS) + " logos");
			continue;
		}
		std::string error;
		if(!checkLogo(logo.hash, logo.data, &error))
		{
			warnings.push_back(where + " is skipped: " + error);
			continue;
		}
		if(counted.count(logo.hash) == 0)
		{
			if(total + logo.data.size() > MAX_TOTAL_SIZE)
			{
				warnings.push_back(where + " is skipped, the logos of a board may have at most "
				                   + std::to_string(MAX_TOTAL_SIZE / 1024) + " KB together");
				continue;
			}
			total += logo.data.size();
			counted.insert(logo.hash);
		}
		kept.push_back(std::move(logo));
	}
	board.logos.swap(kept);
}

std::vector<std::string> Announcement::uniqueHashes() const
{
	std::vector<std::string> hashes;
	std::set<std::string> seen;
	for(const auto& entry : logos)
	{
		if(seen.insert(entry.hash).second)
			hashes.push_back(entry.hash);
	}
	return hashes;
}

bool readPacketType(RakNet::BitStream& stream, SponsorPacketType& type)
{
	unsigned char value = 0;
	if(!stream.Read(value) || value > (unsigned char)SponsorPacketType::LOGO)
		return false;
	type = (SponsorPacketType)value;
	return true;
}

void writeAnnouncement(RakNet::BitStream& stream, const Board& board, const std::string& version)
{
	writeHeader(stream, SponsorPacketType::BOARD);
	writeHash(stream, version);
	const std::string style = boardStyleName(board.style);
	stream.Write((unsigned char)style.size());
	stream.Write(style.data(), (int)style.size());
	stream.Write((unsigned char)board.logos.size());
	for(const auto& logo : board.logos)
	{
		writeHash(stream, logo.hash);
		stream.Write((unsigned int)logo.data.size());
	}
}

bool readAnnouncement(RakNet::BitStream& stream, Announcement& announcement)
{
	Announcement result;
	if(!readHash(stream, result.version))
		return false;

	unsigned char styleLength = 0;
	if(!stream.Read(styleLength) || styleLength == 0 || styleLength > MAX_STYLE_NAME)
		return false;
	char style[MAX_STYLE_NAME];
	if(!stream.Read(style, styleLength))
		return false;
	result.styleName.assign(style, styleLength);

	unsigned char count = 0;
	if(!stream.Read(count) || count > MAX_LOGOS)
		return false;

	std::map<std::string, std::uint32_t> sizes;
	std::size_t total = 0;
	for(unsigned i = 0; i < count; ++i)
	{
		Announcement::Entry entry;
		unsigned int size = 0;
		if(!readHash(stream, entry.hash) || !stream.Read(size))
			return false;
		entry.size = size;
		if(entry.size == 0 || entry.size > MAX_FILE_SIZE)
			return false;
		auto known = sizes.find(entry.hash);
		if(known == sizes.end())
		{
			sizes[entry.hash] = entry.size;
			total += entry.size;
			if(total > MAX_TOTAL_SIZE)
				return false;
		}
		else if(known->second != entry.size)
		{
			return false;
		}
		result.logos.push_back(entry);
	}

	announcement = std::move(result);
	return true;
}

void writeRequest(RakNet::BitStream& stream, const std::string& version, const std::vector<std::string>& hashes)
{
	writeHeader(stream, SponsorPacketType::REQUEST);
	writeHash(stream, version);
	stream.Write((unsigned char)hashes.size());
	for(const auto& hash : hashes)
		writeHash(stream, hash);
}

bool readRequest(RakNet::BitStream& stream, std::string& version, std::vector<std::string>& hashes)
{
	std::string readVersion;
	if(!readHash(stream, readVersion))
		return false;
	unsigned char count = 0;
	if(!stream.Read(count) || count > MAX_LOGOS)
		return false;
	std::vector<std::string> result;
	for(unsigned i = 0; i < count; ++i)
	{
		std::string hash;
		if(!readHash(stream, hash))
			return false;
		result.push_back(hash);
	}
	version = readVersion;
	hashes.swap(result);
	return true;
}

void writeLogo(RakNet::BitStream& stream, const LogoFile& logo)
{
	writeHeader(stream, SponsorPacketType::LOGO);
	writeHash(stream, logo.hash);
	stream.Write((unsigned int)logo.data.size());
	stream.Write(logo.data.data(), (int)logo.data.size());
}

bool readLogo(RakNet::BitStream& stream, LogoFile& logo)
{
	LogoFile result;
	unsigned int size = 0;
	if(!readHash(stream, result.hash) || !stream.Read(size))
		return false;
	if(size == 0 || size > MAX_FILE_SIZE || size > unreadBytes(stream))
		return false;
	result.data.resize(size);
	if(!stream.Read(&result.data[0], (int)size))
		return false;
	logo = std::move(result);
	return true;
}

// - - - - - - - - - - - - - - - - - -
// 			server side
// - - - - - - - - - - - - - - - - - -

void BoardSender::setBoard(Board board)
{
	mBoard = std::move(board);
	mVersion = boardVersion(mBoard);
	mHasBoard = true;
}

void BoardSender::writeAnnouncement(RakNet::BitStream& stream) const
{
	sponsors::writeAnnouncement(stream, mBoard, mVersion);
}

std::vector<const LogoFile*> BoardSender::answer(RakNet::BitStream& request, std::set<std::string>& sent) const
{
	std::vector<const LogoFile*> result;
	std::string version;
	std::vector<std::string> hashes;
	if(!mHasBoard || !readRequest(request, version, hashes) || version != mVersion)
		return result;

	for(const auto& hash : hashes)
	{
		if(sent.count(hash) != 0)
			continue;
		for(const auto& logo : mBoard.logos)
		{
			if(logo.hash == hash)
			{
				sent.insert(hash);
				result.push_back(&logo);
				break;
			}
		}
	}
	return result;
}

// - - - - - - - - - - - - - - - - - -
// 			logo cache
// - - - - - - - - - - - - - - - - - -

FileLogoCache::FileLogoCache(std::string directory) : mDirectory(std::move(directory))
{
}

std::string FileLogoCache::path(const std::string& hash) const
{
	return mDirectory + "/" + logoFileName(hash);
}

bool FileLogoCache::load(const std::string& hash, std::string& data)
{
	if(!isSha256Hex(hash))
		return false;
	try
	{
		if(!FileSystem::getSingleton().exists(path(hash)))
			return false;
		FileRead file(path(hash));
		const std::uint32_t length = file.length();
		if(length == 0 || length > MAX_FILE_SIZE)
			return false;
		std::vector<char> content = file.readRawBytes(length);
		data.assign(content.begin(), content.end());
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}

void FileLogoCache::store(const std::string& hash, const std::string& data)
{
	if(!isSha256Hex(hash))
		return;
	try
	{
		FileSystem& fs = FileSystem::getSingleton();
		if(!fs.isDirectory(mDirectory))
			fs.mkdir(mDirectory);
		FileWrite file(path(hash));
		file.write(data.data(), data.size());
		file.close();
	}
	catch(const std::exception& e)
	{
		std::cerr << "Warning: cannot cache a sponsor logo: " << e.what() << std::endl;
	}
}

void FileLogoCache::keepOnly(const std::set<std::string>& keep)
{
	try
	{
		FileSystem& fs = FileSystem::getSingleton();
		if(!fs.isDirectory(mDirectory))
			return;
		for(const auto& name : fs.enumerateFiles(mDirectory, ".bmp"))
		{
			if(isSha256Hex(name) && keep.count(name) == 0)
				fs.deleteFile(path(name));
		}
	}
	catch(const std::exception&)
	{
		// a cache that cannot be cleaned up still works
	}
}

// - - - - - - - - - - - - - - - - - -
// 			player side
// - - - - - - - - - - - - - - - - - -

BoardReceiver::BoardReceiver(LogoCache& cache) : mCache(cache)
{
}

void BoardReceiver::reset()
{
	mAnnounced = false;
	mReady = false;
	mAnnouncement = Announcement();
	mLogos.clear();
	mProblem.clear();
}

BoardReceiver::Result BoardReceiver::handle(RakNet::BitStream& packet, RakNet::BitStream& answer)
{
	mProblem.clear();
	SponsorPacketType type;
	if(!readPacketType(packet, type))
	{
		mProblem = "unknown sponsor packet";
		return Result::NOTHING;
	}
	switch(type)
	{
		case SponsorPacketType::BOARD:
			return announce(packet, answer);
		case SponsorPacketType::LOGO:
			return receiveLogo(packet);
		default:
			return Result::NOTHING;
	}
}

BoardReceiver::Result BoardReceiver::announce(RakNet::BitStream& packet, RakNet::BitStream& answer)
{
	Announcement announcement;
	if(!readAnnouncement(packet, announcement))
	{
		// the board shown so far stays
		mProblem = "the sponsor board from the server is not valid";
		return Result::NOTHING;
	}

	// the same board again changes nothing, the logos are shown or on their way
	if(mAnnounced && announcement.version == mAnnouncement.version)
		return Result::NOTHING;

	reset();
	mAnnounced = true;
	mAnnouncement = std::move(announcement);

	// the cache holds at most the logos of one board, also when a board never completes
	const std::vector<std::string> hashes = mAnnouncement.uniqueHashes();
	mCache.keepOnly(std::set<std::string>(hashes.begin(), hashes.end()));

	std::vector<std::string> missing;
	for(const auto& hash : hashes)
	{
		std::uint32_t size = 0;
		for(const auto& entry : mAnnouncement.logos)
		{
			if(entry.hash == hash)
				size = entry.size;
		}
		std::string data;
		if(mCache.load(hash, data) && data.size() == size && checkLogo(hash, data))
			mLogos[hash] = std::move(data);
		else
			missing.push_back(hash);
	}

	if(missing.empty())
		return finish();

	writeRequest(answer, mAnnouncement.version, missing);
	return Result::REQUEST;
}

BoardReceiver::Result BoardReceiver::receiveLogo(RakNet::BitStream& packet)
{
	LogoFile logo;
	if(!readLogo(packet, logo))
	{
		mProblem = "a sponsor logo from the server is not valid";
		return Result::NOTHING;
	}
	if(!mAnnounced || mReady || mLogos.count(logo.hash) != 0)
		return Result::NOTHING;

	bool announced = false;
	for(const auto& entry : mAnnouncement.logos)
	{
		if(entry.hash == logo.hash)
		{
			announced = entry.size == logo.data.size();
			break;
		}
	}
	if(!announced)
	{
		mProblem = "the server sent a sponsor logo that is not on the board";
		return Result::NOTHING;
	}

	std::string error;
	if(!checkLogo(logo.hash, logo.data, &error))
	{
		mProblem = "a sponsor logo from the server is skipped: " + error;
		return Result::NOTHING;
	}

	mCache.store(logo.hash, logo.data);
	mLogos[logo.hash] = std::move(logo.data);
	for(const auto& entry : mAnnouncement.logos)
	{
		if(mLogos.count(entry.hash) == 0)
			return Result::NOTHING;
	}
	return finish();
}

BoardReceiver::Result BoardReceiver::finish()
{
	mReady = true;
	// logos of older boards are not needed anymore
	std::set<std::string> keep;
	for(const auto& logo : mLogos)
		keep.insert(logo.first);
	mCache.keepOnly(keep);
	return Result::READY;
}

std::vector<std::vector<char>> BoardReceiver::logoFiles() const
{
	std::vector<std::vector<char>> files;
	if(!mReady)
		return files;
	for(const auto& entry : mAnnouncement.logos)
	{
		const std::string& data = mLogos.at(entry.hash);
		files.emplace_back(data.begin(), data.end());
	}
	return files;
}

}
