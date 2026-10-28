#pragma once
#include <cstdint>
namespace item_capacity_rules {
struct pool_layout {std::uint32_t bitmap,ring,head,tail,ring_count,buckets,live,handles,mask,shift,size;};
constexpr pool_layout pool(unsigned n,unsigned stride) {
 pool_layout p{}; p.bitmap=n*stride;p.ring=p.bitmap+(n+31)/32*4;p.head=p.ring+n*4+4;
 p.tail=p.head+4;p.ring_count=p.head+8;p.buckets=p.head+12;p.live=p.buckets+(n+31)/32*4;
 p.handles=p.live+4;p.mask=p.handles+n*4;p.shift=p.mask+4;p.size=p.mask+12;return p;
}
inline constexpr auto instances=pool(1024,0x34), attached=pool(4096,0x14);
inline constexpr std::uint32_t records=0x7a00, enhancement_bitmap=records+4096*0x28;
inline constexpr std::uint32_t definitions=enhancement_bitmap+128*4, states=definitions+1024*0x3c;
inline constexpr std::uint32_t world=states+1024*2, world_bitmap=world+1024*8, manager_size=world_bitmap+32*4;
static_assert(manager_size==0x41480 && instances.size==0xf120 && attached.size==0x1c420);
// Retail/E10A2 callers retain the original locations; W2 preflight uses these
// object-relative live counts (the pools start at object+4).
constexpr unsigned instance_count_offset(bool w2){return w2?instances.live+4:0x3854;}
constexpr unsigned attached_count_offset(bool w2){return w2?attached.live+4:0x239c;}
}
