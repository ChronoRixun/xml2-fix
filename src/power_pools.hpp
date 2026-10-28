#pragma once
#include "power_pool_layout.hpp"
#include <Windows.h>
#include <string>

namespace power_pools {
void install(HMODULE game);
void register_agreement();
const power_pool_layout::layout& descriptor(power_pool_layout::pool which);
bool installed(power_pool_layout::pool which);
std::string status();
}
