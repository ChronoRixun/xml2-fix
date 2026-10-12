#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

// X-Men Legends 1 breakable parts (stats <Multipart>), the decisions only: no game memory here.
// Original: the XBE's CMultipartManager (getter 0x89fc0, vtable 0x3cf6e4). XMen2.exe kept the manager's
// skin bookkeeping (getter 0x48d1f0, vtable 0x689864) but not part health, damage, effects or power links.
namespace multipart_rules {
using u32=std::uint32_t;

// XML1's parser 0x89500 refuses a fifth part (cmp [rec+0x178],4); XMen2.exe's record holds five.
constexpr int max_parts=4;

// linkToPower names, in the bit order of the XBE's table 0x3cf640.. / 0x44e4d0 (1,2,4,..,0x20).
// 0x88bd0 sets a bit when the attribute CONTAINS the name (strstr 0x3416b0), case-sensitive.
enum power_bit:u32 { melee_attack=1, melee_smash=2, power_attack=4, power_smash=8, power_move=0x10, power_action=0x20 };
constexpr std::array<std::string_view,6> power_names{"meleeAttack","meleeSmash","powerAttack","powerSmash","powerMove","powerAction"};
constexpr u32 link_bits(std::string_view text){
    u32 bits=0;
    for(std::size_t i=0;i<power_names.size();++i)
        if(text.find(power_names[i])!=std::string_view::npos)bits|=1u<<i;
    return bits;
}

// AI controller buttons (XBE table 0x452c78: per attack request {held bit, pressed bit}; XMen2.exe's brains
// use the same encoding, e.g. 0x10 attack, 0x20 smash, 0x80 with 0x10/0x20/0x40/0x100 a power).
constexpr u32 button_attack=0x10, button_smash=0x20, button_move=0x40, button_power=0x80, button_action=0x100;

// XML1's attack handler (0xeea60) asks the multipart manager (slot 7, 0x89440) before pressing the buttons of
// a requested attack; a disabled attack presses nothing. Here: clear the buttons of a disabled attack from the
// brain's output words (both held and pressed).
inline void filter_buttons(u32 disabled,u32* output){
    if(!disabled||!output)return;
    const u32 all=output[0]|output[1];
    u32 clear=0;
    if(all&button_power){
        if((all&button_attack)&&(disabled&power_attack))clear|=button_power|button_attack;
        if((all&button_smash)&&(disabled&power_smash))clear|=button_power|button_smash;
        if((all&button_move)&&(disabled&power_move))clear|=button_power|button_move;
        if((all&button_action)&&(disabled&power_action))clear|=button_power|button_action;
    }else{
        if((all&button_attack)&&(disabled&melee_attack))clear|=button_attack;
        if((all&button_smash)&&(disabled&melee_smash))clear|=button_smash;
    }
    output[0]&=~clear;output[1]&=~clear;
}

// One part as the XBE stores it (0x48-byte entries from record+8): health int, power bits; the names live in
// the module that owns the strings.
struct part { bool breakable=false; int health=0; u32 power=0; };
struct parts { std::array<part,max_parts> value{}; int count=0; };

// Per actor (XBE clone 0x89bf0): the current part starts at 1 (constructor 0x89b80), health is the authored
// value, nothing disabled.
struct state {
    int current=1;
    std::array<int,max_parts> health{};
    u32 disabled=0;
    int count=0;
};
inline state spawn(const parts& p){
    state s;s.count=p.count;
    for(int i=0;i<p.count;++i)s.health[i]=p.value[i].health;
    if(!s.count)s.current=0;
    return s;
}
// 0x88760: step the current part, wrapping past the last part to 0 (none) and skipping parts at <= 0 health.
inline void advance(state& s,int step){
    auto move=[&]{
        s.current+=step;
        if(s.current>s.count)s.current=0;
        else if(s.current<0)s.current=s.count;
    };
    move();
    while(s.current!=0&&s.health[s.current-1]<1)move();
}
// Result of one hit or a scripted destroy: the part that broke (0-based) or -1.
// 0x899c0 -> 0x890e0: the hit's whole damage goes to the CURRENT part (no hit location). A part already at <= 0
// moves the current part on instead. Taken below 1, the part breaks only when the native roll in [0,2] is 0
// (0x1233c0(0,2)); otherwise it stays unbroken at <= 0 and the next hit moves past it.
template<class Roll> int hit(state& s,const parts& p,int damage,Roll roll){
    if(s.count<=0||s.current==0)return -1;
    const int k=s.current-1;
    if(s.health[k]<1){advance(s,1);return -1;}
    s.health[k]-=damage;
    if(s.health[k]>0)return -1;
    if(roll()!=0)return -1;
    s.disabled|=p.value[k].power;
    advance(s,1);
    return k;
}
// 0x88fa0 (script destroyMultipartPiece(actor, n), n 1-based): breaks part n if it still has health.
inline int destroy(state& s,const parts& p,int one_based){
    const int k=one_based-1;
    if(k<0||k>=s.count||s.health[k]<=0)return -1;
    s.health[k]=0;
    s.disabled|=p.value[k].power;
    advance(s,1);
    return k;
}
}
