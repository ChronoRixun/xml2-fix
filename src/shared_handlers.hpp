#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace shared_handlers {
std::uint16_t revision();
void install(HMODULE game);
bool requested();
bool installed();
bool verified();
std::array<std::uint32_t,4> values();
}
