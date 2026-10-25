#pragma once

// [Balance] ReceiveRounding / DamageCap: two more of X-Men Legends 1's damage numbers that [Balance] Rounding=xml1
// does not reach (gameplay rule 41, docs/balance.md "The receive side and the cap"). Kept apart from the patching so
// the tests can check it without the game: the ini values' rules, the arithmetic of both games, every retail byte
// relied on, every byte written and the emitted stub code.
//
// Every XMen2.exe address is the retail build's (image base 0x400000, SHA-256 146cd9c3...a1669b); every XML1 one is
// from X-Men Legends I's default.xbe (Xbox, 2004; addresses as loaded at 0x10000). No game code is embedded.
//
//   ReceiveRounding = xml1. The damage receive (XMen2.exe 0x437490; XML1 0x45260) takes a hit's damage through the
//     victim's flat defense and its scale (the `defense` and `scale` the receive's two affecter reads leave at
//     [esp+0x14] / [esp+0x10]). XML1 keeps the damage as an integer (+0x94) and converts after each stage
//     (0x45517..0x45558):
//         d = trunc(max(int damage - defense, 0)); damage = trunc(d x scale)      (_ftol, 0x1a41f0 -> 0x3439c4)
//     XMen2.exe keeps a float (+0x9c) through both (0x437a54..0x437a7c): damage = max(damage - defense, 0) x scale.
//     So 10 with defense 0.5 and scale 0.5 is 4 in XML1 and 4.75 here. The two calls below give the receive XML1's
//     stages: the load of the damage (0x437a54, `fld dword [esi+0x9c]`) truncates it, as XML1's field is an
//     integer, and the scale (0x437a75, `fmul dword [esp+0x10]; mov ecx,[esi]`) truncates the clamped difference,
//     multiplies and truncates again. Each truncation is the low 32 bits of x87 `fistp` with round-toward-zero, as
//     XML1's _ftol (fistp qword under a chop control word, eax kept); the control word is restored.
//   DamageCap = xml1. A hit's damage (XMen2.exe 0x453a60; XML1 0x5cca0) is capped at the end: XML1 at 32000
//     (0x5ce0d: cmp eax,0x7d00; jle; mov eax,0x7d00), XMen2.exe at 1,000,000 (0x453bfb: fcom dword [0x685bf4];
//     ...; fld dword [0x685bf4]). The two operands point at 32000.0f instead; 0x685bf4 itself stays (other code
//     reads it).
//
// Each key is on its own: absent or `xml2` writes nothing. Nothing is saved.

