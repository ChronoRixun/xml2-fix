#include "centered_ui_rules.hpp"
#include "texture_filtering_rules.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace centered_ui;
    for(const auto [w,h]:{std::pair{640u,480u},{1280u,960u},{1280u,800u},{1920u,1080u},
                          {2520u,1080u},{3440u,1440u},{3840u,1080u},{1365u,768u}}) {
        const auto t=make(w,h,1);assert(t);
        const auto f=panel_fields(*t);
        assert(f.sx==f.sy && f.sy==(t->legacy_4_3?float(double(h)/480.):float(h)*(1.f/480.f)));
        assert(std::abs(-double(f.left)*f.sx-t->ox)<.001);
        for(const point p: {point{0,0},{33,146},{177,146},{229,353},{320,240},{639,479}}) {
            const auto native=panel_pixel(f,p);
            assert(std::abs(native.x-t->pixel(p).x)<.001 && std::abs(native.y-t->pixel(p).y)<.001);
            const auto inverse=panel_logical(f,native);
            assert(std::abs(inverse.x-p.x)<.001 && std::abs(inverse.y-p.y)<.001);
        }
        // Inclusive native rectangle endpoints, including a one-pixel-wide control.
        for(int width:{1,40,196}) {
            const auto a=panel_pixel(f,{33,146}),b=panel_pixel(f,{double(33+width-1),163});
            assert(std::trunc(a.x)<=std::trunc(b.x));
            const double first=std::trunc(a.x),last=std::trunc(b.x);
            // Retail stores inclusive geometric endpoints, but its actual
            // PtInRect hit convention excludes right/bottom. Preserve that quirk.
            RECT rect{LONG(first),0,LONG(last),10};
            assert(bool(PtInRect(&rect,{LONG(first),5}))==(first<last));
            assert(!PtInRect(&rect,{LONG(last),5}) && !PtInRect(&rect,{LONG(first-1),5}));
            // The slider/scrollbar inverse uses its current pixel edges, never the desktop width.
            for(double value:{0.,.25,.5,1.}) {
                const double pixel=first+value*(last-first+1);
                assert(std::abs(pixel_fraction(pixel,first,last)-value)<1e-9);
            }
        }
        if(t->legacy_4_3) {
            assert(f.left==0 && f.sx==float(w)/640.f);
            for(int x=0;x<640;++x) assert(panel_pixel(f,{double(x),0}).x==double(x)*f.sx);
        }
    }
    using texture_filtering::world_path;
    for(unsigned value:{0x2u,0x12u,0x42u,0x52u,0x102u,0x112u,0x142u,0x152u}) assert(texture_filtering::world_fvf(value));
    for(unsigned value:{0u,5u,4u,0x80000002u,0x10002u,0x212u}) assert(!texture_filtering::world_fvf(value));
    for(const auto* path:{"\\\\ravendata1\\xmen\\Assets\\tilesets\\haarp\\floor.png",
                          "\\\\Ravendata1\\Xmen\\Art\\Models\\Actors\\01_Cyclops\\0101-7.png",
                          "assets/tilesets/haarp/wall.png","art/models/actors/wolverine/skin.png"}) assert(world_path(path));
    for(const auto* path:{"", "floor.png", "textures/ui/lch.png", "assets/tilesets/ui/map.png",
                          "assets/tilesets/fonts/a.png", "assets/tilesets/haarp/floor_atlas.png",
                          "art/models/actors/hero/glyphs.png", "assets/tilesets/fx/spritesheet.png",
                          "xassets/tilesets/haarp/floor.png","assets/tilesets/../ui/foo.png",
                          "art/objects/haarp/crate.png"}) assert(!world_path(path));
    remastered_rules::settings defaults;assert(!defaults.enabled && !defaults.anisotropy && !defaults.centered_hud && !defaults.anisotropy_on_point_art);
    std::cout<<"PASS 1c: eight aspects, 4:3 identity, pixel hits/drag inverse, world paths/exclusions/defaults\n";
}
