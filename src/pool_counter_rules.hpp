#pragma once
// Fixed-storage sampler state. Formatting and logging belong to the consumer thread.
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <limits>

namespace pool_counter_rules {
constexpr std::size_t max_pools = 128;
struct source {
    std::uint32_t root=0, offset=0, cap=0, guard=0;
    bool indirect=false, free_count=false;
};
template<class Memory> std::int32_t read_source(Memory& mem,const source& s) {
    std::uint32_t root=s.root, value=0;
    if(!root || !s.cap) return -1;
    if(s.guard && (!mem.read(s.guard,&value,4) || !(value&1)))return -1;
    if(s.indirect && (!mem.read(root,&root,4) || root<0x10000 || (root&3)))return -1;
    if(std::uint64_t(root)+s.offset+4>0x100000000ull || !mem.read(root+s.offset,&value,4))return -1;
    if(s.free_count) {
        if(value>s.cap)return -1;
        value=s.cap-value;
    }
    return value<=0x7fffffff ? static_cast<std::int32_t>(value) : -1;
}
struct range {
    std::uint32_t root=0, records=0, bitmap=0, stride=0, field=0, cap=0;
};
template<class Memory> std::int32_t range_item(Memory& mem,const range& r,std::uint32_t slot) {
    std::uint32_t root=0,bits=0,value=0;
    if(!r.root || !r.cap || r.cap>16384 || slot>=r.cap || r.stride<4 || r.field>r.stride-4 ||
       !mem.read(r.root,&root,4) || root<0x10000 || (root&3))return -1;
    const auto item=std::uint64_t(root)+r.records+std::uint64_t(slot)*r.stride+r.field;
    const auto bitmap=std::uint64_t(root)+r.bitmap+(slot/32)*4;
    if(item+4>0x100000000ull || bitmap+4>0x100000000ull ||
       !mem.read(static_cast<std::uint32_t>(bitmap),&bits,4) || !(bits&(1u<<(slot%32))))return -1;
    if(!mem.read(static_cast<std::uint32_t>(item),&value,4) || value>0x7fffffff)return -1;
    return static_cast<std::int32_t>(value);
}
template<class Memory> std::int32_t largest(Memory& mem,const range& r) {
    std::uint32_t root=0;
    if(!r.root || !r.cap || r.cap>16384 || r.stride<4 || r.field>r.stride-4 ||
       !mem.read(r.root,&root,4) || root<0x10000 || (root&3))return -1;
    if(std::uint64_t(root)+r.records+std::uint64_t(r.cap)*r.stride+4>0x100000000ull ||
       std::uint64_t(root)+r.bitmap+std::uint64_t((r.cap+31)/32)*4>0x100000000ull)return -1;
    std::int32_t peak=0;
    for(std::uint32_t word=0;word<(r.cap+31)/32;++word) {
        std::uint32_t bits=0;
        if(!mem.read(root+r.bitmap+word*4,&bits,4))return -1;
        for(unsigned bit=0;bit<32 && word*32+bit<r.cap;++bit) if(bits&(1u<<bit)) {
            std::uint32_t value=0;
            if(!mem.read(root+r.records+(word*32+bit)*r.stride+r.field,&value,4) || value>0x7fffffff)return -1;
            peak=(std::max)(peak,static_cast<std::int32_t>(value));
        }
    }
    return peak;
}
struct counter {
    const char* name = "";
    std::uint32_t cap = 0;
    std::int32_t live = -1, peak_zone = -1, peak_session = -1;
    std::uint32_t refused = 0, refused_zone = 0;
    std::uint32_t exceeded = 0, exceeded_zone = 0;
    bool particle_array = false;
    bool corrupt = false;
};
struct snapshot {
    std::array<counter,max_pools> pools{};
    std::size_t size = 0;
    std::array<char,128> zone{};
    bool active = false;
    std::uint32_t epoch = 0, dropped = 0;
};
struct tracker {
    snapshot state{};
    std::size_t add(const char* name, std::uint32_t cap) {
        if(state.size==max_pools)return max_pools;
        const auto i = state.size++;
        state.pools[i].name=name; state.pools[i].cap=cap; return i;
    }
    void observe(std::size_t i, std::int32_t live) {
        if(i>=state.size)return;
        auto& c=state.pools[i]; c.live=live;
        if(live<0)return;
        if(state.active)c.peak_zone=(std::max)(c.peak_zone,live);
        c.peak_session=(std::max)(c.peak_session,live);
        c.corrupt=c.corrupt || static_cast<std::uint32_t>(live)>c.cap;
    }
    void begin(const char* name) {
        state.zone.fill(0);
        // Zone names come from game memory: keep log/JSON control characters out.
        for(std::size_t i=0;i<state.zone.size()-1 && name[i];++i) {
            const auto c=static_cast<unsigned char>(name[i]);
            state.zone[i]=c>=32 && c<127 && c!='"' && c!='\\' ? static_cast<char>(c) : '_';
        }
        state.active=true; ++state.epoch;
        for(std::size_t i=0;i<state.size;++i) {
            state.pools[i].peak_zone=-1;state.pools[i].refused_zone=0;
            state.pools[i].exceeded_zone=0;
        }
    }
    void end() { state.active=false; }
    void refusal(std::size_t i, std::uint32_t delta) {
        if(i>=state.size)return;
        auto& c=state.pools[i];c.refused+=delta;
        if(state.active)c.refused_zone+=delta;
    }
};
}
