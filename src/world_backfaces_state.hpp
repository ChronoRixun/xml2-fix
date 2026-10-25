#pragma once

#include <cstdint>

// The back-face flag the movement trace helper passes to the collider (vt+0x88) when the DLL owns that helper:
// with [Limits] EntityCapacity the entity-capacity transaction bridges XMen2.exe's 0x4a9650 to
// entity_query::QueryApi_004a9650, so the retail code world_backfaces bridges (0x4a96c0) never runs. The owner
// passes current(): XMen2.exe's own 1 unless [Game] WorldBackfaces=xml1 installed, then the current zone's flag (0 in
// a listed X-Men Legends 1 zone, or in every zone without a list; 1 elsewhere) - the same function the retail
// helper's bridge calls.
namespace world_backfaces_state
{
	// Used while no source is set (the game's own 1; tests set it directly).
	inline volatile std::uint8_t trace_back_faces = 1;
	// Set by world_backfaces::install once rule 30 is in place: the current zone's flag.
	inline std::uint8_t(__cdecl* volatile source)() = nullptr;
	inline std::uint8_t current()
	{
		const auto s = source;
		return s ? s() : trace_back_faces;
	}
}
