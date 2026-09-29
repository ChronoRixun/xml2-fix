#pragma once

// [Game] ReviewStats=0: the Review menu has four tabs - Screens, Cinematics, Comics, Concepts - and no
// Stats page, for a mod whose campaign had none (the X-Men Legends 1 port: XML1's Review had those four
// categories, and XML2's Stats page lists XML2's five acts, hard-coded). Kept apart from the patching so
// xml2_test can check it without the game: every retail byte the change relies on and the five bytes it
// writes.
//
// The research is in the xml1-port repository (tools/xml1build/SPEC.md 21.4.3); every address below was
// read again from the retail XMen2.exe for this code (image base 0x400000, no relocations) and is in
// `guards`.
//
// What the game does: a menu file with type="REVIEW_PATHS_MENU" (XML2's UI/menus/review) is built by
// 0x5b7b00 (the factory's compare at 0x5b84f3), vtable 0x69ee7c. Its tabs are five text items looked up
// by name through the table 0x6e6e28 (option01_text .. option05_text; index 4 = Stats), and the tab
// shown is the global 0x8afef0 (0 Screens, 1 Cinematics, 2 Comics, 3 Concepts, 4 Stats). Tab 4 builds
// the Stats list (0x5d0c20, the act loop 1..5), the others the review_paths list of that category
// (0x5d1220). The game variable reviewmode (-1 from the main menu, 0..3 from the four
// review<Category>() script functions) says how the menu opens:
//   - the open (vt+0x10, 0x5d1c60): reviewmode 0..4 (cmp eax, 5 / jge at 0x5d1c87) makes that tab the
//     one shown and hides the others (item flag +0x54 bit 8, 0x5ade70); anything else keeps the tab
//     shown last. Then it builds the list: Stats when the tab is 4 (0x5d1d0e..0x5d1d1a).
//   - the tab change (0x5d17d0, the only writer of 0x8afef0 besides the open, the main menu's first
//     update step (0 at 0x5c926f) and resetgame (0 at 0x5f2f7b)): with reviewmode 0..4 it does nothing (cmp
//     eax, 5 / jl at 0x5d17f6: one category, no tabs); otherwise tab + step, past the last (cmp esi, 5
//     / jl at 0x5d1808) the first, before the first the last (mov esi, 4 at 0x5d1816), then the list
//     (0x5d1780). Its callers: the update's left / right (vt+0x38, 0x5d19eb) and vt+0x40 (0x5d1882).
//   - the mouse (vt+0x80, 0x5d04d0): on a button release it looks up the five tab items (the pushes
//     0x5d04ef..0x5d0531), hit-tests every one that is there and shown (cmp edi, 5 at 0x5d05b1), and
//     turns a click on another tab into that many left / right presses - the tab change above.
//
// The change: five imm8s, so that only tabs 0..3 exist:
//   0x5d180a 5 -> 4  the tab change: past Concepts comes Screens
//   0x5d1817 4 -> 3  the tab change: before Screens comes Concepts
//   0x5d05b3 5 -> 4  the mouse: four tab items hit-tested
//   0x5d1c89 5 -> 4  the open: reviewmode 4 is no category (the menu opens as from the main menu)
//   0x5d17f8 5 -> 4  the tab change: ... and so it has tabs
// Tab 4 then can't be reached: the Stats list code, the tab table and the menu file are left as they
// are. The menu file's fifth tab item (option05_text, its backdrop option05_focus) is still drawn if the
// file has it: a mod's review menu leaves those two items out (the port's does; every lookup of an item
// the file lacks returns nothing and is skipped, 0x5adc10 / 0x5ade70 / 0x5adf80).

