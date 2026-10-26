#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
// Measured programs at XML1 0x4532f8/0x453370; zero padding is never executed.
namespace blob_rules {
constexpr std::array<unsigned,19> nyc{11,12,13,3,8,1,10,11,12,13,7,8,1,10,5,10,9,14,15};
constexpr std::array<unsigned,13> astral{3,8,1,10,7,8,1,10,5,10,9,14,15};
// wait_until is an absolute simulation-time deadline, including at pickup opcode 12.
struct state {unsigned pc=0,hero_index=0;float advanced_at=0,until=0,melee_at=0,wait_until=0;std::uint32_t prop=0;bool close=false;unsigned scratch=0;};
inline unsigned opcode(const state& s,bool astral_mode){return astral_mode?astral[s.pc%astral.size()]:nyc[s.pc%nyc.size()];}
inline void advance(state& s,float now){++s.pc;s.advanced_at=now;s.scratch=0;}
constexpr int engine_heaviness(int h){return h<=1?0:h>=4?3:h-1;}
inline int heaviness(std::optional<int> xml1,int byte){
    if(xml1)return *xml1>=0&&*xml1<=5&&engine_heaviness(*xml1)==byte?*xml1:byte;
    return byte>=1&&byte<=3?byte+1:byte;
}
template<unsigned Size> class weight_store {
    struct entry {std::uint32_t handle=0;int value=-1;};std::array<entry,Size> entries{};
public:
    void set(unsigned slot,std::uint32_t handle,int value){if(slot<Size)entries[slot]={handle,value>=0&&value<=5?value:-1};}
    std::optional<int> get(unsigned slot,std::uint32_t handle) const {
        if(slot>=Size||!handle||entries[slot].handle!=handle||entries[slot].value<0)return {};
        return entries[slot].value;
    }
};
constexpr float prop_radius=180;
struct prop {std::uint32_t handle=0,flags=0;float dz=0;int weight=0;bool liftable=false;};
// Native sphere membership already accounts for spatial bounds. XML1 adds only
// the height and flag tests, not another origin-distance filter after that query.
inline bool prop_geometry(const prop& p){return p.handle&&!(p.flags&(1u<<17))&&std::fabs(p.dz)<=6;}
inline bool eligible(const prop& p){return prop_geometry(p)&&p.liftable;}
template<class Range> std::uint32_t select(const Range& props){int best=-1;std::uint32_t result=0;for(const auto& p:props)if(eligible(p)&&p.weight>best){best=p.weight;result=p.handle;}return result;}
struct jump {std::uint32_t owner=0,hero=0;float at=0;};
inline bool jump_due(const jump& j,float now){return j.hero&&now>j.at;}
inline bool jump_stale(const jump& j,float now){return now>j.at+1;}
inline bool indexed_target(bool living,float distance){return living&&distance<400;}
inline bool jump_candidate(bool living,bool ai,float distance){return living&&ai&&distance<500;}
template<class Game> void update(state& s,unsigned op,float now,Game& g){
    auto next=[&]{advance(s,now);};
    auto attack=[&](int slot,std::uint32_t bits,float jump_delay,bool taunt){
        if(!g.available(slot,false))return;
        g.buttons(bits);if(taunt)g.taunt(.4f);if(jump_delay)g.jump(now+jump_delay);
        s.wait_until=now+.5f;g.wait(.5f);next();
    };
    switch(op){
    case 1:{
        const float distance=g.distance();
        if(distance<(s.close?140.f:100.f)){
            s.close=true;g.stop();
            if(g.face(.97f)&&distance<=140){
                if(g.target_blocked())s.melee_at=now+2.1f;
                else if(now>s.melee_at)g.buttons(g.random()>.25f?0x10:0x20);
            }
        }else if(distance>140)s.close=false;
        if(now>s.until)next();break;
    }
    case 2:next();break;
    case 3:attack(5,0x90,.3f,true);break;
    case 4:attack(7,0x180,0,true);break;
    case 5:attack(6,0xa0,.6f,true);break;
    case 6:attack(8,0xc0,0,true);break;
    case 7:if(g.distance()<170)attack(6,0xa0,.6f,false);else attack(5,0x90,.3f,false);break;
    case 8:s.until=now+10;next();break;
    case 9:s.until=now+5;next();break;
    case 10:++s.hero_index;next();break;
    case 11:s.prop=g.query();if(s.prop)g.approach(s.prop);s.wait_until=now+.2f;g.wait(.2f);next();break;
    case 12:
        if(!g.valid(s.prop)){next();break;}
        g.approach(s.prop);
        if(g.arrived(s.prop)){g.stop();if(g.face_prop(s.prop,.7f)&&g.available(3,true)){
            g.buttons(0x100);g.taunt(1);g.stop();g.clear_navigation_counter();s.wait_until=now+.5f;g.wait(.5f);next();}}
        break;
    case 13:
        if(g.carried()!=s.prop){if(g.carried()){g.drop_other();}else next();break;}
        g.stop();if(!g.valid(s.prop)){next();break;}
        if(!g.aim())break;
        if(g.available(1,true)){g.throw_object();s.wait_until=now+.2f;g.wait(.2f);next();}break;
    case 14:if(now>s.until)next();break;
    case 15:s.pc=0;break;
    default:next();break;
    }
}
}
