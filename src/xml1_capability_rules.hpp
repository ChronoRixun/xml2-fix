#pragma once
#include <array>
#include <cstdint>
#include <string_view>

// Effective XML1 controller state for the shared gameplay-capability record
// (docs/gameplay-agreement.md, IDs 3 and 4). Values describe what is installed,
// never what the INI requested.
namespace xml1_capability_rules {
struct registration { std::array<std::uint32_t,4> values{}; bool verified=true; };

// Stable 32-bit FNV-1a of the exact saved-stage flag name (case-sensitive: the
// native flag reader 0x4a0190 matches names exactly). Two peers bound to the same
// bit range of different variables would otherwise agree and then reconstruct
// different phases after a post-shield load.
constexpr std::uint32_t flag_hash(std::string_view name){
    std::uint32_t h=2166136261u;
    for(unsigned char c:name){h^=c;h*=16777619u;}
    return h;
}
// Master Mold revision 1: values {installed, saved-stage first bit, width, flag hash}.
// Increment it whenever peers with equal values could still simulate the
// encounter differently (decision rules, timing, hero choice, effects).
constexpr std::uint16_t master_mold_revision=1;
inline registration master_mold(bool requested,bool installed,std::string_view flag,int first_bit,int width){
    if(!requested)return {{0,0,0,0},true};          // verified off
    if(!installed||flag.empty()||flag.size()>32||first_bit<1||width<1||width>32)return {{0,0,0,0},false}; // requested, failed
    return {{1,static_cast<std::uint32_t>(first_bit),static_cast<std::uint32_t>(width),flag_hash(flag)},true};
}

// Turret revision 2: values {installed, placements, FNV-1a-32 of every applied placement line in
// order, 0}. Peers with different authored placements, weapons or scanner values are refused.
// Revision 1 (no controller) advertised verified off; a process that does not request the
// controller keeps advertising revision 1 {0,0,0,0}, so it still meets 1.4.0 peers.
constexpr std::uint16_t turret_revision=2;
constexpr std::uint16_t turret_revision_for(bool requested){return requested?turret_revision:1;}
inline registration turrets(bool requested,bool installed,std::uint32_t placements,std::uint32_t lines_hash){
    if(!requested)return {{0,0,0,0},true};
    if(!installed||placements==0)return {{0,0,0,0},false};
    return {{1,placements,lines_hash,0},true};
}

// X-Men Legends 1's skill and stat point rules (rank_costs.cpp, ID 5) revision 3: values {installed parts (1 RankCosts,
// 2 LevelSkillPoints, 4 LevelStatPoints), highest cost honoured (7) or 0, the bonus level step (5) or 0, the bonus
// level's skill points (2) | its stat points (8) << 8}. Revisions 1 and 2 (never released) had fewer parts. The costs
// themselves are the build's talent data.
constexpr std::uint16_t rank_costs_revision=3;

// XML1 settle without crushes ([Game] PlacementCrush=xml1, ID 11) and packs without double heals
// ([Game] DoubleHeal=0, ID 12), revision 1: values {installed,0,0,0}. Not requested (no key, or the game's own
// value): verified off. Requested but not installed (guards, protection, readback; for PlacementCrush also a value
// that is neither xml1 nor xml2): unverified, which closes host and join.
constexpr std::uint16_t placement_crush_revision=1;
constexpr std::uint16_t double_heal_revision=1;
inline registration installed_switch(bool requested,bool installed){
    if(!requested)return {{0,0,0,0},true};
    if(!installed)return {{0,0,0,0},false};
    return {{1,0,0,0},true};
}

// X-Men Legends 1's pack amount ([Game] PackAmount=xml1, ID 13), revision 1: values {installed,0,0,0}. Not requested
// (no key, or xml2): verified off. Requested but not installed (guards, protection, readback, a value that is neither
// xml1 nor xml2): unverified, which closes host and join.
constexpr std::uint16_t pack_amount_revision=1;
inline registration pack_amount(bool requested,bool installed){ return installed_switch(requested,installed); }
}
