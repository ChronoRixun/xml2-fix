#pragma once
#include <Windows.h>
#include "xml1_capability_rules.hpp"
// [Game] RankCosts=1: X-Men Legends 1's per-rank skill costs (rank_costs_rules.hpp).
namespace rank_costs {
void install(HMODULE game);
// Captured by install(): Game.RankCosts was requested.
bool requested();
bool installed();
// Effective state for shared gameplay rule 5: {installed, the highest cost the data may give, 0, 0}; verified off when
// not requested, unverified when requested but not installed.
xml1_capability_rules::registration capability();
}
