#pragma once

#include <Windows.h>

// The Review menu's tabs, for mods whose campaign had no Stats page: XML2's Review menu has five tabs,
// Screens, Cinematics, Comics, Concepts and Stats, and its Stats page lists XML2's five acts (the act
// count is in the exe). The X-Men Legends 1 port's Review has XML1's four.
//
//   [Game]
//   ReviewStats = 0     ; 0: no Stats tab - left / right, the pad and the mouse go round the other four
//
// The menu file still draws what it has: a mod's review menu leaves the Stats tab's two items out
// (option05_text and its backdrop option05_focus). The research and every byte relied on are in
// review_menu_rules.hpp. Without the key (or with 1) nothing is patched.
namespace review_menu
{
	void install(HMODULE game);
}
