#pragma once

// [Game] PlacementCrush: X-Men Legends 1 does not crush objects while a zone places them (issue #79 of the X-Men
// Legends 1 port), kept apart from the hook so xml2_test can check the decision and every retail byte it relies on.
//
// What XMen2.exe does. A physical object's "touched by another object" slot, vt+0xac = 0x498ef0 (shared by the
// object classes whose damage slot vt+0xb0 is 0x497e70), lets the heavier of two touching objects (heaviness byte
// +0x31d more than twice the other's, or the larger one) deal the lighter one a hit of 1,000,000 damage
// (0x499071 builds it, 0x49908d hands it to the victim's damage slot): the victim breaks, or, where the hit is
// refused (XML2 Fix's BreakRule=xml1 refuses it for most XML1 objects), it still plays the hit's effects - the
// default hit sound common/melee/hit_normal (0x454080 -> 0x453660 -> audio vt+0x5c). Touches also happen while an
// object is settled on the floor: the settle event CEvent<CGameEntity, ESettle> (functor vtable 0x6873a4, its call
// 0x4729f0 = the entity's vt+0x98(3), the floor routine 0x474080 and its move 0x419f20). XMen2.exe posts it from the
// spawn-init virtual 0x474c00 (0x474c63, 0.5 s after the spawn; physents through 0x496db0), so it runs for every
// object at a zone's load and for every object spawned in play (death drops, script spawns, debris). When a zone loads, two
// pieces of furniture authored overlapping (a desk chair tucked under a roll-top desk in the X-Mansion dormitories)
// "crush" each other there; the crushed piece is knocked into the other, whose impact (0x499100, vt+0x1b0) is a
// second hit, and the player hears two hits from an empty room (port harness: 2 hit sounds on 3/3 fresh entries; 13
// touches during the settle).
//
// What X-Men Legends 1 does. It has no settle at all (default.xbe has no ESettle event; its own touch-crush 0x92970
// has no settle that could call it), so nothing it places or spawns is crushed there: the original Xbox game, recorded on the
// same first entry to the dormitories (xemu), plays no hit sound there (the port's own hit sample matched at noise
// level, r 0.33/0.37, against r 0.62/0.74 on the original's real punches).
//
// PlacementCrush=xml1 refuses the touch while a settle runs (the fix's own call in the functor's slot counts it): the
// touch then does what the game's own early-out does (0x4990ea: nothing). Emulated in the harness before it was
// written: 0 hit sounds on 4/4 fresh entries, the impact hit gone too. Touches outside a settle keep the game's rule
// (objects bumping or thrown into each other in play); settle-time touches are refused for spawns in play as well.

#include "limits_rules.hpp" // guard, matches, image_base

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace placement_crush_rules
{
	using limits_rules::guard;
	using address = std::uint32_t;

	// ---- The ini value -------------------------------------------------------------------------------------

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

	// "xml1" or "xml2", any case; a ';' starts a comment; spaces around are ignored.
	inline rule_choice parse_rule(const std::string_view raw)
	{
		rule_choice c;
		std::string v;
		for (const char ch : raw)
		{
			if (ch == ';')
			{
				break;
			}
			v.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch));
		}
		const auto first = v.find_first_not_of(" \t");
		const auto last = v.find_last_not_of(" \t");
		v = first == std::string::npos ? std::string() : v.substr(first, last - first + 1);
		if (v.empty())
		{
			return c;
		}
		c.set = true;
		if (v == "xml1")
		{
			c.value = rule::xml1;
		}
		else if (v != "xml2")
		{
			c.error = "isn't xml1 or xml2";
		}
		return c;
	}

	// ---- The decision ---------------------------------------------------------------------------------------

	// Refuse the touch only on xml1 while a settle runs (its depth above 0).
	inline bool refuse(const rule chosen, const int settle_depth)
	{
		return chosen == rule::xml1 && settle_depth > 0;
	}

	// ---- The site ------------------------------------------------------------------------------------------------

	// 0x498ef0: `sub esp, 0x8c` (81 ec 8c 00 00 00) -> jmp rel32 + nop; the stub refuses (ret 4) or redoes the
	// displaced instruction and goes on at 0x498ef6.
	constexpr address touch_site = 0x498ef0;
	constexpr address touch_resume = 0x498ef6;
	constexpr std::array<std::uint8_t, 6> retail_touch{0x81, 0xec, 0x8c, 0x00, 0x00, 0x00};

	// The settle event's functor: its vtable's call slot holds 0x4729f0, which the fix's own call replaces (the
	// entity's vt+0x98(3), counted).
	constexpr address settle_slot = 0x6873a4;
	constexpr address settle_call = 0x4729f0;
	constexpr address entity_settle_slot = 0x98;
	constexpr int settle_mode = 3;

	inline constexpr std::array<guard, 6> guards{{
		{0x498ef0, "81ec8c0000005355568bf18b0d348471008b0157ff90f801000084c00f85d8010000", "the touch slot's start and the game's own early-out test (0x498ef0)"},
		{0x49904e, "a154fa67005050506a016a0068800000006a016a006a00518b4f1c8bc489088d4c2458e8ea07f9ff", "the crush hit built for the victim's handle (0x49904e-0x499075, 0x429860)"},
		{0x499085, "c744244400247449ff92b0000000", "the 1,000,000 damage handed to the victim's damage slot vt+0xb0 (0x499085)"},
		{0x4990ea, "5f5e5d5b81c48c000000c20400", "the touch slot's early-out: nothing (0x4990ea)"},
		{0x4729f0, "8b4c24048b016a03ff9098000000c208", "the settle event's call: the entity's vt+0x98(3), ret 8 (0x4729f0)"},
		{0x6873a0, "6cc96a00f0294700409c4100", "the settle event functor's vtable: its call 0x4729f0 (0x6873a4)"},
	}};

	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - limits_rules::image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}
}
