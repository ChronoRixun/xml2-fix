#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace geometry_sharing_rules
{
 using address = std::uint32_t;
 using guard = limits_rules::guard;
 struct layout { address geometry_vtable, vertex_vtable; };
 enum class comparison { equal, different, unsupported };
 constexpr address hook_va = 0x56d42c, continue_va = 0x56d432, next_candidate_va = 0x56d617;
 constexpr address geometry_vtable_rva = 0x21cb8, vertex_vtable_rva = 0xdd6a0;
 constexpr std::array<guard,4> exe_guards{{
  {0x56d353,"8bf03bf789742418","candidate register and saved pointer"},
  {0x56d424,"3ac30f85eb010000","format counts checked before the hook"},
  {hook_va,"8b44241433f6","vertex-loop entry"},
  {next_candidate_va,"8b5d088b74242446","next candidate path"}
 }};
 // RVAs relative to the actual loaded module, never preferred-base addresses.
 constexpr std::array<guard,7> gfx_guards{{
  {0x4010,"8d411cc3","legacy vertex format"},
  {0x22e0,"8b410cc3","legacy vertex count"},
  {0x1d60,"8b01c1e80883e00fc3","blend-index count"},
  {0x1d30,"8b01c1e80483e00fc3","blend-weight count"},
  {0xa310,"8a413dc3","packed blend-index offset"},
  {0xa2c0,"8a4138c3","legacy stride"},
  {0x4605d,"33c08b51088a4138560faf4424088b725033d28a513903c6","legacy vertex payload"}
 }};
 constexpr std::array<guard,2> attrs_guards{{
  {0x3090,"8b410c8b400cc3","legacy geometry vertex array/count"},
  {0x17000,"83ec105355","legacy geometry apply"}
 }};
 template<class Guards,class Matches> const guard* first_mismatch(const Guards& guards,Matches matches)
 {
  for(const auto& g:guards)if(!matches(g.va,g.hex))return &g;
  return nullptr;
 }
 template<class Memory> std::optional<address> word(Memory& mem,address a)
 {
  address value=0;
  return mem.read(a,&value,sizeof value)?std::optional<address>(value):std::nullopt;
 }
 template<class Memory> std::optional<unsigned> byte(Memory& mem,address a)
 {
  std::uint8_t value=0;
  return mem.read(a,&value,sizeof value)?std::optional<unsigned>(value):std::nullopt;
 }
 struct vertex_view { address data,count;unsigned stride,index_offset,index_count; };
 template<class Memory> std::optional<vertex_view> view(Memory& mem,address geometry,const layout& types)
 {
  if(!geometry||word(mem,geometry)!=types.geometry_vtable)return std::nullopt;
  const auto va=word(mem,geometry+12);
  if(!va||!*va||word(mem,*va)!=types.vertex_vtable)return std::nullopt;
  const auto count=word(mem,*va+12),format=word(mem,*va+28);
  const auto stride=byte(mem,*va+56),offset=byte(mem,*va+61);
  if(!count||!format||!stride||!offset)return std::nullopt;
  // Basic legacy position/normal/color, weight/index counts and up to eight UVs.
  // Point sprites, tangent/binormal extensions and reserved flags stay retail.
  constexpr address known_flags=0x000f0ff7;
  const unsigned indices=(*format>>8)&15,weights=(*format>>4)&15,uvs=(*format>>16)&15;
  if((*format&~known_flags)||!(*format&1)||indices>4||weights>4||uvs>8)return std::nullopt;
  if(!indices)return vertex_view{0,*count,*stride,*offset,0};
  // The concrete legacy array packs the index slots into one four-byte field.
  if(*stride<4||*offset>*stride-4)return std::nullopt;
  const auto memory=word(mem,*va+8);
  if(!memory||!*memory)return std::nullopt;
  const auto data=word(mem,*memory+0x50);
  if(!data||(!*data&&*count))return std::nullopt;
  const auto end=std::uint64_t(*data)+(*count?std::uint64_t(*count-1)* *stride+*offset+4:0);
  if(end>std::numeric_limits<address>::max())return std::nullopt;
  return vertex_view{*data,*count,*stride,*offset,indices};
 }
 template<class Memory> comparison compare(Memory& mem,address incoming,address candidate,const layout& types)
 {
  const auto a=view(mem,incoming,types),b=view(mem,candidate,types);
  if(!a||!b||a->count!=b->count||a->index_count!=b->index_count)return comparison::unsupported;
  if(!a->index_count)return comparison::equal;
  bool different=false;
  for(address i=0;i<a->count;++i)
  {
   const auto x=word(mem,a->data+i*a->stride+a->index_offset),y=word(mem,b->data+i*b->stride+b->index_offset);
   if(!x||!y)return comparison::unsupported;
   different |= *x!=*y;
  }
  return different?comparison::different:comparison::equal;
 }
 inline std::array<std::uint8_t,6> jump(address at,address target)
 {
  std::array<std::uint8_t,6> bytes{0xe9,0,0,0,0,0x90};
  const address relative=target-at-5;
  std::memcpy(bytes.data()+1,&relative,4);
  return bytes;
 }
 // Authored x86 bridge, shared with execution tests. EDI=incoming, ESI=candidate.
 // The callback returns nonzero to keep the retail comparison, zero to advance.
 inline std::vector<std::uint8_t> bridge(address callback,address resume,address reject)
 {
  std::vector<std::uint8_t> b{0x9c,0x60,0x8b,0xc4,0x81,0xec,0x10,0x02,0,0,0x83,0xe4,0xf0,0x89,0x84,0x24,0,0x02,0,0,0x0f,0xae,0x04,0x24,0x56,0x57,0xb8};
  const auto append_word=[&](address v){for(unsigned n=0;n<4;++n)b.push_back(static_cast<std::uint8_t>(v>>(8*n)));};
  append_word(callback);
  b.insert(b.end(),{0xff,0xd0,0x83,0xc4,0x08,0x85,0xc0,0x75,0});
  const auto branch=b.size()-1;
  const auto restore=[&]{b.insert(b.end(),{0x0f,0xae,0x0c,0x24,0x8b,0xa4,0x24,0,0x02,0,0,0x61,0x9d});};
  restore();b.push_back(0x68);append_word(reject);b.push_back(0xc3);
  b[branch]=static_cast<std::uint8_t>(b.size()-(branch+1));
  restore();b.insert(b.end(),{0x8b,0x44,0x24,0x14,0x33,0xf6}); // guarded displaced instructions
  b.push_back(0x68);append_word(resume);b.push_back(0xc3);
  return b;
 }
}
