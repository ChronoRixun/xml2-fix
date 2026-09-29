#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstring>
#include <string>

// New Game's starting team and the default hero unlocks, for mods that bring their own campaign (the
// X-Men Legends 1 port starts, as XML1 does, with Wolverine alone).
//
// XMen2.exe's startFirstMission (0x4a7b10) seats four heroes by name before it runs
// Scripts/menus/new_game.py: it pushes the string constants "magneto", "cyclops", "wolverine" and
// "storm" (two pushes per slot, 0x4a7b41..0x4a7bfe). Its resetgame (0x5f2e70) unlocks 17 heroes by name
// (pushes 0x5f30c2..0x5f32ee). An empty name clears a party slot (0x46c8dd tests the first byte) and the
// unlock of "" does nothing (0x449c08), and the game already uses the empty string 0x681968 for "no hero".
//
// The save folder: saves and settings.dat (the profile, which keeps the hero unlocks from game to game)
// live in Documents\Activision\X-Men Legends 2\Save\, shared by every copy of the game on the PC. A mod
// with its own campaign needs its own: XML2's unlocks are stored by herostat index, so another roster's
// heroes come up unlocked. 0x55e760 formats the folder from the push at 0x55e7ad, the screenshot folder
// from the push at 0x4019f0 (both "%s\Activision\X-Men Legends 2\...\"), and creates it (0x629850).
//
//   [Game]
//   NewGameTeam = wolverine        ; up to four names, comma separated; missing slots are empty
//   ResetUnlocks = 0               ; 0 = resetgame unlocks nobody (the mod's scripts unlock heroes)
//   SaveFolder = X-Men Legends     ; saves in Documents\Activision\<SaveFolder>\Save (and Screenshots)
//   NewGamePlus = 0                ; 0 = no "use saved game statistics" choice once Hard is unlocked
//
// NewGamePlus: after a win on Normal the profile has Hard unlocked, and New Game's setDifficultyLevel then
// offers XML2's New Game+ (default or saved statistics) instead of starting; a campaign that had none
// (XML1) starts at once with the default statistics. The bytes are in new_game_plus_rules.hpp.
//
// Without the keys nothing is patched. Every push operand is checked against the retail value first.
namespace new_game
{
	constexpr std::size_t save_folder_max = 64;

	// A single folder name Windows accepts: no separators or reserved characters, not "." / "..", no
	// leading or trailing space or dot, at most save_folder_max characters.
	inline bool valid_save_folder(const std::string& name)
	{
		if (name.empty() || name.size() > save_folder_max || name.front() == ' ' || name.back() == ' ' || name.back() == '.' || name == "." || name == "..")
		{
			return false;
		}
		for (const char c : name)
		{
			if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126 || std::strchr("\\/:*?\"<>|%", c)) // % too: the name goes into a printf format
			{
				return false;
			}
		}
		return true;
	}

	void install(HMODULE game);
}
