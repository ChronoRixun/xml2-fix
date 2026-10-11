#pragma once
#include <Windows.h>
#include "xml1_capability_rules.hpp"
namespace mold_controller {
void install(HMODULE game);
// Captured by install(): Game.Xml1MasterMold was requested.
bool requested();
// Effective installed state for shared gameplay rule 3 (after install()).
xml1_capability_rules::registration capability();
bool beam_target(void* actor, void*& target);
}
