#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>

namespace stun_rule
{
 constexpr std::uint16_t revision = 2;
 void install(HMODULE game);
 bool requested();
 bool installed();
 bool verified();
 std::array<std::uint32_t, 4> values();
}
