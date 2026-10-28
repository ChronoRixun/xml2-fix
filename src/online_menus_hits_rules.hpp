#pragma once
// Mouse hit boxes of the co-op screens (060p live item 7; docs/online-menus.md "Mouse hit boxes").
//
// XMen2.exe's menu mouse (base CMenu vt+0x80 = 0x5ae0a0; the lobbies' 0x5ba2b0 / 0x5c8da0 fall through to it) gets the
// point in VIEW units (0x5f9eb0: x = left + px * width / [0xa09ffc], y = 384 - py * 384 / [0xa0a000], integers; the view is
// 384 tall and aspect x 384 wide, centred on 512) and asks each focusable item in turn whether it holds it (item vt+0x74:
// text 0x5c6490 -> 0x5bc1b0). An item answers with the rectangle the engine laid its text out in (vt+0x64 = 0x5bc530:
// +0x70 = round(anchor x) + round(box left), +0x72 = round(anchor y) + round(box top), +0x74 / +0x76 = rounded width /
// height, +0x7a = round(box top), the box's top relative to its anchor) - the same rectangle CText aligns the text in, so
// it cannot be widened without moving the text. Legends Classic draws each row's focus bar larger than that box (12 view
// units left and right, 5 above, 2 below for an ordinary row), so the top of every bar and its two ends were dead.
//
// The builder writes every focusable item's drawn bar, in view units, to Data/lc_online_hits.txt (this contract). On our
// screens only, the DLL then answers "which item holds this point" with the bars, and hands the engine a point inside the
// winning item's own rectangle (so the engine's focus, hover sound and click code stay the retail ones), or a point no item
// holds when the point lies outside every bar. A point outside every bar AND every listed item's rectangle is passed on
// unchanged (prompt hover, unlisted items), as is every point on every other menu.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace online_menus_hits {
inline constexpr char contract[] = "lc-online-hits/1";
inline constexpr std::size_t max_entries = 1024;
inline constexpr float view_height = 384.0f;
inline constexpr float view_limit = 4096.0f;

// A drawn focus bar: view units, x to the right, y UP (the engine's menu space). Left/right/bottom/top.
struct bar { float left=0,bottom=0,right=0,top=0; };
struct entry { std::string menu, item; bar rect; };
struct table { std::vector<entry> entries; };

inline bool name_ok(std::string_view s) {
    if(s.empty() || s.size()>63) return false;
    for(const char c:s) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return false;
    return true;
}
inline bool number(std::string_view s,float& out) {
    if(s.empty() || s.size()>24) return false;
    char text[32]{}; for(std::size_t i=0;i<s.size();++i) { const char c=s[i]; if(!((c>='0'&&c<='9')||c=='-'||c=='.')) return false; text[i]=c; }
    char* end=nullptr; const double v=std::strtod(text,&end);
    if(end!=text+s.size() || !std::isfinite(v) || v<-view_limit || v>view_limit) return false;
    out=static_cast<float>(v); return true;
}
// The whole file or nothing: any malformed line refuses the table (the screens then keep the engine's own hit test).
inline bool parse(std::string_view text,table& out,std::string& why) {
    out.entries.clear();
    auto next_line=[&](std::string_view& line)->bool {
        if(text.empty()) return false;
        const auto n=text.find('\n'); if(n==std::string_view::npos) { why="last line not terminated"; return false; }
        line=text.substr(0,n); text.remove_prefix(n+1); return true;
    };
    std::string_view line;
    if(!next_line(line) || line!=contract) { if(why.empty()) why="not "+std::string(contract); return false; }
    while(next_line(line)) {
        std::array<std::string_view,6> f{}; std::size_t n=0;
        while(!line.empty()) {
            const auto sp=line.find(' ');
            const auto token=line.substr(0,sp);
            if(token.empty() || n==f.size()) { why="bad fields"; out.entries.clear(); return false; }
            f[n++]=token; if(sp==std::string_view::npos) break; line.remove_prefix(sp+1);
            if(line.empty()) { why="bad fields"; out.entries.clear(); return false; }
        }
        entry e;
        if(n!=6 || !name_ok(f[0]) || !name_ok(f[1]) || !number(f[2],e.rect.left) || !number(f[3],e.rect.bottom) ||
           !number(f[4],e.rect.right) || !number(f[5],e.rect.top) || !(e.rect.left<e.rect.right) || !(e.rect.bottom<e.rect.top)) {
            why="bad entry"; out.entries.clear(); return false;
        }
        if(out.entries.size()==max_entries) { why="too many entries"; out.entries.clear(); return false; }
        e.menu=std::string(f[0]); e.item=std::string(f[1]); out.entries.push_back(std::move(e));
    }
    if(!why.empty()) { out.entries.clear(); return false; }
    if(out.entries.empty()) { why="no entries"; return false; }
    return true;
}

