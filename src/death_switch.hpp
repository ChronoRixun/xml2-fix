#pragma once
#include <Windows.h>
#include "death_switch_rules.hpp"
// [Game] DeathSwitchClear (docs/death-switch.md).
namespace death_switch
{
 void install(HMODULE game);
 // Captured by install(): Game.DeathSwitchClear was requested.
 bool requested();
 bool installed();
 // Effective installed state for shared gameplay rule 15 (after install()).
 death_switch_rules::registration capability();
 // Hand-overs whose successor had its move intent cleared (test pipe / log).
 unsigned long cleared();
}
