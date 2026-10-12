#pragma once
// [Game] RankCosts=1: X-Men Legends 1's per-rank skill costs (legends-classic issue #35).
//
// The first game gives a talent rank a cost: a <level cost="N"> of the talent's data (XML1's parser 0xb4250 keeps up
// to three (rank, cost) pairs per talent; CTalent vt+0x34 = 0xb3ad0 answers 1 for any other rank). Every place that
// moves a skill point for a rank reads it there: the skills menu charges the next rank's cost and refunds the removed
// rank's (0x16c800), greys a talent the hero can't pay for (0x169200), the automatic spending tries a rank only when
// the hero has its cost (0xae8a0), and losing levels takes ranks back until their costs cover the lost points (0xb1ca0).
// X-Men Legends II's code charges 1 everywhere and never reads the attribute, although the port's talent files keep it
// (legends-classic heroes.py: `cost` copied verbatim onto the level).
//
// XMen2.exe facts this file relies on (CStats+4 is the saved block; its talents are 8-byte entries from block+0x1c,
// the count at block+0xb8: id word, committed rank in the low 6 bits of +2, ranks added in the open skills menu in
// the low 6 bits of +3; a rank's total is the two together, 0x5f00e0):
//   0x544a30  the block's unspent skill points (__thiscall, ax)          0x43a5b0  their setter
//   0x4692f0  a CStats' saved block (__thiscall)                       0x4c05b0  the talent registry (cdecl)
//   registry vt+0x14 (0x4bdbe0) a talent by a pointer to its id         0x4bdea0  a talent's <level> node for a 0-based
//   0x5649c0  an XMLB node's attribute by name (__thiscall, ret 4)        rank (__thiscall, ret 4; count-aware)
// The sites (every skill-point add or subtract for a rank, found from the callers of 0x43a5b0 and 0x544a30):
//   0x5f05ea  can-add (0x5f0590): "points > 0" -> "points >= cost of the next rank"; the skills screen uses this
//             answer both to allow + and to draw the talent as buyable or greyed (0x5df239)
//   0x5e5d86  skills screen +: DEC EDI -> EDI -= cost of the rank just added
//   0x5e5dcf  skills screen -: INC EDI -> EDI += cost of the rank just removed
//   0x5de6f8  the screen's dependency sweep (0x5de570, a rank whose requirement went away): +1 -> + its cost
//   0x43c7c8  automatic spending (0x43c760): a talent whose next rank costs more than the hero has ends the spending
//             (as if the points had run out: the new-power slot pass still runs)
//   0x43c80d  automatic spending: -1 -> - cost of the rank just added
//   0x5d2898  the Training respec (0x5d2810): committed - base -> the costs of the ranks above base
//   0x4bbd19  losing levels (0x4bbae0): each rank taken back covers its cost, not 1 (the loop's JNZ becomes JG at
//             0x4bbd21, as XML1's `while (missing > 0)`)
// Not changed, because no rank cost is involved: the "points to spend" test 0x4b7b00 (points > 0), the per-level point
// (0x4bbb75), the Training's bonus point 0x5d2290, the item point 0x4c923a, the online state copy (0x60bd40 sets the
// host's numbers as they are) and the save (the unspent points and the ranks are stored; nothing is recomputed).
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rank_costs_rules
{
	// A cost the data can give: XML1 keeps it in 3 bits (0xb4250: idx | cost << 5) and never authors 0; anything
	// else (absent, not a number, 0, above 7) is the stock 1.
	constexpr int max_cost = 7;
	constexpr int parse_cost(const char* text)
	{
		if (!text)
			return 1;
		while (*text == ' ' || *text == '\t')
			++text;
		int value = 0, digits = 0;
		for (; *text >= '0' && *text <= '9' && digits < 4; ++text, ++digits)
			value = value * 10 + (*text - '0');
		while (*text == ' ' || *text == '\t')
			++text;
		if (!digits || *text || value < 1 || value > max_cost)
			return 1;
		return value;
	}

	// The skills screen and the automatic spending may add a rank only when the hero has its whole cost (XML1 0x16ca2e:
	// points < cost refuses).
	constexpr bool affordable(int points, int cost) { return points > 0 && points >= cost; }

	// The 0-based index of the rank a site prices, from the talent's total (committed + added) as the site sees it.
	enum class moment : int
	{
		next_rank = 0,          // can-add: the total before the rank is bought; the next rank's index is the total
		after_add = -1,         // a rank was just added: its index is the new total - 1
		after_remove = 0,       // a rank was just removed: its index is the new total
		before_remove = -1,     // the rank about to be removed is the top one: index total - 1
	};
	constexpr int rank_index(int total, moment m) { return total + static_cast<int>(m); }

	// Training's respec (0x5d2810) puts a talent back to its base rank and returns what the ranks above it cost. Below
	// base (not authored) it keeps the stock difference.
	template <typename Cost>
	constexpr int respec_refund(int committed, int base, Cost&& cost_of_index)
	{
		if (committed <= base)
			return committed - base;
		int sum = 0;
		for (int i = base; i < committed; ++i)
			sum += cost_of_index(i);
		return sum;
	}

	// Losing a level (0x4bbae0): `missing` points the hero had already spent are taken back rank by rank; a rank taken
	// back covers its whole cost and the loop runs while points are still missing (XML1 0xb1ca0 `for (; missing > 0;
	// missing -= cost)`; an overshoot is not refunded, as there).
	constexpr int level_down_remaining(int missing, int removed_cost) { return missing - removed_cost; }

	// What the patched level-down loop subtracts besides its own DEC EBP: cost - 1.
	constexpr int level_down_extra(int removed_cost) { return removed_cost - 1; }

	// ---- the sites ----
	struct guard
	{
		std::uint32_t va;
		std::string_view hex;
		const char* what;
	};

	// Code the stubs call or return into, and the function bodies around each site (retail bytes).
	inline constexpr std::array<guard, 19> guards{{
		{0x544a30, "668b4114c3", "unspent skill points getter (ax = block+0x14)"},
		{0x4692f0, "8d4104c3", "a CStats' saved block (stats+4)"},
		{0x4c05b0, "a19c33770085c0750ae8c2ffffffa19c337700c3", "the talent registry's getter"},
		{0x68ec38, "e0db4b00", "the talent registry's vt+0x14: 0x4bdbe0"},
		{0x4bdea0, "518b49085333c032", "a talent's <level> node for a rank (0x4bdea0)"},
		{0x5649c0, "5355568be9578b7d", "XMLB attribute lookup (0x5649c0)"},
		{0x46dce0, "64a1000000008a0d", "game object getter (the screen + stub tail-calls it)"},
		{0x43b2f0, "83ec08538b5c2410", "a block's committed rank setter (0x43b2f0, ret 8 at 0x43b361)"},
		{0x43b361, "c20800", "0x43b2f0 returns with ret 8"},
		{0x43cf86, "c20800", "the talent pick 0x43cc40 returns with ret 8"},
		{0x5f05e0, "8b4c24143bc87d2a8bcde84144f5ff6685c07e1e", "can-add: total < max, then points > 0"},
		{0x5e5d7e, "84c00f84140100004fe8547fe8ff8b10558bc8ff92c8010000", "skills screen +: the rank is set, DEC EDI"},
		{0x5e5dca, "e83152e5ff84c0740547b301eb048a5c2413e83f2bff", "skills screen -: the rank is set, INC EDI"},
		{0x5de6ef, "8bcfe8faabe8ff8bc8e83363f6ff8bcf4050", "dependency sweep: points + 1"},
		{0x43c7c4, "53568bcfe873040000663dffff89442410", "automatic spending: the talent pick"},
		{0x43c80b, "8bc8e81e8210008bce4850", "automatic spending: points - 1"},
		{0x5d2892, "33c08a068bcf83e03f2bc38be8", "respec: committed - base"},
		{0x4bbd13, "8d4c241c50518bcfe8d0f5f7ff4d0f856effffff", "losing levels: one rank per missing point"},
		{0x43c8a8, "8b44247085c0", "automatic spending: after the loop (new-power slots)"},
	}};

	// The writes: a CALL to the site's stub over the retail instructions (padded with NOPs), except the screen's -
	// (CALL, then JMP SHORT to the common tail 0x5e5ddc over the old else branch) and the level-down loop test (JNZ ->
	// JG). `before` is retail.
	enum class shape { call, call_then_jump_to_tail, jg };
	struct site
	{
		std::uint32_t va;
		std::string_view before;
		shape kind;
		const char* what;
	};
	enum site_id : std::size_t { can_add, screen_add, screen_remove, sweep_refund, auto_pick, auto_charge, respec, level_down_call, level_down_test, site_count };
	inline constexpr std::array<site, site_count> sites{{
		{0x5f05ea, "e84144f5ff", shape::call, "can-add: points >= the next rank's cost"},
		{0x5e5d86, "4fe8547fe8ff", shape::call, "skills screen +: - the added rank's cost"},
		{0x5e5dcf, "84c0740547b301eb048a5c2413", shape::call_then_jump_to_tail, "skills screen -: + the removed rank's cost"},
		{0x5de6f8, "e83363f6ff8bcf40", shape::call, "dependency sweep: + the removed rank's cost"},
		{0x43c7c8, "e873040000", shape::call, "automatic spending: stop at a rank the hero can't pay for"},
		{0x43c80d, "e81e8210008bce48", shape::call, "automatic spending: - the added rank's cost"},
		{0x5d2898, "83e03f2bc38be8", shape::call, "respec: the costs of the ranks above base"},
		{0x4bbd19, "8bcfe8d0f5f7ff", shape::call, "losing levels: a rank taken back covers its cost"},
		{0x4bbd21, "0f85", shape::jg, "losing levels: loop while points are missing (JNZ -> JG)"},
	}};

	constexpr std::size_t hex_size(std::string_view hex) { return hex.size() / 2; }
	constexpr std::uint8_t hex_byte(std::string_view hex, std::size_t i)
	{
		auto nibble = [](char c) { return static_cast<std::uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10); };
		return static_cast<std::uint8_t>(nibble(hex[2 * i]) << 4 | nibble(hex[2 * i + 1]));
	}
	inline std::vector<std::uint8_t> bytes_of(std::string_view hex)
	{
		std::vector<std::uint8_t> out(hex_size(hex));
		for (std::size_t i = 0; i < out.size(); ++i)
			out[i] = hex_byte(hex, i);
		return out;
	}

	// The bytes a site gets for a stub at `stub` (the stub's absolute address; unused for the JG).
	inline std::vector<std::uint8_t> patched(const site& s, std::uint32_t stub)
	{
		std::vector<std::uint8_t> out(hex_size(s.before), 0x90);
		if (s.kind == shape::jg)
		{
			out[0] = 0x0f;
			out[1] = 0x8f;
			return out;
		}
		const std::uint32_t rel = stub - (s.va + 5);
		out[0] = 0xe8;
		for (int i = 0; i < 4; ++i)
			out[1 + i] = static_cast<std::uint8_t>(rel >> (8 * i));
		if (s.kind == shape::call_then_jump_to_tail)
		{
			// 0x5e5dd4: JMP SHORT 0x5e5ddc (the retail common tail after both branches)
			out[5] = 0xeb;
			out[6] = static_cast<std::uint8_t>(0x5e5ddc - (s.va + 7));
		}
		return out;
	}
}
