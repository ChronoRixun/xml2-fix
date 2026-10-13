#pragma once

// [Game] NpcPowerRule: X-Men Legends 1's rules for when a computer-controlled character uses a power, kept apart from
// the installer so xml2_test can check them without the game: the ini value's rules, the decisions both games make
// (as plain arithmetic), every retail byte the change relies on and the code of its three bridges.
//
// Every XMen2.exe address is the retail build's (image base 0x400000, no relocations); every XML1 one is from XML1's
// default.xbe (Xbox, 2004; its addresses as loaded at 0x10000). docs/npc-power-rule.md has the design and the
// measurements.
//
// The two games share one AI design for powers: four power slots per AI (aitype, a mask from the same 18-entry
// table, aireusetime), one picker that takes a random power whose reuse time has passed and whose aitype range
// holds the target's distance (XML1 0x1081f0, XMen2.exe 0x50fe50: the same function), and the same callers. Three
// things differ, and together they make every XML1 enemy with powers use them far more often in XMen2.exe:
//
//   1. Paying for a power. Both games charge a power trigger's powerusage through one function (XML1 0xcde90 ->
//      0x3f410, XMen2.exe 0x4daa30 -> 0x4318d0). XML1 skips the charge only for an AI-flagged character whose AI
//      time (+0x550) is set (at most -0.5 or at least 0.001: a party hero handed to the AI) and whose controller
//      then says no (0xcde95..0xcdedf). Every other character pays, enemies included. XMen2.exe skips the test of
//      the AI time: every AI-flagged character (actor+0x3d8 bit 0, every enemy) asks its controller, and the
//      controller answers yes only once after a queued command (0x503160: AI+0x2bc, set by 0x50b120). So no enemy
//      ever pays: its energy stays full and nothing but aireusetime limits its powers.
//      Rule: the test XML1 makes, on XMen2.exe's fields (+0x768 is XML1's +0x550: the same writers, e.g. 0x5068a0
//      clears it where XML1's 0x10b5c0 does).
//   2. How often the melee-range power decision runs. XML1's (0xee260) re-arms its timer (AI+0x1b8) for 2.0 s, or
//      0.5 s for a character with a controller (actor+0x330 != -1: a party hero); XMen2.exe's (0x518770; AI+0x1cc)
//      for 0.3 s (0x681ebc) whoever it is. Rule: XML1's 2.0 / 0.5 (actor+0x3d4 is XML1's +0x330: the same test
//      gates the buff-self powers in 0x10ad70 and 0x512ed0).
//   3. Energy before that decision. XML1 picks a melee-range power only with at least half of its energy:
//      max (word +0x2bc) / energy <= 2.0, unless AI flag 0x28a bit 3 forces it (0xee37a..0xee3ae). XMen2.exe has
//      no such test there. Rule: the same test on XMen2.exe's fields (energy getter 0x41d5d0, maximum +0x314, the
//      forcing flag AI+0x2b6 bit 3, which 0x518770 already reads for its timer), as max <= 2 x energy.
//
// Nothing a save reads changes; the energy fields already exist and are saved as before.