// The point the engine handed the menu, as a continuous view point. With the screen pixel the message carried (exact):
// the pixel's centre through the drawn projection (the same one the bar is rasterised with). Without it: the centre of the
// view-unit cell the engine truncated the point to (x toward zero, y = 384 - floor(py * 384 / H)).
struct view_point { float x=0,y=0; bool exact=false; };
inline float view_width(int screen_w,int screen_h) { return view_height*float(screen_w)/float(screen_h); }
inline bool engine_point_of_pixel(int px,int py,int screen_w,int screen_h,int& x,int& y) {
    // 0x5f9eb0: iVar6 = ftol(width), x = ftol(left + (px * iVar6) / W); iVar7 = ftol(384), y = iVar7 - (py * iVar7) / H.
    if(screen_w<64 || screen_h<64 || screen_w>16384 || screen_h>16384) return false;
    const float width=view_width(screen_w,screen_h), left=-(width-512.0f)/2.0f;
    const int iw=static_cast<int>(width);
    x=static_cast<int>(left+float((px*iw)/screen_w)); y=384-(py*384)/screen_h; return true;
}
inline view_point from_pixel(int px,int py,int screen_w,int screen_h) {
    const float width=view_width(screen_w,screen_h), left=-(width-512.0f)/2.0f;
    return {left+(float(px)+0.5f)*width/float(screen_w), view_height-(float(py)+0.5f)*view_height/float(screen_h), true};
}
inline view_point from_engine(int x,int y) { return {x>=0 ? float(x)+0.5f : float(x)-0.5f, float(y)-0.5f, false}; }
// Direct3D's fill rule for the bar's quad: a pixel is drawn when its centre is inside, the top and left edges included.
inline bool inside(const bar& b,const view_point& p) { return p.x>=b.left && p.x<b.right && p.y>b.bottom && p.y<=b.top; }

// The engine's own rectangle of an item (0x5bc1b0 / 0x5c6490, PtInRect: right and bottom excluded), from its fields.
struct item_box { int left=0,top=0,right=0,bottom=0; };
inline item_box engine_box(std::int16_t x70,std::int16_t y72,std::int16_t w74,std::int16_t h76,std::int16_t top7a) {
    const int shift=int(top7a)/2;
    return {int(x70), int(y72)-int(h76)-1+shift, int(w74)-1+int(x70), int(y72)+shift};
}
inline bool holds(const item_box& r,int x,int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }

// A point inside `target` that none of `others` holds (the engine asks items in its own order; the winner must be the
// target whatever that order is). Centre first, then every point, nearest rows to the centre first. False: none exists.
template<class Others>
inline bool free_point(const item_box& target,const Others& others,int& x,int& y) {
    if(target.right<=target.left || target.bottom<=target.top) return false;
    auto free_at=[&](int px,int py) { for(const auto& o:others) if(holds(o,px,py)) return false; return true; };
    const int cx=(target.left+target.right-1)/2, cy=(target.top+target.bottom-1)/2;
    if(free_at(cx,cy)) { x=cx; y=cy; return true; }
    const int height=target.bottom-target.top;
    for(int d=0;d<height;++d) for(const int py:{cy-d,cy+d}) {
        if(py<target.top || py>=target.bottom) continue;
        for(int px=target.left;px<target.right;++px) if(free_at(px,py)) { x=px; y=py; return true; }
    }
    return false;
}
// Several bars hold the point: the focused item's (the one bar on screen) if it is among them, else the one whose own box
// is nearest the point (first listed on a tie). Index into `held`, -1 for none.
struct candidate { bool focused=false; item_box box; };
inline float box_distance(const item_box& r,const view_point& p) {
    const float dx=std::max({float(r.left)-p.x,0.0f,p.x-float(r.right)}), dy=std::max({float(r.top)-p.y,0.0f,p.y-float(r.bottom)});
    return dx*dx+dy*dy;
}
inline int choose(const std::vector<candidate>& held,const view_point& p) {
    if(held.empty()) return -1;
    for(std::size_t i=0;i<held.size();++i) if(held[i].focused) return int(i);
    int best=0;
    for(std::size_t i=1;i<held.size();++i) if(box_distance(held[i].box,p)<box_distance(held[std::size_t(best)].box,p)) best=int(i);
    return best;
}
// The point no item holds: far outside the view (shorts in the engine's fields; PtInRect compares ints).
inline constexpr int nowhere_x=-30000, nowhere_y=-30000;
}
