#pragma once

// [Game] NpcPowerRule: X-Men Legends 1's rules for when a computer-controlled character uses a power, kept apart from
// the installer so xml2_test can check them without the game: the ini value's rules, the decisions both games make
// (as plain arithmetic), every retail byte the change relies on and the emitted bridge code.
//
// Every XMen2.exe address is the retail build's (image base 0x400000, no relocations); every XML1 one is from XML1's
// default.xbe (Xbox, 2004; its addresses as loaded at 0x10000). docs/npc-power-rule.md has the design and the
// measurements.
//
// The two games share one AI design for powers: four power slots per AI (aitype, a mask from the same 18-entry
// table, aireusetime), one picker that takes a random power whose reuse time has passed and whose aitype range
// holds the target's distance (XML1 0x1081f0, XMen2.exe 0x50fe50: the same selection design), and the same callers. Three
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
// And two in the melee combos (the brain's combo picker: XML1 0xeea60, XMen2.exe 0x518fd0; same shape):
//
//   4. The combo level. Both AI inits give a computer-controlled character's brain a combo level the same way: 2 for
//      a ranged-type NPC (AI type not 0..3) without the leader bit, the top level otherwise, 3 for Toad and Pyro
//      ("oad", "yro"; XML1 0xfd330 / 0xfd737, XMen2.exe 0x504a20 / 0x504f5b). XML1's top level is 6 of its 6 combos
//      (default.xbe 0x452bac buttons, 0x452c30 lengths, 0x452c18 weights 5/25/20/15/15/15, summed over the first
//      `level` combos by 0xedb20); XMen2.exe's is 11 ("finishers") of 12 named rows over 11 combos (0x6dd4ac,
//      0x6dd4d8, rows 0x6dd508; 0x517c30). Then XMen2.exe adds one call XML1 doesn't make: the level from npcstat
//      `combolevel` (stats+0x2a8, 0x505011), 0 ("basic") for every entry without the attribute - and level 0 never
//      starts a combo (0x518fd0: `level > 0 || controller != -1`). So the first game's NPCs, which carry no
//      `combolevel`, only ever throw one attack (attacklight1) at a time.
//      Rule: the override goes (0x505011: the call becomes `add esp,4`), and the three rows XMen2.exe's init picks -
//      2, 3 and 11 - hold XML1's levels 2, 3 and 6 over XML1's six combos, written into combo slots 0..5. Rows 0,
//      1 and 4..10 keep their weights; nothing reaches them without `combolevel`, which no XML1 entry has.
//   5. The pause after starting a combo. XML1 runs the attack decision's tail after a combo start (0xef040: jmp
//      0xef199), which sets the next attack time to now + meleetimeroffset + rand x meleetimerrandomadd (0.6 + up to
//      1.2 s by default) and the crowd delay. XMen2.exe returns at once (0x51993a, 0x519b04: mov al,1; ... ret 8),
//      so with combos its enemies attack again the moment a combo ends. Rule: both exits jump to the tail (0x519840)
//      as the single-attack path already does.
//   The continuation additionally follows XML1's timer (0xeebec..0xeecb7), rather than aborting a combo when
//   the next move cannot chain immediately (PC 0x51920b). Its final pause adds the last per-button delay,
//   not PC's fixed 0.2 s (0x5192cb). The XML1 timer advances even while actor+0x764 bit 0 is set; the existing
//   press routine still handles the actor's action suppression. The second XML1 combo table (brain mode 1)
//   has no PC counterpart and is not introduced here. This candidate needs matched live acceptance.
//
//   6. Visibility: XML1's 0xfe550 uses positions raised by 60 and unit sweep extents; its query scheduler 0xfc990
//      serves one query per AI tick and preserves a waiting actor's reservation. XMen2.exe uses different
//      endpoints, extents 15, two queries and a first-request bypass. The sites below restore the scheduler;
//      npc_visibility_rules.hpp supplies XML1's geometry and deferred/hit decisions. Both engines exclude
//      team characters from the collision candidates. A deferred query is not physical obstruction.
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
#include <utility>
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

	// XML1 0xfc990 reserves the next query for the waiting actor and serves one query per AI timestamp.
	// XMen2.exe 0x502730 instead grants the first request irrespective of that reservation and serves two.
	inline constexpr site query_priority_site{0x5027bd, "3bcb741f8b46108b1520ac78003b501c7411", 18, "the AI query's priority reservation"};
	inline constexpr site query_budget_site{0x5027e0, "83f9020f8d61010000", 3, "the AI query's per-tick budget"};
	inline constexpr site visibility_site{0x505250, "81ecd400000053568bf1", 6, "the AI visibility query entry"};
	inline constexpr site physical_target_site{0x4297b0, "568d4c2408e8f6bc0300", 0, "the physical-entity handle resolver"};
	inline constexpr site query_entry_site{0x502730, "6aff689853670064a100000000", 0, "the AI collision query entry"};

	// 4. The combo level override at the end of the AI init: mov edx,[ecx]; push eax; call [edx+0x44]; mov ecx,[esp+0x7c].
	inline constexpr site combolevel_site{0x50500e, "8b1150ff52448b4c247c", 0, "the AI init's combolevel override (0x504a20)"};
	constexpr address combolevel_call_va = 0x505011;
	// 5. The two combo starts' exits (mov [ebp+0x3c],esi; mov al,1; pop edi; pop esi; pop ebp; pop ebx; add esp,0x58;
	//    ret 8) and the tail they should reach (mov eax,[esp+0x1c], the single-attack path's jump target).
	inline constexpr site combo_exit_site{0x519937, "89753cb0015f5e5d5b83c458c20800", 0, "the combo start's exit (0x518fd0)"};
	inline constexpr site combo_exit2_site{0x519b01, "89753cb0015f5e5d5b83c458c20800", 0, "the leader combo start's exit (0x518fd0)"};
	inline constexpr site combo_tail_site{0x519840, "8b44241c", 0, "the attack decision's tail (0x518fd0)"};
	constexpr address combo_tail_va = 0x519840;
	constexpr address combo_exit_va = 0x51993a, combo_exit2_va = 0x519b04;
	// The combo tables (.data): 11 button-list pointers, 11 lengths, 12 rows of 11 weights.
	constexpr address combo_buttons_va = 0x6dd4ac, combo_lengths_va = 0x6dd4d8, combo_rows_va = 0x6dd508;
	constexpr std::size_t combo_row_size = 0x2c;
	inline constexpr site combo_buttons_site{combo_buttons_va, "40d46d0044d46d0048d46d0050d46d005cd46d0064d46d00", 0, "combo slots 0..5's buttons (0x6dd4ac)"};
	inline constexpr site combo_lengths_site{combo_lengths_va, "010000000100000002000000030000000200000002000000", 0, "combo slots 0..5's lengths (0x6dd4d8)"};
	inline constexpr site combo_row2_site{combo_rows_va + 2 * combo_row_size, "0000f0410000f0410000f0410000f04100000000000000000000000000000000000000000000000000000000", 0, "combo row 2 ('easy', 0x6dd560)"};
	inline constexpr site combo_row3_site{combo_rows_va + 3 * combo_row_size, "0000000000000000000000000000484200004842000048420000000000000000000000000000000000000000", 0, "combo row 3 ('medium', 0x6dd58c)"};
	inline constexpr site combo_row11_site{combo_rows_va + 11 * combo_row_size, "000000000000000000000000000000000000000000000000000000000000c8410000c8410000c8410000c841", 0, "combo row 11 ('finishers', 0x6dd6ec)"};

	// XML1's default combos (default.xbe 0x452bac / 0x452c30; 4 = attack, 5 = smash, the same action bits in both
	// games) and weights (0x452c18); level L uses the first L.
	inline constexpr std::array<std::array<std::uint32_t, 5>, 6> xml1_combos{{{4}, {4, 5}, {4, 4, 4}, {5, 5}, {4, 5, 4, 5}, {4, 5, 4, 4, 4}}};
	inline constexpr std::array<std::uint32_t, 6> xml1_combo_lengths{1, 2, 3, 2, 4, 5};
	inline constexpr std::array<float, 6> xml1_combo_weights{5, 25, 20, 15, 15, 15};
	// XMen2.exe row -> XML1 level: the init's 2 stays 2, Toad/Pyro's 3 stays 3, its top level 11 is XML1's 6.
	inline constexpr std::array<std::pair<std::size_t, std::size_t>, 3> combo_row_levels{{{2, 2}, {3, 3}, {11, 6}}};
	inline std::array<float, 11> xml1_row(const std::size_t level)
	{
		std::array<float, 11> row{};
		for (std::size_t i = 0; i < level && i < xml1_combo_weights.size(); ++i)
		{
			row[i] = xml1_combo_weights[i];
		}
		return row;
	}

	// 6. The combo's continue step: mov ecx,[ebp+0x30]; mov edx,[ebp+0x38]; cmp [edx+ecx*4+4],5 (the chain check's
	//    button), XMen2.exe's press code, its end code and the common continuation.
	inline constexpr site combo_continue_site{0x51920b, "8b4d308b5538837c8a0405", 6, "the combo's continue step (0x518fd0)"};
	inline constexpr site combo_press_site{0x51924f, "8bcfe8cad7f7ff84c0", 0, "the combo's press code (0x518fd0)"};
	inline constexpr site combo_end_site{0x51928a, "c6452c00", 0, "the combo's end code (0x518fd0)"};
	inline constexpr site combo_after_site{0x5192f8, "837d08ff", 0, "the combo step's continuation (0x518fd0)"};
	constexpr address combo_continue_va = 0x51920b, combo_continue_resume_va = 0x519211, combo_press_va = 0x51924f,
	                  combo_end_va = 0x51928a, combo_after_va = 0x5192f8;
	// The brains live in the AI table: AI i at 0x78ba14 + i x 0x61c (0x5250b0's 32 slots), its brain at AI+0x4c8.
	constexpr address brain0_va = 0x78ba14 + 0x4c8;
	constexpr std::uint32_t ai_stride = 0x61c, ai_slots = 32, ai_now = 0x550;
	// XML1's per-press delays for its six combos (default.xbe 0x452b94).
	inline constexpr std::array<std::array<float, 5>, 6> xml1_combo_delays{{{0.0f}, {0.2f, 0.2f}, {0.2f, 0.2f, 0.2f}, {0.2f, 0.2f},
	                                                                          {0.2f, 0.2f, 0.2f, 0.2f}, {0.3f, 0.3f, 0.3f, 0.3f, 0.6f}}};
	inline constexpr float max_combo_delay = [] {
		float maximum = 0;
		for (const auto& row : xml1_combo_delays) for (const auto delay : row) if (delay > maximum) maximum = delay;
		return maximum;
	}();

	inline constexpr site combo_end_delay_site{0x5192cb, "d805fc2f6800eb1a", 6, "the completed combo pause"};
	inline constexpr site ai_pool_site{0x5252f7, "b914ba7800", 0, "the AI pool base"};
	inline constexpr site ai_stride_site{0x525091, "69c91c060000", 0, "the AI pool stride"};
	inline constexpr site ai_count_site{0x5250bf, "83fa20", 0, "the AI pool count"};
	inline constexpr site ai_brain_site{0x504b1c, "8dbec8040000", 0, "the embedded combo brain"};

	inline constexpr std::array<const site*, 28> sites{&charge_site, &recheck_site, &recheck_actor_site, &picker_site, &energy_getter_site,
	                                                   &combolevel_site, &combo_exit_site, &combo_exit2_site, &combo_tail_site,
	                                                   &combo_buttons_site, &combo_lengths_site, &combo_row2_site, &combo_row3_site, &combo_row11_site,
	                                                   &query_priority_site, &query_budget_site, &visibility_site, &physical_target_site, &query_entry_site,
	                                                   &combo_continue_site, &combo_press_site, &combo_end_site, &combo_after_site, &combo_end_delay_site,
	                                                   &ai_pool_site, &ai_stride_site, &ai_count_site, &ai_brain_site};

	inline std::vector<std::uint8_t> retail_bytes(const site& s, const std::size_t count)
	{
		std::vector<std::uint8_t> bytes(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			bytes[i] = limits_rules::hex_byte(s.hex, i);
		}
		return bytes;
	}

	// `count` retail bytes of a site from byte `from`.
	inline std::vector<std::uint8_t> sub_bytes(const site& s, const std::size_t from, const std::size_t count)
	{
		std::vector<std::uint8_t> bytes(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			bytes[i] = limits_rules::hex_byte(s.hex, from + i);
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

	// The block holds code, constants and six combo sequences, followed by a separate writable page of deadlines.
	struct layout
	{
		address base;
		address charge, recheck, gate, constants;
		address minus_half() const { return constants; }
		address thousandth() const { return constants + 4; }
		address two() const { return constants + 8; }
		address half() const { return constants + 12; }
		address max_delay() const { return constants + 16; }
		address combos = 0;
		address combo(const std::size_t i) const { return combos + static_cast<address>(20 * i); }
		address cont = 0, start = 0, delays = 0, next = 0, end_delay = 0; // the combo step bridges, XML1's delays, the per-brain times
	};
	// The block: one page of code and constants (made execute-read), then one page of data (read-write): the next
	// press time of each of the 32 brains.
	constexpr std::size_t charge_size = 57, recheck_size = 23, gate_size = 47, cont_size = 149, start_size = 71, end_delay_size = 32;
	constexpr std::size_t code_size = 4096, block_size = 8192;
	inline layout layout_at(const address base)
	{
		layout l{base, base, base + 64, base + 96, base + 160};
		l.combos = base + 192; // 6 x 20 bytes (up to five buttons each)
		l.cont = base + 320;
		l.start = base + 480;
		l.delays = base + 560; // 6 x 20 bytes
		l.next = base + static_cast<address>(code_size);
		l.end_delay = base + 688;
		return l;
	}
	inline std::vector<std::uint8_t> constants()
	{
		std::vector<std::uint8_t> data(20);
		std::uint32_t values[5] = {float_bits(-0.5f), 0x3a83126fu /* default.xbe 0x3c832c, 0.001 */, float_bits(xml1_recheck), float_bits(xml1_recheck_party), float_bits(max_combo_delay)};
		std::memcpy(data.data(), values, sizeof(values));
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

	// 6. The continue step (jumped to from 0x51920b; ebp = the brain, edi = the actor, esi = the pad state; eax, ecx,
	//    edx are free - XMen2.exe's next instructions load them). Brains outside the table take XMen2.exe's own step.
	inline std::vector<std::uint8_t> cont_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x8b, 0xc5,                               // 00 mov eax,ebp
			0x2d, 0, 0, 0, 0,                         // 02 sub eax,brain0
			0x72, 0x6a,                               // 07 jb fallback (0x73)
			0x33, 0xd2,                               // 09 xor edx,edx
			0xb9, 0x1c, 0x06, 0x00, 0x00,             // 0b mov ecx,0x61c
			0xf7, 0xf1,                               // 10 div ecx
			0x85, 0xd2,                               // 12 test edx,edx
			0x75, 0x5d,                               // 14 jnz fallback
			0x83, 0xf8, 0x20,                         // 16 cmp eax,32
			0x73, 0x58,                               // 19 jae fallback
			0x8d, 0x14, 0x85, 0, 0, 0, 0,             // 1b lea edx,[eax*4+next]
			0x8b, 0x4d, 0x04,                         // 22 mov ecx,[ebp+4] (the AI)
			0xd9, 0x81, 0x50, 0x05, 0x00, 0x00,       // 25 fld dword [ecx+0x550] (now)
			0xd8, 0x1a,                               // 2b fcomp dword [edx]
			0xdf, 0xe0,                               // 2d fnstsw ax
			0xf6, 0xc4, 0x41,                         // 2f test ah,0x41: now <= next
			0x75, 0x4a,                               // 32 jnz check remaining time (0x7e)
			0x8b, 0x45, 0x30,                         // 34 mov eax,[ebp+0x30]
			0x40,                                     // 37 inc eax
			0x89, 0x45, 0x30,                         // 38 mov [ebp+0x30],eax
			0x3b, 0x45, 0x34,                         // 3b cmp eax,[ebp+0x34]
			0x7d, 0x2e,                               // 3e jge endcombo (0x6e)
			0x8b, 0x4d, 0x3c,                         // 40 mov ecx,[ebp+0x3c] (the combo)
			0x83, 0xf9, 0x06,                         // 43 cmp ecx,6
			0x73, 0x0f,                               // 46 jae zero (0x57)
			0x6b, 0xc9, 0x14,                         // 48 imul ecx,ecx,20
			0x8d, 0x4c, 0x81, 0xfc,                   // 4b lea ecx,[ecx+eax*4-4]: delay[i - 1]
			0xd9, 0x81, 0, 0, 0, 0,                   // 4f fld dword [ecx+delays]
			0xeb, 0x02,                               // 55 jmp addnow
			0xd9, 0xee,                               // 57 zero: fldz
			0x8b, 0x4d, 0x04,                         // 59 addnow: mov ecx,[ebp+4]
			0xd8, 0x81, 0x50, 0x05, 0x00, 0x00,       // 5c fadd dword [ecx+0x550]
			0xd9, 0x1a,                               // 62 fstp dword [edx]
			0xe9, 0, 0, 0, 0,                         // 64 jmp 0x51924f (press)
			0xe9, 0, 0, 0, 0,                         // 69 notyet: jmp 0x5192f8
			0xe9, 0, 0, 0, 0,                         // 6e endcombo: jmp 0x51928a
			0x8b, 0x4d, 0x30,                         // 73 fallback: mov ecx,[ebp+0x30] (displaced)
			0x8b, 0x55, 0x38,                         // 76 mov edx,[ebp+0x38] (displaced)
			0xe9, 0, 0, 0, 0,                         // 79 jmp 0x519211
			// A slot can survive a clock rewind/reuse. Never wait beyond the longest table delay.
			0xd9, 0x02,                               // 7e fld dword [edx] (next)
			0xd8, 0xa1, 0x50, 0x05, 0x00, 0x00,       // 80 fsub dword [ecx+0x550] (now)
			0xd8, 0x1d, 0, 0, 0, 0,                   // 86 fcomp dword [max_delay]
			0xdf, 0xe0,                               // 8c fnstsw ax
			0xf6, 0xc4, 0x45,                         // 8e test ah,0x45: greater only (NaN still waits)
			0x74, 0xa1,                               // 91 jz due (0x34)
			0xeb, 0xd4,                               // 93 jmp notyet (0x69)
		};
		put32(code, 0x03, brain0_va);
		put32(code, 0x1e, l.next);
		put32(code, 0x51, l.delays);
		put32(code, 0x65, combo_press_va - (l.cont + 0x69));
		put32(code, 0x6a, combo_after_va - (l.cont + 0x6e));
		put32(code, 0x6f, combo_end_va - (l.cont + 0x73));
		put32(code, 0x7a, combo_continue_resume_va - (l.cont + 0x7e));
		put32(code, 0x88, l.max_delay());
		return code;
	}
	// The combo start (the exits' jump; ebp = the brain, [ebp+0x3c] = the combo): next = now + delay[0], then the
	// decision's tail.
	inline std::vector<std::uint8_t> start_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x8b, 0xc5,                         // 00 mov eax,ebp
			0x2d, 0, 0, 0, 0,                   // 02 sub eax,brain0
			0x72, 0x39,                         // 07 jb done (0x42)
			0x33, 0xd2,                         // 09 xor edx,edx
			0xb9, 0x1c, 0x06, 0x00, 0x00,       // 0b mov ecx,0x61c
			0xf7, 0xf1,                         // 10 div ecx
			0x85, 0xd2,                         // 12 test edx,edx
			0x75, 0x2c,                         // 14 jnz done
			0x83, 0xf8, 0x20,                   // 16 cmp eax,32
			0x73, 0x27,                         // 19 jae done
			0x8d, 0x14, 0x85, 0, 0, 0, 0,       // 1b lea edx,[eax*4+next]
			0x8b, 0x4d, 0x3c,                   // 22 mov ecx,[ebp+0x3c]
			0x83, 0xf9, 0x06,                   // 25 cmp ecx,6
			0x73, 0x0b,                         // 28 jae zero (0x35)
			0x6b, 0xc9, 0x14,                   // 2a imul ecx,ecx,20
			0xd9, 0x81, 0, 0, 0, 0,             // 2d fld dword [ecx+delays]
			0xeb, 0x02,                         // 33 jmp addnow
			0xd9, 0xee,                         // 35 zero: fldz
			0x8b, 0x4d, 0x04,                   // 37 addnow: mov ecx,[ebp+4]
			0xd8, 0x81, 0x50, 0x05, 0x00, 0x00, // 3a fadd dword [ecx+0x550]
			0xd9, 0x1a,                         // 40 fstp dword [edx]
			0xe9, 0, 0, 0, 0,                   // 42 done: jmp 0x519840
		};
		put32(code, 0x03, brain0_va);
		put32(code, 0x1e, l.next);
		put32(code, 0x2f, l.delays);
		put32(code, 0x43, combo_tail_va - (l.start + 0x47));
		return code;
	}
	inline std::vector<std::uint8_t> continue_jump(const layout& l)
	{
		const address rel = l.cont - (combo_continue_va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90};
	}
	// ST0 already holds now + melee offset + random addition. XML1 adds delay[length - 1]. EAX/ECX and
	// flags are dead here; the next native instructions reload ECX and pass ST0 to the scheduler.
	inline std::vector<std::uint8_t> end_delay_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x8b, 0x4d, 0x3c,                   // 00 mov ecx,[ebp+0x3c]
			0x83, 0xf9, 0x06,                   // 03 cmp ecx,6
			0x73, 0x11,                         // 06 jae fallback
			0x6b, 0xc9, 0x14,                   // 08 imul ecx,ecx,20
			0x8b, 0x45, 0x30,                   // 0b mov eax,[ebp+0x30] (length)
			0x8d, 0x4c, 0x81, 0xfc,             // 0e lea ecx,[ecx+eax*4-4]
			0xd8, 0x81, 0, 0, 0, 0,             // 12 fadd dword [ecx+delays]
			0xc3,                               // 18 ret
			0xd8, 0x05, 0xfc, 0x2f, 0x68, 0x00, // 19 fallback: fadd dword [0.2]
			0xc3,
		};
		put32(code, 0x14, l.delays);
		return code;
	}
	inline std::vector<std::uint8_t> end_delay_call(const layout& l)
	{
		std::vector<std::uint8_t> code{0xe8, 0, 0, 0, 0, 0x90};
		put32(code, 1, l.end_delay - (combo_end_delay_site.va + 5));
		return code;
	}

	// The whole block's bytes for an allocation at `base`.
	inline std::vector<std::uint8_t> block(const address base)
	{
		const auto l = layout_at(base);
		std::vector<std::uint8_t> bytes(code_size, 0xcc);
		const auto put = [&](const address at, const std::vector<std::uint8_t>& code) { std::memcpy(bytes.data() + (at - base), code.data(), code.size()); };
		put(l.charge, charge_bridge(l));
		put(l.recheck, recheck_bridge(l));
		put(l.gate, gate_bridge(l));
		put(l.constants, constants());
		for (std::size_t i = 0; i < xml1_combos.size(); ++i)
		{
			std::vector<std::uint8_t> buttons(4 * xml1_combo_lengths[i]);
			std::memcpy(buttons.data(), xml1_combos[i].data(), buttons.size());
			put(l.combo(i), buttons);
			std::vector<std::uint8_t> delays(20);
			std::memcpy(delays.data(), xml1_combo_delays[i].data(), 20);
			put(l.delays + static_cast<address>(20 * i), delays);
		}
		put(l.cont, cont_bridge(l));
		put(l.start, start_bridge(l));
		put(l.end_delay, end_delay_bridge(l));
		return bytes;
	}

	// The combo writes: slots 0..5's buttons (pointers into the block) and lengths, and rows 2, 3, 11.
	inline std::vector<std::uint8_t> combo_buttons_bytes(const layout& l)
	{
		std::vector<std::uint8_t> bytes(24);
		for (std::size_t i = 0; i < 6; ++i)
		{
			put32(bytes, 4 * i, l.combo(i));
		}
		return bytes;
	}
	inline std::vector<std::uint8_t> combo_lengths_bytes()
	{
		std::vector<std::uint8_t> bytes(24);
		std::memcpy(bytes.data(), xml1_combo_lengths.data(), 24);
		return bytes;
	}
	inline std::vector<std::uint8_t> combo_row_bytes(const std::size_t level)
	{
		const auto row = xml1_row(level);
		std::vector<std::uint8_t> bytes(combo_row_size);
		std::memcpy(bytes.data(), row.data(), combo_row_size);
		return bytes;
	}
	// 0x505011: call [edx+0x44] -> add esp,4 (the pushed level is dropped; the init's own level stays).
	inline std::vector<std::uint8_t> combolevel_skip() { return {0x83, 0xc4, 0x04}; }
	// 0x51993a / 0x519b04: mov al,1; pop edi; pop esi; pop ebp; pop ebx -> jmp (the start bridge, which ends in a jmp
	// to the tail 0x519840); nop.
	inline std::vector<std::uint8_t> combo_exit_jump(const address at, const address target)
	{
		const address rel = target - (at + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90};
	}
	// The original reservation predicate fits the existing 18-byte run. ECX keeps the query count for the
	// following budget check; EAX/EDX are scratch here. The queued-age comparison at 0x5027cf stays in place.
	inline std::vector<std::uint8_t> query_priority_bytes()
	{
		return {0x8b, 0x15, 0x20, 0xac, 0x78, 0x00, // mov edx,[reserved actor]
		        0x3b, 0xd3, 0x74, 0x19,             // cmp edx,ebx (zero); je budget
		        0x8b, 0x46, 0x10,                   // mov eax,[esi+actor]
		        0x3b, 0x50, 0x1c, 0x74, 0x11};     // cmp edx,[eax+handle]; je budget
	}
	inline std::vector<std::uint8_t> query_budget_bytes() { return {0x83, 0xf9, 0x01}; }
	inline std::vector<std::uint8_t> visibility_jump(const address target)
	{
		std::vector<std::uint8_t> bytes{0xe9, 0, 0, 0, 0, 0x90};
		put32(bytes, 1, target - (visibility_site.va + 5));
		return bytes;
	}
}
