#pragma once
#include <array>
#include <cstdlib>
namespace online_text_entry_rules {
// Bounded, allocation-free queue on the DirectInput/game thread.
struct inbox {
    std::array<int,64> values{};unsigned begin=0,count=0;
    void clear() {begin=count=0;}
    unsigned size() const {return count;}
    bool empty() const {return count==0;}
    int front() const {return values[begin];}
    void pop_front() {if(count) {begin=(begin+1)%64;--count;}}
    void push_back(int value) {if(count<64) {values[(begin+count)%64]=value;++count;}}
    void push_front(int value) {if(count==64) --count;begin=(begin+63)%64;values[begin]=value;++count;}
};
inline constexpr int accept=256,left=257,right=258,up=259,down=260;
inline int key(unsigned scan,bool shift,bool caps) {
    constexpr unsigned letters[]{0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    for(unsigned i=0;i<26;++i) if(scan==letters[i]) return (shift!=caps ? 'A' : 'a')+int(i);
    if(scan>=2 && scan<=11) return shift ? "!@#$%^&*()"[scan-2] : "1234567890"[scan-2];
    switch(scan) {
    case 0x0e:return 8;case 0x01:return 27;case 0x39:return ' ';
    case 0x34:return shift ? '>' : '.';case 0x33:return shift ? '<' : ',';case 0x0c:return shift ? '_' : '-';
    case 0x1c:case 0x9c:return accept;
    case 0xcb:return left;case 0xcd:return right;case 0xc8:return up;case 0xd0:return down;
    case 0x53:return '.';
    default:break;
    }
    constexpr unsigned pad[]{0x52,0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49};
    for(unsigned i=0;i<10;++i) if(scan==pad[i]) return '0'+int(i);
    return 0;
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
