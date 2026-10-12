#pragma once

// [Game] RevivalCost=xml1: the first game's price to bring a fallen hero back through the roster.
//
// The first game (default.xbe): the roster's revival (0x16ba41-0x16bab4) charges 0xaf6b0 = 200 * f * f, f the item
// manager's word +0x1694 (getter 0x7e060), set (0x82a05 zone placement, 0x167ad8 roster acceptance, 0x17c855 shop) from
// the campaign registry's vt+0x58 = 0x54cb0: the highest byte CStats+0x1d over the registry's roster list (+0xdbcc,
// count +0xdccc), skipping guests (+0x3a1 bit 0x40). CStats+0x1c is the level (getter 0xaf700, raised by 0xaf950
// up to 45); +0x1d is the level last acknowledged (0xafa40: a level-up is pending while +0x1c > +0x1d), so f is the
// roster's highest level once its level-ups were shown.
//
// XMen2.exe: 0x4b8830 = max(200, 2 * L * L), L the revived hero's own level (0x4b87c0, CStats+0x1c), called by the
// roster (0x5be238, 0x5be254, 0x5e4162) and the revival's fund check and debit (0x5e5354, 0x5e538f). The hook replaces
// its first instruction with a jump to 200 * f * f, f = the registry's highest roster level (0x449960: the herostat
// list +0x120dc, count +0x12330, skipping +0x2ad bit 0x20) - XMen2.exe's own counterpart of 0x54cb0, which its shop
// prices use too. XMen2.exe keeps no acknowledged-level byte, so f is the current highest level.

#include "limits_rules.hpp"

#include <array>
#include <cstdint>

namespace revival_cost_rules
{
	constexpr DWORD cost = 0x4b8830;          // __thiscall int (CStats*): the revival price
	constexpr DWORD roster_level = 0x449960;  // __thiscall int (registry): the highest roster level
	constexpr DWORD registry_getter = 0x44b8f0;
	constexpr DWORD registry_vtable = 0x68544c;
	inline constexpr std::array<limits_rules::guard, 3> guards{{
		{cost, "e88bffffff0fbfc00fafc0d1e03dc80000007d05b8c8000000c3", "0x4b8830: max(200, 2 * level^2) of the revived hero"},
		{roster_level, "5153568bf18b8e3023010033c033db85c9894424087e5955", "0x449960: the highest level over the registry's roster"},
		{registry_getter, "64a1000000006aff68b132670050a10c77710064892500000000", "the registry's getter (0x44b8f0)"},
	}};
	constexpr std::int64_t original_price(const std::int64_t f) { return 200 * f * f; }
	constexpr std::int64_t retail_price(const std::int64_t level) { return level * level * 2 < 200 ? 200 : level * level * 2; }
	// The jump written over the first instruction: E9 rel32 to `target`.
	inline std::array<std::uint8_t, 5> jump_to(const std::uint32_t target)
	{
		const std::uint32_t rel = target - (cost + 5);
		return {0xe9, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8), static_cast<std::uint8_t>(rel >> 16),
		        static_cast<std::uint8_t>(rel >> 24)};
	}
}
