#pragma once

#include <Windows.h>

// New Game's starting team and the default hero unlocks, for mods that bring their own campaign (the
// X-Men Legends 1 port starts, as XML1 does, with Wolverine alone).
//
// XMen2.exe's startFirstMission (0x4a7b10) seats four heroes by name before it runs
// Scripts/menus/new_game.py: it pushes the string constants "magneto", "cyclops", "wolverine" and
// "storm" (two pushes per slot, 0x4a7b41..0x4a7bfe). Its resetgame (0x5f2e70) unlocks 17 heroes by name
// (pushes 0x5f30c2..0x5f32ee). An empty name clears a party slot (0x46c8dd tests the first byte) and the
// unlock of "" does nothing (0x449c08), and the game already uses the empty string 0x681968 for "no hero".
//
//   [Game]
//   NewGameTeam = wolverine        ; up to four names, comma separated; missing slots are empty
//   ResetUnlocks = 0               ; 0 = resetgame unlocks nobody (the mod's scripts unlock heroes)
//
// Without the keys nothing is patched. Every push operand is checked against the retail value first.
namespace new_game
{
	void install(HMODULE game);
}
