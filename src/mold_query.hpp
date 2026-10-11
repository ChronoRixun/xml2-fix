#pragma once
#include <cstdint>
namespace mold_query {
bool supported();
std::uint32_t select(void* actor,bool forced);
}
