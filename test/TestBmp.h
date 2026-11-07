#pragma once

#include <cstddef>
#include <cstdint>

// Helpers that build BMP files for the sponsor tests. Bytes is std::string or std::vector<char>.

template<class Bytes>
void putU16(Bytes& d, std::size_t at, std::uint16_t v)
{
	d[at] = char(v & 0xFF);
	d[at + 1] = char(v >> 8);
}

template<class Bytes>
void putU32(Bytes& d, std::size_t at, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		d[at + i] = char((v >> (8 * i)) & 0xFF);
}

// writes the file header and the start of the info header that every BMP has
template<class Bytes>
void writeBmpHeader(Bytes& d, std::uint32_t size, std::uint32_t pixelOffset, std::uint32_t infoSize,
                    std::int32_t width, std::int32_t height, std::uint16_t bpp, std::uint32_t compression)
{
	d[0] = 'B';
	d[1] = 'M';
	putU32(d, 2, size);
	putU32(d, 10, pixelOffset);
	putU32(d, 14, infoSize);
	putU32(d, 18, std::uint32_t(width));
	putU32(d, 22, std::uint32_t(height));
	putU16(d, 26, 1);
	putU16(d, 28, bpp);
	putU32(d, 30, compression);
}
