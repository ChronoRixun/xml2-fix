#pragma once
#include <cstdint>
// XML1 0x113e80. Deadlines are integer truncations, waits and taunts are separate services.
namespace shadowking_rules {
struct state {int mode=0,spear_at=0,smash_at=0;bool boost=false,xtreme=false,chasing=false;};
inline state initial(float now,int mode){state s;s.mode=mode;s.spear_at=s.smash_at=int(now+6);return s;}
struct action {std::uint32_t buttons=0;float wait=0;bool taunt=false,preferred90=false,follow=false,close=false;};
template<class Available> action update(state& s,float now,float d,Available available){
    action a;const bool requested_xtreme=s.xtreme;
    if(s.xtreme&&available(8)){s.xtreme=false;a.buttons=0xc0;a.wait=.3f;}
    // First form gives a pending xtreme priority over spear, then still checks smash and shield.
    if(s.mode==0){
        if(!requested_xtreme&&100<d&&d<300&&now>s.spear_at&&available(5)){s.spear_at=int(now+6);a.buttons|=0x90;a.wait=.6f;a.taunt=true;return a;}
        if(d<120&&now>s.smash_at&&available(6)){s.smash_at=int(now+7);a.buttons|=0xa0;a.wait=.6f;a.taunt=true;return a;}
        if(s.boost&&d<300&&available(7)){s.boost=false;a.buttons|=0x180;a.wait=.6f;a.taunt=true;a.preferred90=true;return a;}
        if(d>300||(s.chasing&&d>180)){s.chasing=true;a.follow=true;return a;}
        s.chasing=false;a.close=true;return a;
    }
    if(s.boost&&d<300&&available(7)){s.boost=false;a.buttons|=0x180;a.wait=.6f;a.taunt=true;return a;}
    if(100<d&&d<170&&now>s.spear_at&&available(5)){s.spear_at=int(now+5);a.buttons|=0x90;a.wait=.6f;a.taunt=true;return a;}
    if(d<100)a.close=true;else a.follow=true;
    return a;
}
inline action update(state& s,float now,float d,bool slot5,bool slot6,bool slot7,bool slot8){
    return update(s,now,d,[&](int slot){return slot==5?slot5:slot==6?slot6:slot==7?slot7:slot8;});
}
}
