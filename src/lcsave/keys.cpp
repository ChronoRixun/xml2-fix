#include "keys.hpp"

#include <cstring>

namespace lcsave
{
	namespace
	{
		const char* const domain_tags[17] = {nullptr, "STAT", "ITEM", "ENHC", "NAMA", "NAMB", "TRIT", "TALN", "COST", "MSNF",
			"OBJV", "DRCS", "DRGR", "REVW", "XPNT", "ZONE", "TRIV"};

		// cp1252 0x80-0x9f as Unicode; 0 = undefined (Python decodes those as U+FFFD with 'replace')
		const std::uint16_t cp1252_high[32] = {0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
			0x2039, 0x0152, 0, 0x017d, 0, 0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161,
			0x203a, 0x0153, 0, 0x017e, 0x0178};

		inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
		inline std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
	}

	const char* domain_tag(unsigned number)
	{
		return number >= 1 && number <= 16 ? domain_tags[number] : nullptr;
	}

	unsigned domain_number(std::string_view tag)
	{
		for (unsigned n = 1; n <= 16; ++n)
		{
			if (tag == domain_tags[n])
			{
				return n;
			}
		}
		return 0;
	}

	std::uint32_t code_point(std::uint8_t byte, Enc enc)
	{
		if (byte < 0x80 || byte >= 0xa0 || enc == Enc::latin1)
		{
			return byte;
		}
		const auto c = cp1252_high[byte - 0x80];
		return c ? c : 0xfffd;
	}

	bool encodable_cp1252(std::string_view text, Enc enc)
	{
		for (const unsigned char c : text)
		{
			if (c >= 0x80 && c < 0xa0 && (enc == Enc::latin1 || !cp1252_high[c - 0x80]))
			{
				return false;
			}
		}
		return true;
	}

	std::string canonical_field(std::string_view text, Enc enc)
	{
		std::string out;
		out.reserve(text.size() + 8);
		for (unsigned char b : text)
		{
			if (b >= 0x80 && b < 0xa0 && (enc == Enc::latin1 || !cp1252_high[b - 0x80]))
			{
				b = '?';                           // encode('cp1252', 'replace')
			}
			if (b >= 'A' && b <= 'Z')
			{
				out.push_back(static_cast<char>(b + 32));
			}
			else if (b == '%')
			{
				out += "%25";
			}
			else if (b == '/')
			{
				out += "%2f";
			}
			else
			{
				out.push_back(static_cast<char>(b));
			}
		}
		return out;
	}

	std::string canonical_key(const std::vector<std::string_view>& fields, Enc enc)
	{
		std::string out;
		for (std::size_t i = 0; i < fields.size(); ++i)
		{
			if (i)
			{
				out.push_back('/');
			}
			out += canonical_field(fields[i], enc);
		}
		return out;
	}

	Hash key_hash(const std::vector<std::string_view>& fields, Enc enc)
	{
		const auto k = canonical_key(fields, enc);
		return sha256(reinterpret_cast<const std::uint8_t*>(k.data()), k.size());
	}

	Hash key_hash1(std::string_view field, Enc enc)
	{
		const auto k = canonical_field(field, enc);
		return sha256(reinterpret_cast<const std::uint8_t*>(k.data()), k.size());
	}

	std::string hex(const std::uint8_t* data, std::size_t size)
	{
		static const char digits[] = "0123456789abcdef";
		std::string out(size * 2, '0');
		for (std::size_t i = 0; i < size; ++i)
		{
			out[2 * i] = digits[data[i] >> 4];
			out[2 * i + 1] = digits[data[i] & 15];
		}
		return out;
	}

	bool from_hex(std::string_view text, std::uint8_t* out, std::size_t size)
	{
		if (text.size() != size * 2)
		{
			return false;
		}
		auto digit = [](char c) -> int
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		for (std::size_t i = 0; i < size; ++i)
		{
			const int hi = digit(text[2 * i]);
			const int lo = digit(text[2 * i + 1]);
			if (hi < 0 || lo < 0)
			{
				return false;
			}
			out[i] = static_cast<std::uint8_t>(hi << 4 | lo);
		}
		return true;
	}

	std::uint32_t make_id(unsigned domain, std::uint32_t serial)
	{
		if (domain < 1 || domain > 127 || serial < 1 || serial > serial_max)
		{
			return 0;
		}
		return (static_cast<std::uint32_t>(domain) << 24) | serial;
	}

	std::uint32_t make_keyref(std::uint32_t index)
	{
		return index < keyref_bit ? (keyref_bit | index) : 0;
	}

	unsigned id_domain(std::uint32_t value)
	{
		if (value == 0 || (value & keyref_bit))
		{
			return 0;
		}
		return (value >> 24) & 0x7f;
	}

