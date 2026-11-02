#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace boss_target_rules {
struct hero {std::uint32_t handle=0;float distance=99999,dz=0;bool living=false,ai=false;};
struct avalanche_target {std::uint32_t nearest=0,controlled=0;float distance=99999;};
inline avalanche_target avalanche(const std::array<hero,5>& heroes){
    avalanche_target result;
    for(const auto& h:heroes){if(!h.living)continue;if(!h.ai)result.controlled=h.handle;
        if(h.distance<result.distance){result.distance=h.distance;result.nearest=h.handle;}}
    return result;
}
inline hero pyro(const std::array<hero,5>& heroes){
    hero result;
    for(const auto& h:heroes)if(h.living&&std::fabs(h.dz)<36&&h.distance<result.distance)result=h;
    return result;
}
inline std::uint32_t mystique(const std::array<hero,5>& heroes){
    hero ai,controlled;
    for(const auto& h:heroes){if(!h.living)continue;auto& chosen=h.ai?ai:controlled;if(h.distance<chosen.distance)chosen=h;}
    return ai.handle?ai.handle:controlled.handle;
}
}