#include "ini_rules.hpp"    // value_text
#include "limits_rules.hpp" // matches, hex_byte, hex_size, image_base

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace npc_power_rules
{
	using address = std::uint32_t;
	using limits_rules::image_base;

	// ---- The ini value ----------------------------------------------------------------------------------

	enum class rule
	{
		xml2,
		xml1,
	};

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
		refused.error = "isn't xml1 (X-Men Legends 1's power use for computer-controlled characters) or xml2 (the game's own)";
		return refused;
	}

	// ---- The decisions, as plain arithmetic ---------------------------------------------------------------

	// 1. Does a power trigger's cost come off this character's energy (before the hero-team cheat and the cost
	//    affecters, which both games apply after)? `ai_flag`: actor+0x3d8 bit 0 (XML1 +0x334 bit 0); `ai_time`:
	//    actor+0x768 (XML1 +0x550); `controller_says_yes`: the controller's answer (XMen2.exe 0x503160).
	inline bool pays_xml2(const bool ai_flag, const float /*ai_time*/, const bool controller_says_yes)
	{
		return !ai_flag || controller_says_yes;
	}
	// XML1 0xcde95: fld [+0x550]; fcomp [-0.5]; test ah,0x41; jne ask; fld [+0x550]; fcomp [0.001]; test ah,5; jnp pay.
	// A NaN asks (C3 set at the first test).
	inline bool pays_xml1(const bool ai_flag, const float ai_time, const bool controller_says_yes)
	{
		if (!ai_flag)
		{
			return true;
		}
		const bool asks = std::isnan(ai_time) || ai_time <= -0.5f || ai_time >= 0.001f;
		return !asks || controller_says_yes;
	}

	// 2. Seconds until the melee-range power decision runs again. `controller`: actor+0x3d4 (XML1 +0x330).
	constexpr float xml2_recheck = 0.3f;   // 0x681ebc
	constexpr float xml1_recheck = 2.0f;   // default.xbe 0x3c9404
	constexpr float xml1_recheck_party = 0.5f; // default.xbe 0x3c7084
	inline float recheck_xml1(const std::int32_t controller)
	{
		return controller == -1 ? xml1_recheck : xml1_recheck_party;
	}

	// 3. May the melee-range decision pick a power? XML1: max / energy <= 2.0 (or the forcing flag). The bridge tests
	//    max <= 2 x energy, the same for every positive energy and a maximum of 0; an energy of 0 refuses as
	//    max / 0 = +inf does in XML1 (the one difference: a NaN energy refuses here and passes there).
	inline bool energy_allows_xml1(const float energy, const float max, const bool forced)
	{
		return forced || max <= 2.0f * energy;
	}

	// ---- The sites -----------------------------------------------------------------------------------------

	struct site
	{
		address va;          // where the guarded bytes start
		std::string_view hex; // the retail bytes, all guarded
		std::size_t patched;  // how many of them the change writes over (from va)
		const char* what;
	};

	// 1. The charge's AI test: test byte [esi+0x3d8],1; je pay (0x43190e); then the controller's question at 0x4318f0.
	inline constexpr site charge_site{0x4318e7, "f686d803000001741ee8eb390f00", 9, "the power cost's AI test (0x4318d0)"};
	constexpr address charge_ask_va = 0x4318f0; // call 0x5252e0: ask the controller
	constexpr address charge_pay_va = 0x43190e; // the charge itself
	// 2. The decision's re-check time: fadd dword [0x681ebc] (now + 0.3), then mov eax,[edi+4]; fstp [eax+0x1cc]. The
	//    guard also covers the controller read at 0x5188c3 (ebp is the deciding character there).
	inline constexpr site recheck_site{0x5188d8, "d805bc1e68008b4704d998cc010000", 6, "the melee-range power decision's re-check time (0x518770)"};
	inline constexpr site recheck_actor_site{0x5188c3, "8b85d4030000", 0, "the deciding character in ebp (0x518770)"};
	// 3. The decision's picker call: push 4; call 0x50fe50; test al,al.
	inline constexpr site picker_site{0x51891e, "6a04e82b75ffff84c0", 0, "the melee-range power decision's picker call (0x518770)"};
	constexpr address picker_call_va = 0x518920;
	constexpr address picker_va = 0x50fe50;
	constexpr address energy_getter_va = 0x41d5d0; // fld [ecx+0x288] (or the maximum under the energy cheats)
	inline constexpr site energy_getter_site{0x41d601, "d98688020000", 0, "the energy getter's read of +0x288 (0x41d5d0)"};

	inline constexpr std::array<const site*, 5> sites{&charge_site, &recheck_site, &recheck_actor_site, &picker_site, &energy_getter_site};

	inline std::vector<std::uint8_t> retail_bytes(const site& s, const std::size_t count)
	{
		std::vector<std::uint8_t> bytes(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			bytes[i] = limits_rules::hex_byte(s.hex, i);
		}
		return bytes;
	}

	inline const site* first_mismatch(const std::uint8_t* image)
	{
		for (const auto* s : sites)
		{
			if (!limits_rules::matches(image + (s->va - image_base), s->hex))
			{
				return s;
			}
		}
		return nullptr;
	}

	// ---- The bridges -----------------------------------------------------------------------------------------

	inline void put32(std::vector<std::uint8_t>& code, const std::size_t at, const std::uint32_t value)
	{
		for (std::size_t i = 0; i < 4; ++i)
		{
			code[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
		}
	}

	inline std::uint32_t float_bits(const float f)
	{
		std::uint32_t bits = 0;
		std::memcpy(&bits, &f, 4);
		return bits;
	}

	// The block: the three bridges, then the four constants. One allocation, laid out by `layout`.
	struct layout
	{
		address base;
		address charge, recheck, gate, constants;
		address minus_half() const { return constants; }
		address thousandth() const { return constants + 4; }
		address two() const { return constants + 8; }
		address half() const { return constants + 12; }
	};
	constexpr std::size_t charge_size = 57, recheck_size = 23, gate_size = 47, block_size = 192;
	inline layout layout_at(const address base)
	{
		return {base, base, base + 64, base + 96, base + 160};
	}
	inline std::vector<std::uint8_t> constants()
	{
		std::vector<std::uint8_t> data(16);
		std::uint32_t values[4] = {float_bits(-0.5f), 0x3a83126fu /* default.xbe 0x3c832c, 0.001 */, float_bits(xml1_recheck), float_bits(xml1_recheck_party)};
		std::memcpy(data.data(), values, 16);
		return data;
	}

	// 1. Jumped to from 0x4318e7 (esi = the paying character; eax is free: the code before ends in fnstsw ax and
	//    both continuations reload it).
	inline std::vector<std::uint8_t> charge_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0xf6, 0x86, 0xd8, 0x03, 0x00, 0x00, 0x01, // test byte [esi+0x3d8],1
			0x74, 0x2b,                               // jz pay (+43)
			0xd9, 0x86, 0x68, 0x07, 0x00, 0x00,       // fld dword [esi+0x768]
			0xd8, 0x1d, 0, 0, 0, 0,                   // fcomp dword [-0.5]
			0xdf, 0xe0,                               // fnstsw ax
			0xf6, 0xc4, 0x41,                         // test ah,0x41
			0x75, 0x13,                               // jnz ask (+19): at most -0.5, or NaN
			0xd9, 0x86, 0x68, 0x07, 0x00, 0x00,       // fld dword [esi+0x768]
			0xd8, 0x1d, 0, 0, 0, 0,                   // fcomp dword [0.001]
			0xdf, 0xe0,                               // fnstsw ax
			0xf6, 0xc4, 0x05,                         // test ah,5
			0x7b, 0x05,                               // jnp pay (+5): below 0.001
			0xe9, 0, 0, 0, 0,                         // ask: jmp 0x4318f0
			0xe9, 0, 0, 0, 0,                         // pay: jmp 0x43190e
		};
		put32(code, 17, l.minus_half());
		put32(code, 36, l.thousandth());
		put32(code, 48, charge_ask_va - (l.charge + 52));
		put32(code, 53, charge_pay_va - (l.charge + 57));
		return code;
	}

	// 2. Called from 0x5188d8 in place of fadd [0x681ebc] (st0 = the game clock, ebp = the deciding character); the
	//    flags it leaves are dead (the next flag test follows an add).
	inline std::vector<std::uint8_t> recheck_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x83, 0xbd, 0xd4, 0x03, 0x00, 0x00, 0xff, // cmp dword [ebp+0x3d4],-1
			0x74, 0x07,                               // je npc (+7)
			0xd8, 0x05, 0, 0, 0, 0,                   // fadd dword [0.5]
			0xc3,                                     // ret
			0xd8, 0x05, 0, 0, 0, 0,                   // npc: fadd dword [2.0]
			0xc3,                                     // ret
		};
		put32(code, 11, l.half());
		put32(code, 18, l.two());
		return code;
	}

	// 3. Called from 0x518920 in place of the picker (thiscall, ecx = the AI, five stack arguments, ret 0x14). Unless
	//    the forcing flag is set, refuses (al = 0, as the picker refusing) when max > 2 x energy; else jumps to the
	//    picker with the caller's frame untouched. eax and edx are free (the picker sets them); ecx is kept.
	inline std::vector<std::uint8_t> gate_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0xf6, 0x81, 0xb6, 0x02, 0x00, 0x00, 0x08, // test byte [ecx+0x2b6],8
			0x75, 0x1c,                               // jnz go (+28)
			0x51,                                     // push ecx
			0x8b, 0x49, 0x10,                         // mov ecx,[ecx+0x10]
			0xe8, 0, 0, 0, 0,                         // call 0x41d5d0 (st0 = energy)
			0x59,                                     // pop ecx
			0x8b, 0x41, 0x10,                         // mov eax,[ecx+0x10]
			0xd8, 0xc0,                               // fadd st0,st0
			0xd8, 0x98, 0x14, 0x03, 0x00, 0x00,       // fcomp dword [eax+0x314]
			0xdf, 0xe0,                               // fnstsw ax
			0xf6, 0xc4, 0x01,                         // test ah,1: 2 x energy below max (or NaN)
			0x75, 0x05,                               // jnz refuse (+5)
			0xe9, 0, 0, 0, 0,                         // go: jmp 0x50fe50
			0x32, 0xc0,                               // refuse: xor al,al
			0xc2, 0x14, 0x00,                         // ret 0x14
		};
		put32(code, 14, energy_getter_va - (l.gate + 18));
		put32(code, 38, picker_va - (l.gate + 42));
		return code;
	}

	// The writes at the sites.
	inline std::vector<std::uint8_t> charge_jump(const layout& l)
	{
		const address rel = l.charge - (charge_site.va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90, 0x90, 0x90, 0x90};
	}
	inline std::vector<std::uint8_t> recheck_call(const layout& l)
	{
		const address rel = l.recheck - (recheck_site.va + 5);
		return {0xe8, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90};
	}
	inline std::vector<std::uint8_t> picker_call(const layout& l)
	{
		const address rel = l.gate - (picker_call_va + 5);
		return {0xe8, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24)};
	}

	// The whole block's bytes for an allocation at `base`.
	inline std::vector<std::uint8_t> block(const address base)
	{
		const auto l = layout_at(base);
		std::vector<std::uint8_t> bytes(block_size, 0xcc);
		const auto put = [&](const address at, const std::vector<std::uint8_t>& code) { std::memcpy(bytes.data() + (at - base), code.data(), code.size()); };
		put(l.charge, charge_bridge(l));
		put(l.recheck, recheck_bridge(l));
		put(l.gate, gate_bridge(l));
		put(l.constants, constants());
		return bytes;
	}
}
