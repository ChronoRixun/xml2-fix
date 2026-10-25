#pragma once
#include <array>
#include <cstdint>
#include <string_view>
// XML1 0x114d30. Phase requests deliberately preserve pending flags and deadlines.
namespace magneto_rules {
struct tile {std::uint32_t handle=0;float distance=0;};
struct state {
    bool shield=false,off=true,radial=false,environment=true,floor=false,smash=true,attack=false;
    float smash_interval=5,attack_interval=6,smash_at=0,attack_at=0,environment_at=0,wait_until=0;
    float target_until=0,hurt_at=-1,shield_at=0;bool shield_fx=false;
    std::uint32_t target=0;unsigned hero_index=0,cache_index=0;
    std::array<tile,5> tiles{};
};
inline void request(state& s,std::string_view phase){
    if(phase=="magshield"){s.shield=true;s.smash_interval*=2;s.attack_interval*=2;}
    else if(phase=="mag2"){s.off=true;s.radial=true;s.environment=true;s.floor=false;s.smash=true;s.attack=true;s.smash_interval=5;s.attack_interval=6;}
    else if(phase=="mag3"){s.off=true;s.radial=false;s.environment=true;s.floor=false;s.smash=true;s.attack=false;s.smash_interval=4;s.attack_interval=6;}
    else if(phase=="mag4"){s.shield=true;s.radial=true;s.environment=false;s.floor=false;s.smash=true;s.attack=false;s.smash_interval=3;s.attack_interval=6;}
    else if(phase=="mag5"){s.off=true;s.radial=true;s.environment=true;s.floor=true;s.smash=true;s.attack=true;s.smash_interval=2;s.attack_interval=3;}
}
inline int tile_slot(const state& s,std::uint32_t h){for(unsigned i=0;i<s.tiles.size();++i)if(s.tiles[i].handle==h)return int(i);return -1;}
inline bool release_tile(bool running,float distance,float recorded){return !running||distance>recorded;}
enum class environmental {none,general,floor,tracked,full};
struct environment_plan {environmental kind=environmental::none;int slot=-1;float deadline=0,wait=0;};
inline environment_plan plan_environment(const state& s,float now,std::uint32_t candidate,bool general,bool visible,
                                         const std::array<bool,5>& valid){
    if(!s.environment||!(now>s.environment_at)||!candidate)return {};
    if(general)return visible?environment_plan{environmental::general,-1,now+5,1}:environment_plan{};
    if(!s.floor)return {};
    if(tile_slot(s,candidate)>=0)return {environmental::tracked,-1,now+.2f,0};
    for(unsigned i=0;i<valid.size();++i)if(!valid[i])return {environmental::floor,int(i),now+.3f,.1f};
    return {environmental::full,-1,now+.1f,0};
}
inline bool hurt(state& s,float now){if(!s.shield_fx||now<s.shield_at||now-s.hurt_at<.25f)return false;s.hurt_at=now;return true;}
template<class Game> bool pending(state& s,float now,Game& g){
    if(s.shield&&g.play("balls_return")){s.shield=false;s.shield_fx=true;s.shield_at=now+.5f;s.wait_until=now+.5f;g.wait(.5f);return true;}
    if(s.off&&g.play("balls_off")){s.off=false;s.shield_fx=false;s.wait_until=now+1;g.wait(1);return true;}
    if(s.radial&&g.available(8)){s.radial=false;g.buttons(0xc0);g.taunt(1);s.wait_until=now+.5f;g.wait(.5f);return true;}
    return false;
}
template<class Game> bool powers(state& s,float now,Game& g){
    if(!g.visible()||!g.face(.9f))return false;
    if(s.smash&&now>s.smash_at&&g.available(6)){g.buttons(0xa0);g.taunt(.5f);g.wait(.5f);s.wait_until=now+.5f;s.smash_at=now+s.smash_interval;return true;}
    if(s.attack&&now>s.attack_at&&g.available(5)){g.buttons(0x90);g.taunt(.5f);g.wait(.5f);s.wait_until=now+.5f;s.attack_at=now+s.attack_interval;return true;}
    return false;
}
}
