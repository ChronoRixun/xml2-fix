#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace effect_pools_rules {
using u32=std::uint32_t;
enum class pool { primitive_defs, vectors };
enum class field { capacity,last_index,bitmap_words,bitmap_a,ring,ring_write,ring_read,
    ring_count,bitmap_b,live,ids,mask,shift,presets,preset_count,size,object };
struct site {u32 va;std::string_view hex;unsigned offset,size;field member;u32 retail;};
struct guard {u32 va;std::string_view hex,what;};
struct descriptor {
    u32 capacity,bitmap_words,bitmap_a,ring,ring_write,ring_read,ring_count,bitmap_b,
        live,ids,mask,shift,presets,preset_count,size;
    constexpr bool operator==(const descriptor&) const = default;
};
constexpr u32 stock_capacity(pool p){return p==pool::vectors?710:816;}
constexpr u32 maximum(pool p){return p==pool::vectors?16384:8192;}
constexpr u32 record_size(pool p){return p==pool::vectors?24:0x118;}
constexpr u32 owner(pool p){return p==pool::vectors?0x706680:0x70b64c;}
constexpr u32 init_guard(pool p){return p==pool::vectors?0x70b648:0x70b64c;}
constexpr descriptor describe(pool p,u32 n){
    const u32 words=(n+31)/32,a=n*record_size(p),ring=a+words*4,w=ring+(n+1)*4;
    const u32 b=w+12,live=b+words*4,tail=live+4;
    if(p==pool::vectors)return {n,words,a,ring,w,w+4,w+8,b,live,0,0,0,tail,tail+84*4,tail+84*4+4};
    const u32 mask=tail+n*4;
    return {n,words,a,ring,w,w+4,w+8,b,live,tail,mask,mask+4,0,0,mask+8};
}
constexpr u32 value(field f,const descriptor& d,u32 object){
    switch(f){
    case field::capacity:return d.capacity;case field::last_index:return d.capacity-1;
    case field::bitmap_words:return d.bitmap_words;case field::bitmap_a:return d.bitmap_a;
    case field::ring:return d.ring;case field::ring_write:return d.ring_write;
    case field::ring_read:return d.ring_read;case field::ring_count:return d.ring_count;
    case field::bitmap_b:return d.bitmap_b;case field::live:return d.live;
    case field::ids:return d.ids;case field::mask:return d.mask;case field::shift:return d.shift;
    case field::presets:return d.presets;case field::preset_count:return d.preset_count;
    case field::size:return d.size;case field::object:return object;
    }return 0;
}
inline u32 capacity(pool p,std::optional<std::string_view> text){
    if(!text)return stock_capacity(p);
    auto s=*text;
    while(!s.empty() && (s.front()==' '||s.front()=='\t'))s.remove_prefix(1);
    while(!s.empty() && (s.back()==' '||s.back()=='\t'))s.remove_suffix(1);
    u32 n=0;auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);
    return error==std::errc{}&&end==s.data()+s.size()&&n>=stock_capacity(p)&&n<=maximum(p)?n:stock_capacity(p);
}
}
