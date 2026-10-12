#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>

// X-Men Legends 1's solo mode for a mod's own campaign ([Game] SoloMode=1; docs/solo-mode.md).
//
// The first game's solo-trigger boxes call enterSoloMode('_ACTIVATOR_') at a gap the leader crosses alone; four
// moving-bridge scripts call exitSoloMode. While the party is solo its AI heroes neither take up formation behind
// the leader nor run their follow movement (they stop where they stand, the original's else branch), don't use a
// bridge power for the leader, don't take the ally-assist step and ignore the follow-me command; combat and damage
// are untouched. The party leaves solo mode by script, or automatically once every living peer can reach the hero
// the game follows (the original's world-update rule: first check 1.5 s after solo began, then every second).
// Nothing is saved: a character made after the toggle (zone load, save load) is not solo, as in the original.
//
// XMen2.exe dropped the solo tests from the same AI functions; the fix puts them back at the six sites
// solo_mode.cpp lists, all-or-nothing in one transaction after every guard byte matched. Scripts reach it through
// setSoloMode(name, on), registered with the forced parties' functions (forced_teams.hpp).
namespace solo_mode
{
	// Reads [Game] SoloMode and installs the AI sites. From DllMain, before XMen2.exe's own code runs.
	void install(HMODULE game);
	bool requested();
	bool installed();
	// setSoloMode(name, on) for the character `entity` the script named: on - default.xbe's enterSoloMode (0x9eef0),
	// off - exitSoloMode (0x9ef70). Nothing happens without the installed sites.
	void script_request(void* entity, bool on, const char* name);
	// Shared gameplay rule (docs/gameplay-agreement.md): {installed, revision of the decision rules, 0, 0}.
	struct registration { std::array<std::uint32_t, 4> values{}; bool verified = true; };
	registration capability();
}
