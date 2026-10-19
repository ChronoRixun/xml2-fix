#pragma once
#include <Windows.h>
namespace lockon_effects {
void install(HMODULE game);
bool requested();
bool installed();
bool verified();
}
