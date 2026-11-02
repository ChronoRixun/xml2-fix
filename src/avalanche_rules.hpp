#pragma once
#include <cmath>
#include <cstdint>

// X-Men Legends 1's Avalanche brain (mount2 and astral3_4), as decision rules without game memory. Original
// (default.xbe): setstate avalanche / avalanche_astral (id 9) -> AI state 0x24 (init 0x10f6f0 zeroes 0x4feda0..ac) ->
// state 0x25 brain 0x111ab0, which owns every update until endbattle leaves the state. Globals: 0x4feda0 mode,
// a4 / a8 the close / mid deadlines, ac the health band, 0x4fee30 the target (docs/xml1-controllers.md, "Avalanche").
namespace avalanche_rules {
// Constants (default.xbe .rdata): 0x3c8244 100 (close range and the health percentage scale), 0x3da020 0.06 and
// 0x3c72f0 6 (the band), 0x3c76b0 4 (both deadlines), 0x3ca9ec 400 (mid range), 0x3c7084 0.5 (smash or attack; the
// mid-range taunt chance), 0x3c6e40 1 (the close taunt chance), 0x47c34f80 99999 (no hero yet).
constexpr float close_range=100.f, mid_range=400.f, cooldown=4.f, no_hero=99999.f;
constexpr int power_smash_slot=6;
constexpr std::uint32_t power_smash_buttons=0xa0, power_attack_buttons=0x90, smash_buttons=0x20, attack_buttons=0x10;

// The band: trunc(6 - trunc(health%) x 0.06f), as the original computes it (two _ftol truncations; the product of
// the float constant and an integer is exact in double, so double arithmetic gives the x87 result).
inline int band(float health,float health_max){
    if(!(health_max>0))return 0;
    const int percent=static_cast<int>(static_cast<double>(health)/static_cast<double>(health_max)*100.0);
    return static_cast<int>(6.0-static_cast<double>(0.06f)*percent);
}

struct state { int mode=0,band=0; float close_at=0,mid_at=0; std::uint32_t target=0; };

struct actions {
    bool band_changed=false; bool mode_bit=false;      // write brain bit 3 (PC +0x2b7, XML1 +0x28b) = mode_bit
    bool face=false; bool face_last=false;              // face nearest living hero (close) or last controlled hero (mid)
    std::uint32_t buttons=0;
    bool taunt_always=false;                            // close power smash: random < 1.0 -> taunt
    bool taunt_half=false;                              // mid power attack: random < 0.5 -> taunt (0x10f690)
    bool roll_smash=false;                              // close, before the deadline: random < 0.5 -> smash, else attack
};

struct view {
    float now=0;
    float health=0,health_max=0;
    float nearest=no_hero;            // distance to the nearest eligible hero (no_hero = none)
    bool slot_available=false;        // normal slot 6 (power_smash)
};

// One update. `roll_smash` asks the adapter to draw the random and press smash (< 0.5) or attack.
inline actions update(state& s,const view& v){
    actions a;
    const int k=band(v.health,v.health_max);
    if(k!=s.band){s.band=k;a.band_changed=true;a.mode_bit=(k&1)!=0;s.mode=a.mode_bit?1:0;}
    if(s.mode<0||s.mode>1)return a;
    if(v.nearest<close_range){
        a.face=true;
        if(v.now>s.close_at){
            if(v.slot_available){s.close_at=v.now+cooldown;a.buttons=power_smash_buttons;a.taunt_always=true;}
        }else a.roll_smash=true;
        return a;
    }
    if(v.nearest<mid_range&&v.now>s.mid_at){
        a.face_last=true;
        if(v.slot_available){s.mid_at=v.now+cooldown;a.buttons=power_attack_buttons;a.taunt_half=true;}
    }
    return a;
}
}
