#pragma once
#include <array>
#include <cstdint>

// Effective XML1 controller state for the shared gameplay-capability record
// (docs/gameplay-agreement.md, IDs 3 and 4). Values describe what is installed,
// never what the INI requested.
namespace xml1_capability_rules {
struct registration { std::array<std::uint32_t,4> values{}; bool verified=true; };

// Master Mold revision 1: values {installed, saved-stage first bit, width, 0}.
// Increment it whenever peers with equal values could still simulate the
// encounter differently (decision rules, timing, hero choice, effects).
constexpr std::uint16_t master_mold_revision=1;
inline registration master_mold(bool requested,bool installed,int first_bit,int width){
    if(!requested)return {{0,0,0,0},true};          // verified off
    if(!installed||first_bit<1||width<1||width>32)return {{0,0,0,0},false}; // requested, failed
    return {{1,static_cast<std::uint32_t>(first_bit),static_cast<std::uint32_t>(width),0},true};
}

// No turret scanner/weapon controller exists in this build. Revision 1 always
// advertises verified off; a future controller registers its own revision/values.
constexpr std::uint16_t turret_revision=1;
inline registration turrets(){ return {{0,0,0,0},true}; }
}
