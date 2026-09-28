#pragma once

#include <Windows.h>

// X-Men Legends 1's hero levels and kill XP, for a mod that carries XML1's XP amounts (the X-Men Legends 1 port: its
// objectives, scripts and npcstat are XML1's, and on XML2's curve an act-9 objective takes a level-1 hero to 40):
//
//   [Game]
//   XPCurve = xml1   ; XML1's level table (cap 45) and kill XP (half of each kill to every hero, the bench included)
//
// Without the key, or with xml2, nothing is patched. The research, every byte relied on and every byte written are
// in xp_curve_rules.hpp.
namespace xp_curve
{
	// Reads [Game] XPCurve and patches XMen2.exe for it. Must run before the game first asks for a level's XP - from
	// DllMain (see exports.cpp): the herostat load at start-up does, for every hero's starting level.
	void install(HMODULE game);
}
