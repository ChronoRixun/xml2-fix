#pragma once
#include "limits_rules.hpp"
#include <array>
namespace boss_native_sites {
// Marrow's shipped backboss binding already reaches the retained PC initializer and tick. Never replace it.
inline constexpr std::array<limits_rules::guard,6> marrow{{
    {0x52107c,"bfbc5869008bf5b909000000","backboss parser"},
    {0x51f49a,"8b4c24106a1ee88b5bfeff","backboss requests native init"},
    {0x51788a,"6844506900578bcee8f91f0100","native area1 initializer"},
    {0x51789e,"578bcee86a210100","native handler dispatch"},
    {0x529890,"83ec48a1f8386f005355","native Marrow initializer"},
    {0x529a10,"81ecdc000000a1f8386f00","native Marrow handler"}}};
}
