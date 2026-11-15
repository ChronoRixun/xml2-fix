#pragma once
#include <cstdint>
#include <cstring>

namespace danger_room_prizes_rules
{
    // Native reward rows are 0x14 bytes, at course +4/+0x18/+0x2c.
    // Mirror the payload branches in the native prize formatter (0x4ca0e0).
    // Thresholds and the awarded/newly-awarded bits do not describe a prize.
    inline bool has_payload(const std::uint8_t* row, bool wide_items = false)
    {
        std::uint32_t xp = 0;
        std::uint16_t character = 0;
        std::memcpy(&xp, row + 4, sizeof xp);
        std::memcpy(&character, row + 0xa, sizeof character);
        std::uint16_t item = row[8];
        if (wide_items) std::memcpy(&item, row + 8, sizeof item);
        return xp != 0 || item != (wide_items ? 0xffff : 0xff) || row[0] != 4 || row[1] != 6 ||
               (row[0x11] & 6) != 0 || character != 0;
    }
}
