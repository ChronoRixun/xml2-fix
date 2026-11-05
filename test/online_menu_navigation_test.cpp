#include "online_menu_navigation_rules.hpp"
#include "online_text_entry_rules.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
using namespace online_menu_navigation_rules;
int main() {
    for(const auto s:{sample{1,0},sample{-1,0},sample{0,1},sample{0,-1}}) {
        navigation n;const auto expected=s.x>0 ? right : s.x<0 ? left : s.y>0 ? up : down;
        assert(n.update(s,0)==expected);assert(n.update(s,299)==none);
        assert(n.update(s,300)==expected);assert(n.update(s,399)==none);
        assert(n.update(s,400)==expected);assert(n.update(s,950)==expected);
        assert(n.update(s,950)==none);assert(n.update(s,999)==none);assert(n.update(s,1000)==expected);
        assert(n.update({},1001)==none);assert(n.update(s,1002)==expected);
    }
    for(const auto bit:{11u,12u,16u,17u}) {
        navigation n;sample s{0,0,1u<<bit};const auto expected=bit==11 ? up : bit==12 ? down : bit==16 ? right : left;
        assert(n.update(s,0)==expected);assert(n.update(s,299)==none);assert(n.update(s,300)==expected);
        assert(n.update(s,400)==expected);assert(n.update({},401)==none);assert(n.update(s,402)==expected);
        n.reset(s,1000);assert(n.update(s,1000)==none);assert(n.update(s,1299)==none);assert(n.update(s,1300)==expected);
    }
    for(float sign:{-1.f,1.f}) {
        navigation n;const auto expected=sign>0 ? right : left;
        assert(n.update({.5f*sign,0},0)==none);assert(n.update({.51f*sign,0},1)==expected);
        for(unsigned i=2;i<300;++i) assert(n.update({(i%2 ? .49f : .51f)*sign,0},i)==none);
        assert(n.update({.36f*sign,0},300)==none);assert(n.update({.36f*sign,0},301)==expected);
        assert(n.update({.35f*sign,0},302)==none);assert(n.update({.49f*sign,0},303)==none);
        assert(n.update({.51f*sign,0},304)==expected);
        assert(n.update({-.8f*sign,0},305)==(sign>0 ? left : right));
    }
    // Stable diagonal, all quadrants; one cardinal event, no alternating axes.
    for(float x:{-1.f,1.f}) for(float y:{-1.f,1.f}) {
        navigation n;assert(n.update({x,y},0)==(y>0 ? up : down));
        for(unsigned i=1;i<300;++i) assert(n.update({x*(i%2 ? .98f : 1.f),y*.99f},i)==none);
        assert(n.update({x,0},300)==(x>0 ? right : left));
    }
    navigation n;
    assert(n.update({.9f,.6f},0)==right);assert(n.update({.7f,.75f},1)==none);
    assert(n.update({.4f,.9f},2)==up);
    n.reset();
    assert(n.update({1,0,(1u<<11)},3)==up);
    assert(n.update({1,0,(1u<<11)|(1u<<12)},4)==none);
    assert(n.update({0,0,(1u<<16)|(1u<<17)},5)==none);
    assert(n.update({std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()},6)==none);
    n.reset();assert(n.update({1,0},0xfffffff0u)==right);
    assert(n.update({1,0},0x11bu)==none);assert(n.update({1,0},0x11cu)==right); // clock rollover
    // Retail axis bits never become buttons or unsigned directions.
    for(unsigned bit:{0u,1u,30u,31u}) assert(online_text_entry_rules::pad(1u<<bit)==0);
    assert(online_text_entry_rules::pad((1u<<21)|(1u<<4))==27);
    assert(online_text_entry_rules::pad(1u<<4)==online_text_entry_rules::accept);
    unsigned cells=0;
    for(int count:{5,11,36}) {
        const bool address=count==11;
        for(int start=0;start<=40;++start) if(online_text_entry_rules::enabled(start,count,address)) {
            ++cells;bool reached[41]{};reached[start]=true;
            for(int pass=0;pass<41;++pass) for(int i=0;i<=40;++i) if(reached[i])
                for(int d:{online_text_entry_rules::left,online_text_entry_rules::right,online_text_entry_rules::up,online_text_entry_rules::down}) {
                    const auto target=online_text_entry_rules::move(i,d,count,address);
                    assert(online_text_entry_rules::enabled(target,count,address));reached[target]=true;
                }
            for(int i=0;i<=40;++i) if(online_text_entry_rules::enabled(i,count,address)) assert(reached[i]);
        }
    }
    std::cout<<"PASS signed stick/D-pad directions, diagonal stability, hysteresis, native repeat, reset, clock wrap; all-to-all grid reachability from "<<cells<<" cells\n";
}
