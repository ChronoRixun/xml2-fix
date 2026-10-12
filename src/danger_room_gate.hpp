#pragma once
#include <Windows.h>

// [Game] DangerRoomGate=<item name>: the first game's main-menu Danger Room gate (danger_room_gate_rules.hpp).
// Without the key nothing is installed: the item stays always available.
namespace danger_room_gate
{
	void install(HMODULE game);
}
