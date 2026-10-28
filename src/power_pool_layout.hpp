#pragma once
#include <cstdint>

// Outer object offsets, including the four-byte interface vtable. Internal
// template methods receive outer+4; the generated ledger records that bias.
namespace power_pool_layout {
using u32 = std::uint32_t;
enum class pool { attached, vf };
struct layout {
 u32 capacity, stride, built, ring, write, read, free, used, live, ids, mask, shift, size;
};
constexpr layout layout_for(pool which, u32 n) {
 const u32 stride=which==pool::attached?0x68:0x14, words=(n+31)/32;
 const u32 built=4+n*stride, ring=built+words*4, write=ring+(n+1)*4;
 const u32 used=write+12, live=used+words*4, ids=live+4, mask=ids+n*4;
 return {n,stride,built,ring,write,write+4,write+8,used,live,ids,mask,mask+4,mask+8};
}
constexpr u32 stock_capacity(pool which) { return which==pool::attached?128:192; }
constexpr u32 owner(pool which) { return which==pool::attached?0x7988d4:0x79ba3c; }
constexpr u32 getter(pool which) { return which==pool::attached?0x538990:0x54c690; }
constexpr auto stock_attached=layout_for(pool::attached,128), stock_vf=layout_for(pool::vf,192);
static_assert(layout_for(pool::attached,256).size==0x7060);
static_assert(layout_for(pool::vf,256).size==0x1c60);
}
