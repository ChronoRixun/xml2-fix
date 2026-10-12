#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

// Master Mold's native action-entity queries under both entity layouts.
// Stock (EntityCapacity off): the retail query manager and its five entry points.
// Raised (EntityCapacity=1, docs/entity-capacity.md): the startup transaction
// replaced each entry with an E9 jump into this DLL's query owner. The controller
// keeps calling the native entry addresses either way; only the expectation
// about their bytes, the free-query test and the result bound change.
namespace mold_query_rules {
using u32=std::uint32_t;
struct entry { u32 va; std::string_view retail; const char* what; };
// Retail bytes include the stock layout constants the old guard relied on
// (results acquisition reads the order busy byte at manager+0xdc0).
inline constexpr std::array<entry,5> entries{{
    {0x4ab550,"64a1000000008a0d0089","query pool getter"},
    {0x4ab6d0,"568bf1e878feffff33c9","query acquisition"},
    {0x4ab770,"e8dbfdffff33c98d90c00d0000","query results acquisition and retail layout"},
    {0x4ab720,"568b31e828feffff85f6","query release"},
    {0x4ab7d0,"e87bfdffff8b44240485","results release"}}};
// `code` holds at least retail.size()/2 bytes read at `va`. Stock: exactly the
// retail bytes. Raised: an E9 rel32 jump to exactly `installed`, the replacement
// the capacity transaction bound for that site (0: none). Anything else (a stale
// retail entry, another destination, or a jump while stock) refuses the controller.
inline bool entry_ok(bool raised,const unsigned char* code,const entry& e,u32 installed){
    if(!raised)return limits_rules::matches(code,e.retail);
    if(!installed||code[0]!=0xe9)return false;
    std::int32_t delta=0;std::memcpy(&delta,code+1,4);
    return e.va+5+static_cast<u32>(delta)==installed;
}
// Stock busy bytes: query +0x33 stride 0x1e4; orders at +0x5b0, busy +0x810, stride 0x814.
inline bool stock_pool_free(const unsigned char* manager){
    bool query_free=false,order_free=false;
    for(u32 i=0;i<3;++i){query_free|=manager[0x33+i*0x1e4]==0;order_free|=manager[0xdc0+i*0x814]==0;}
    return query_free&&order_free;
}
// Both acquisitions assume a free slot: the retail one has no busy check and the
// raised owner fails fast when all three queries are busy. Always test first:
// stock by the retail busy bytes, raised by the owner's free query/order counts.
inline bool pool_free(bool raised,const unsigned char* stock_manager,int free_queries,int free_orders){
    if(raised)return free_queries>0&&free_orders>0;
    return stock_manager&&stock_pool_free(stock_manager);
}
// A query result never holds more entities than the installed World.
inline bool result_count_ok(int count,u32 world){return count>=0&&static_cast<u32>(count)<=world;}
}
