#pragma once
#include <Windows.h>
#include "xml1_capability_rules.hpp"
// [Game] RankCosts=1: X-Men Legends 1's per-rank skill costs (rank_costs_rules.hpp).
namespace rank_costs {
void install(HMODULE game);
// Captured by install(): Game.RankCosts was requested.
bool requested();
bool installed();
// [Game] LevelSkillPoints=xml1: X-Men Legends 1's 2 skill points on every 5th level (rank_costs_rules::level_skill_points).
bool level_requested();
bool level_installed();
// Effective state for shared gameplay rule 5 (revision 2): {rank costs installed, the highest cost honoured (7) or 0,
// the bonus level step (5) or 0, its skill points (2) or 0}; all zero and verified when neither key asks; unverified
// when a requested part is not installed.
xml1_capability_rules::registration capability();
}
