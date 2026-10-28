#pragma once
#include "script_pools_layout.hpp"
#include <Windows.h>
#include <string>

namespace script_pools
{
// Enlist before the shared startup transaction; install after ScriptFunctions.
// OFF does not contribute or register a rule.
void require_agreement(HMODULE game);
void install(HMODULE game);
const std::array<pool_layout,4>& descriptor();
// ForcedTeams consumes the published value-allocator guards after relocation.
bool value_makers_match();
void sample(); // game thread only; the pipe reads the published atomic snapshot
std::string telemetry_json();
std::string status();
}
