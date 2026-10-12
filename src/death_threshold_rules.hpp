#pragma once

// [Game] DeathRule: X-Men Legends 1's death threshold, kept apart from the installer so xml2_test can check it without
// the game: the ini value's rules, the x87 comparisons the game's code makes before and after the change, and every
// retail byte the change relies on.
//
// Every XMen2.exe address is the retail build's (image base 0x400000, no relocations); every XML1 one is from XML1's
// default.xbe (Xbox, 2004).
//
// What XML1 does. The physical entities' damage function (0x92220, shared by every class that can be hurt; a
// character's own damage function 0x46270 delegates to it) takes the hit's whole-number damage from the health
// float (+0x240) and then dies when the health is BELOW 1.0 (0x9266f: fcomp [1.0]; test ah,5; jp alive). The same
// "below 1" is XML1's line in the few other places that ask whether a hit just killed something:
//   - the character's damage function: the last-stand window (a hero's protection that cancels a hit and makes the
//     hero briefly invulnerable) opens when the health after the hit would be at most 1.0 (0x463bf), and a hit that
//     left the character below 1.0 is marked as a killing hit (type 8, 0x4648f);
//   - the harm entities' AI hazard announcement runs only while their health is at least 1.0 (0x46ba7);
//   - the affectable harm entities' damage function returns at once below 1.0 and, after the hit, records the killer
//     below 1.0 (0x46e0c, 0x46eca);
//   - the power-gated bridge objects' damage function (0x94460: the magma / ice / telekinesis bridges a matching
//     power builds) reacts only while the object's health is at least 1.0 (0x944e4).
// Every other XML1 health test (about 175 of them: is it alive, may it be hit, is the corpse to be removed) is
// "above 0", as in XMen2.exe.
//
// What XMen2.exe does. The same functions (0x497e70, 0x438a10, 0x439350, 0x439500, 0x49a570) draw each of those lines at 0.0
// instead: an entity dies only when its health reaches 0. XMen2.exe's damage is a float (a hit's fraction of a point
// is rounded up to 1 only when the whole hit is below 1), so an enemy can be left alive at 0.2 of a point, attacked
// on and on, with its death script and death drops waiting for a further hit. XML1's whole-number damage never left a
// fraction to sit on, and its line at 1.0 would have ended it anyway.
//
// DeathRule=xml1 moves exactly those seven comparisons, and the bossundying hold below, to XML1's line: each one's
// float operand is pointed from the game's 0.0 (0x680030) to the game's own 1.0 (0x68012c), and where XML1 tests "below" rather than "at most" the
// status mask of the test that follows goes from 0x41 (C0|C3) to 0x05 (C0|C2), XML1's mask, so a health of exactly
// 1.0 stays alive as in XML1. And because XML1's whole numbers always left a killing hit at 0 or below, a hit that
// enters the death branch above 0 has its health set to 0 there (death_bridge below), so the kill handler, the death
// drops and every "is it alive" test see the dead entity XML1's would have been.
//
// One comparison has no XML1 counterpart: XMen2.exe's `bossundying` entities (+0x30d bit 0x40, set by the entity
// attribute and setBossUndying) are put back to 1.0 BEFORE the death test when their health is at most 0
// (0x4983ce). Left at 0.0 it would let such a boss die from 0.5; it moves to "below 1.0" with the death test, so
// it still holds the boss at 1.0. XML1's own undying flag (+0x2b8 bit 0x10 there, +0x30c bit 0x10 here) restores
// 1.0 inside the death branch on both engines and is untouched.

