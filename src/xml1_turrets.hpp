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
// The scheduler dispatch pointer this module installed at the timed-event manager vtable slot vt+0xc (0 when not
// installed): a later module whose retail guard covers that slot composes with it (join_in_progress follow-up 8).
std::uint32_t installed_process_hook();
// Every peer calls once at the canonical late-restart frame-0 epoch, even for the same zone/time.
void cohort_epoch(float time);
struct digest { std::uint32_t hash = 0, placements = 0, live = 0, flights = 0; };
// Read-only semantic state (entity handles normalized by the installed world's index width).
digest probe_digest(unsigned index_bits);
// Effective installed state for shared gameplay rule 4 (after install()).
xml1_capability_rules::registration capability();
}
