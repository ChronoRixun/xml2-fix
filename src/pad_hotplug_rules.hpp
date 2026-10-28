#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace pad_hotplug_rules
{
    constexpr std::uint32_t image_base = 0x400000;
    constexpr std::uint32_t entry = 0x61862a, next = 0x618630;
    constexpr std::uint32_t mapping = 0x6efa48;
    constexpr unsigned max_map_actions = 50; // 50 * 4 * 12 bytes, first-pad metadata at +0x964
    // Advanced Options' accept handler (not startup): native GUID matches are
    // 0..3. Other values mean missing, new or empty.
    // Reserve every known match first, then give unresolved saved slots distinct
    // remaining ordinals. A newly connected device can therefore use its bindings.
    inline std::array<std::uint32_t, 4> resolve(std::array<std::uint32_t, 4> slots)
    {
        unsigned used = 0;
        for (auto& slot : slots)
            if (slot < 4 && !(used & (1u << slot))) used |= 1u << slot;
            else slot = UINT32_MAX;
        for (auto& slot : slots)
            if (slot == UINT32_MAX)
                for (unsigned i = 0; i < 4; ++i)
                    if (!(used & (1u << i))) { slot = i; used |= 1u << i; break; }
        return slots;
    }
    struct guard { std::uint32_t va; std::string_view hex; };
    inline constexpr guard guards[] {
        {entry, "391d48fa6e00"}, // first use of completed saved-to-current mapping
        {0x6185ce, "c745009a020000"}, // missing saved GUID sentinel
        {0x618616, "c70009030000"}, // new current GUID sentinel
        {0x61862a, "391d48fa6e00b8780300007505a348fa6e00"}, // empty slot fallback
        {0x61877a, "81ff9a0200007508c70100000000eb75"}, // pad 1 missing -> erase action
        {0x618798, "81fd9a0200007508c70100000000eb57"}, // pad 2
        {0x6187b7, "81fb9a0200007508c70100000000eb38"}, // pad 3
        {0x6187d6, "81fe9a0200007508c70100000000eb19"}, // pad 4
        {0x6187f3, "c1e21025ffff00000bd08911"}, // retain control, change only device number
        {0x628e20, "8a44240483ec185355568bf1884602576a01"}, // complete native input refresh
        {0x628e49, "8b86e42700008b106a0450ff5210"}, // controller enumeration (validated against retail)
    };
}
