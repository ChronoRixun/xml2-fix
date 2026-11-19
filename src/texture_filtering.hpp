#pragma once
#include "remastered_rules.hpp"
#include <cstdint>
namespace texture_filtering {
// Admission gate, run before any AF hook or side table allocation.
void configure(const remastered_rules::settings& settings);
void device_ready(void* device,std::uint64_t generation);
void device_unavailable(void* device);
void device_recovered(void* device);
}
