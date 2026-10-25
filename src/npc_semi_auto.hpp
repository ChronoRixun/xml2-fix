#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace npc_semi_auto
{
 void install(HMODULE game);
 bool requested();
 bool installed();
 bool verified();
 // Extends rule 16 (words 2 and 3 of npc_power's revision: 11 in 1.8.0-beta.7, 14 in beta 8 with the character bullet ray), never allocates a new rule ID. OFF leaves its
 // digest intact. Candidate revisions 6-9 (attack-101b, beta6-officer) are retired; 5 was the withdrawn role
 // experiment. The weapon brain needs NpcPowerRule=xml1, so it only ever ships under rule 16's own revision.
 std::array<std::uint32_t, 4> values(std::array<std::uint32_t, 4> power_values);
 // Rule 16's detail when requested: the SHA-256 of the complete canonical weapon table (npc_semi_auto_rules::
 // table_digest), so two peers with different rows always refuse. Null when not requested.
 const std::array<std::uint8_t, 32>* detail();
}
