#pragma once
#include <Windows.h>
#include <cstdint>
#include "xml1_capability_rules.hpp"
#include "xml1_hook_transaction.hpp"
// Master Mold, a client of the shared XML1 boss dispatcher (xml1_bosses.hpp), which installs it.
namespace mold_controller {
void register_ai_layout();
// Reads Game.Xml1MasterMold and checks this controller's guards; true = ready, with its beam patch in `beam`.
bool prepare(HMODULE game,xml1_hook_transaction::patch& beam);
// The shared transaction's outcome for a prepared controller (ok, or refused with/without a verified rollback).
void committed(bool ok,bool safe);
void script_table_refused();
// Captured by prepare(): Game.Xml1MasterMold was requested.
bool requested();
bool active();
// Effective installed state for shared gameplay rule 3 (after the shared install).
xml1_capability_rules::registration capability();
bool beam_target(void* actor, void*& target);
// Shared hook callbacks: brain dispatch (true = handled) and the three script-table entries (before the native call).
bool brain(void* brain,std::uint32_t* output);
void on_sequence(void* args);
void on_default_target(void* args);
void on_node(void* args);
}
