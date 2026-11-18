#pragma once
#include <cstdint>

// Native free-play state 0xfd holds the four competitive modes. Course indices,
// free practice 0xfe and the inactive sentinel 0xff are deliberately excluded.
namespace skirmish_rules
{
    inline constexpr std::uint32_t danger_state = 0x782728, kind = 0x784824, drmode = 0x781f54;
    inline constexpr bool active(std::uint8_t danger, std::uint32_t mode)
    {
        return danger == 0xfd && mode >= 1 && mode <= 4;
    }
}
