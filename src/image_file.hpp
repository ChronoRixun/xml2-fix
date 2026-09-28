#pragma once

// A frame as a file, without an image library: BMP, or PNG made of "stored" (uncompressed)
// deflate blocks - as big as the BMP, but it opens anywhere. The input is what the frame capture
// reads from the Direct3D back buffer: 32-bit BGRX rows, top-down, no padding. Kept apart from
// the capture so xml2_test can check the bytes.

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace image_file
{
	// CRC-32 as PNG and zlib use it; pass the previous result to continue over more data.
	inline std::uint32_t crc32(const std::span<const std::uint8_t> data, std::uint32_t crc = 0)
	{
		static const auto table = []
		{
			std::array<std::uint32_t, 256> t{};
			for (std::uint32_t i = 0; i < 256; ++i)
			{
				std::uint32_t c = i;
				for (int k = 0; k < 8; ++k)
				{
					c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				}
				t[i] = c;
			}
			return t;
		}();

		crc = ~crc;
		for (const auto byte : data)
		{
			crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
		}
		return ~crc;
	}

	inline std::uint32_t adler32(const std::span<const std::uint8_t> data)
	{
		std::uint32_t a = 1;
		std::uint32_t b = 0;
		for (const auto byte : data)
		{
			a = (a + byte) % 65521;
			b = (b + a) % 65521;
		}
		return (b << 16) | a;
	}

	namespace detail
	{
		inline void put_le32(std::vector<std::uint8_t>& out, const std::uint32_t value)
		{
			for (int shift = 0; shift < 32; shift += 8)
			{
				out.push_back(static_cast<std::uint8_t>(value >> shift));
			}
		}

		inline void put_le16(std::vector<std::uint8_t>& out, const std::uint16_t value)
		{
			out.push_back(static_cast<std::uint8_t>(value));
			out.push_back(static_cast<std::uint8_t>(value >> 8));
		}

		inline void put_be32(std::vector<std::uint8_t>& out, const std::uint32_t value)
		{
			for (int shift = 24; shift >= 0; shift -= 8)
			{
				out.push_back(static_cast<std::uint8_t>(value >> shift));
			}
		}

		// A PNG chunk: length, type, data, CRC over type and data.
		inline void put_chunk(std::vector<std::uint8_t>& out, const char (&type)[5], const std::span<const std::uint8_t> data)
		{
			put_be32(out, static_cast<std::uint32_t>(data.size()));
			const std::array<std::uint8_t, 4> tag{static_cast<std::uint8_t>(type[0]), static_cast<std::uint8_t>(type[1]),
			                                      static_cast<std::uint8_t>(type[2]), static_cast<std::uint8_t>(type[3])};
			out.insert(out.end(), tag.begin(), tag.end());
			out.insert(out.end(), data.begin(), data.end());
			put_be32(out, crc32(data, crc32(tag)));
		}
	}

	// 24-bit bottom-up BMP.
	inline std::vector<std::uint8_t> encode_bmp(const unsigned width, const unsigned height, const std::span<const std::uint8_t> bgrx)
	{
		const std::uint32_t stride = (width * 3 + 3) & ~3u;
		const std::uint32_t pixel_bytes = stride * height;
		std::vector<std::uint8_t> out;
		out.reserve(54 + pixel_bytes);

		out.push_back('B');
		out.push_back('M');
		detail::put_le32(out, 54 + pixel_bytes);
		detail::put_le32(out, 0);
		detail::put_le32(out, 54);
		detail::put_le32(out, 40); // BITMAPINFOHEADER
		detail::put_le32(out, width);
		detail::put_le32(out, height);
		detail::put_le16(out, 1);
		detail::put_le16(out, 24);
		detail::put_le32(out, 0); // BI_RGB
		detail::put_le32(out, pixel_bytes);
		detail::put_le32(out, 2835); // 72 dpi
		detail::put_le32(out, 2835);
		detail::put_le32(out, 0);
		detail::put_le32(out, 0);

		for (unsigned y = height; y-- > 0;)
		{
			const auto* row = bgrx.data() + static_cast<size_t>(y) * width * 4;
			for (unsigned x = 0; x < width; ++x)
			{
				out.push_back(row[x * 4 + 0]);
				out.push_back(row[x * 4 + 1]);
				out.push_back(row[x * 4 + 2]);
			}
			for (std::uint32_t pad = width * 3; pad < stride; ++pad)
			{
				out.push_back(0);
			}
		}
		return out;
	}

	// 8-bit RGB PNG; the zlib stream holds the rows as stored deflate blocks.
	inline std::vector<std::uint8_t> encode_png(const unsigned width, const unsigned height, const std::span<const std::uint8_t> bgrx)
	{
		// Filtered scanlines: filter byte 0 then RGB.
		std::vector<std::uint8_t> raw;
		raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3));
		for (unsigned y = 0; y < height; ++y)
		{
			raw.push_back(0);
			const auto* row = bgrx.data() + static_cast<size_t>(y) * width * 4;
			for (unsigned x = 0; x < width; ++x)
			{
				raw.push_back(row[x * 4 + 2]);
				raw.push_back(row[x * 4 + 1]);
				raw.push_back(row[x * 4 + 0]);
			}
		}

		std::vector<std::uint8_t> zlib;
		zlib.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
		zlib.push_back(0x78); // deflate, 32K window
		zlib.push_back(0x01); // no preset dictionary, check bits
		for (size_t offset = 0; offset < raw.size() || raw.empty();)
		{
			const size_t length = std::min<size_t>(65535, raw.size() - offset);
			const bool last = offset + length >= raw.size();
			zlib.push_back(last ? 1 : 0); // BFINAL, BTYPE 00 = stored
			detail::put_le16(zlib, static_cast<std::uint16_t>(length));
			detail::put_le16(zlib, static_cast<std::uint16_t>(~length));
			zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
			offset += length;
			if (raw.empty())
			{
				break;
			}
		}
		detail::put_be32(zlib, adler32(raw));

		std::vector<std::uint8_t> out;
		out.reserve(zlib.size() + 64);
		const std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
		out.insert(out.end(), signature.begin(), signature.end());

		std::vector<std::uint8_t> header;
		detail::put_be32(header, width);
		detail::put_be32(header, height);
		header.push_back(8); // bit depth
		header.push_back(2); // colour type: RGB
		header.push_back(0); // compression
		header.push_back(0); // filter
		header.push_back(0); // no interlace
		detail::put_chunk(out, "IHDR", header);
		detail::put_chunk(out, "IDAT", zlib);
		detail::put_chunk(out, "IEND", {});
		return out;
	}
}
