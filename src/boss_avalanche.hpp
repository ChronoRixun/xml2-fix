#pragma once
#include <Windows.h>
#include <cstdint>
// X-Men Legends 1's Avalanche brain (avalanche_rules.hpp), a client of the shared XML1 boss dispatcher
// (xml1_bosses.hpp), which installs it when [Game] Xml1Bosses lists "avalanche".
namespace boss_avalanche {
bool prepare(HMODULE game);
void committed(bool ok);
bool installed();
// Shared hook callbacks: brain dispatch (true = handled) and a script pattern request (source, sequence).
bool brain(void* brain,std::uint32_t* output);
void on_sequence(const char* source,const char* sequence);
bool in_scope();
}
