#pragma once
#include "e4_core_state.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <optional>

namespace e4_core_rules {
using u32=std::uint32_t;
constexpr unsigned capacity=96;
constexpr unsigned team_cap(unsigned controllers){return controllers<=32?4:controllers<=64?8:12;}
inline constexpr std::array<u32,4> agreement{96,96u|(2u<<16),124u|(124u<<16),15};
// Tier zero is an independent stock-sized FIFO. Tier one never changes its
// cursors. At <=32, admission counts tier zero independently of extended
// teams waiting for fold-back; above32 the total P2 band cap applies.
struct tiers {
    std::array<u32,4> fifo{0,1,2,3};
    unsigned enqueue=0,dequeue=0,free=4;
    std::array<u32,3> live{};
    void reset(){*this=tiers{};}
    bool active(unsigned i)const{return i<capacity&&(live[i/32]&(1u<<(i%32)));}
    unsigned count()const{unsigned n=0;for(auto w:live)n+=std::popcount(w);return n;}
    unsigned admission_count(unsigned controllers)const{return controllers<=32?std::popcount(live[0]&15u):count();}
    std::optional<unsigned> pop(unsigned controllers){
        if(admission_count(controllers)>=team_cap(controllers))return {};
        unsigned i=capacity;
        if(free){i=fifo[dequeue];dequeue=(dequeue+1)%4;--free;}
        else if(controllers>32)for(unsigned j=4;j<capacity;++j)if(!active(j)){i=j;break;}
        if(i==capacity||active(i))return {};
        live[i/32]|=1u<<(i%32);return i;
    }
    bool release(unsigned i){
        if(!active(i))return false;
        live[i/32]&=~(1u<<(i%32));
        if(i<4){if(free==4)return false;fifo[enqueue]=i;enqueue=(enqueue+1)%4;++free;}
        return true;
    }
    bool extended()const{return (live[0]&~15u)||live[1]||live[2];}
};
struct reservations {
    std::array<u32,3> pending{};unsigned reserved=0;
    bool has(unsigned i)const{return i<capacity&&(pending[i/32]&(1u<<(i%32)));}
    bool available(unsigned actor_live,unsigned controller_free)const{
        return actor_live<capacity&&controller_free>reserved;
    }
    bool admit(unsigned i){if(i>=capacity||has(i)||reserved==capacity)return false;pending[i/32]|=1u<<(i%32);++reserved;return true;}
    bool cancel(unsigned i){if(!has(i))return false;pending[i/32]&=~(1u<<(i%32));--reserved;return true;}
    bool begin(unsigned i,unsigned controller_free)const{return has(i)?controller_free>0:controller_free>reserved;}
    bool valid(unsigned controller_live)const{
        unsigned n=0;for(auto w:pending)n+=std::popcount(w);
        return n==reserved&&controller_live<=capacity&&reserved<=capacity-controller_live;
    }
};
// Revision 1 of the fold-back reset class table has no D field. T and S
// bytes deliberately survive, matching the retail timeout reference transition.
inline constexpr std::array<u32,2> team_only_fields{0x2e8,0x350};
constexpr bool needs_actor_feature(unsigned current_live,unsigned remembered_dynamic_peak){return current_live>32||remembered_dynamic_peak>32;}
}
