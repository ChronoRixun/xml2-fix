#pragma once
#include <cstdint>
namespace boss_owner_rules {
inline bool reset_required(std::uint32_t owner,std::uintptr_t brain,unsigned zone,float last,
                           std::uint32_t next_owner,std::uintptr_t next_brain,unsigned next_zone,float now){
    return owner!=next_owner||zone!=next_zone||(brain&&next_brain&&brain!=next_brain)||now<last;
}
}
