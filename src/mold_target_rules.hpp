#pragma once
#include <cstdint>
#include <span>
#include <limits>
namespace mold_target_rules {
struct candidate {
    std::uint32_t handle;
    std::uint32_t flags;
    bool core, can_activate;
    float boss_distance_squared, activation_distance;
};
// Candidates arrive in native query order. Range/collision membership belongs
// to that query; origin distance is not a substitute for entity bounds.
inline std::uint32_t choose(bool forced,float hero_distance_squared,std::span<const candidate> candidates) {
    if(!forced && !(hero_distance_squared<380.f*380.f))return 0;
    std::uint32_t chosen=0;
    float closest=std::numeric_limits<float>::max();
    for(const auto& c:candidates){
        // XBE team bits 26/28/29 are PC none/enemy/altenemy 28/30/31;
        // aishootme is XBE bit 23, PC bit 24. PC enable is bit 20.
        constexpr auto excluded=(1u<<28)|(1u<<30)|(1u<<31);
        if(!c.handle||!c.core||!c.can_activate||(c.flags&excluded)||
            !(c.flags&(1u<<24))||!(c.flags&(1u<<20)))continue;
        if(forced){
            if(c.boss_distance_squared<closest){chosen=c.handle;closest=c.boss_distance_squared;}
        }else if(c.activation_distance>=0){
            // XBE 0x1162f0 stores this candidate at stack+0x18. The later
            // distance call uses it again, comparing the candidate to itself.
            // Keep the last eligible query result, as 0x11637f does.
            chosen=c.handle;
        }
    }
    return chosen;
}
}
