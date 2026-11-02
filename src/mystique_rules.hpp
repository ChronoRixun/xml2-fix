#pragma once
#include <cstdint>
#include <string_view>
// XML1 0x111520 and request setters: mode, copied state and grenade deadline are independent.
namespace mystique_rules {
struct state {int mode=0;bool active=true,copied=false;float grenade_at=0,restore_at=0;std::uint32_t target=0,copy_target=0;};
inline bool request(state& s,std::string_view name,float now){
    if(name=="myst1"){s.mode=0;return true;}
    if(name=="myst2"){s.mode=1;s.copied=false;s.grenade_at=now+2;s.active=true;return true;}
    if(name=="myst3"){s.mode=2;s.active=false;return true;}
    if(name=="mystique"||name=="gopattern"){
        // XML1's timed restoration effect survives a brain initializer. Keep its substitute independent too.
        const auto expiry=s.restore_at;const auto copy_target=s.copy_target;s={};s.restore_at=expiry;s.copy_target=copy_target;return true;
    }
    return false;
}
inline bool grenade_due(const state& s,float now){return s.mode==1&&now>s.grenade_at;}
inline void copy(state& s,float now,float duration,std::uint32_t target){s.copied=true;s.restore_at=now+duration;s.target=target;s.copy_target=target;}
inline bool restore_due(const state& s,float now,bool target_living){return s.restore_at>0&&(!target_living||now>=s.restore_at);}
inline std::uint32_t close_buttons(bool copied,float chance,float attack_roll){
    if(!copied)return 0x180;
    if(chance<=.98f)return 0;
    return attack_roll<.5f?0x10u:0x20u;
}
}
