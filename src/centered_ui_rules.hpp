#pragma once

// Section C's coordinate contract only. No engine pointers, patches, or drawing.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>

namespace centered_ui {
struct point { double x=0, y=0; };
struct clip { int left=0, top=0, right=0, bottom=0; }; // exclusive right/bottom
struct transform {
    std::uint32_t width=0, height=0;
    std::uint64_t generation=0;
    double s=0, ox=0, oy=0;
    bool legacy_4_3=false, wide_pilot=false;
    double q() const { return 1.25*s; }
    point pixel(point logical) const { return {ox+s*logical.x,oy+s*logical.y}; }
    point logical(point pixel) const { return {(pixel.x-ox)/s,(pixel.y-oy)/s}; }
    point view(point pixel) const { return {(pixel.x-ox)/q(),384-(pixel.y-oy)/q()}; }
    point view_pixel(point view) const { return {ox+q()*view.x,oy+q()*(384-view.y)}; }
    bool inside(point pixel) const {
        return pixel.x>=ox && pixel.y>=oy && pixel.x<ox+640*s && pixel.y<oy+480*s;
    }
    // Candidate wide UI-receiver quantization. Never replaces the retail 4:3 branch.
    std::optional<point> receiver(point pixel) const {
        if (legacy_4_3 || !wide_pilot || !inside(pixel)) return {};
        const auto v=view(pixel);
        return point{std::trunc(v.x),std::ceil(v.y)};
    }
    point pixel_center_view(int x,int y) const { return view({x+.5,y+.5}); }
    clip outward_clip(point a,point b) const {
        a=pixel(a); b=pixel(b);
        const auto bound=[](double value,std::uint32_t limit) { return std::clamp(value,0.,double(limit)); };
        return {int(bound(std::floor(a.x),width)),int(bound(std::floor(a.y),height)),
                int(bound(std::ceil(b.x),width)),int(bound(std::ceil(b.y),height))};
    }
};
inline std::optional<transform> make(std::uint32_t w,std::uint32_t h,std::uint64_t generation) {
    // Bounds keep integer clip endpoints representable; actual D3D8 RTs are much smaller.
    if (!w || !h || w>0x7fffffffu || h>0x7fffffffu || !generation) return {};
    transform t; t.width=w;t.height=h;t.generation=generation;
    t.s=std::min(double(w)/640.,double(h)/480.);
    t.ox=(w-640*t.s)/2;t.oy=(h-480*t.s)/2;
    const auto wx=std::uint64_t(w)*480,hy=std::uint64_t(h)*640;
    t.legacy_4_3=wx==hy;t.wide_pilot=wx>=hy;
    return t;
}
// BXIG's native X formula is logicalX*sy-left*sx (not logicalX*sx).
struct panel_scale { float sx=1,sy=1,left=0; };
inline panel_scale panel_fields(const transform& t) {
    // Retain BXIG's native height reciprocal/multiply. Direct division can
    // round below an integer raster boundary (800/480 * 3 became pixel 4).
    const float scale=t.legacy_4_3?float(t.s):float(t.height)*(1.f/480.f);
    float left=float(-t.ox/scale);
    // left is stored as float, then multiplied before native truncation.
    // Bias one ULP outward when its product undershoots the intended origin:
    // otherwise 240 becomes 239, or 1200 becomes 1199, in the real widget code.
    if(-double(left)*scale<t.ox) left=std::nextafter(left,-std::numeric_limits<float>::infinity());
    return {scale,scale,left};
}
inline point panel_pixel(panel_scale f,point p) {return {p.x*f.sy-double(f.left)*f.sx,p.y*f.sy};}
inline point panel_logical(panel_scale f,point p) {return {(p.x+double(f.left)*f.sx)/f.sy,p.y/f.sy};}
inline double pixel_fraction(double pixel,double first,double last) {return (pixel-first)/(last-first+1);}
}
