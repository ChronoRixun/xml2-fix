#pragma once
#include <Windows.h>

// Companion to LC's xml1_life_range attribute. No release/agreement identity is assigned here.
namespace control_life {
void install(HMODULE game);
bool installed();
}
