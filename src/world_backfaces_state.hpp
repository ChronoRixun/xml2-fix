#pragma once

#include <cstdint>

// The back-face flag the movement trace helper passes to the collider (vt+0x88) when the DLL owns that helper:
// with [Limits] EntityCapacity the entity-capacity transaction bridges XMen2.exe's 0x4a9650 to
// entity_query::QueryApi_004a9650, so the retail operand world_backfaces patches (0x4a96c1) never runs. The owner
// passes this value instead: 1 (the game's own two-sided sweep) unless [Game] WorldBackfaces=xml1 installed (0).
namespace world_backfaces_state
{
	inline volatile std::uint8_t trace_back_faces = 1;
}
