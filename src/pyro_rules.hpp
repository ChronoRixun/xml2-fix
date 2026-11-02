#pragma once
#include <cmath>
#include <cstdint>
// XML1 0x111ef0: astral Pyro only; the five native handles are attacker reservations.
namespace pyro_rules {
struct state {bool fire_up=true;float deadline=0;};
struct action {std::uint32_t buttons=0;float taunt=0;};
constexpr int none_team=0x1c,enemy_team=0x1e;
inline bool height_ok(float dz){return std::fabs(dz)<36.f;}
inline action update(state& s,float now,float distance,bool slot5,bool slot8){
    if(!(now>s.deadline))return {};
    if(s.fire_up){if(distance<300&&slot8){s.deadline=now+3;return {0xc0,1};}}
    else if(distance<200){if(slot5){s.deadline=now+5;return {0x90,.5f};}}
    else if(distance<300&&slot8){s.deadline=now+5;return {0xc0,1};}
    return {};
}
}
