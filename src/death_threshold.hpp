#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

// X-Men Legends 1's death threshold, for a mod that carries the first game (the X-Men Legends 1 port):
//
//   [Game]
//   DeathRule = xml1   ; an entity dies on the hit that leaves it below 1 health, as in X-Men Legends 1
//
// Without the key, or with xml2, nothing is patched and an entity dies at 0 as XMen2.exe has it. With it, the seven
// comparisons XML1 makes against 1.0 instead of 0.0 (the damage function's death test and the four functions that
// share its line; death_threshold_rules.hpp) use 1.0, and a killing hit that leaves health above 0 sets it to 0 as
// XML1's whole numbers always did (a small bridge at the death branch's entry). Nothing a save reads changes.
namespace death_threshold
{
	// Reads [Game] DeathRule and patches XMen2.exe for it, all eight sites or none. From DllMain (see exports.cpp):
	// before the game's first hit.
	void install(HMODULE game);

	bool requested(); // the key says xml1
	bool installed(); // and every site carries the rule
	bool verified();  // nothing is half-installed: not requested, or installed
	// The shared gameplay record's entry (rule 9, revision 1): {installed ? 1 : 0, 0, 0, 0}.
	std::array<std::uint32_t, 4> values();
}