	std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t start)
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
		std::uint32_t crc = ~start;
		for (std::size_t i = 0; i < size; ++i)
		{
			crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
		}
		return ~crc;
	}

	Hash sha256(const std::uint8_t* data, std::size_t size)
	{
		static const std::uint32_t k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
		std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		auto block = [&](const std::uint8_t* p)
		{
			std::uint32_t w[64];
			for (int i = 0; i < 16; ++i)
			{
				w[i] = std::uint32_t(p[4 * i]) << 24 | std::uint32_t(p[4 * i + 1]) << 16 | std::uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
			}
			for (int i = 16; i < 64; ++i)
			{
				const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
				const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
			for (int i = 0; i < 64; ++i)
			{
				const auto t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
				const auto t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
				hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
			}
			h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
		};
		std::size_t i = 0;
		for (; i + 64 <= size; i += 64)
		{
			block(data + i);
		}
		std::uint8_t tail[128] = {};
		const std::size_t rest = size - i;
		if (rest)
		{
			std::memcpy(tail, data + i, rest);
		}
		tail[rest] = 0x80;
		const std::size_t total = rest + 9 <= 64 ? 64 : 128;
		const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8;
		for (int j = 0; j < 8; ++j)
		{
			tail[total - 1 - j] = static_cast<std::uint8_t>(bits >> (8 * j));
		}
		block(tail);
		if (total == 128)
		{
			block(tail + 64);
		}
		Hash out{};
		for (int j = 0; j < 8; ++j)
		{
			out[4 * j] = static_cast<std::uint8_t>(h[j] >> 24);
			out[4 * j + 1] = static_cast<std::uint8_t>(h[j] >> 16);
			out[4 * j + 2] = static_cast<std::uint8_t>(h[j] >> 8);
			out[4 * j + 3] = static_cast<std::uint8_t>(h[j]);
		}
		return out;
	}

	Sha1 sha1(const std::uint8_t* data, std::size_t size)
	{
		std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
		auto block = [&](const std::uint8_t* p)
		{
			std::uint32_t w[80];
			for (int i = 0; i < 16; ++i)
			{
				w[i] = std::uint32_t(p[4 * i]) << 24 | std::uint32_t(p[4 * i + 1]) << 16 | std::uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
			}
			for (int i = 16; i < 80; ++i)
			{
				w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
			}
			auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
			for (int i = 0; i < 80; ++i)
			{
				std::uint32_t f, k;
				if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
				else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
				else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
				else { f = b ^ c ^ d; k = 0xCA62C1D6; }
				const auto t = rotl(a, 5) + f + e + k + w[i];
				e = d; d = c; c = rotl(b, 30); b = a; a = t;
			}
			h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
		};
		std::size_t i = 0;
		for (; i + 64 <= size; i += 64)
		{
			block(data + i);
		}
		std::uint8_t tail[128] = {};
		const std::size_t rest = size - i;
		if (rest)
		{
			std::memcpy(tail, data + i, rest);
		}
		tail[rest] = 0x80;
		const std::size_t total = rest + 9 <= 64 ? 64 : 128;
		const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8;
		for (int j = 0; j < 8; ++j)
		{
			tail[total - 1 - j] = static_cast<std::uint8_t>(bits >> (8 * j));
		}
		block(tail);
		if (total == 128)
		{
			block(tail + 64);
		}
		Sha1 out{};
		for (int j = 0; j < 5; ++j)
		{
			out[4 * j] = static_cast<std::uint8_t>(h[j] >> 24);
			out[4 * j + 1] = static_cast<std::uint8_t>(h[j] >> 16);
			out[4 * j + 2] = static_cast<std::uint8_t>(h[j] >> 8);
			out[4 * j + 3] = static_cast<std::uint8_t>(h[j]);
		}
		return out;
	}

	std::string py_lower(std::string_view text, Enc enc)
	{
		std::string out(text);
		for (auto& ch : out)
		{
			const auto b = static_cast<unsigned char>(ch);
			if ((b >= 'A' && b <= 'Z') || (b >= 0xc0 && b <= 0xde && b != 0xd7))
			{
				ch = static_cast<char>(b + 32);
			}
			else if (enc == Enc::cp1252)
			{
				if (b == 0x8a || b == 0x8c || b == 0x8e)
				{
					ch = static_cast<char>(b + 0x10);      // S, OE, Z with caron / ligature
				}
				else if (b == 0x9f)
				{
					ch = static_cast<char>(0xff);          // Y with diaeresis
				}
			}
		}
		return out;
	}

	std::string py_repr(std::string_view text, Enc enc)
	{
		// Python 3 str.__repr__: single quotes unless the text has ' and no "; printable characters as they are
		bool single = false, dbl = false;
		for (const char c : text)
		{
			single |= c == '\'';
			dbl |= c == '"';
		}
		const char q = single && !dbl ? '"' : '\'';
		std::string out(1, q);
		static const char digits[] = "0123456789abcdef";
		for (const unsigned char b : text)
		{
			const auto cp = code_point(b, enc);
			if (b == static_cast<unsigned char>(q) || b == '\\')
			{
				out.push_back('\\');
				out.push_back(static_cast<char>(b));
			}
			else if (b == '\n') out += "\\n";
			else if (b == '\r') out += "\\r";
			else if (b == '\t') out += "\\t";
			else if (cp < 0x20 || cp == 0x7f || (cp >= 0x80 && cp <= 0xa0) || cp == 0xad)
			{
				out += "\\x";
				out.push_back(digits[cp >> 4]);
				out.push_back(digits[cp & 15]);
			}
			else
			{
				out.push_back(static_cast<char>(b));
			}
		}
		out.push_back(q);
		return out;
	}
}
