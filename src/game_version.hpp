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
	// LC may start only with a working exclusion hook, including when the version patch is refused.
	bool startup_allowed();
	bool needs_gameplay_agreement();
	bool gameplay_online_allowed();
	bool require_gameplay_agreement(HMODULE game);
	void bind_gameplay_agreement(bool verified);
	void bind_gameplay_refusal(void (*show)());
	void install(HMODULE game);
}
