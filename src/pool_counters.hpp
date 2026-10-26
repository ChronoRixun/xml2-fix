#pragma once
#include <Windows.h>
#include <string>

// D0 reads on the game thread at Present; pipe/log consumers see copies only.
// No rule, capacity change or game-image write belongs to the sampler.
namespace pool_counters {
void install(HMODULE game);
// Called only after startup checks succeeded; worker lifetime is the DLL's process lifetime.
void start_logging();
bool enabled();
void sample();
std::string json();
// Integer-only observer callback. The saved registers/flags are never modified.
void __cdecl refused(unsigned site, const unsigned* saved);
}
