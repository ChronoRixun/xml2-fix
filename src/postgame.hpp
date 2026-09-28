#pragma once

#include <Windows.h>

// What runs after the end credits, for mods with their own campaign: XML2's credits end the game
// by loading XML2's last zone (act5/egypt/egypt6); the X-Men Legends 1 port plays XML1's closing
// movie and goes back to the main menu instead.
//
//   [Game]
//   PostgameScript = x1/menus/postgame   ; Scripts\x1\menus\postgame.py runs in place of the egypt6 load
//
// The end-of-game save before it stays. The research and every byte relied on are in
// postgame_rules.hpp. Without the key nothing is patched.
namespace postgame
{
	void install(HMODULE game);
}
