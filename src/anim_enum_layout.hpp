#pragma once
#include <cstdint>

namespace anim_enum_layout
{
struct descriptor
{
    std::uint32_t enums, records;
    std::uint32_t record_stride, enum_array, tail, count_off, floats_off;
    std::uint32_t table_count_off, table_size;
    std::uint32_t builtin_first = 0xd2, builtin_count = 23, ext_first = 0xe9;
};
constexpr descriptor make(std::uint32_t n, std::uint32_t r)
{
    return {n,r,4*n+0x3c,8,8+4*n,12+4*n,16+4*n,8*n+4,8*n+8};
}
inline constexpr auto retail = make(210,16), raised = make(512,128);
static_assert(retail.record_stride==0x384 && retail.count_off==0x354 && retail.floats_off==0x358);
static_assert(retail.table_size==0x698 && retail.table_count_off==0x694);
static_assert(raised.record_stride==0x83c && raised.count_off==0x80c && raised.floats_off==0x810);
static_assert(raised.table_size==0x1008 && raised.records*raised.record_stride==269824);
static_assert(raised.enums<=4096 && raised.enums<32768 && raised.records==128);
}
