#pragma once
#include <Windows.h>
#include <cstdint>
namespace boss_pyro {
bool prepare(HMODULE game);
void committed(bool ok);
bool installed();
bool brain(void*,std::uint32_t*);
bool in_scope();
void on_sequence(const char*,const char*);
void on_default_target(const char*);
}
