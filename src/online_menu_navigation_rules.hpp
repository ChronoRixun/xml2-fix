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
    // One signed axis. `sign` is the last engaged direction, cleared only by a
    // true neutral (|v|<.35). A sprung stick that overshoots past -.5 on
    // release ("it moved and came back") is not a reversal: the opposite side
    // engages only after neutral, or after it has been held for 80 ms.
    struct axis_state {
        int engaged=0,sign=0;bool blocked=false;std::uint32_t since=0;
        int read(float value,std::uint32_t now) {
            if(!std::isfinite(value)) return engaged=0;
            if(std::abs(value)<.35f) sign=0;
            int out=0;
            if(value>.5f) out=1;
            else if(value<-.5f) out=-1;
            else if(engaged>0 && value>.35f) out=1;
            else if(engaged<0 && value<-.35f) out=-1;
            if(out && out==-sign) {
                if(!blocked) {blocked=true;since=now;}
                if(std::int32_t(now-since)<80) return engaged=0;
            }
            blocked=false;
            if(out) sign=out;
            return engaged=out;
        }
    };
    axis_state horizontal,vertical;
    direction active=none;
    std::uint32_t next=0;
    bool repeat=false; // the last update() result came from held repeat, not a fresh push
    direction read(sample s,std::uint32_t now) {
        const int horizontal=this->horizontal.read(s.x,now),vertical=this->vertical.read(s.y,now);
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
        horizontal={};vertical={};repeat=false;active=none;active=read(s,now);next=now+300;
    }
    direction update(sample s,std::uint32_t now) {
        const auto value=read(s,now);repeat=false;
        if(value!=active) { active=value;next=now+300;return value; }
        if(value==none || std::int32_t(now-next)<0) return none;
        repeat=true;
        // At most one cell per frame, even following a delayed frame. Preserve
        // the 100 ms phase without issuing a backlog of focus moves.
        next+=((now-next)/100+1)*100;
        return value;
    }
};
}
