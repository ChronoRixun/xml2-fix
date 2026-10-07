#pragma once

// Identity primitives of the LC save (SPEC-new-save 3.1-3.3), byte for byte as tools/lcsave/keys.py:
// domains, canonical key bytes, the SHA-256 key hash, the 32-bit id grammar, CRC-32 (IEEE 802.3, zlib),
// SHA-1. Text is bytes as the engine has them; `Enc` says how Python decoded those bytes, because the
// reference decodes save text as cp1252 and build data (xmlb) as latin-1, and the two differ for the
// bytes 0x80-0x9f (canonical keys, lower-casing and JSON escapes follow the decoded characters).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lcsave
{
	using Hash = std::array<std::uint8_t, 32>;      // SHA-256 of a canonical key
	using Sha1 = std::array<std::uint8_t, 20>;

	enum class Enc : std::uint8_t
	{
		cp1252,     // text read from a save (Python: bytes.decode('cp1252', 'replace'))
		latin1,     // text read from the build's data (Python: xmlb decodes latin-1)
	};

	// domain number -> tag (SPEC 3.2); 0 for an unknown number
	const char* domain_tag(unsigned number);
	unsigned domain_number(std::string_view tag);   // 0 for an unknown tag
	constexpr std::uint32_t keyref_bit = 0x80000000u;
	constexpr std::uint32_t serial_max = 0xFFFFFFu;

	// one key field: ASCII letters lower-cased, '%' -> "%25", '/' -> "%2f"; characters cp1252 can not encode
	// become '?' (Python's encode('cp1252', 'replace'))
	std::string canonical_field(std::string_view text, Enc enc);
	std::string canonical_key(const std::vector<std::string_view>& fields, Enc enc);
	Hash key_hash(const std::vector<std::string_view>& fields, Enc enc);
	Hash key_hash1(std::string_view field, Enc enc);
	std::string hex(const std::uint8_t* data, std::size_t size);
	template <std::size_t N> std::string hex(const std::array<std::uint8_t, N>& a) { return hex(a.data(), N); }
	bool from_hex(std::string_view text, std::uint8_t* out, std::size_t size);

	// id grammar (SPEC 3.1); make_id returns 0 for parts outside domain 1..127 / serial 1..0xffffff
	std::uint32_t make_id(unsigned domain, std::uint32_t serial);
	std::uint32_t make_keyref(std::uint32_t index);
	unsigned id_domain(std::uint32_t value);
	inline std::uint32_t id_serial(std::uint32_t value) { return value & serial_max; }
	inline bool is_keyref(std::uint32_t value) { return (value & keyref_bit) != 0; }

	std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t start = 0);
	inline std::uint32_t crc32(const std::vector<std::uint8_t>& data) { return crc32(data.data(), data.size()); }
	Hash sha256(const std::uint8_t* data, std::size_t size);
	Sha1 sha1(const std::uint8_t* data, std::size_t size);

	// cp1252 / latin-1 helpers for the Python behaviours the converter and the dumps depend on
	std::uint32_t code_point(std::uint8_t byte, Enc enc);          // the decoded character (0xfffd: undefined)
	bool encodable_cp1252(std::string_view text, Enc enc);         // encode('cp1252') would succeed
	std::string py_lower(std::string_view text, Enc enc);          // str.lower() on the decoded text
	std::string py_repr(std::string_view text, Enc enc);           // repr() of the decoded text
}
