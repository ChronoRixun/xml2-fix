#pragma once

#include <Windows.h>

// X-Men Legends 1's Xtraction Point menu had no world map, for a mod that keeps XML2's town
// centres in its zone data without offering them (the X-Men Legends 1 port: the menu's Xtract
// choice opens XML2's world map, whose five acts' town centres are not the mod's):
//
//   [Game]
//   Xtract = 0   ; an Xtraction Point's menu offers its title, Change Team and Save only
//
// Without the key, or with 1, nothing is patched. With 0, the block of extractionPoint's code
// that adds the Xtract choice is jumped over. The research, every byte relied on and the two
// bytes written are in xtract_rules.hpp.
namespace xtract
{
	// Reads [Game] Xtract and patches XMen2.exe for it. From DllMain (see exports.cpp): before the
	// game's first zone and its first Xtraction Point.
	void install(HMODULE game);
}