#include "ini_rules.hpp"   // value_text
#include "limits_rules.hpp" // matches, hex_byte, image_base

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace death_threshold_rules
{
	using address = std::uint32_t;
	using limits_rules::image_base;

	// ---- The ini value ----------------------------------------------------------------------------------

	enum class rule
	{
		xml2,
		xml1,
	};

	// The rule asked for, or why the value isn't one. `set` false and `error` empty: the key isn't set.
	struct rule_choice
	{
		rule value = rule::xml2;
		bool set = false;
		std::string error;
	};

	inline rule_choice parse_rule(const std::string_view raw)
	{
		const auto value = ini_rules::value_text(raw);
		if (value.empty())
		{
			return {};
		}
		std::string lower(value);
		for (auto& c : lower)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower == "xml1")
		{
			return {rule::xml1, true, {}};
		}
		if (lower == "xml2")
		{
			return {rule::xml2, true, {}};
		}
		rule_choice refused;
		refused.error = "isn't xml1 (X-Men Legends 1's death below 1 health) or xml2 (the game's own, at 0)";
		return refused;
	}

	// ---- The x87 comparisons -------------------------------------------------------------------------------

	constexpr address zero_constant = 0x680030; // XMen2.exe's float 0.0, the operand of every changed comparison
	constexpr address one_constant = 0x68012c;  // XMen2.exe's float 1.0 (also read by 0x438b3d and 0x498367)
	constexpr std::uint8_t xml2_mask = 0x41;    // test ah,0x41: C0|C3, "at most"
	constexpr std::uint8_t xml1_mask = 0x05;    // test ah,0x05: C0|C2, "below" (default.xbe 0x9267d)

	// fcom/fcomp leave C0 (bit 0 of ah) for "below", C3 (bit 6) for "equal", nothing for "above" and C0|C2|C3 for an
	// unordered pair (a NaN). `test ah,mask` then sets the parity flag from the bits it kept: jp is taken when an even
	// number of them is set (none counts as even).
	inline std::uint8_t status_ah(const float value, const float operand)
	{
		if (value != value || operand != operand)
		{
			return 0x45;
		}
		return value < operand ? 0x01 : value == operand ? 0x40 : 0x00;
	}

	inline bool parity_even(std::uint8_t bits)
	{
		bits ^= bits >> 4;
		bits ^= bits >> 2;
		bits ^= bits >> 1;
		return (bits & 1) == 0;
	}

	// What each test decides, as the code runs it: true when the branch the game takes for "past the line" (death,
	// the window open, the early return) is taken, i.e. jp NOT taken.
	inline bool past_line(const float health, const float operand, const std::uint8_t mask)
	{
		return !parity_even(static_cast<std::uint8_t>(status_ah(health, operand) & mask));
	}

	inline bool dies_xml2(const float health) { return past_line(health, 0.0f, xml2_mask); }
	inline bool dies_with_rule(const float health) { return past_line(health, 1.0f, xml1_mask); }
	// XML1's death test in plain arithmetic (default.xbe 0x9266f: fcomp [1.0]; test ah,5; jp alive): below 1 dies;
	// 1 and above, and a NaN (C0|C2 set: an even count, jp taken), live.
	inline bool dies_xml1(const float health) { return health < 1.0f; }

	// ---- The sites -----------------------------------------------------------------------------------------

	// One comparison: its retail bytes from the health load through the conditional jump after the test (all of them
	// guarded), where the 0.0 operand is (its four bytes), and where the test's mask byte is when XML1 tests "below"
	// (0 = the mask stays: XML1 tests "at most" there too).
	struct site
	{
		address va;
		std::string_view hex;
		std::size_t operand;
		std::size_t mask;
		const char* what;
		address xml1; // default.xbe's comparison
	};

	inline constexpr std::array<site, 8> sites{{
		{0x4983ce, "d9867c020000d81d30006800dfe0f6c4417a0f", 8, 16, "bossundying: back to 1.0 before the death test (no XML1 counterpart)", 0},
		{0x4983f0, "d9867c020000d81d30006800dfe0f6c4410f8a07030000", 8, 16, "the physical damage function's death test", 0x92675},
		{0x438b47, "d9867c020000d8642424d81530006800dfe0f6c4417a22", 12, 0, "the character's last-stand window (health after the hit)", 0x463bf},
		{0x438c28, "d9867c0200005fd81d300068005bdfe0f6c4417a10", 9, 18, "the character's killing-hit mark (hit type 8)", 0x4648f},
		{0x439357, "d9867c02000057d81d30006800dfe0f6c4410f8bab000000", 9, 17, "the harm entity's AI hazard announcement", 0x46bae},
		{0x43953c, "d9877c020000d81d30006800dfe0f6c4410f8bd0000000", 8, 16, "the affectable harm's early return", 0x46e12},
		{0x4395fa, "d9877c020000d81d30006800dfe0f6c4417a16", 8, 16, "the affectable harm's killer record", 0x46ed0},
		{0x49a646, "d9867c020000d81d30006800dfe0f6c4410f8bd9010000", 8, 16, "the power-gated bridge objects' reaction", 0x944ea},
	}};

	// The death branch's entry, guarded like the sites (the jump to the bridge goes there; no operand, no mask).
	inline constexpr site death_branch_site{0x498407, "6a008d44242850518b4e1c", 0, 0, "the death branch's entry (the killing hit's health to 0)", 0x92686};

	inline std::vector<std::uint8_t> retail_bytes(const site& s)
	{
		std::vector<std::uint8_t> bytes(limits_rules::hex_size(s.hex));
		for (std::size_t i = 0; i < bytes.size(); ++i)
		{
			bytes[i] = limits_rules::hex_byte(s.hex, i);
		}
		return bytes;
	}

	// The site's bytes with the rule: the operand pointed at the 1.0, the mask (where there is one) XML1's.
	inline std::vector<std::uint8_t> rule_bytes(const site& s)
	{
		auto bytes = retail_bytes(s);
		for (std::size_t i = 0; i < 4; ++i)
		{
			bytes[s.operand + i] = static_cast<std::uint8_t>(one_constant >> (8 * i));
		}
		if (s.mask)
		{
			bytes[s.mask] = xml1_mask;
		}
		return bytes;
	}

	// ---- The killing hit's health ------------------------------------------------------------------------------

	// XML1's line at 1 lives in a game of whole numbers: health and damage are whole (the health formula ceils, the
	// damage is a 16-bit integer, regeneration is whole points), so the hit that takes health below 1 takes it to 0
	// or less, and everything after the death test in XML1 - the kill handler's XP and its once-only bit
	// (default.xbe 0x45e20: nothing while health is above 0), every "is it alive" test, the corpse's removal - sees
	// a health of at most 0. XMen2.exe's float damage can leave 0.2. With the death test alone such an entity enters
	// the death branch (state 11) yet keeps 0.2: no kill XP, no death drop (seen in play: the HAARP officer's keycard
	// never appeared), and every "above 0" test still calls it alive. So, as the hit enters the death branch with
	// health above 0, the fix sets it to 0 through the entity's own health setter (vtable slot 0x1a4, the call the hit
	// itself made at 0x4983bf): what an XML1 hit below 1 always left. Undying is unaffected (XMen2.exe's
	// bossundying already put it back to 1.0 before the test; XML1's undying sets 1.0 inside the branch, after).
	constexpr address death_branch_va = 0x498407; // the death branch's first instruction (after jp alive at 0x498401)
	constexpr address death_branch_resume_va = 0x49840d;
	constexpr std::string_view death_branch_displaced = "6a008d442428"; // push 0; lea eax,[esp+0x28]
	constexpr address health_setter_slot = 0x1a4;

	// The bridge the death branch jumps to: when health (esi+0x27c) is above `zero` (the game's 0.0), the setter with
	// 0.0; then the two displaced instructions and back. eax, ecx and edx are free there (0x49840d onwards loads them
	// before use); the x87 stack is left as found.
	inline std::vector<std::uint8_t> death_bridge(const address zero, const address resume, const address at)
	{
		std::vector<std::uint8_t> code{
			0xd9, 0x86, 0x7c, 0x02, 0x00, 0x00, // fld dword [esi+0x27c]
			0xd8, 0x1d, 0, 0, 0, 0,             // fcomp dword [zero]
			0xdf, 0xe0,                         // fnstsw ax
			0xf6, 0xc4, 0x41,                   // test ah,0x41: at most 0 (or NaN): keep
			0x75, 0x0c,                         // jnz keep
			0x8b, 0x06,                         // mov eax,[esi]
			0x8b, 0xce,                         // mov ecx,esi
			0x6a, 0x00,                         // push 0.0f
			0xff, 0x90, 0xa4, 0x01, 0x00, 0x00, // call [eax+0x1a4] (thiscall, pops its argument)
			0x6a, 0x00,                         // keep: push 0          (displaced)
			0x8d, 0x44, 0x24, 0x28,             // lea eax,[esp+0x28]    (displaced)
			0xe9, 0, 0, 0, 0,                   // jmp resume
		};
		for (std::size_t i = 0; i < 4; ++i)
		{
			code[8 + i] = static_cast<std::uint8_t>(zero >> (8 * i));
		}
		const address relative = resume - (at + static_cast<address>(code.size()));
		for (std::size_t i = 0; i < 4; ++i)
		{
			code[code.size() - 4 + i] = static_cast<std::uint8_t>(relative >> (8 * i));
		}
		return code;
	}

	// The jump written over the displaced instructions: e9 and the distance, a nop for the sixth byte.
	inline std::vector<std::uint8_t> death_jump(const address bridge)
	{
		const address relative = bridge - (death_branch_va + 5);
		return {0xe9, static_cast<std::uint8_t>(relative), static_cast<std::uint8_t>(relative >> 8), static_cast<std::uint8_t>(relative >> 16),
		        static_cast<std::uint8_t>(relative >> 24), 0x90};
	}

	// The first site (or the 1.0 constant, reported as nullptr with `constant_ok` false) that `image` (XMen2.exe
	// mapped at its base) doesn't have; nullptr with `constant_ok` true when all match.
	inline const site* first_mismatch(const std::uint8_t* image, bool& constant_ok)
	{
		constant_ok = limits_rules::matches(image + (one_constant - image_base), "0000803f") &&
		              limits_rules::matches(image + (zero_constant - image_base), "00000000");
		for (const auto& s : sites)
		{
			if (!limits_rules::matches(image + (s.va - image_base), s.hex))
			{
				return &s;
			}
		}
		if (!limits_rules::matches(image + (death_branch_va - image_base), death_branch_site.hex))
		{
			return &death_branch_site;
		}
		return nullptr;
	}
}
