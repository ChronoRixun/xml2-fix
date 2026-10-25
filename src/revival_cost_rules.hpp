#pragma once

// [Game] RevivalCost=xml1: the first game's price to bring a fallen hero back through the roster.
//
// The first game (default.xbe): the roster's revival (0x16ba41-0x16bab4; fund check and debit 0x16ba5e, 0x16ba94)
// charges 0xaf6b0 = 200 * G * G. G is the item manager's progression group, the word +0x1694 (getter 0x7e060), set
// (0x82a12 zone placement, 0x167aec roster acceptance, 0x17c869 shop) from the campaign registry's vt+0x58 = 0x54150:
// G = min(5, L / 10 + 1), L = vt+0x50 = 0x54c30, the highest current level (CStats+0x1c, getter 0xaf700, raised by
// 0xaf950 up to 45) over the registry's roster list (+0xdbcc, count +0xdccc), skipping guests (+0x3a1 bit 0x40), 0 for
// an empty list. So a revival costs 200 / 800 / 1,800 / 3,200 / 5,000 for a highest level of 0-9 / 10-19 / 20-29 /
// 30-39 / 40-45, whoever fell. (vt+0x54 = 0x54cb0, the highest level last acknowledged (CStats+0x1d), is the slot
// next to it and plays no part: 1.5.0 to 1.8.0-beta.7 read the wrong slot and charged 200 * L * L - 80,000 at 20.)
//
// XMen2.exe: 0x4b8830 = max(200, 2 * L * L), L the revived hero's own level (0x4b87c0, CStats+0x1c), called by the
// roster (0x5be238, 0x5be254, 0x5e4162) and the revival's fund check and debit (0x5e5354, 0x5e538f). The hook replaces
// its first instruction with a jump to 200 * G * G, G = price_factor(L), L = the registry's highest roster level
// (0x449960: the herostat list +0x120dc, count +0x12330, skipping +0x2ad bit 0x20, 0 for an empty list) - XMen2.exe's
// own counterpart of 0x54c30, which its shop prices use too. The first game refreshes its stored G at the three
// points above; the hook works G out at every price call, so a level-up inside a zone moves the price at once instead
// of at the next placement, roster or shop (the same on every peer).
//
// Which heroes count: the first game's list +0xdbcc is filled at the stats load (0x56526, one entry per playable
// character as it is parsed), not by joining - its L counts every roster hero, joined or not, except guests.
// XMen2.exe's +0x120dc is filled the same way at its stats load (0x44c24c) and 0x449960 skips +0x2ad bit 0x20. So
// both count the whole playable roster. An empty or unreadable roster is L 0 (unreadable: 1), G 1, 200, as in the
// first game.

#include "limits_rules.hpp"

#include <array>
#include <cstdint>

namespace revival_cost_rules
{
	constexpr DWORD cost = 0x4b8830;          // __thiscall int (CStats*): the revival price
	constexpr DWORD roster_level = 0x449960;  // __thiscall int (registry): the highest roster level
	constexpr DWORD registry_getter = 0x44b8f0;
	constexpr DWORD registry_vtable = 0x68544c;
	constexpr DWORD roster_loop = 0x4499a4;   // 0x449960's loop: the guest skip and the running maximum
	inline constexpr std::array<limits_rules::guard, 4> guards{{
		{cost, "e88bffffff0fbfc00fafc0d1e03dc80000007d05b8c8000000c3", "0x4b8830: max(200, 2 * level^2) of the revived hero"},
		{roster_level, "5153568bf18b8e3023010033c033db85c9894424087e5955", "0x449960: the highest level over the registry's roster"},
		{roster_loop, "a8207514e813ee06008b4c24100fbfc03bc87f0489442410", "0x4499a4: skip guests (+0x2ad bit 0x20), keep the highest level"},
		{registry_getter, "64a1000000006aff68b132670050a10c77710064892500000000", "the registry's getter (0x44b8f0)"},
	}};
	constexpr std::int64_t max_factor = 5;
	constexpr std::int64_t original_price(const std::int64_t g) { return 200 * g * g; }
	// 0x54150: the progression group of the roster's highest level, min(5, L / 10 + 1); below 0 (never seen) as 0.
	constexpr std::int64_t price_factor(const std::int64_t highest)
	{
		const std::int64_t g = highest < 0 ? 1 : highest / 10 + 1;
		return g > max_factor ? max_factor : g;
	}
	constexpr std::int64_t price_for_level(const std::int64_t highest) { return original_price(price_factor(highest)); }
	// What 0x449960 / 0x54c30 pick from a roster: the highest level of a hero that isn't a guest, 0 for none. The
	// hook calls the game's own function; this model is for the tests.
	struct roster_hero { int level; bool guest; };
	template <class Range> constexpr int highest_counted(const Range& roster)
	{
		int highest = 0;
		for (const roster_hero& h : roster)
			if (!h.guest && h.level >= highest) highest = h.level;
		return highest;
	}
	constexpr std::int64_t retail_price(const std::int64_t level) { return level * level * 2 < 200 ? 200 : level * level * 2; }
	// The jump written over the first instruction: E9 rel32 to `target`.
	inline std::array<std::uint8_t, 5> jump_to(const std::uint32_t target)
	{
		const std::uint32_t rel = target - (cost + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24)};
	}
}
