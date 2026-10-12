// multipart_rules.hpp without the game: XML1's breakable-part decisions (XBE CMultipartManager).
#include "multipart_rules.hpp"
#include <iostream>
#include <vector>

namespace {
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
using namespace multipart_rules;

parts two_arms(){
    parts p;p.count=2;
    p.value[0]={true,275,link_bits("powerSmash powerAction")};
    p.value[1]={true,275,link_bits("meleeAttack powerAction")};
    return p;
}
struct rolls { std::vector<int> v; std::size_t i=0; int operator()(){return i<v.size()?v[i++]:0;} };

void links(){
    CHECK(link_bits("")==0);
    CHECK(link_bits("powerSmash powerAction")==(power_smash|power_action));
    CHECK(link_bits("meleeAttack powerAction")==(melee_attack|power_action));
    CHECK(link_bits("powerSmash meleeAttack")==(power_smash|melee_attack));
    // strstr semantics: a name anywhere in the text, case-sensitive
    CHECK(link_bits("xpowerMovex")==power_move);
    CHECK(link_bits("powersmash")==0);
}
void spawn_state(){
    const auto s=spawn(two_arms());
    CHECK(s.current==1&&s.count==2&&s.health[0]==275&&s.health[1]==275&&s.disabled==0);
    parts none;CHECK(spawn(none).current==0);
}
void damage_goes_to_the_current_part(){
    auto p=two_arms();auto s=spawn(p);rolls r{{0}};
    CHECK(hit(s,p,100,r)==-1);CHECK(s.health[0]==175&&s.health[1]==275&&s.current==1);
    CHECK(hit(s,p,175,r)==0);              // exactly 0 health breaks on a 0 roll
    CHECK(s.current==2&&s.disabled==(power_smash|power_action));
    CHECK(hit(s,p,300,r)==1);CHECK(s.current==0);
    CHECK(s.disabled==(power_smash|power_action|melee_attack));
    CHECK(hit(s,p,999,r)==-1);             // no part left: body damage only
}
void a_failed_roll_leaves_the_part_dead_but_whole(){
    auto p=two_arms();auto s=spawn(p);rolls r{{1,2,0}};
    CHECK(hit(s,p,400,r)==-1);             // below 1, roll 1: not broken
    CHECK(s.health[0]<=0&&s.current==1&&s.disabled==0);
    CHECK(hit(s,p,5,r)==-1);               // next hit only moves on (0x890f6), no damage to the next part
    CHECK(s.current==2&&s.health[1]==275);
    CHECK(hit(s,p,400,r)==-1);             // roll 2: the left arm is spent unbroken too
    CHECK(hit(s,p,1,r)==-1&&s.current==0&&s.disabled==0);
}
void advance_wraps_and_skips(){
    parts p;p.count=3;for(int i=0;i<3;++i)p.value[i]={true,10,0};
    auto s=spawn(p);s.health[1]=0;
    advance(s,1);CHECK(s.current==3);      // part 2 is spent: skipped
    advance(s,1);CHECK(s.current==0);
    advance(s,-1);CHECK(s.current==3);
}
void scripted_destroy(){
    auto p=two_arms();auto s=spawn(p);
    CHECK(destroy(s,p,2)==1);CHECK(s.health[1]==0&&s.disabled==(melee_attack|power_action));
    CHECK(s.current==0);                   // 0x88fa0 advances from the current part whichever part it broke
    CHECK(destroy(s,p,2)==-1);             // already broken
    CHECK(destroy(s,p,3)==-1&&destroy(s,p,0)==-1);
    parts helmet;helmet.count=1;helmet.value[0]={true,9999,0};
    auto j=spawn(helmet);CHECK(destroy(j,helmet,1)==0&&j.current==0);
}
void buttons(){
    u32 out[2]{0x10,0x10};filter_buttons(melee_attack,out);CHECK(out[0]==0&&out[1]==0);
    u32 smash[2]{0,0x20};filter_buttons(melee_attack,smash);CHECK(smash[1]==0x20);
    u32 power[2]{0x80,0x20};filter_buttons(power_smash,power);CHECK(power[0]==0&&power[1]==0);
    u32 keep[2]{0x80,0x10};filter_buttons(power_smash|melee_attack,keep);CHECK(keep[0]==0x80&&keep[1]==0x10);
    u32 action[2]{0x180,0x180};filter_buttons(power_action,action);CHECK(action[0]==0&&action[1]==0);
    u32 move[2]{0x80,0x40|0x4};filter_buttons(power_move,move);CHECK(move[0]==0&&move[1]==0x4);
    u32 none[2]{0x10,0x10};filter_buttons(0,none);CHECK(none[0]==0x10);
}
}
int main(){
    links();spawn_state();damage_goes_to_the_current_part();a_failed_roll_leaves_the_part_dead_but_whole();
    advance_wraps_and_skips();scripted_destroy();buttons();
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
