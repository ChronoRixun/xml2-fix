#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace multipart {
// [Game] Xml1Multipart: X-Men Legends 1's breakable parts (Juggernaut's helmet, the Sentinels' arms).
void install(HMODULE game);
// Captured by install(): the key was requested.
bool requested();
// Shared gameplay rule 6 values: {installed ? 1 : 0, rules revision of the part logic, 0, 0}.
struct registration { std::array<std::uint32_t,4> values{}; bool verified=true; };
registration capability();
}
