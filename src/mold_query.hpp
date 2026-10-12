#pragma once
#include <cstdint>
namespace mold_query {
bool supported();
std::uint32_t select(void* actor,bool forced);
std::uint32_t waypoint(void* hero);
// _ACTIVE_HERO_ offline; in an online session the shared player-slot hero.
void* active_hero(std::uint32_t& handle);
bool is_a(void* entity,std::uint32_t descriptor);
// Both native temporary query pools have a free entry (either layout); shared with the turret controller.
bool pools_ready();
}
