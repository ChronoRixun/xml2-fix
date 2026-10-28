#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <cstring>

// Published only after the W3-4 transaction commits. Native records retain their
// original stride and fields; consumers use this descriptor even at stock.
namespace effect_templates {
struct descriptor {
    std::uint32_t definition_base_offset=0x4874, mask_offset=0x419c, stride=0x44;
    std::uint32_t capacity=220;
};
inline descriptor published_layout{};
inline const descriptor& layout() { return published_layout; }
inline std::array<unsigned char,21> validator_bytes(const descriptor& d=layout()) {
    std::array<unsigned char,21> b{0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1,0x8b,0x96,0,0,0,0,0x23,0xd0,0x81,0xfa,0,0,0,0};
    std::memcpy(b.data()+9,&d.mask_offset,4);std::memcpy(b.data()+17,&d.capacity,4);return b;
}
inline unsigned char* definitions(void* owner, const descriptor& d=layout()) {
    return static_cast<unsigned char*>(owner)+d.definition_base_offset;
}
inline const unsigned char* definition(const void* owner, std::uint32_t handle, const descriptor& d=layout()) {
    const auto* pool=static_cast<const unsigned char*>(owner)+d.definition_base_offset;
    std::uint32_t mask{}; std::memcpy(&mask,pool+d.mask_offset,4);
    return pool+(handle&mask)*d.stride;
}
}
