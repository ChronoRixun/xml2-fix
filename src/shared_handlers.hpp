#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace shared_handlers {
constexpr std::uint16_t revision=1;
void install(HMODULE game);
bool requested();
bool installed();
bool verified();
std::array<std::uint32_t,4> values();
}
