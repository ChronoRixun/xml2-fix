#pragma once
#include <Windows.h>
namespace danger_room_healthbars
{
 void install(HMODULE game);
 bool installed();
 bool requested();
 bool verified();
}
