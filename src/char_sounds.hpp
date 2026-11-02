#pragma once
#include <Windows.h>
#include <string>
namespace patch_transaction { class transaction; }
namespace char_sounds
{
// Before the shared startup transaction: arrange the online exclusion hook.
void require_agreement(HMODULE game);
// After entity_capacity::after_commit: checks the published actor layout.
// All eight R1 edits and the allocation observer commit as one unit.
void contribute(patch_transaction::transaction&, HMODULE game);
std::string telemetry_json();
}
