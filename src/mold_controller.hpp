#pragma once
#include <Windows.h>
namespace mold_controller {
void install(HMODULE game);
bool beam_target(void* actor, void*& target);
}
