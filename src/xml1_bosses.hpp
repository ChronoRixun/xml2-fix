#pragma once
#include <Windows.h>
#include "xml1_capability_rules.hpp"
// The shared XML1 boss dispatcher (docs/xml1-controllers.md, "Shared boss dispatcher"): one brain-dispatch detour at
// 0x5173f3 and the three script-table entries, installed in one startup transaction for every requested client -
// Master Mold ([Game] Xml1MasterMold, rule 3) and the bosses [Game] Xml1Bosses lists (rule 40). Nothing requested:
// nothing is written.
namespace xml1_bosses {
void install(HMODULE game);
// [Game] Xml1Bosses named at least one boss (rule 40 registers only then).
bool requested();
// Effective state for rule 40 (after install()).
xml1_capability_rules::registration capability();
}