#include "limits_rules.hpp" // guard, matches, image_base, operand_write

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace review_menu_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;
	using limits_rules::operand_write;

	constexpr int retail_tabs = 5;   // Screens, Cinematics, Comics, Concepts, Stats
	constexpr int tabs_without_stats = 4;
	constexpr DWORD tab_names = 0x6e6e28; // option01_text .. option05_text
	constexpr DWORD shown_tab = 0x8afef0;

	// One imm8 the change writes: `va` is the byte itself, in the instruction at `instruction`.
	struct site
	{
		DWORD instruction;
		DWORD va;
		std::uint8_t retail;
		std::uint8_t without_stats;
		const char* what;
	};

	inline constexpr std::array<site, 5> sites{{
		{0x5d1808, 0x5d180a, 5, 4, "the tab change: cmp esi, 5 - past the last tab comes the first"},
		{0x5d1816, 0x5d1817, 4, 3, "the tab change: mov esi, 4 - before the first tab comes the last"},
		{0x5d05b1, 0x5d05b3, 5, 4, "the mouse: cmp edi, 5 - the tab items hit-tested"},
		{0x5d1c87, 0x5d1c89, 5, 4, "the open: cmp eax, 5 - reviewmode 0..4 opens that one tab"},
		{0x5d17f6, 0x5d17f8, 5, 4, "the tab change: cmp eax, 5 - reviewmode 0..4 has no tabs to change"},
	}};

	// The span the writes lie in (two pages of the exe's code).
	constexpr DWORD span_begin = 0x5d05b3;
	constexpr DWORD span_end = 0x5d1c8a;

	// Every byte of XMen2.exe the change relies on, read from the retail build. All must match before any
	// byte is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 16> guards{{
		// The class.
		{0x5b84f3, "68e0fb690056e864a00b0083c40885c0751b8bcfe894ecffff85c00f84660100008bc8e8e5f5ffff",
		 "a menu of type=\"REVIEW_PATHS_MENU\" is built by 0x5b7b00 (0x5b84f3)"},
		{0x69fbe0, "5245564945575f50415448535f4d454e5500", "the type name \"REVIEW_PATHS_MENU\" (0x69fbe0)"},
		{0x5b7b00, "568bf1e8b8faffffc7067cee69008bc65ec3", "0x5b7b00 sets the vtable 0x69ee7c (0x5b7b00)"},
		{0x69ee8c, "601c5d00", "REVIEW_PATHS_MENU vt+0x10 = its open 0x5d1c60 (0x69ee8c)"},
		{0x69eeb4, "a0185d0090ef480070185d00", "REVIEW_PATHS_MENU vt+0x38 = its update 0x5d18a0, vt+0x40 = 0x5d1870 (0x69eeb4)"},
		{0x69eefc, "d0045d00", "REVIEW_PATHS_MENU vt+0x80 = its mouse handler 0x5d04d0 (0x69eefc)"},
		// The tabs.
		{0x6e6e28, "881e6a00781e6a00681e6a00581e6a00481e6a00", "the tab table 0x6e6e28: option01_text .. option05_text (0x6e6e28)"},
		{0x6a1e48, "6f7074696f6e30355f746578740000006f7074696f6e30345f746578740000006f7074696f6e30335f746578740000006f7074696f6e30325f74657874000000"
		           "6f7074696f6e30315f74657874000000",
		 "\"option05_text\" .. \"option01_text\" (0x6a1e48)"},
		// The code that picks the tab.
		{0x5d04d0,
		 "8b44241083ec243d020200005355568be9740b3d050200000f85750100005768881e6a008bcd83ceff83cbffe80fd7fdff68781e6a008bcd89442424e8ffd6fdff68681e6a008b"
		 "cd89442428e8efd6fdff68581e6a008bcd8944242ce8dfd6fdff68481e6a008bcd89442430e8cfd6fdff8944243033ff8b44bc2085c07461f6405408745b8bc88b01ff505084c0"
		 "74028bdf8b44bc200fbf48760fbf50722bd14a895424140fbf4870894c24100fbf50728954241c0fbf48740fbf50708d4411ff8b4c243c8b542438518944241c528d44241850ff"
		 "1530f2670085c074028bf74783ff057c9183fbff5f",
		 "the mouse handler: the five tab items looked up, hit-tested, cmp edi, 5 (0x5d04d0-0x5d05b9)"},
		{0x5d1780, "833df0fe8a0004568bf17507e88ff4ffffeb05e888faffff", "the list: Stats (0x5d0c20) when the tab is 4, else the category's (0x5d1780)"},
		{0x5d17d0,
		 "578bf9e8487100008b108bc8ff929001000083f8ff7e14e8347100008b108bc8ff929001000083f8057c68a1f0fe8a008b4c2408568d340883fe057c0433f6eb0a83feff7f05be04"
		 "0000008b1485286e6e006a00528bcfe854c7fdff8b04b5286e6e006a01508bcf8935f0fe8a00e83dc7fdff8bcfe836ffffffe8d17000008b105e5fc7442404010000008bc8ffa2e8"
		 "0000005fc20400",
		 "the tab change: reviewmode 0..4 none, else tab + step wrapped at 5 / -1 (0x5d17d0-0x5d1866)"},
		{0x5d1870, "56578b7c240c8a0784c08bf174090fbec050e849ffffff578bcee84197fdff5f5ec20400", "vt+0x40: a tab change by the argument's first byte (0x5d1870)"},
		{0x5d19dd, "8a4424143ac374220fbed0528bcee8e0fdffff", "the update: left / right is a tab change (0x5d19dd)"},
		{0x5d1c60,
		 "56578bf1e8b76c00008b108bc8ff929001000083f8ff7e5ce8a36c00008b108bc8ff929001000083f8057d48e88f6c00008b108bc8ff9290010000a3f0fe8a0033ff8b04bd286e6e"
		 "006a00508bcee8cdc2fdff8b0df0fe8a008b14bd286e6e003bf90f94c151528bcee8a2c1fdff4783ff057ccea1f0fe8a00538d3c85000000008bd88b87286e6e006a00508bcee8"
		 "8dc2fdff8b8f286e6e006a01518bce891df0fe8a00e877c2fdffa1f0fe8a00bf040000003bc75b8bce7507e801efffffeb05e8faf4ffff",
		 "the open: reviewmode 0..4 picks the tab, the list Stats when it is 4 (0x5d1c60-0x5d1d25)"},
		// The other writers of the tab: the main menu (0) and resetgame.
		{0x5c926f, "c705f0fe8a0000000000", "the main menu's first update step shows tab 0 next time (0x5c926f)"},
		{0x5f2f7b, "891df0fe8a00", "resetgame shows tab 0 next time (ebx = 0 since 0x5f2eb6; 0x5f2f7b)"},
	}};

	// The first guard `image` (XMen2.exe's image base: 0x400000 in the game) doesn't match, or null.
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	// What the change writes: each site's byte without the Stats tab.
	inline std::vector<operand_write> writes()
	{
		std::vector<operand_write> result;
		for (const auto& s : sites)
		{
			result.push_back({s.va, 1, s.without_stats});
		}
		return result;
	}

	inline void apply(std::uint8_t* image)
	{
		for (const auto& w : writes())
		{
			limits_rules::apply_write(image, w);
		}
	}
}
