#pragma once

#include <Windows.h>

// The version the game tells GameSpy (OpenSpy) and other players, so a mod that is another game on the
// same GameSpy name keeps its online games apart from XML2's:
//
//   [Online]
//   GameVersion=X1.0   ; 1 to 4 letters, digits, '.', '-' or '_'; not set: the game's own 1.30
//
// Players with different versions don't see or join each other's games. Written into XMen2.exe's
// string from DllMain, before the game copies it; the research and every byte relied on are in
// game_version_rules.hpp. Without the key nothing is patched.
namespace game_version
{
	void install(HMODULE game);
}