#include "ini_rules.hpp"
#include "limits_rules.hpp" // guard, matches, hex_byte, hex_size, image_base

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace balance_parity_rules
{
	using address = std::uint32_t;
	using limits_rules::guard;
	using limits_rules::image_base;

	// ---- The ini values -----------------------------------------------------------------------------------

	struct choice
	{
		bool xml1 = false;  // asked for XML1's
		bool set = false;   // the key has a value
		std::string error;  // the value is neither xml1 nor xml2
	};

	inline choice parse(const std::string_view raw)
	{
		const auto value = ini_rules::value_text(raw);
		if (value.empty()) return {};
		std::string lower(value);
		for (auto& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		if (lower == "xml1") return {true, true, {}};
		if (lower == "xml2") return {false, true, {}};
		choice refused;
		refused.set = true;
		refused.error = "isn't xml1 or xml2";
		return refused;
	}

	// ---- The arithmetic -----------------------------------------------------------------------------------

	// XML1's _ftol on a value: round toward zero, the low 32 bits of the 64-bit result (a NaN or an overflow gives
	// the x87 integer indefinite 0x8000000000000000, whose low half is 0).
	inline std::int32_t xml1_ftol(const double value)
	{
		if (!(value == value) || value >= 9.2233720368547758e18 || value <= -9.2233720368547758e18) return 0;
		return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value))));
	}
	// XMen2.exe's receive stages (0x437a54..0x437a7c), in double: max(damage - defense, 0) x scale.
	inline double receive_xml2(const float damage, const float defense, const float scale)
	{
		double d = static_cast<double>(damage) - defense;
		if (d < 0.0) d = 0.0; // fld 0; fcomp st1; test ah,0x41; jne keep: 0 < d, 0 == d and unordered keep d
		return d * scale;
	}
	// XML1's (0x45517..0x45558): the integer damage, trunc(max(damage - defense, 0)), trunc(that x scale).
	inline std::int32_t receive_xml1(const std::int32_t damage, const float defense, const float scale)
	{
		double d = static_cast<double>(damage) - defense;
		if (d < 0.0) d = 0.0;
		const std::int32_t whole = xml1_ftol(d);
		return xml1_ftol(static_cast<double>(whole) * scale);
	}
	// What the patched receive computes for XMen2.exe's float damage: its load truncated (XML1's integer field),
	// then XML1's stages.
	inline std::int32_t receive_patched(const float damage, const float defense, const float scale)
	{
		return receive_xml1(xml1_ftol(damage), defense, scale);
	}

	constexpr float xml1_damage_cap = 32000.0f; // default.xbe 0x5ce0d: 0x7d00
	constexpr float xml2_damage_cap = 1000000.0f; // 0x685bf4
	inline float capped(const float damage, const float cap)
	{
		return damage > cap ? cap : damage; // fcom; test ah,0x41; jne keep: only a greater value is replaced
	}

	// ---- The sites -----------------------------------------------------------------------------------------

	struct site
	{
		address va;
		std::string_view hex; // the retail bytes, all guarded
		const char* what;
	};

	// The receive's two stages and the store after them (0x437a54..0x437a81): fld [esi+0x9c]; fsub [esp+0x14];
	// fld [0x680030]; fcomp st1; fnstsw ax; test ah,0x41; jne +8; fstp st0; fld [0x680030]; fmul [esp+0x10];
	// mov ecx,[esi]; push edi; fstp [esi+0x9c].
	inline constexpr site receive_site{0x437a54, "d9869c000000d8642414d90530006800d8d9dfe0f6c4417508ddd8d90530006800d84c24108b0e57d99e9c000000",
	                                   "the damage receive's defense and scale stages (0x437490)"};
	constexpr address receive_load_va = 0x437a54;  // fld dword [esi+0x9c] (6 bytes)
	constexpr address receive_scale_va = 0x437a75; // fmul dword [esp+0x10]; mov ecx,[esi] (6 bytes)
	constexpr address zero_constant_va = 0x680030; // 0.0f
	// The hit's cap (0x453bfb..0x453c0f): fcom dword [0x685bf4]; fnstsw ax; test ah,0x41; jne +8; fstp st0;
	// fld dword [0x685bf4].
	inline constexpr site cap_site{0x453bfb, "d815f45b6800dfe0f6c4417508ddd8d905f45b6800", "a hit's damage cap (0x453a60)"};
	inline constexpr site cap_constant_site{0x685bf4, "00247449", "the hit's cap, 1,000,000.0f (0x685bf4)"};
	constexpr address cap_compare_operand_va = 0x453bfd, cap_load_operand_va = 0x453c0c;

	enum feature : unsigned
	{
		receive_rounding = 1,
		damage_cap = 2,
	};

	inline const site* first_mismatch(const std::uint8_t* image, const unsigned features)
	{
		const site* all[] = {&receive_site, &cap_site, &cap_constant_site};
		const unsigned need[] = {receive_rounding, damage_cap, damage_cap};
		for (std::size_t i = 0; i < 3; ++i)
		{
			if ((features & need[i]) && !limits_rules::matches(image + (all[i]->va - image_base), all[i]->hex)) return all[i];
		}
		return nullptr;
	}

	inline std::vector<std::uint8_t> retail_bytes(const site& s, const address at, const std::size_t count)
	{
		std::vector<std::uint8_t> bytes(count);
		for (std::size_t i = 0; i < count; ++i) bytes[i] = limits_rules::hex_byte(s.hex, at - s.va + i);
		return bytes;
	}

	// ---- The stubs -----------------------------------------------------------------------------------------

	inline void put32(std::vector<std::uint8_t>& code, const std::size_t at, const std::uint32_t value)
	{
		for (std::size_t i = 0; i < 4; ++i) code[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
	}

	// The block: the truncation helper, the two stubs, the cap constant.
	struct layout
	{
		address base;
		address trunc() const { return base; }
		address load() const { return base + 48; }
		address scale() const { return base + 64; }
		address cap() const { return base + 96; }
	};
	constexpr std::size_t trunc_size = 40, load_size = 12, scale_size = 18, block_size = 100;

	// st0 = x -> st0 = (double) low 32 bits of trunc(x), as XML1's _ftol then fild: every register, the flags and
	// the control word as they were; the x87 stack depth unchanged.
	inline std::vector<std::uint8_t> trunc_helper()
	{
		return {
			0x50,                         // 00 push eax
			0x9c,                         // 01 pushfd
			0x83, 0xec, 0x0c,             // 02 sub esp,12
			0xd9, 0x3c, 0x24,             // 05 fnstcw [esp]
			0x8b, 0x04, 0x24,             // 08 mov eax,[esp]
			0x80, 0xcc, 0x0c,             // 0b or ah,0x0c (round toward zero)
			0x66, 0x89, 0x44, 0x24, 0x02, // 0e mov [esp+2],ax
			0xd9, 0x6c, 0x24, 0x02,       // 13 fldcw [esp+2]
			0xdf, 0x7c, 0x24, 0x04,       // 17 fistp qword [esp+4]
			0xd9, 0x2c, 0x24,             // 1b fldcw [esp]
			0xdb, 0x44, 0x24, 0x04,       // 1e fild dword [esp+4] (the low half, as _ftol's eax)
			0x83, 0xc4, 0x0c,             // 22 add esp,12
			0x9d,                         // 25 popfd
			0x58,                         // 26 pop eax
			0xc3,                         // 27 ret
		};
	}

	// Called from 0x437a54 in place of `fld dword [esi+0x9c]`: the damage, truncated (XML1's field is an integer).
	inline std::vector<std::uint8_t> load_stub(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0xd9, 0x86, 0x9c, 0x00, 0x00, 0x00, // 00 fld dword [esi+0x9c]
			0xe8, 0, 0, 0, 0,                   // 06 call trunc
			0xc3,                               // 0b ret
		};
		put32(code, 7, l.trunc() - (l.load() + 11));
		return code;
	}
	// Called from 0x437a75 in place of `fmul dword [esp+0x10]; mov ecx,[esi]` (st0 = the clamped difference): trunc,
	// x the scale ([esp+0x10] at the site is [esp+0x14] here), trunc, then the replaced `mov ecx,[esi]`.
	inline std::vector<std::uint8_t> scale_stub(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0xe8, 0, 0, 0, 0,       // 00 call trunc
			0xd8, 0x4c, 0x24, 0x14, // 05 fmul dword [esp+0x14]
			0xe8, 0, 0, 0, 0,       // 09 call trunc
			0x8b, 0x0e,             // 0e mov ecx,[esi]
			0xc3,                   // 10 ret
			0xcc,
		};
		put32(code, 1, l.trunc() - (l.scale() + 5));
		put32(code, 10, l.trunc() - (l.scale() + 14));
		return code;
	}

	inline std::vector<std::uint8_t> block(const address base)
	{
		const layout l{base};
		std::vector<std::uint8_t> bytes(block_size, 0xcc);
		const auto helper = trunc_helper();
		std::memcpy(bytes.data() + (l.trunc() - base), helper.data(), helper.size());
		const auto load = load_stub(l);
		std::memcpy(bytes.data() + (l.load() - base), load.data(), load.size());
		const auto scale = scale_stub(l);
		std::memcpy(bytes.data() + (l.scale() - base), scale.data(), scale.size());
		const float cap = xml1_damage_cap;
		std::memcpy(bytes.data() + (l.cap() - base), &cap, 4);
		return bytes;
	}

	// A call rel32 from `at` to `to`, then a nop: the 6-byte replacement of both receive sites.
	inline std::vector<std::uint8_t> call6(const address at, const address to)
	{
		std::vector<std::uint8_t> code{0xe8, 0, 0, 0, 0, 0x90};
		put32(code, 1, to - (at + 5));
		return code;
	}
	inline std::vector<std::uint8_t> operand(const address value)
	{
		std::vector<std::uint8_t> code(4);
		put32(code, 0, value);
		return code;
	}

	struct site_write
	{
		address va;
		std::vector<std::uint8_t> before, after;
	};
	// The writes of `features` for a block at `base`.
	inline std::vector<site_write> writes_for(const unsigned features, const address base)
	{
		const layout l{base};
		std::vector<site_write> out;
		if (features & receive_rounding)
		{
			out.push_back({receive_load_va, retail_bytes(receive_site, receive_load_va, 6), call6(receive_load_va, l.load())});
			out.push_back({receive_scale_va, retail_bytes(receive_site, receive_scale_va, 6), call6(receive_scale_va, l.scale())});
		}
		if (features & damage_cap)
		{
			out.push_back({cap_compare_operand_va, retail_bytes(cap_site, cap_compare_operand_va, 4), operand(l.cap())});
			out.push_back({cap_load_operand_va, retail_bytes(cap_site, cap_load_operand_va, 4), operand(l.cap())});
		}
		return out;
	}

	// Gameplay rule 41: {receive rounding installed, the cap in effect when DamageCap is installed (32000), 0, 0}.
	constexpr std::uint16_t agreement_revision = 1;
	inline std::array<std::uint32_t, 4> agreement_values(const unsigned installed)
	{
		return {(installed & receive_rounding) ? 1u : 0u, (installed & damage_cap) ? 32000u : 0u, 0u, 0u};
	}
}
