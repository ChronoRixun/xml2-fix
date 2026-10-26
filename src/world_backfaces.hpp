#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>

// X-Men Legends 1's one-sided level collision for movement, for a mod that carries XML1's levels (the X-Men Legends 1
// port: XMen2.exe's movement sweeps also meet the back faces of a level's walls, so actors stop or wedge where the
// first game lets them walk through, e.g. HAARP's 520 staging pocket in haarp2_1):
//
//   [Game]
//   WorldBackfaces = xml1   ; actor and entity movement sweeps meet only the level's front faces, as in XML1
//   [WorldBackfaces]
//   ZoneCount = 198         ; required: only these zones (the mod's XML1 levels); without a valid list nothing is patched
//   Z1 = arbiter/a_int/arb2_1
//
// Without the key, empty, xml2 or off, nothing is patched and no agreement entry is registered (the released record).
// The research and every byte relied on are in world_backfaces_rules.hpp. A gameplay rule both co-op peers must
// share: rule 30 of the gameplay agreement (docs/gameplay-agreement.md, docs/world-backfaces.md).
namespace world_backfaces
{
	void install(HMODULE game);
	std::string status(); // "world back faces xml1" / "world back faces game's own" / "world back faces unavailable"

	// For the shared gameplay agreement (rule 30): requested = the key asks for something other than the game's own;
	// installed = the one-sided rule is in place; verified = not requested, or installed.
	bool requested();
	bool installed();
	bool verified();
	// Rule 30 revision 2: {installed, scoped (a zone list), zones listed, 0}; the detail is the list's SHA-256
	// (world_backfaces_rules::digest) when scoped, else null.
	std::array<std::uint32_t, 4> values();
	const std::array<std::uint8_t, 32>* detail();
}
