#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace char_sound_sites
{
constexpr std::uint32_t capacity=127, stride=0xdc;
enum class value { bound, base, key, end };
struct site { std::uint32_t va; std::string_view hex; unsigned offset; value replacement; };
// R1, design 4.6.1: six address operands and BOTH signed-imm8 bounds.
constexpr std::array<site,8> sites{{
    {0x438cb8,"83f810",2,value::bound},
    {0x438cc3,"05480d7100",1,value::base},
    {0x438cc9,"b8480d7100",1,value::base},
    {0x438dfc,"bb4c0d7100",1,value::key},
    {0x438e50,"05480d7100",1,value::base},
    {0x438e8d,"81fb0c1b7100",2,value::end},
    {0x438eb4,"81c6480d7100",2,value::base},
    {0x42198d,"83f810",2,value::bound},
}};
}
