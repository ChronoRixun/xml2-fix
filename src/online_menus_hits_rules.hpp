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
// unchanged (prompt hover, unlisted items), as is every point on every other menu. Fix E2: the handed-over point lies in both
// of the item's engine boxes (first pass: hover; second pass: click) and in no other focusable item's, walked in whatever
// order the engine's item tree gives; the parser is strict and locale-independent (see parse).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <charconv>
#include <set>
#include <system_error>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace online_menus_hits {
inline constexpr char contract[] = "lc-online-hits/1";
inline constexpr std::size_t max_entries = 1024;
inline constexpr std::size_t max_line = 256;          // bytes before the LF
inline constexpr std::size_t max_file = 256 * 1024;
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
// Locale-independent (std::from_chars; strtod follows the C locale's decimal separator): optional '-', digits, optional
// '.' and digits, within +/-view_limit.
inline bool number(std::string_view s,float& out) {
    if(s.empty() || s.size()>24) return false;
    std::size_t digits=0,dots=0;
    for(std::size_t i=0;i<s.size();++i) {
        const char c=s[i];
        if(c>='0'&&c<='9') ++digits;
        else if(c=='.') ++dots;
        else if(!(c=='-' && i==0)) return false;
    }
    if(!digits || dots>1) return false;
    double v=0;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),v,std::chars_format::fixed);
    if(r.ec!=std::errc() || r.ptr!=s.data()+s.size() || !std::isfinite(v) || v<-view_limit || v>view_limit) return false;
    out=static_cast<float>(v); return true;
}
// The whole file or nothing: any malformed line refuses the table (the screens then keep the engine's own hit test) and
// `why` says which line and what ("line 7: bad entry"). Strict by design: LF only (no CR), no byte order mark, no blank
// line, single spaces, lines of at most max_line bytes, every (menu, item) pair once.
inline bool parse(std::string_view text,table& out,std::string& why) {
    out.entries.clear();
    std::size_t number_of_line=0;
    auto fail=[&](const char* what)->bool { why="line "+std::to_string(number_of_line)+": "+what; out.entries.clear(); return false; };
    auto next_line=[&](std::string_view& line)->bool {
        if(text.empty()) return false;
        ++number_of_line;
        const auto n=text.find('\n'); if(n==std::string_view::npos) { fail("last line not terminated"); return false; }
        line=text.substr(0,n); text.remove_prefix(n+1); return true;
    };
    std::string_view line;
    if(text.size()>=3 && text.substr(0,3)=="﻿") { number_of_line=1; return fail("byte order mark"); }
    if(!next_line(line)) { if(why.empty()) fail("empty file"); return false; }
    if(line!=contract) return fail(line.find('\r')!=std::string_view::npos ? "carriage return (CRLF)" : "bad header (not lc-online-hits/1)");
    std::set<std::pair<std::string,std::string>> seen;
    while(next_line(line)) {
        if(line.size()>max_line) return fail("line too long");
        if(line.find('\r')!=std::string_view::npos) return fail("carriage return (CRLF)");
        std::array<std::string_view,6> f{}; std::size_t n=0;
        while(!line.empty()) {
            const auto sp=line.find(' ');
            const auto token=line.substr(0,sp);
            if(token.empty() || n==f.size()) return fail("bad fields");
            f[n++]=token; if(sp==std::string_view::npos) break; line.remove_prefix(sp+1);
            if(line.empty()) return fail("bad fields");
        }
        entry e;
        if(n!=6 || !name_ok(f[0]) || !name_ok(f[1]) || !number(f[2],e.rect.left) || !number(f[3],e.rect.bottom) ||
           !number(f[4],e.rect.right) || !number(f[5],e.rect.top) || !(e.rect.left<e.rect.right) || !(e.rect.bottom<e.rect.top))
            return fail("bad entry");
        if(out.entries.size()==max_entries) return fail("too many entries");
        e.menu=std::string(f[0]); e.item=std::string(f[1]);
        if(!seen.insert({e.menu,e.item}).second) return fail("duplicate entry");
        out.entries.push_back(std::move(e));
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

// The engine's own rectangles of an item, from its fields (+0x70 x, +0x72 y, +0x74 w, +0x76 h, +0x7a box top; PtInRect: right
// and bottom excluded). `shifted` is the box the item's own hit test (vt+0x74: 0x5c6490 -> 0x5bc1b0) asks, moved by +0x7a / 2;
// `first` is the UNSHIFTED box the menu's first pass asks (0x5ae0a0 over desctext1..5: hover highlight and hover sound; the
// lobbies' 0x5ba2b0 / 0x5c8da0 over their buttons and text_playername1: focus and hover). For an ordinary row +0x7a is 0 and
// the two are the same box; for a prompt the shifted box sits about 7 units off the first-pass box.
struct item_box { int left=0,top=0,right=0,bottom=0; };
struct item_rects { item_box shifted, first; };
inline item_box engine_box(std::int16_t x70,std::int16_t y72,std::int16_t w74,std::int16_t h76,std::int16_t top7a,bool shifted=true) {
    const int shift=shifted ? int(top7a)/2 : 0;
    return {int(x70), int(y72)-int(h76)-1+shift, int(w74)-1+int(x70), int(y72)+shift};
}
inline item_rects engine_rects(std::int16_t x70,std::int16_t y72,std::int16_t w74,std::int16_t h76,std::int16_t top7a) {
    return {engine_box(x70,y72,w74,h76,top7a,true),engine_box(x70,y72,w74,h76,top7a,false)};
}
inline bool holds(const item_box& r,int x,int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
// Some engine pass of this item answers for the point (either box).
inline bool holds_any(const item_rects& r,int x,int y) { return holds(r.shifted,x,y) || holds(r.first,x,y); }
inline item_box intersection(const item_box& a,const item_box& b) {
    return {std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
}

// A point that is inside `target` (both of its passes, so the hover highlight, the hover sound and the click all name the
// target) and that no OTHER focusable item of the menu answers for in either of its passes - whatever order the engine walks
// them in (the walk is an in-order walk of the name-keyed item tree, not the table's order). Centre first, then every point,
// nearest rows to the centre first. When the target's two boxes share no point (the first-pass box is then irrelevant to the
// shifted box's click), the shifted box alone. False: no such point exists.
template<class Others>
inline bool free_point_in(const item_box& region,const Others& others,int& x,int& y) {
    if(region.right<=region.left || region.bottom<=region.top) return false;
    auto free_at=[&](int px,int py) { for(const auto& o:others) if(holds_any(o,px,py)) return false; return true; };
    const int cx=(region.left+region.right-1)/2, cy=(region.top+region.bottom-1)/2;
    if(free_at(cx,cy)) { x=cx; y=cy; return true; }
    const int height=region.bottom-region.top;
    for(int d=0;d<height;++d) for(const int py:{cy-d,cy+d}) {
        if(py<region.top || py>=region.bottom) continue;
        for(int px=region.left;px<region.right;++px) if(free_at(px,py)) { x=px; y=py; return true; }
    }
    return false;
}
template<class Others>
inline bool free_point(const item_rects& target,const Others& others,int& x,int& y) {
    const item_box both=intersection(target.shifted,target.first);
    if(free_point_in(both,others,x,y)) return true;
    return free_point_in(target.shifted,others,x,y);
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

// ---- Fix K (060r live failure 8): which items really ANSWER ---------------------------------------------------------------
// Fix E2 counted every item with +0x54 bit 3 as one that could answer the engine's walk. That bit is ENABLED (the item parser
// 0x5bc981 sets it for every item, enabled="false" clears it; neverfocus is bit 5, hidden bit 2), so every model and label
// counted too - and on the co-op screens each row's focus model (<row>_focus, MENU_ITEM_MODEL, shown, laid out at the row's
// own anchor and box: online_menus.py scene_layout) covered the row's whole box. No point was free, the remap gave up, and
// the engine got the raw point: live, the Hub rows hit exactly the retail text boxes (060r mh / scan probes = 060q).
// The engine's own answer (vt+0x74, base 0x5ae0a0 second pass; lobbies 0x5ba2b0 / 0x5c8da0 fall through to it):
//   CMenuItem / Model / ActorModel / Effect: 0x5bc1b0 - not hidden (+0x54 bit 2), the text (+0x24) not empty, PtInRect of the
//     shifted box. A model has no text: it never answers.
//   CMenuItemText / TextBox / Bar / Binary / List / ListItems / ListChars / CharSummary: 0x5c6490 - PtInRect of the shifted
//     box, then a button release (0x202) whose token text (+0x9c) names a menu code answers with that code, else 0x5bc1b0.
//   Any other class (ListBox 0x5c0e10, ListCycle 0x5c40a0, ...): its box, conservatively.
// The walk asks the shown (vt+4 false) and enabled (bit 3) items in the item tree's order and stops at the first that answers;
// a neverfocus item (bit 5) that answers ends the walk without taking the focus. The first pass (0x5ae0a0 over desctext1..5)
// tests the UNSHIFTED box of each shown, enabled prompt (no text test) and sets the hover index of the first that holds it.
struct engine_item {
    item_rects box;
    bool shown=false, enabled=false;
    bool text_test=false;   // 0x5bc1b0 / 0x5c6490 (needs text); false: answers wherever its box holds the point
    bool has_text=false;    // +0x24 text not empty (or, for 0x5c6490, a +0x9c token text)
};
inline bool answers(const engine_item& e,int x,int y) {
    return e.shown && e.enabled && holds(e.box.shifted,x,y) && (!e.text_test || e.has_text);
}
struct engine_menu {
    std::vector<engine_item> walk;          // the item tree in the engine's walk order
    std::array<int,5> prompts{-1,-1,-1,-1,-1}; // walk index of desctext1..5 (-1: none)
    std::vector<int> lobby_buttons;         // the lobby's own first pass (host: start / kick / leave; join: ready / leave)
    int picker=-1;                          // text_playername1: the lobbies ask its unshifted box whatever its state
    int focused=-1;                         // walk index of the focused item (CMenu +0x324), -1 none / not in the walk
};
// The walk index of the item the engine's second pass answers with, -1 for none.
inline int second_pass(const engine_menu& m,int x,int y) {
    for(std::size_t i=0;i<m.walk.size();++i) if(answers(m.walk[i],x,y)) return int(i);
    return -1;
}
// The hover index the first pass sets (desctextN -> N-1), -1 for none.
inline int first_pass(const engine_menu& m,int x,int y) {
    for(int n=0;n<5;++n) {
        const int i=m.prompts[std::size_t(n)];
        if(i<0) continue;
        const auto& e=m.walk[std::size_t(i)];
        if(e.shown && e.enabled && holds(e.box.first,x,y)) return n;
    }
    return -1;
}
// The lobbies' own pass would act on the point before the base walk could give it to `target` (the picker's box, or another of
// the lobby's buttons when the focus is on one of them - 0x5ba2b0 navigates to the hit button, and a click on the focused
// button is its Accept).
inline bool lobby_takes(const engine_menu& m,int target,int x,int y) {
    if(m.picker>=0 && m.picker!=target && holds(m.walk[std::size_t(m.picker)].box.first,x,y)) return true;
    bool focus_on_button=false;
    for(const int b:m.lobby_buttons) if(b==m.focused && m.walk[std::size_t(b)].enabled) focus_on_button=true;
    if(!focus_on_button) return false;
    for(const int b:m.lobby_buttons) if(b!=target && m.walk[std::size_t(b)].enabled && holds(m.walk[std::size_t(b)].box.first,x,y)) return true;
    return false;
}
inline int prompt_of(const engine_menu& m,int item) {
    for(int n=0;n<5;++n) if(m.prompts[std::size_t(n)]==item) return n;
    return -1;
}
// The engine, handed (x, y), focuses / clicks `target` and hovers its prompt (or no prompt when it is not one).
inline bool engine_picks(const engine_menu& m,int target,int x,int y) {
    return second_pass(m,x,y)==target && first_pass(m,x,y)==prompt_of(m,target) && !lobby_takes(m,target,x,y);
}
// A point the engine gives to `target`: inside both of its boxes if there is one (hover and click), else inside the box its
// own test asks; centre first, then the rows nearest the centre. False: the engine cannot be made to pick it anywhere.
inline bool picked_point_in(const engine_menu& m,int target,const item_box& region,int& x,int& y) {
    if(region.right<=region.left || region.bottom<=region.top) return false;
    const int cx=(region.left+region.right-1)/2, cy=(region.top+region.bottom-1)/2;
    if(engine_picks(m,target,cx,cy)) { x=cx; y=cy; return true; }
    const int height=region.bottom-region.top;
    for(int d=0;d<height;++d) for(const int py:{cy-d,cy+d}) {
        if(py<region.top || py>=region.bottom || (d==0 && py!=cy)) continue;
        for(int px=region.left;px<region.right;++px) if(engine_picks(m,target,px,py)) { x=px; y=py; return true; }
    }
    return false;
}
inline bool picked_point(const engine_menu& m,int target,int& x,int& y) {
    const auto& box=m.walk[std::size_t(target)].box;
    return picked_point_in(m,target,intersection(box.shifted,box.first),x,y) || picked_point_in(m,target,box.shifted,x,y);
}
// The first item (walk index) other than `target` whose answer covers the centre of the target's box - named in the diagnostic
// when no point can be given to the target.
inline int blocker_of(const engine_menu& m,int target) {
    const auto& b=m.walk[std::size_t(target)].box.shifted;
    const int cx=(b.left+b.right-1)/2, cy=(b.top+b.bottom-1)/2;
    for(std::size_t i=0;i<m.walk.size();++i) if(int(i)!=target && answers(m.walk[i],cx,cy)) return int(i);
    return -1;
}
}
