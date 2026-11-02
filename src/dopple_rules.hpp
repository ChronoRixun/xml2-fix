#pragma once
#include <cstdint>

// X-Men Legends 1's doppelganger overlay (the colosseum's four dopples), as decision rules without game memory.
// Original (default.xbe): the dop_* setstates (ids 0x22..0x25) store each dopple's handle in slots 0x4feda0..ac
// (Wolverine, Colossus, Jean, Cyclops); the AI's pre-dispatch 0x117030 runs the overlay 0x116f70 on every update
// of an enemy-team actor while 0x4fee08 is set, and a handled update skips the native state machine. Handlers by
// slot (table 0x117014): Wolverine 0x1136f0, Colossus 0x1138a0, Jean 0x113c50, Cyclops 0x113b70; shared tick 0x110470,
// once per distinct game time. Each handler reads its partner's slot: Wolverine + Colossus throw the fastball special,
// Jean sets off the arena's aishootme hazards near the hero. Jean's states 1/2 and Cyclops' reply are kept as the
// original has them, without an entry the original lacks (docs/xml1-controllers.md, "Doppelgangers").
namespace dopple_rules {
enum class who { wolverine, colossus, jean, cyclops };

// The original's constants (default.xbe .rdata): 0x3ca9ec 400 (pairing range), 0x3c6de0 5 (fastball re-arm),
// 0x3d6464 55 (hand-off distance), 0x3cc230 7 (Jean's hazard interval), 0x3c9760 0.05 (taunt chance), move speed 3.0,
// waits 1.0 and 0.4 (Cyclops), hazard query radius 400 with the activation-radius test (flag 1).
constexpr float pair_range=400.f, rearm=5.f, handoff=55.f, hazard_interval=7.f, taunt_chance=.05f;
constexpr float move_speed=3.f, wait_long=1.f, wait_short=.4f, hazard_radius=400.f;
// Not in the original: a fastball that has not been thrown 10 s after it started is abandoned (both flags 0), so a
// throw the PC move set never performs cannot hold both dopples for the rest of the fight (live check D2).
constexpr float fastball_watchdog=10.f;
// Buttons (both output words, unchanged bit values on PC): 0x100 action/guard, 0x80 power, 0x10 attack.
constexpr std::uint32_t throw_buttons=0x100, power_attack_buttons=0x90;
constexpr int guard_slot=3, power_attack_slot=5;

// The overlay's shared state (XML1 0x4fedb0 Wolverine flag, b4 Colossus flag, b8 Jean, bc Cyclops, c4 re-arm time,
// cc hazard time, d0 last tick time). Zeroed when the overlay starts (dopple init 0x10f760).
struct shared {
    int wolverine=0,colossus=0,jean=0,cyclops=0;
    float rearm_at=0,hazard_at=0,last_tick=0,fastball_started=0;
    bool ticked=false;
};

// What the adapter must do for one update. Movement and facing use the partner's origin.
struct actions {
    bool handled=false;          // skip the native state machine (the original returns true)
    bool move_to_partner=false;  // move-to (vt+0x160) the partner, speed 3.0, no movement name
    bool stop=false;             // movement state, movement target, physical stop (in that order)
    bool face_partner=false;     // face the partner's flattened direction
    bool face_immediate=false;   // Wolverine: immediate transform; Colossus: movement transform
    bool target_hero=false;      // combat target = the active hero (Colossus' throw)
    bool brain_target_hero=false;// brain target = the active hero (Jean's hazards)
    bool activate_hazard=false;  // query and activate the nearest hazard (Jean)
    std::uint32_t buttons=0;     // OR into both output words
    float wait=0;                // native brain wait, seconds (0 = none)
};

// Inputs of one update of one dopple.
struct view {
    float now=0;
    bool partner_valid=false;    // the partner resolves, is a character and has health > 0
    float partner_distance=0;    // between the two origins (3D)
    bool hero=false;             // an active hero resolves and is a character
    bool slot_available=false;   // the slot this state tests (Colossus: alternate 3; Jean/Cyclops: normal 5)
};

// The shared tick (0x110470): runs once per distinct game time, before the handler of whichever dopple updates.
// Returns true when a fastball starts; the adapter then clears Wolverine's movement target and navigation.
inline bool tick(shared& s,float now,bool both_alive,float pair_distance){
    if(s.ticked&&now==s.last_tick)return false;
    s.ticked=true;s.last_tick=now;
    if(!(s.rearm_at<now)||s.colossus!=0||s.wolverine!=0||!both_alive||!(pair_distance<pair_range))return false;
    s.colossus=s.wolverine=1;s.rearm_at=now+rearm;s.fastball_started=now;
    return true;
}

// One handler. The taunt roll (random < 0.05) happens before this in the adapter, as in the original.
inline actions update(shared& s,who w,const view& v){
    actions a;
    if(w==who::wolverine||w==who::colossus){
        int& flag=w==who::wolverine?s.wolverine:s.colossus;
        if(!v.partner_valid){flag=0;return a;}
        if((s.wolverine!=0||s.colossus!=0)&&v.now-s.fastball_started>fastball_watchdog){s.wolverine=s.colossus=0;return a;}
        if(flag==1){
            a.handled=true;a.move_to_partner=true;
            if(v.partner_distance<handoff){a.stop=true;flag=2;a.face_partner=true;a.face_immediate=w==who::wolverine;}
            return a;
        }
        if(flag!=2)return a;
        a.handled=true;
        if(w==who::wolverine){a.wait=wait_long;return a;}
        a.face_partner=true;
        if(v.slot_available){
            a.target_hero=v.hero;a.buttons=throw_buttons;s.colossus=0;s.wolverine=0;a.wait=wait_long;
        }
        return a;
    }
    if(w==who::jean){
        if(!v.partner_valid){s.jean=0;return a;}
        switch(s.jean){
        case 0:
            if(!(v.now>s.hazard_at))return a;
            s.hazard_at=v.now+hazard_interval;s.jean=3;a.handled=true;return a;
        case 1:
            s.cyclops=1;s.jean=0;a.handled=true;return a;
        case 2:
            a.handled=true;
            if(v.slot_available){s.jean=1;a.buttons=power_attack_buttons;a.wait=wait_long;}
            return a;
        case 3:
            a.handled=true;
            if(v.hero){a.brain_target_hero=true;a.activate_hazard=true;s.jean=0;a.wait=wait_long;}
            return a;
        default:return a;
        }
    }
    if(!v.partner_valid){s.cyclops=0;return a;}
    if(s.cyclops==1){
        a.handled=true;
        if(v.slot_available){s.cyclops=0;a.buttons=power_attack_buttons;a.wait=wait_short;}
        return a;
    }
    if(s.cyclops==2)a.handled=true;
    return a;
}

// Which slot a dopple tests in its current state (-1 none, else {slot, alternate}).
struct slot_test {int slot=-1;bool alternate=false;};
inline slot_test slot_for(const shared& s,who w){
    if(w==who::colossus&&s.colossus==2)return {guard_slot,true};
    if(w==who::jean&&s.jean==2)return {power_attack_slot,false};
    if(w==who::cyclops&&s.cyclops==1)return {power_attack_slot,false};
    return {};
}
}
