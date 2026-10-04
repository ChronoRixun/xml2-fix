#pragma once

#include <Windows.h>

#include <string>

// Geometry sharing: when the game loads a skinned mesh it shares a loaded one whose positions and
// weights match, even when their packed blend (bone) indices differ, and one of them is then drawn
// with the other's bones (in the X-Men Legends I port, NPC outlines deformed into large black spikes).
// With the key set, a candidate whose indices differ is skipped and the game goes on looking.
//
//   [Game]
//   GeometrySharingBlendIndices=1   ; off when absent or 0; the X-Men Legends I port sets it
//
// Nothing is hooked without the key, or when any code guard in XMen2.exe, libIGGfx.dll or
// libIGAttrs.dll isn't the retail build's. Geometry this doesn't know keeps the game's own comparison
// (geometry_sharing_rules.hpp).
namespace geometry_sharing
{
	// Reads [Game] GeometrySharingBlendIndices and, when it is 1, hooks the candidate loop. From DllMain
	// (see exports.cpp).
	void install(HMODULE game);

	// For the test pipe's status: "geometry sharing on; geometry comparisons 120; geometry rejected 3;
	// geometry fallback 0". "on": the key is 1 and the hook is in; "off": the key is absent or 0;
	// "unavailable": the key is 1 but a guard refused (the log says which). Aggregate counts only.
	std::string status();
}
