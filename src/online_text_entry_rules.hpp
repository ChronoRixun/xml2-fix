#pragma once
#include <array>
#include <cstdlib>
namespace online_text_entry_rules {
// Bounded, allocation-free queue on the shared window/game input thread.
struct inbox {
    std::array<int,64> values{};unsigned begin=0,count=0;
    void clear() {begin=count=0;}
    unsigned size() const {return count;}
    bool empty() const {return count==0;}
    int front() const {return values[begin];}
    void pop_front() {if(count) {begin=(begin+1)%64;--count;}}
    bool push_back(int value) {if(count==64) return false;values[(begin+count)%64]=value;++count;return true;}
    void push_front(int value) {if(count==64) --count;begin=(begin+63)%64;values[begin]=value;++count;}
};
inline constexpr int accept=256,left=257,right=258,up=259,down=260;
// Printable characters belong exclusively to Windows' translated WM_CHAR lane.
// Physical scan codes are used only for layout-independent editor controls.
inline int key(unsigned scan) {
    switch(scan) {
    case 0x0e:return 8;case 0x01:return 27;
    case 0x1c:case 0x9c:return accept;
    case 0xcb:return left;case 0xcd:return right;case 0xc8:return up;case 0xd0:return down;
    default:return 0;
    }
}
inline constexpr int unsupported=261;
inline int character(unsigned value,unsigned scan=0,bool address=false) {
    // A locale's numpad decimal separator still denotes a period in an IPv4
    // address. Keep a main-keyboard comma and every name request unchanged.
    if(address && scan==0x53 && value==',') value='.';
    // Ignore WM_CHAR copies of DI controls (and Ctrl shortcuts). Do not narrow
    // Unicode/IME text to a byte: the native editor's charset is ASCII only.
    if(value<32 || value==127) return 0;
    return value<127 ? int(value) : unsupported;
}
// Retail UI codes: MOVE_Y=1, MOVE_X=0, MOVE_RIGHT=30, MOVE_DOWN=31;
// NextHero/PreviousHero and Increase/DecreaseHeroAggr are the bound D-pad.
inline int pad(unsigned edge) {
    if(edge&(1u<<21)) return 27;
    if(edge&(1u<<4)) return accept;
    if(edge&((1u<<1)|(1u<<11))) return up;
    if(edge&((1u<<31)|(1u<<12))) return down;
    if(edge&((1u<<0)|(1u<<17))) return left;
    if(edge&((1u<<30)|(1u<<16))) return right;
    return 0;
}
inline bool enabled(int i,int count,bool address) {
    return i>=0 && i<=40 && (i<36 ? i<count : !address || i==36 || i==39 || i==40);
}
inline int move(int selected,int direction,int count,bool address) {
    const auto x=[](int i) { return i<36 ? 70+(i%9)*56+25 : i<40 ? 61+(i-36)*132+61 : 109; };
    const auto y=[](int i) { return i<36 ? 193+(i/9)*36 : i<40 ? 352 : 416; };
    int best=selected,score=1000000;
    for(int i=0;i<=40;++i) if(enabled(i,count,address) && i!=selected) {
        const int dx=x(i)-x(selected),dy=y(i)-y(selected);
        const bool horizontal=direction==left || direction==right;
        const int forward=direction==left ? -dx : direction==right ? dx : direction==up ? -dy : dy;
        if(forward<=0) continue;
        const int candidate=forward+4*std::abs(horizontal ? dy : dx);
        if(candidate<score) {score=candidate;best=i;}
    }
    return best;
}
}
