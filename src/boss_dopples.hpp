#pragma once
#include <Windows.h>
#include <cstdint>
// X-Men Legends 1's doppelganger overlay in the colosseum (dopple_rules.hpp), a client of the shared XML1 boss
// dispatcher (xml1_bosses.hpp), which installs it when [Game] Xml1Bosses lists "dopples".
namespace boss_dopples {
// Checks the overlay's guards; true = ready (the dispatcher then writes the shared brain-dispatch detour).
bool prepare(HMODULE game);
void committed(bool ok);
bool installed();
// [Game] Xml1BossFull (default 1): the dopple setstates' bossfull flag is written at binding (live check D3).
bool bossfull();
// Shared hook callback (true = handled: skip the native state machine).
bool brain(void* brain,std::uint32_t* output);
}
