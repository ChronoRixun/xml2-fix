#pragma once

#include <Windows.h>

#include <string>

// X-Men Legends 1 settles no object on the floor, so nothing it places crushes anything, for a mod that carries XML1's
// zones (the X-Men Legends 1 port: furniture authored overlapping, a desk chair tucked under a desk, "crushes" the
// lighter piece when XMen2.exe settles it on the floor, and the player hears hit sounds from an empty room). XMen2.exe
// settles every object at a zone's load and each one spawned in play 0.5 s after it appears (death drops, script
// spawns, debris):
//
//   [Game]
//   PlacementCrush = xml1   ; while an object is settled on the floor, one object touching another doesn't crush it
//
// Without the key, or with xml2, nothing is patched. During play the game's own rule stays. The research, every
// byte relied on and the code written are in placement_crush_rules.hpp.
namespace placement_crush
{
	// Reads [Game] PlacementCrush and hooks XMen2.exe for it. From DllMain (see exports.cpp): before the first zone.
	void install(HMODULE game);

	// For the test pipe's status: "placement crush xml1; touches refused while settling 13".
	std::string status();

	// For the shared gameplay agreement (rule 11): the key asked for xml1 (or held a value that is neither xml1 nor
	// xml2), and whether the change is in place.
	bool requested();
	bool installed();
}
