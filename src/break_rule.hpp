#pragma once

#include <Windows.h>

#include <string>

// X-Men Legends 1's rule for which attack breaks which object, for a mod that carries XML1's objects and attacks
// (the X-Men Legends 1 port: XML2 keeps an object's structure in 0..2 and caps a melee hit's level at 1, so a
// port that keeps XML1's sturdier objects breakable at all lets a plain punch break what XML1 kept for powers):
//
//   [Game]
//   BreakRule = xml1   ; an object whose definition carries xml1structure breaks only to XML1's level for it
//
// Without the key, or with xml2, nothing is patched. With it, a hit the game is about to let damage an object is
// refused when XML1's level of the hit (the attack's authored level, a hero's Might, the damageLevel affecter)
// is below the object's `xml1structure`. Nothing a character, a save or another player's game reads changes. The
// research, every byte relied on and the code written are in break_rule_rules.hpp.
namespace break_rule
{
	// Reads [Game] BreakRule and hooks XMen2.exe for it. From DllMain (see exports.cpp): before the game spawns
	// its first object.
	void install(HMODULE game);

	// For the test pipe's status: "break rule xml1; object hits judged 41; refused 12; without xml1structure 7".
	// "xml2": no key (or xml2); "unavailable": the key is xml1 but a guard refused (the log says which).
	std::string status();
}
