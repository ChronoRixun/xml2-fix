#pragma once
#include <cmath>
#include <cstdint>

namespace online_menu_navigation_rules {
enum direction { none, left, right, up, down };
inline constexpr unsigned dpad_mask=(1u<<11)|(1u<<12)|(1u<<16)|(1u<<17);
struct sample { float x=0,y=0;unsigned held=0; };

// Retail 0x61a810 writes BOTH halves of each movement axis to code 0/1.
// Its bit means nonzero axis, not left/up. 0x5da1a0 reads the signed values
// through manager +0x128 instead. Match its >.5 engagement and 300/100 ms
// repeat, with a .35 release threshold so resting near .5 cannot skip cells.
struct navigation {
    int horizontal=0,vertical=0;
    direction active=none;
    std::uint32_t next=0;
    static int axis(float value,int before) {
        if(!std::isfinite(value)) return 0;
        if(value>.5f) return 1;
        if(value<-.5f) return -1;
        if(before>0 && value>.35f) return 1;
        if(before<0 && value<-.35f) return -1;
        return 0;
    }
    direction read(sample s) {
        horizontal=axis(s.x,horizontal);vertical=axis(s.y,vertical);
        if(s.held&dpad_mask) {
            const int x=int(bool(s.held&(1u<<16)))-int(bool(s.held&(1u<<17)));
            const int y=int(bool(s.held&(1u<<11)))-int(bool(s.held&(1u<<12)));
            // One cardinal cell per tick. Opposing contacts cancel; a D-pad
            // takes precedence over a resting stick. Vertical wins ties.
            return y ? (y>0 ? up : down) : x ? (x>0 ? right : left) : none;
        }
        if(horizontal && vertical) {
            // Keep the chosen diagonal axis until released, or until the
            // other axis clearly dominates; small diagonal noise is inert.
            const float ax=std::abs(s.x),ay=std::abs(s.y);
            if((active==left || active==right) && ay<=ax+.15f) return horizontal>0 ? right : left;
            if((active==up || active==down) && ax<=ay+.15f) return vertical>0 ? up : down;
            return ay>=ax ? (vertical>0 ? up : down) : (horizontal>0 ? right : left);
        }
        return vertical ? (vertical>0 ? up : down) : horizontal ? (horizontal>0 ? right : left) : none;
    }
    void reset(sample s={},std::uint32_t now=0) {
        horizontal=vertical=0;active=none;active=read(s);next=now+300;
    }
    direction update(sample s,std::uint32_t now) {
        const auto value=read(s);
        if(value!=active) { active=value;next=now+300;return value; }
        if(value==none || std::int32_t(now-next)<0) return none;
        // At most one cell per frame, even following a delayed frame. Preserve
        // the 100 ms phase without issuing a backlog of focus moves.
        next+=((now-next)/100+1)*100;
        return value;
    }
};
}
