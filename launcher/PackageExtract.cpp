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

#include "PackageExtract.h"

#include <memory>

#include <archive.h>
#include <archive_entry.h>

#include "FsUtil.h"

namespace
{
	struct ReaderDeleter
	{
		void operator()(archive* a) const { archive_read_free(a); }
	};
	struct WriterDeleter
	{
		void operator()(archive* a) const { archive_write_free(a); }
	};

	std::string archiveError(archive* a, const std::string& what)
	{
		const char* text = archive_error_string(a);
		return what + (text ? std::string(": ") + text : std::string());
	}
}

bool isSafeArchivePath(const std::string& path)
{
	if (path.empty() || path.size() > 1024 || path[0] == '/')
		return false;
	std::string part;
	for (std::size_t i = 0; i <= path.size(); ++i)
	{
		if (i == path.size() || path[i] == '/')
		{
			// a slash at the end marks a folder
			if (i == path.size() && part.empty() && i > 0)
				break;
			if (part.empty() || part == "." || part == "..")
				return false;
#ifdef _WIN32
			if (isUnsafeWindowsName(part))
				return false;
#endif
			part.clear();
			continue;
		}
		const unsigned char c = (unsigned char)path[i];
		if (c < 0x20 || c == 0x7F || c == '\\' || c == ':')
			return false;
		part += (char)c;
	}
	return true;
}

bool isUnsafeWindowsName(const std::string& part)
{
	if (part.empty())
		return false;
	if (part.back() == '.' || part.back() == ' ')
		return true;
	// Windows ignores the extension and spaces before it when it looks for a device name
	std::string base = part.substr(0, part.find('.'));
	while (!base.empty() && base.back() == ' ')
		base.pop_back();
	for (char& c : base)
	{
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
	}
	if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL")
		return true;
	return base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) &&
		   base[3] >= '1' && base[3] <= '9';
}

bool extractPackage(const std::string& data, PackageFormat format, const std::string& destination, std::string& error,
					const ExtractLimits& limits)
{
	std::unique_ptr<archive, ReaderDeleter> reader(archive_read_new());
	std::unique_ptr<archive, WriterDeleter> writer(archive_write_disk_new());
	if (!reader || !writer)
	{
		error = "out of memory";
		return false;
	}
	if (format == PackageFormat::ZIP)
	{
		archive_read_support_format_zip(reader.get());
	}
	else
	{
		archive_read_support_filter_gzip(reader.get());
		archive_read_support_format_tar(reader.get());
	}
	// the paths are checked below as well, these are a second line of defence
	archive_write_disk_set_options(writer.get(), ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT
		| ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_NO_OVERWRITE);

	if (archive_read_open_memory(reader.get(), data.data(), data.size()) != ARCHIVE_OK)
	{
		error = archiveError(reader.get(), "cannot open the package");
		return false;
	}

	uint64_t entries = 0;
	uint64_t total = 0;
	uint64_t unpacked = 0;
	for (;;)
	{
		archive_entry* entry = nullptr;
		const int result = archive_read_next_header(reader.get(), &entry);
		if (result == ARCHIVE_EOF)
			break;
		if (result != ARCHIVE_OK)
		{
			error = archiveError(reader.get(), "the package is damaged");
			return false;
		}
		if (++entries > limits.maxEntries)
		{
			error = "the package has too many files";
			return false;
		}

		const char* name = archive_entry_pathname_utf8(entry);
		if (!name)
			name = archive_entry_pathname(entry);
		const std::string path = name ? name : "";
		if (!isSafeArchivePath(path))
		{
			error = "the package holds a file with a path that is not allowed: " + path;
			return false;
		}
		const auto type = archive_entry_filetype(entry);
		if ((type != AE_IFREG && type != AE_IFDIR) || archive_entry_hardlink(entry) || archive_entry_symlink(entry))
		{
			error = "the package holds something that is not a plain file or folder: " + path;
			return false;
		}
		if (archive_entry_size_is_set(entry) && archive_entry_size(entry) > 0)
		{
			total += (uint64_t)archive_entry_size(entry);
			if (total > limits.maxTotalBytes)
			{
				error = "the package unpacks to too much data";
				return false;
			}
		}

		const int mode = archive_entry_perm(entry);
		archive_entry_set_perm(entry, type == AE_IFDIR || (mode & 0111) ? 0755 : 0644);
		archive_entry_update_pathname_utf8(entry, fsu::join(destination, path).c_str());

		if (archive_write_header(writer.get(), entry) != ARCHIVE_OK)
		{
			error = archiveError(writer.get(), "cannot write " + path);
			return false;
		}
		if (type == AE_IFREG)
		{
			for (;;)
			{
				const void* block = nullptr;
				size_t size = 0;
				la_int64_t offset = 0;
				const int read = archive_read_data_block(reader.get(), &block, &size, &offset);
				if (read == ARCHIVE_EOF)
					break;
				if (read != ARCHIVE_OK)
				{
					error = archiveError(reader.get(), "the package is damaged at " + path);
					return false;
				}
				// the sizes in the headers may lie, so count what really comes out
				unpacked += size;
				if (unpacked > limits.maxTotalBytes)
				{
					error = "the package unpacks to too much data";
					return false;
				}
				if (archive_write_data_block(writer.get(), block, size, offset) != ARCHIVE_OK)
				{
					error = archiveError(writer.get(), "cannot write " + path);
					return false;
				}
			}
		}
		if (archive_write_finish_entry(writer.get()) != ARCHIVE_OK)
		{
			error = archiveError(writer.get(), "cannot finish " + path);
			return false;
		}
	}
	if (archive_write_close(writer.get()) != ARCHIVE_OK)
	{
		error = archiveError(writer.get(), "cannot finish the files");
		return false;
	}
	if (entries == 0)
	{
		error = "the package is empty";
		return false;
	}
	return true;
}
