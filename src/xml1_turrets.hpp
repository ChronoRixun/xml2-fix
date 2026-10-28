#pragma once
#include <Windows.h>
#include "xml1_capability_rules.hpp"
// XML1 scan turrets (Game.Xml1Turrets, builder-supplied [Xml1Turrets]): the first game's scanner,
// rotation and weapon timing on the existing turret physents, driven from the native event
// scheduler's per-frame dispatch. Default off; a failed install keeps the inert physents.
namespace xml1_turrets {
void register_ai_layout();
void install(HMODULE game);
bool requested();
// Effective installed state for shared gameplay rule 4 (after install()).
xml1_capability_rules::registration capability();
}
