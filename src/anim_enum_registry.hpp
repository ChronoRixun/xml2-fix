#pragma once
#include "anim_enum_layout.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace anim_enum_registry
{
// Defined in the generated name header, shared with the published DLL block.
struct entry { std::string name; int id; };
class registry
{
    std::vector<entry> sorted_;
    std::uint32_t hash_ = 2166136261u;
public:
    bool load(std::string_view sidecar, std::string& error);
    int lookup(std::string_view lowercased_native_buffer) const;
    std::size_t size() const { return sorted_.size(); }
    std::uint32_t hash() const { return hash_; }
    std::array<std::uint32_t,4> agreement() const
    { return {anim_enum_layout::raised.enums | (anim_enum_layout::raised.records<<16),static_cast<std::uint32_t>(size()),hash_,1}; }
};
}
