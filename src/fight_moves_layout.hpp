#pragma once
#include "limits_rules.hpp"

namespace fight_moves_layout
{
constexpr DWORD stock_count=332, max_count=2048, record_size=0x148, pool_pointer=0x789e1c;
constexpr DWORD style_live=0xc60, style_capacity=150;
struct pool_descriptor { DWORD base=0, count=stock_count; };
struct layout { DWORD count,words,bitmap_a,ring,write,read,free,bitmap_b,live,size; };
constexpr layout layout_for(DWORD n)
{
    const DWORD words=(n+31)/32, a=n*record_size, ring=a+words*4, write=ring+(n+1)*4;
    const DWORD b=write+12,live=b+words*4;
    return {n,words,a,ring,write,write+4,write+8,b,live,live+4};
}
inline DWORD capacity(const std::optional<std::string>& text)
{
    const auto n=text?limits_rules::parse_count(*text):std::nullopt;
    return n&&*n>=static_cast<int>(stock_count)&&*n<=static_cast<int>(max_count)?static_cast<DWORD>(*n):stock_count;
}
constexpr std::optional<DWORD> slot(pool_descriptor pool,DWORD move)
{
    if(!pool.base||move<pool.base)return std::nullopt;
    const DWORD d=move-pool.base;
    if(d%record_size||d/record_size>=pool.count)return std::nullopt;
    return d/record_size;
}
enum class field { capacity,last_index,words,bitmap_a,ring,write,read,free,bitmap_b,live,size };
struct site { DWORD va;std::string_view hex;unsigned offset,width;field value; };
#include "w3_fightmoves_sites.hpp"
constexpr DWORD value(const layout& l,field f)
{
    switch(f){
    case field::capacity:return l.count;case field::last_index:return l.count-1;
    case field::words:return l.words;case field::bitmap_a:return l.bitmap_a;
    case field::ring:return l.ring;case field::write:return l.write;
    case field::read:return l.read;case field::free:return l.free;
    case field::bitmap_b:return l.bitmap_b;case field::live:return l.live;
    case field::size:return l.size;
    }return 0;
}
}
