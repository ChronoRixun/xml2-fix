#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>

namespace classic_hud_rules
{
    using uv = std::array<float, 8>;
    constexpr std::uint32_t primitive_slot=0x69dcf0, uv_slot=0x69dc60;
    constexpr std::uint32_t primitive=0x59a140, coordinates=0x5ec560;
    constexpr std::uint32_t wheel_slot=0x69dc78, wheel_draw=0x5a70a0;
    constexpr std::uint32_t hud_batch=0x81d7e0+0x85918;
    constexpr int health=32, energy=33, frame=34, small_frame=35, inner=36, small_inner=37;
    // Atlas sprite 38 was the small-map frame of beta 5 and is unused; the ids
    // stay where they are so the builder's atlas contract does not renumber.
    constexpr int experience=39;
    inline bool hud_batch_address(std::uintptr_t address)
    { return address>=hud_batch && address<=hud_batch+2*0x104 && (address-hud_batch)%0x104==0; }
    inline float energy_offset(int skew) { return skew<0 ? -3.75f : 3.75f; }

    inline uv rect(float x0,float y0,float x1,float y1)
    { return {x0/256,y1/512,x0/256,y0/512,x1/256,y1/512,x1/256,y0/512}; }

    inline bool native_cell(const float* stock, unsigned x, unsigned y)
    {
        const uv expected{x/128.f,(y+32)/256.f,x/128.f,y/256.f,
                          (x+32)/128.f,(y+32)/256.f,(x+32)/128.f,y/256.f};
        return std::equal(expected.begin(),expected.end(),stock);
    }

    inline uv map_uv(unsigned id, const float* stock)
    {
        if(id==0) return rect(129,128,191,192); // original portrait ring
        // Owned Xbox counter cells; the UV contract prevents remapping an
        // unrelated/custom sprite with the same numeric ID. Values stay native.
        if(id==10 && native_cell(stock,0,128)) return rect(128.5f,64.5f,159.5f,95.5f);
        if(id==11 && native_cell(stock,32,128)) return rect(160.5f,64.5f,191.5f,95.5f);
        if(id==12 && native_cell(stock,64,128)) return rect(160.5f,32.5f,191.5f,63.5f);
        if(id==health) return rect(.5f,256.5f,63.5f,259.5f);
        if(id==energy) return rect(.5f,264.5f,63.5f,267.5f);
        if(id==frame) return rect(.5f,272.5f,223.5f,295.5f);
        if(id==small_frame) return rect(.5f,300.5f,47.5f,311.5f);
        if(id==inner) return rect(.5f,320.5f,221.5f,341.5f);
        if(id==small_inner) return rect(.5f,348.5f,45.5f,357.5f);
        if(id==experience) return rect(.5f,360.5f,63.5f,362.5f);
        uv out{};
        for(unsigned i=0;i<8;++i) out[i]=stock[i]*.5f;
        return out;
    }

    inline int hero_fill(std::uintptr_t caller)
    {
        switch(caller) {
        case 0x5a3939: case 0x5a3a45: case 0x5a3ad2: return health;
        case 0x5a3c42: case 0x5a3d50: case 0x5a3ddd: return energy;
        default: return -1;
        }
    }
    inline bool target_draw(std::uintptr_t caller)
    { return caller==0x599361 || caller==0x599435 || caller==0x599534 || caller==0x5996a7; }

    template<class Measure>
    std::string fit_target_name(std::string_view source,float available,Measure measure)
    {
        if(!std::isfinite(available) || available<=0) return {};
        std::string text(source);
        auto fits=[&](const std::string& value) {
            const float width=measure(value);
            return std::isfinite(width) && width>=0 && width<=available;
        };
        if(fits(text)) return text;
        // The native title includes the level. Retain that numeric suffix when
        // shortening the display name; ordinary parenthetical words are names.
        std::string suffix;
        const auto split=text.rfind(" (");
        if(split!=std::string::npos && text.back()==')' && split+3<text.size() &&
           std::all_of(text.begin()+split+2,text.end()-1,[](char c){return c>='0' && c<='9';})) {
            suffix=text.substr(split);text.resize(split);
        }
        if(!fits("..."+suffix)) return {};
        while(!text.empty()) {
            text.pop_back();
            const auto candidate=text+"..."+suffix;
            if(fits(candidate)) return candidate;
        }
        return "..."+suffix;
    }

    inline bool experience_draw(std::uintptr_t caller,int channel,int icon,
        const float* size,float fraction)
    {
        return caller==0x5a3f5e && channel==0 && icon==27 && size[1]==2 &&
            std::isfinite(size[0]) && size[0]>=0 && size[0]<=73 &&
            std::isfinite(fraction) && fraction>=0 && fraction<=1;
    }

    inline bool wheel_entry_visible(int type,std::uint32_t value)
    {
        return (type==0 && value!=0) || (type==1 && static_cast<std::int32_t>(value)>-1) ||
            (type==2 && static_cast<std::int32_t>(value)>0);
    }

    inline bool wheel_ring(std::uintptr_t caller,int channel,int icon,const float* size)
    {
        return caller==0x5a7599 && channel==1 && icon==0 &&
            std::isfinite(size[0]) && size[0]>=8 && size[0]<=80 && size[1]==size[0];
    }

    inline bool prompt_background(std::uintptr_t caller,int channel,int icon,const float* size)
    {
        return caller==0x59e147 && channel==1 && icon==27 &&
            std::isfinite(size[0]) && std::isfinite(size[1]) &&
            size[0]>=8 && size[0]<=1024 && size[1]>=8 && size[1]<=128;
    }

    inline int target_art(float width, std::uintptr_t caller)
    {
        if(caller==0x599361) return width==48 ? small_frame : frame;
        // The native point-UV solid sample can disappear after a menu/load
        // round-trip on the composite atlas. Give interior, trail and health
        // explicit coverage; keep the native width, pulse and colour.
        return width==48 ? small_inner : inner;
    }

    struct target_box {float x=0,z=0,width=0,height=0;bool valid=false;};
    inline target_box target_origin(const float* position,const float* size,int skew)
    {
        const bool supported=(size[0]==224 && size[1]==24)||(size[0]==48 && size[1]==12);
        return {position[0]+skew*.5f,position[2],size[0],size[1],
            supported && std::isfinite(position[0]) && std::isfinite(position[2])};
    }
    inline std::array<float,3> target_position(const target_box& box,std::uintptr_t caller,float depth)
    {
        const float inset=caller==0x599361 ? 0.f : caller==0x599435 ? 1.f : 4.f;
        return {box.x+inset,depth,box.z-inset};
    }
}
