#pragma once
#include <array>
#include <cstdint>

namespace script_pools
{
using u32 = std::uint32_t;
constexpr u32 system_pointer = 0x787740, retail_size = 0x3a284;
// Four pointer pools. The native ring reserves N+1 words and wraps at N.
struct pool_layout
{
    u32 base, capacity, stride, words, built, ring, write, read, free, used, live, size;
};
constexpr pool_layout layout(u32 base, u32 n, u32 stride)
{
    const u32 words = (n + 31) / 32, built = n * stride;
    const u32 ring = built + words * 4, write = ring + (n + 1) * 4;
    const u32 used = write + 12, live = used + words * 4;
    return {base,n,stride,words,built,ring,write,write+4,write+8,used,live,live+4};
}
constexpr std::array<pool_layout,4> stock{{
    layout(0xc714,620,0x38), layout(0x5db8,1556,12),
    layout(0x15918,120,0x230), layout(0x2f2f4,30,0x5c4)}};
constexpr auto raised_layout()
{
    std::array<pool_layout,4> result{};
    u32 tail = retail_size;
    constexpr std::array<u32,4> capacities{4096,8192,512,128};
    for (unsigned i=0;i<4;++i)
    {
        result[i] = layout(tail,capacities[i],stock[i].stride);
        tail += result[i].size;
    }
    return result;
}
inline constexpr auto raised = raised_layout();
constexpr u32 raised_size = raised.back().base + raised.back().size;
static_assert(stock[0].base+stock[0].size==0x15918);
static_assert(stock[1].base+stock[1].size==0xc094);
static_assert(stock[2].base+stock[2].size==0x261ac);
static_assert(stock[3].base+stock[3].size==0x3a080);
}
