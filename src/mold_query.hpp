#pragma once
#include <cstdint>
namespace mold_query {
bool supported();
std::uint32_t select(void* actor,bool forced);
std::uint32_t waypoint(void* hero);
bool is_a(void* entity,std::uint32_t descriptor);
}
