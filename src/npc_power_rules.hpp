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
// And two in how a hit becomes a block (revision 10). In both games a block is a melee to-hit miss: the melee
// contact (XML1 0x5c590, XMen2.exe 0x453160) rolls (0x5c240 / 0x44f450), a miss (2) zeroes the hit's damage and sets
// its "blocked" flag (XML1 0x5c965: damage info +0x60 bit 1, the hit record's +0x6c; XMen2.exe 0x45355c: +0x68 bit
// 0, the record's +0x74) before the victim's hit entry, and the victim's popup takes the block branch for a flagged
// hit (XML1 0x44696, XMen2.exe 0x435c30). The always-hit cases, the chance's inputs ([Balance] HitChance=xml1 gives
// XML1's formula) and the zeroing are the same. Two things differ:
//
//   7. Power attacks. XML1 never rolls an attack flagged PowerAttack (0x5c3c0: damage info +0x60 bit 0, result 1, no
//      draw). XMen2.exe's name for that flag is damagescale (PowerAttacks=xml1 reads the builder's `x1power` as
//      `none`; a miss counts only for `normal`, 0x45333f), but its roll has no damagescale test - only the move's
//      combo text (+0x35 bit 2, 0x44f58e) - so such an attack is rolled, a miss zeroes its damage (0x44f725) and
//      0x45355c flags it: the victim blocks a power attack that XML1 always lands.
//      Rule: at 0x44f58e an attack whose damagescale isn't `normal` takes the roll's always-hit return (result 1,
//      0x44f5aa's `mov edi,1; jmp 0x44f6bd`, no draw), as XML1 returns before its draw.
//   8. The receive's block_chance roll. XMen2.exe's damage receive 0x437490 multiplies effect 0x37 (`block_chance`)
//      mode 0 and mode 1 and zeroes the damage when its draw (0x437ae0) is at most that (0x437b08); it sets no flag,
//      so the hit then neither blocks nor pains. XML1's receive 0x45260 has no such roll (default.xbe has no
//      `block_chance` string). With no effect-0x37 affecter the chance is 0 (mode 0 starts at 0.0, 0x550410), so it
//      fires only on a draw of exactly 0.0.
//      Rule: 0x437b08's `jp` (draw above the chance: skip) becomes a `jmp`; the draw stays, so the random sequence
//      is the game's.
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

	// 7. How the melee to-hit roll (0x44f450) goes on from 0x44f58e, by the attack's +0x35 (bit 2: the move has combo
	//    text; bits 3-6: damagescale, `normal` = 1 = 0x08). `combo_text`: the game's always-hit path (0x44f594),
	//    `rolled`: the draw as before (0x44f5b4), `hit`: result 1 without a draw (XML1's PowerAttack, 0x5c3c0).
	enum class roll_route
	{
		combo_text,
		rolled,
		hit,
	};
	constexpr std::uint8_t combo_text_bit = 0x04, damage_scale_bits = 0x78, damage_scale_normal = 0x08;
	inline roll_route roll_route_xml2(const std::uint8_t attack_35)
	{
		return (attack_35 & combo_text_bit) ? roll_route::combo_text : roll_route::rolled;
	}
	inline roll_route roll_route_xml1(const std::uint8_t attack_35)
	{
		if (attack_35 & combo_text_bit)
		{
			return roll_route::combo_text;
		}
		return (attack_35 & damage_scale_bits) == damage_scale_normal ? roll_route::rolled : roll_route::hit;
	}
	// Both games flag the hit blocked exactly when the roll returned 2 (XML1 0x5c95e, XMen2.exe 0x453555).
	inline bool flagged_blocked(const int roll_result) { return roll_result == 2; }

	// 8. Does the receive's block_chance roll zero the damage (0x437aee..0x437b08: damage above 0, then the draw at
	//    most the chance; an unordered compare skips)? XML1: never.
	inline bool block_chance_zeroes_xml2(const float damage, const float draw, const float chance)
	{
		return damage > 0.0f && draw <= chance;
	}
	inline bool block_chance_zeroes_xml1(float, float, float) { return false; }

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
	// 7. (revision 10) The hit's reaction test at the end of the pain decision 0x435850: test byte [esi+0xac],8; jz
	//    0x435be2. Bit 8 still set here = the hit makes the victim react (pain or block, chosen after this). XML1's
	//    brain (0xeea60) keeps running and pressing on its step clock while the body reacts, so the steps that come due
	//    during the reaction are spent; XMen2.exe's brain is not called while the body is busy, and its first call after
	//    the reaction presses the overdue step at once (the combo resumes from idle). The bridge marks the victim's AI
	//    slot; the continue step then spends every step that came due before the brain ran again.
	inline constexpr site reaction_site{0x435b6e, "f686ac00000008746b", 7, "the hit's reaction test (0x435850)"};
	constexpr address reaction_resume_va = 0x435b75;
	constexpr address ai0_va = 0x78ba14;
	// 8. (revision 10) The melee-range power decision's entry (sub esp,0x14; push ebx; push ebp; ecx = the brain).
	//    XML1's decision (0xee260) also runs while the body reacts: a re-check that falls due then is spent (the
	//    power press finds no `powers` chain in pain/block) and re-armed. XMen2.exe's first call after the reaction
	//    found it overdue and the power went off from idle. A reaction-marked slot re-arms every re-check that fell
	//    due before this call (timer + 2.0 s, or 0.5 s for a character with a controller), then the decision as before.
	inline constexpr site power_entry_site{0x518770, "83ec1453555657", 5, "the melee-range power decision's entry (0x518770)"};
	constexpr address power_entry_resume_va = 0x518775;
	constexpr address melee_power_call_return_va = 0x5193e7; // after 0x518fd0's call [edx+0xc] (0x5193e4)
	constexpr address game_clock_owner_va = 0x46dce0; // its vt+0x160 is the clock 0x518770 compares AI+0x1cc with
	inline constexpr site game_clock_site{0x518833, "e8a854f5ff8b4f048b91cc010000", 0, "the power decision's clock read (0x518770)"};
	// 9. (revision 10) The melee decision's entry 0x518fd0 (sub esp,0x58; push ebx; push ebp; ecx = the brain). XML1's
	//    attack decision also falls due while the body reacts; it is then spent (the attack press finds no chain in
	//    pain/block) and the next attack time is set again by the decision's tail (now + meleetimeroffset + random x
	//    meleetimerrandomadd). XMen2.exe made the overdue decision on the first idle frame after the reaction (a fresh
	//    attack within 0.3 s of 12-14 pains per four windows, against 1 on the Xbox). With no combo running, a
	//    reaction-marked slot whose next attack time (AI+0x1c) fell due re-arms it the tail's way from that time
	//    (AI+0x288 + random x AI+0x28c, the random draw of 0x560190(1) as the completion code uses), at most four
	//    times, then the decision as before.
	inline constexpr site attack_entry_site{0x518fd0, "83ec5853555657", 5, "the melee decision's entry (0x518fd0)"};
	constexpr address attack_entry_resume_va = 0x518fd5;
	constexpr address game_random_va = 0x560190;
	inline constexpr site attack_random_site{0x5192b8, "6a01e8d16e0400d84c2424", 0, "the combo completion's random draw (0x518fd0)"};
	// 10. (revision 10) The melee to-hit roll's combo-text test (test byte [esi+0x35],4; jz 0x44f5b4; esi = the
	//    attack, edi = the defender), the combo-text path after it and its always-hit return (mov edi,1; jmp
	//    0x44f6bd), all guarded; the result code at 0x44f6bd; the contact's damagescale test of a miss (0x45333f) and
	//    its blocked flag (0x453555: cmp [esp+0x20],2; jne; or byte [esp+0xa0],1), guarded only: the rule relies on
	//    their field encoding.
	inline constexpr site hit_roll_site{0x44f58e, "f646350474208a44241384c0c6442428017409f6876507000002750abf01000000e909010000", 6,
	                                    "the melee to-hit roll's always-hit test (0x44f450)"};
	inline constexpr site hit_roll_result_site{0x44f6bd, "8b55008bcdff128b10578bc8ff5224", 0, "the melee to-hit roll's result (0x44f450)"};
	inline constexpr site miss_counts_site{0x45333f, "8a463524783c087505c644241200", 0, "the melee contact's damagescale test of a miss (0x453160)"};
	inline constexpr site blocked_flag_site{0x453555, "837c2420027508808c24a000000001", 0, "the melee contact's blocked flag (0x453160)"};
	constexpr address hit_roll_combo_va = 0x44f594, hit_roll_rolled_va = 0x44f5b4, hit_roll_result_va = 0x44f6bd;
	// 11. (revision 10) The damage receive's block_chance roll: both effect-0x37 fetches, the draw (0x437ae0), the
	//    damage test (0x437af9) and the chance test's jp (0x437b08) with the zeroing after it.
	inline constexpr site block_chance_draw_site{0x437a9d,
	                                             "6a37d99e9c000000c744243000000000c744243800000000e8568911008b0e6a01578d542430526a37e845891100d944"
	                                             "2428d84c24206a01680000803f6a00d95c242ce8cb861200d9869c00000083c40cd81d30006800dfe0f6c4410f8593"
	                                             "000000d85c2420dfe0f6c441",
	                                             0, "the hit's block_chance draw (0x437490)"};
	inline constexpr site block_chance_site{0x437b08, "0f8a86000000c7869c00000000000000", 6, "the hit's block_chance test (0x437490)"};
	constexpr address block_chance_skip_va = 0x437b94;

	// 12. (revision 10, item 16 in docs/npc-power-rule.md) The attack-state update's input-lock exit (0x512ed0): mov eax,[esi+0x10]; test byte
	//     [eax+0x764],1; jz 0x512f1a; test byte [eax+0x3d9],0x10; jnz 0x51304a (return before the visibility query and
	//     the brain). actor+0x764 bit 0 is the move's input lock (set at a move's entry when the move locks input,
	//     0x431257..0x431273, and by its lock timers, 0x430bd0 / 0x430bfc: XML1 0x407d8..0x407f4, 0x40257 / 0x40283 on
	//     its +0x54c); actor+0x3d9 bit 0x10 is set by the power-aim helper 0x50a6c0 (0x50aa28, called from the power
	//     decision 0x5189b3 / 0x518a4d) and stays set on the enemies (every logged hit on a BrotherHoodEnergy shows it,
	//     idle included). So an enemy that has used a power skips its whole attack-state update - visibility, brain,
	//     power decision - in every locked move: attacks before their chain window, powers and reactions. XML1's
	//     attack-state update 0x10ad70 has no such exit (0x10ad9b fstp [edx+0x408]; 0x10ada1 the +0x278 test, as
	//     0x512f1a); its brain runs every frame whatever the body does. The jz becomes jmp: the exit is never taken.
	inline constexpr site lock_exit_site{0x512f01, "8b4610f6806407000001740df680d9030000100f8530010000", 0, "the attack-state update's input-lock exit (0x512ed0)"};
	constexpr address lock_exit_va = 0x512f0b;
	// 13. (revision 10, item 17) The melee-range power decision's own input-lock test (0x518770): mov ebp,[esp+0x14] (the
	//     deciding character); test byte [ebp+0x764],1; jne 0x518ac9 (return 0 before the re-check time is even read);
	//     mov ebx,[esp+0x1c]. XML1's decision 0xee260 has none (0xee289 target, 0xee296 / 0xee2a2 the 0xbf730 / +0x3e4
	//     tests, then the timer at 0xee2c2): a re-check that falls due in a locked move is made, its visibility asked,
	//     re-armed (0xee374) and its picker finds no chain - spent. The jne becomes a jump to a bridge that keeps
	//     the native exit for every caller but the melee brain's (return 0x5193e7; the officer weapon brain's call
	//     0x519fe4 stays as measured).
	inline constexpr site power_lock_site{0x5187b9, "8b6c2414f68564070000010f85ff0200008b5c241c", 0, "the power decision's input-lock test (0x518770)"};
	constexpr address power_lock_va = 0x5187c4, power_lock_exit_va = 0x518ac9, power_lock_resume_va = 0x5187ca;
	// 14. (revision 10, item 18) The picker's input-lock test (0x50fe50: sub esp,0xac; push esi; mov esi,ecx; mov eax,[esi+0x10];
	//     test byte [eax+0x764],1; jne 0x50ffeb = return 0). XML1's picker 0x1081f0 has none: a locked move refuses
	//     only through the per-slot chain test (move vt+0x3c at 0x1083a8..0x1083f4; XMen2.exe's move vt+0x44, same
	//     place), so a power whose action the current move chains (an attack's chain window) can be picked. The
	//     melee-range decision's gate (0x518920) enters the picker after the test; other callers keep it.
	inline constexpr site picker_entry_site{0x50fe50, "81ecac000000568bf18b4610f68064070000010f8582010000", 0, "the picker's input-lock test (0x50fe50)"};
	constexpr address picker_unlocked_va = 0x50fe69;

	inline constexpr std::array<const site*, 42> sites{&charge_site, &recheck_site, &recheck_actor_site, &picker_site, &energy_getter_site,
	                                                   &combolevel_site, &combo_exit_site, &combo_exit2_site, &combo_tail_site,
	                                                   &combo_buttons_site, &combo_lengths_site, &combo_row2_site, &combo_row3_site, &combo_row11_site,
	                                                   &query_priority_site, &query_budget_site, &visibility_site, &physical_target_site, &query_entry_site,
	                                                   &combo_continue_site, &combo_press_site, &combo_end_site, &combo_after_site, &combo_end_delay_site,
	                                                   &ai_pool_site, &ai_stride_site, &ai_count_site, &ai_brain_site, &reaction_site,
	                                                   &power_entry_site, &game_clock_site, &attack_entry_site, &attack_random_site,
	                                                   &hit_roll_site, &hit_roll_result_site, &miss_counts_site, &blocked_flag_site,
	                                                   &block_chance_draw_site, &block_chance_site,
	                                                   &lock_exit_site, &power_lock_site, &picker_entry_site};

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
		address react = 0, react_cont = 0, pre_start = 0, mark = 0;   // revision 10: per-brain reaction marks and their bridges
		address react_power = 0, power_front = 0;                     // revision 10: the power decision's marks and front
		address react_attack = 0, attack_front = 0;                   // revision 10: the attack decision's marks and front
		address hit_roll = 0;                                         // revision 10: the to-hit roll's power-attack test
		address power_lock = 0;                                       // revision 10: the power decision's input-lock test
	};
	// The block: one page of code and constants (made execute-read), then one page of data (read-write): the next
	// press time of each of the 32 brains.
	constexpr std::size_t charge_size = 57, recheck_size = 23, gate_size = 59, cont_size = 149, start_size = 71, end_delay_size = 32;
	constexpr std::size_t react_cont_size = 125, power_lock_size = 22, pre_start_size = 43, mark_size = 85, power_front_size = 166, attack_front_size = 182;
	constexpr std::size_t hit_roll_size = 35;
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
		l.react = l.next + 4 * ai_slots; // after the 32 deadlines, in the read-write page
		l.react_cont = base + 800;
		l.pre_start = base + 928;
		l.mark = base + 976;
		l.react_power = l.react + 4 * ai_slots;
		l.power_front = base + 1072;
		l.react_attack = l.react_power + 4 * ai_slots;
		l.attack_front = base + 1248;
		l.hit_roll = base + 2048; // block-bit's bridge (after the old attack front's 1728..1986, now free)
		l.power_lock = base + 2112; // clear of beta6/block-bit's bridge (block+2048, 35 bytes)
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
	//    the forcing flag is set, refuses (al = 0, as the picker refusing) when max > 2 x energy; else enters the
	//    picker with the caller's frame untouched: its first instructions (sub esp,0xac; push esi; mov esi,ecx; mov
	//    eax,[esi+0x10]), then 0x50fe69, past its input-lock test (revision 10, item 18: XML1's picker 0x1081f0
	//    has none). eax and edx are free (the picker sets them); ecx is kept.
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
			0x75, 0x11,                               // jnz refuse (+17)
			0x81, 0xec, 0xac, 0x00, 0x00, 0x00,       // go: sub esp,0xac (the picker's own first instructions)
			0x56,                                     // push esi
			0x8b, 0xf1,                               // mov esi,ecx
			0x8b, 0x46, 0x10,                         // mov eax,[esi+0x10]
			0xe9, 0, 0, 0, 0,                         // jmp 0x50fe69 (after its input-lock test)
			0x32, 0xc0,                               // refuse: xor al,al
			0xc2, 0x14, 0x00,                         // ret 0x14
		};
		put32(code, 14, energy_getter_va - (l.gate + 18));
		put32(code, 50, picker_unlocked_va - (l.gate + 54));
		return code;
	}

	// Item 17: jumped to from 0x5187c4 in place of `jne 0x518ac9`, with the flags of `test byte [ebp+0x764],1` (the
	//     deciding character's input lock). Unlocked: on at 0x5187ca as before. Locked: the melee brain's call (return
	//     address 0x5193e7 at [esp+0x24]: sub esp,0x14 and four pushes since the entry; 0x518660 cleaned its four
	//     arguments) goes on as XML1's 0xee260 does; every other caller takes the native exit 0x518ac9. The next
	//     native instructions (mov ebx,[esp+0x1c]; cmp ebx,esi) set the flags again.
	inline std::vector<std::uint8_t> power_lock_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x74, 0x0f,                                     // 00 jz resume (0x11): not locked
			0x81, 0x7c, 0x24, 0x24, 0, 0, 0, 0,             // 02 cmp dword [esp+0x24],0x5193e7
			0x74, 0x05,                                     // 0a je resume: the melee brain's call
			0xe9, 0, 0, 0, 0,                               // 0c jmp 0x518ac9 (the native exit)
			0xe9, 0, 0, 0, 0,                               // 11 resume: jmp 0x5187ca
		};
		put32(code, 0x06, melee_power_call_return_va);
		put32(code, 0x0d, power_lock_exit_va - (l.power_lock + 0x11));
		put32(code, 0x12, power_lock_resume_va - (l.power_lock + 0x16));
		return code;
	}
	inline std::vector<std::uint8_t> power_lock_jump(const layout& l)
	{
		const address rel = l.power_lock - (power_lock_va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90};
	}
	// Item 16: 0x512f0b: jz 0x512f1a -> jmp 0x512f1a (the exit at 0x512f0d..0x512f14 is never reached).
	inline std::vector<std::uint8_t> lock_exit_bytes() { return {0xeb, 0x0d}; }

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
	// 7. (revision 10) The continue step's front (jumped to from 0x51920b; same registers as the continue step): when the
	//    brain's slot carries a reaction mark, clear it and spend every step whose press time has passed - index + 1 and
	//    next + that step's delay, XML1's clock - completing the combo natively when none is left; then the continue
	//    step as before (it waits for the first step still to come). Brains outside the table, or without a mark, go
	//    straight to the continue step.
	inline std::vector<std::uint8_t> react_cont_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x8b, 0xc5,                               // 00 mov eax,ebp
			0x2d, 0, 0, 0, 0,                         // 02 sub eax,brain0
			0x72, 0x6f,                               // 07 jb cont (0x78)
			0x33, 0xd2,                               // 09 xor edx,edx
			0xb9, 0x1c, 0x06, 0x00, 0x00,             // 0b mov ecx,0x61c
			0xf7, 0xf1,                               // 10 div ecx
			0x85, 0xd2,                               // 12 test edx,edx
			0x75, 0x62,                               // 14 jnz cont
			0x83, 0xf8, 0x20,                         // 16 cmp eax,32
			0x73, 0x5d,                               // 19 jae cont
			0x8d, 0x14, 0x85, 0, 0, 0, 0,             // 1b lea edx,[eax*4+react]
			0x83, 0x3a, 0x00,                         // 22 cmp dword [edx],0
			0x74, 0x51,                               // 25 je cont (no reaction since the last step)
			0xc7, 0x02, 0x00, 0x00, 0x00, 0x00,       // 27 mov dword [edx],0
			0x8d, 0x14, 0x85, 0, 0, 0, 0,             // 2d lea edx,[eax*4+next]
			0x8b, 0x4d, 0x04,                         // 34 mov ecx,[ebp+4] (the AI)
			0xd9, 0x81, 0x50, 0x05, 0x00, 0x00,       // 37 spend: fld dword [ecx+0x550] (now)
			0xd8, 0x1a,                               // 3d fcomp dword [edx]
			0xdf, 0xe0,                               // 3f fnstsw ax
			0xf6, 0xc4, 0x41,                         // 41 test ah,0x41: now <= next (or unordered)
			0x75, 0x32,                               // 44 jnz cont: the continue step waits for it
			0x8b, 0x45, 0x30,                         // 46 mov eax,[ebp+0x30]
			0x40,                                     // 49 inc eax: this step was spent during the reaction
			0x89, 0x45, 0x30,                         // 4a mov [ebp+0x30],eax
			0x3b, 0x45, 0x34,                         // 4d cmp eax,[ebp+0x34]
			0x0f, 0x8d, 0, 0, 0, 0,                   // 50 jge 0x51928a (the combo's end)
			0x8b, 0x4d, 0x3c,                         // 56 mov ecx,[ebp+0x3c] (the combo)
			0x83, 0xf9, 0x06,                         // 59 cmp ecx,6
			0x73, 0x0f,                               // 5c jae zero (0x6d)
			0x6b, 0xc9, 0x14,                         // 5e imul ecx,ecx,20
			0x8d, 0x4c, 0x81, 0xfc,                   // 61 lea ecx,[ecx+eax*4-4]: delay[i - 1]
			0xd9, 0x81, 0, 0, 0, 0,                   // 65 fld dword [ecx+delays]
			0xeb, 0x02,                               // 6b jmp add (0x6f)
			0xd9, 0xee,                               // 6d zero: fldz
			0xd8, 0x02,                               // 6f add: fadd dword [edx]
			0xd9, 0x1a,                               // 71 fstp dword [edx]: the next step's press time
			0x8b, 0x4d, 0x04,                         // 73 mov ecx,[ebp+4]
			0xeb, 0xbf,                               // 76 jmp spend (0x37)
			0xe9, 0, 0, 0, 0,                         // 78 cont: jmp the continue step
		};
		put32(code, 0x03, brain0_va);
		put32(code, 0x1e, l.react);
		put32(code, 0x30, l.next);
		put32(code, 0x52, combo_end_va - (l.react_cont + 0x56));
		put32(code, 0x67, l.delays);
		put32(code, 0x79, l.cont - (l.react_cont + 0x7d));
		return code;
	}
	// The combo start's front (the exits' jump): a new combo starts unmarked, then the start bridge.
	inline std::vector<std::uint8_t> pre_start_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x8b, 0xc5,                                     // 00 mov eax,ebp
			0x2d, 0, 0, 0, 0,                               // 02 sub eax,brain0
			0x72, 0x1d,                                     // 07 jb go (0x26)
			0x33, 0xd2,                                     // 09 xor edx,edx
			0xb9, 0x1c, 0x06, 0x00, 0x00,                   // 0b mov ecx,0x61c
			0xf7, 0xf1,                                     // 10 div ecx
			0x85, 0xd2,                                     // 12 test edx,edx
			0x75, 0x10,                                     // 14 jnz go
			0x83, 0xf8, 0x20,                               // 16 cmp eax,32
			0x73, 0x0b,                                     // 19 jae go
			0xc7, 0x04, 0x85, 0, 0, 0, 0, 0, 0, 0, 0,       // 1b mov dword [eax*4+react],0
			0xe9, 0, 0, 0, 0,                               // 26 go: jmp the start bridge
		};
		put32(code, 0x03, brain0_va);
		put32(code, 0x1e, l.react);
		put32(code, 0x27, l.start - (l.pre_start + 0x2b));
		return code;
	}
	// Jumped to from 0x435b6e (esi = the hit record, [esi] = the victim): when the hit makes the victim react, mark
	// every AI slot whose actor (AI+0x10) is the victim. Registers are kept; the flags come from the displaced test.
	inline std::vector<std::uint8_t> mark_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0xf6, 0x86, 0xac, 0x00, 0x00, 0x00, 0x08,       // 00 test byte [esi+0xac],8
			0x74, 0x40,                                     // 07 jz out (0x49)
			0x50, 0x51, 0x52,                               // 09 push eax; push ecx; push edx
			0x8b, 0x16,                                     // 0c mov edx,[esi] (the victim)
			0xb8, 0, 0, 0, 0,                               // 0e mov eax,AI 0
			0x33, 0xc9,                                     // 13 xor ecx,ecx
			0x39, 0x50, 0x10,                               // 15 slot: cmp [eax+0x10],edx
			0x75, 0x21,                                     // 18 jne other (0x3b)
			0xc7, 0x04, 0x8d, 0, 0, 0, 0, 1, 0, 0, 0,       // 1a mov dword [ecx*4+react],1 (the combo)
			0xc7, 0x04, 0x8d, 0, 0, 0, 0, 1, 0, 0, 0,       // 25 mov dword [ecx*4+react_power],1 (the power decision)
			0xc7, 0x04, 0x8d, 0, 0, 0, 0, 1, 0, 0, 0,       // 30 mov dword [ecx*4+react_attack],1 (the attack decision)
			0x05, 0x1c, 0x06, 0x00, 0x00,                   // 3b other: add eax,0x61c
			0x41,                                           // 40 inc ecx
			0x83, 0xf9, 0x20,                               // 41 cmp ecx,32
			0x72, 0xcf,                                     // 44 jb slot (0x15)
			0x5a, 0x59, 0x58,                               // 46 pop edx; pop ecx; pop eax
			0xf6, 0x86, 0xac, 0x00, 0x00, 0x00, 0x08,       // 49 out: test byte [esi+0xac],8 (displaced)
			0xe9, 0, 0, 0, 0,                               // 50 jmp 0x435b75 (jz 0x435be2)
		};
		put32(code, 0x0f, ai0_va);
		put32(code, 0x1d, l.react);
		put32(code, 0x28, l.react_power);
		put32(code, 0x33, l.react_attack);
		put32(code, 0x51, reaction_resume_va - (l.mark + 0x55));
		return code;
	}
	// Jumped to from 0x518770 (ecx = the brain; eax/edx are scratch at a thiscall entry): a reaction-marked slot
	// re-arms its power re-check (AI+0x1cc) once per re-check that fell due before now (at most 8), then the decision
	// as before (it finds the timer in the future and does nothing this time). Unmarked slots go straight on.
	inline std::vector<std::uint8_t> power_front_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x51,                                     // 00 push ecx (the brain)
			0x8b, 0xc1,                               // 01 mov eax,ecx
			0x2d, 0, 0, 0, 0,                         // 03 sub eax,brain0
			0x0f, 0x82, 0, 0, 0, 0,                   // 08 jb out (0x9b)
			0x33, 0xd2,                               // 0e xor edx,edx
			0xb9, 0x1c, 0x06, 0x00, 0x00,             // 10 mov ecx,0x61c
			0xf7, 0xf1,                               // 15 div ecx
			0x85, 0xd2,                               // 17 test edx,edx
			0x0f, 0x85, 0, 0, 0, 0,                   // 19 jnz out
			0x83, 0xf8, 0x20,                         // 1f cmp eax,32
			0x0f, 0x83, 0, 0, 0, 0,                   // 22 jae out
			0x8d, 0x14, 0x85, 0, 0, 0, 0,             // 28 lea edx,[eax*4+react_power]
			0x83, 0x3a, 0x00,                         // 2f cmp dword [edx],0
			0x0f, 0x84, 0, 0, 0, 0,                   // 32 je out
			0xc7, 0x02, 0x00, 0x00, 0x00, 0x00,       // 38 mov dword [edx],0
			0xe8, 0, 0, 0, 0,                         // 3e call 0x46dce0
			0x8b, 0x10,                               // 43 mov edx,[eax]
			0x8b, 0xc8,                               // 45 mov ecx,eax
			0xff, 0x92, 0x60, 0x01, 0x00, 0x00,       // 47 call [edx+0x160]: ST0 = now (the clock 0x518849 reads)
			0x8b, 0x0c, 0x24,                         // 4d mov ecx,[esp] (the brain)
			0x8b, 0x49, 0x04,                         // 50 mov ecx,[ecx+4] (the AI)
			0x8b, 0x51, 0x10,                         // 53 mov edx,[ecx+0x10] (the actor)
			0x83, 0xba, 0xd4, 0x03, 0x00, 0x00, 0xff, // 56 cmp dword [edx+0x3d4],-1
			0x74, 0x08,                               // 5d je two (0x67)
			0xd9, 0x05, 0, 0, 0, 0,                   // 5f fld dword [0.5] (a character with a controller)
			0xeb, 0x06,                               // 65 jmp have (0x6d)
			0xd9, 0x05, 0, 0, 0, 0,                   // 67 two: fld dword [2.0]
			0xba, 0x08, 0x00, 0x00, 0x00,             // 6d have: mov edx,8 (ST0 = re-check, ST1 = now)
			0xd9, 0x81, 0xcc, 0x01, 0x00, 0x00,       // 72 again: fld dword [ecx+0x1cc]
			0xd8, 0xda,                               // 78 fcomp st(2): the timer against now
			0xdf, 0xe0,                               // 7a fnstsw ax
			0xf6, 0xc4, 0x45,                         // 7c test ah,0x45
			0x74, 0x16,                               // 7f jz done (0x97): timer > now
			0xf6, 0xc4, 0x04,                         // 81 test ah,4
			0x75, 0x11,                               // 84 jnz done: unordered
			0xd9, 0x81, 0xcc, 0x01, 0x00, 0x00,       // 86 fld dword [ecx+0x1cc]
			0xd8, 0xc1,                               // 8c fadd st(0),st(1)
			0xd9, 0x99, 0xcc, 0x01, 0x00, 0x00,       // 8e fstp dword [ecx+0x1cc]: re-armed, as XML1's spent decision
			0x4a,                                     // 94 dec edx
			0x75, 0xdb,                               // 95 jnz again (0x72)
			0xdd, 0xd8,                               // 97 done: fstp st(0)
			0xdd, 0xd8,                               // 99 fstp st(0)
			0x59,                                     // 9b out: pop ecx
			0x83, 0xec, 0x14, 0x53, 0x55,             // 9c sub esp,0x14; push ebx; push ebp (displaced)
			0xe9, 0, 0, 0, 0,                         // a1 jmp 0x518775
		};
		put32(code, 0x04, brain0_va);
		for (const std::size_t at : {std::size_t{0x0a}, std::size_t{0x1b}, std::size_t{0x24}, std::size_t{0x34}})
		{
			put32(code, at, 0x9b - static_cast<std::uint32_t>(at + 4));
		}
		put32(code, 0x2b, l.react_power);
		put32(code, 0x3f, game_clock_owner_va - (l.power_front + 0x43));
		put32(code, 0x61, l.half());
		put32(code, 0x69, l.two());
		put32(code, 0xa2, power_entry_resume_va - (l.power_front + 0xa6));
		return code;
	}
	// Jumped to from 0x518fd0 (ecx = the brain; eax/edx scratch at the thiscall entry): with no combo running, a
	// reaction-marked slot spends every attack decision that fell due before now (at most four): next attack time +
	// meleetimeroffset + random x meleetimerrandomadd, as the decision's tail re-arms it; then the decision as before.
	inline std::vector<std::uint8_t> attack_front_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0x51,                                     // 00 push ecx (the brain)
			0x8b, 0xc1,                               // 01 mov eax,ecx
			0x2d, 0, 0, 0, 0,                         // 03 sub eax,brain0
			0x0f, 0x82, 0, 0, 0, 0,                   // 08 jb out (0xab)
			0x33, 0xd2,                               // 0e xor edx,edx
			0xb9, 0x1c, 0x06, 0x00, 0x00,             // 10 mov ecx,0x61c
			0xf7, 0xf1,                               // 15 div ecx
			0x85, 0xd2,                               // 17 test edx,edx
			0x0f, 0x85, 0, 0, 0, 0,                   // 19 jnz out
			0x83, 0xf8, 0x20,                         // 1f cmp eax,32
			0x0f, 0x83, 0, 0, 0, 0,                   // 22 jae out
			0x8d, 0x14, 0x85, 0, 0, 0, 0,             // 28 lea edx,[eax*4+react_attack]
			0x83, 0x3a, 0x00,                         // 2f cmp dword [edx],0
			0x0f, 0x84, 0, 0, 0, 0,                   // 32 je out
			0x8b, 0x0c, 0x24,                         // 38 mov ecx,[esp] (the brain)
			0x80, 0x79, 0x2c, 0x00,                   // 3b cmp byte [ecx+0x2c],0 (a combo running: its own front;
			0x0f, 0x85, 0, 0, 0, 0,                   // 3f jnz out                 the mark waits for its end)
			0xc7, 0x02, 0x00, 0x00, 0x00, 0x00,       // 45 mov dword [edx],0
			0xe8, 0, 0, 0, 0,                         // 4b call 0x46dce0
			0x8b, 0x10,                               // 50 mov edx,[eax]
			0x8b, 0xc8,                               // 52 mov ecx,eax
			0xff, 0x92, 0x60, 0x01, 0x00, 0x00,       // 54 call [edx+0x160]: ST0 = now
			0x8b, 0x0c, 0x24,                         // 5a mov ecx,[esp]
			0x8b, 0x49, 0x04,                         // 5d mov ecx,[ecx+4] (the AI)
			0x6a, 0x04,                               // 60 push 4 (at most four spent decisions)
			0x8b, 0x4c, 0x24, 0x04,                   // 62 again: mov ecx,[esp+4] (the brain)
			0x8b, 0x49, 0x04,                         // 66 mov ecx,[ecx+4]
			0x90,                                     // 69
			0xd9, 0x41, 0x1c,                         // 6a fld dword [ecx+0x1c] (next attack time)
			0xd8, 0xd9,                               // 6d fcomp st(1): against now
			0xdf, 0xe0,                               // 6f fnstsw ax
			0xf6, 0xc4, 0x45,                         // 71 test ah,0x45
			0x74, 0x30,                               // 74 jz done (0xa6): still to come
			0xf6, 0xc4, 0x04,                         // 76 test ah,4
			0x75, 0x2b,                               // 79 jnz done: unordered
			0x6a, 0x01,                               // 7b push 1
			0xe8, 0, 0, 0, 0,                         // 7d call 0x560190: ST0 = random
			0x83, 0xc4, 0x04,                         // 82 add esp,4
			0x8b, 0x4c, 0x24, 0x04,                   // 85 mov ecx,[esp+4]
			0x8b, 0x49, 0x04,                         // 89 mov ecx,[ecx+4]
			0x90,                                     // 8c
			0xd8, 0x89, 0x8c, 0x02, 0x00, 0x00,       // 8d fmul dword [ecx+0x28c] (meleetimerrandomadd)
			0xd8, 0x81, 0x88, 0x02, 0x00, 0x00,       // 93 fadd dword [ecx+0x288] (meleetimeroffset)
			0xd8, 0x41, 0x1c,                         // 99 fadd dword [ecx+0x1c]
			0xd9, 0x59, 0x1c,                         // 9c fstp dword [ecx+0x1c]: the spent decision's re-arm
			0xff, 0x0c, 0x24,                         // 9f dec dword [esp]
			0x75, 0xbe,                               // a2 jnz again (0x62)
			0x90, 0x90,                               // a4
			0xdd, 0xd8,                               // a6 done: fstp st(0) (now)
			0x83, 0xc4, 0x04,                         // a8 add esp,4 (the count)
			0x59,                                     // ab out: pop ecx
			0x83, 0xec, 0x58, 0x53, 0x55,             // ac sub esp,0x58; push ebx; push ebp (displaced)
			0xe9, 0, 0, 0, 0,                         // b1 jmp 0x518fd5
		};
		put32(code, 0x04, brain0_va);
		for (const std::size_t at : {std::size_t{0x0a}, std::size_t{0x1b}, std::size_t{0x24}, std::size_t{0x34}, std::size_t{0x41}})
		{
			put32(code, at, 0xab - static_cast<std::uint32_t>(at + 4));
		}
		put32(code, 0x2b, l.react_attack);
		put32(code, 0x4c, game_clock_owner_va - (l.attack_front + 0x50));
		put32(code, 0x7e, game_random_va - (l.attack_front + 0x82));
		put32(code, 0xb2, attack_entry_resume_va - (l.attack_front + 0xb6));
		return code;
	}
	inline std::vector<std::uint8_t> attack_entry_jump(const layout& l)
	{
		const address rel = l.attack_front - (attack_entry_site.va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24)};
	}
	inline std::vector<std::uint8_t> power_entry_jump(const layout& l)
	{
		const address rel = l.power_front - (power_entry_site.va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24)};
	}
	inline std::vector<std::uint8_t> mark_jump(const layout& l)
	{
		const address rel = l.mark - (reaction_site.va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90, 0x90};
	}
	// 7. Jumped to from 0x44f58e (esi = the attack, edi = the defender). The combo-text test as before; then an attack
	//    whose damagescale isn't `normal` (XML1's PowerAttack) returns 1 without a draw, as the combo-text path's own
	//    always-hit exit 0x44f5aa does; a `normal` one goes on to the draw. eax is kept around the damagescale read;
	//    flags are dead at all three continuations (each starts with its own test or a call).
	inline std::vector<std::uint8_t> hit_roll_bridge(const layout& l)
	{
		std::vector<std::uint8_t> code{
			0xf6, 0x46, 0x35, 0x04,             // 00 test byte [esi+0x35],4 (displaced: the move's combo text)
			0x0f, 0x85, 0, 0, 0, 0,             // 04 jnz 0x44f594 (the game's combo-text path)
			0x50,                               // 0a push eax
			0x8a, 0x46, 0x35,                   // 0b mov al,[esi+0x35]
			0x24, 0x78,                         // 0e and al,0x78 (damagescale)
			0x3c, 0x08,                         // 10 cmp al,8 (normal)
			0x58,                               // 12 pop eax
			0x0f, 0x84, 0, 0, 0, 0,             // 13 je 0x44f5b4 (rolled as before)
			0xbf, 0x01, 0x00, 0x00, 0x00,       // 19 mov edi,1 (a hit)
			0xe9, 0, 0, 0, 0,                   // 1e jmp 0x44f6bd (the result)
		};
		put32(code, 0x06, hit_roll_combo_va - (l.hit_roll + 0x0a));
		put32(code, 0x15, hit_roll_rolled_va - (l.hit_roll + 0x19));
		put32(code, 0x1f, hit_roll_result_va - (l.hit_roll + 0x23));
		return code;
	}
	inline std::vector<std::uint8_t> hit_roll_jump(const layout& l)
	{
		const address rel = l.hit_roll - (hit_roll_site.va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90};
	}
	// 8. 0x437b08: jp 0x437b94 -> jmp 0x437b94; nop. The draw was already compared and popped (fcomp at 0x437aff), as on
	//    the game's own skip.
	inline std::vector<std::uint8_t> block_chance_skip()
	{
		const address rel = block_chance_skip_va - (block_chance_site.va + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24), 0x90};
	}
	inline std::vector<std::uint8_t> continue_jump(const layout& l)
	{
		const address rel = l.react_cont - (combo_continue_va + 5);
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
		put(l.react_cont, react_cont_bridge(l));
		put(l.pre_start, pre_start_bridge(l));
		put(l.mark, mark_bridge(l));
		put(l.power_front, power_front_bridge(l));
		put(l.attack_front, attack_front_bridge(l));
		put(l.hit_roll, hit_roll_bridge(l));
		put(l.power_lock, power_lock_bridge(l));
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
