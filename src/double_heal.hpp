#pragma once

#include <Windows.h>

#include <string>

// X-Men Legends 1's packs never heal double, for a mod that carries XML1's packs (the X-Men Legends 1 port: XMen2.exe
// sometimes doubles a pack and then plays its own darker "double" sound, which XML1 never had):
//
//   [Game]
//   DoubleHeal = 0   ; a health or energy pack always heals its single amount and plays the pack's own sound
//
// Without the key, or with 1, nothing is patched. The research and every byte relied on are in double_heal_rules.hpp.
// A gameplay rule both co-op peers must share: it is not registered in the gameplay agreement yet.
namespace double_heal
{
	void install(HMODULE game);
	std::string status(); // "double heal off" / "double heal game's own" / "double heal unavailable"
}
