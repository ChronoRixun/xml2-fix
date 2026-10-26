#pragma once
#include "power_pool_layout.hpp"
#include "patch_transaction.hpp"
#include <array>
#include <charconv>
#include <optional>

namespace power_pool_plan {
using namespace power_pool_layout;
enum class field { built,ring,write,read,free,used,live,ids,mask,shift,size,capacity,last,ring_read,ring_free };
struct site { u32 va; std::string_view hex; unsigned offset,size; field f; unsigned bias; u32 stock; };
struct clear_site { u32 va; std::string_view hex; unsigned reg; };
struct helper { u32 va; std::string_view hex; u32 call; std::string_view call_hex; };
#include "power_pool_sites.hpp"
constexpr u32 value(field f,const layout& l) {
 switch(f) {
 case field::built:return l.built; case field::ring:return l.ring;
 case field::write:return l.write; case field::read:return l.read;
 case field::free:return l.free; case field::used:return l.used;
 case field::live:return l.live; case field::ids:return l.ids;
 case field::mask:return l.mask; case field::shift:return l.shift;
 case field::size:return l.size; case field::capacity:return l.capacity;
 case field::last:return l.capacity-1; case field::ring_read:return l.read-l.ring;
 case field::ring_free:return l.free-l.ring;
 }
 return 0;
}
struct request { bool requested=false,valid=true; };
inline request choose(std::optional<std::string_view> text) {
 if(!text)return {};
 u32 n=0;const auto result=std::from_chars(text->data(),text->data()+text->size(),n);
 return {true,result.ec==std::errc{} && result.ptr==text->data()+text->size() && n==256};
}
inline void dword(std::vector<std::uint8_t>& b,u32 v) {
 for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(i*8)));
}
inline void put(std::vector<std::uint8_t>& b,unsigned offset,unsigned size,u32 v) {
 for(unsigned i=0;i<size;++i)b[offset+i]=static_cast<std::uint8_t>(v>>(i*8));
}
constexpr unsigned stride=256, slots=5, code_size=stride*slots;
struct plan { patch_transaction::contribution transaction; std::array<std::vector<std::uint8_t>,slots> code; };
inline plan make(pool which,u32 storage) {
 const auto l=layout_for(which,256);plan p;auto& c=p.transaction;
 c.owner=which==pool::attached?"AttachedPowerups":"VFNodes";
 const auto operands=[&](const auto& rows) {
  for(const auto& s:rows) {
   c.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"power pool operand"});
   std::vector<std::uint8_t> b(s.size);put(b,0,s.size,value(s.f,l)-s.bias);
   c.writes.push_back({s.va+s.offset,std::move(b),"power pool layout"});
  }
 };
 if(which==pool::attached)operands(attached_sites);else operands(vf_sites);
 const auto clone=[&](unsigned slot,const helper& h,const auto& rows) {
  c.guards.push_back({h.va,patch_transaction::bytes_of(h.hex),"shared helper unchanged"});
  c.guards.push_back({h.call,patch_transaction::bytes_of(h.call_hex),"private helper call"});
  auto& b=p.code[slot];b=patch_transaction::bytes_of(h.hex);
  for(const auto& s:rows)put(b,s.va-h.va+s.offset,s.size,value(s.f,l)-s.bias);
  c.writes.push_back(patch_transaction::relocation(h.call,"e8",true,storage+slot*stride,0,"pool-only helper redirect"));
 };
 if(which==pool::attached) {clone(0,helper_0,helper_fields_0);clone(1,helper_1,helper_fields_1);}
 else clone(0,helper_2,helper_fields_2);
 const auto clears=[&](const auto& rows) {
  for(unsigned i=0;i<rows.size();++i) {
   const auto& s=rows[i];const auto origin=storage+(i+2)*stride;
   c.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"complete bitmap clear"});
   // MOV [reg+offset],0 does not change GPRs or EFLAGS. Avoid string
   // instructions so DF is irrelevant and even a nonzero incoming DF is kept.
   auto& b=p.code[i+2];
   for(unsigned j=0;j<8;++j) {b.insert(b.end(),{0xc7,static_cast<std::uint8_t>(0x40|s.reg),static_cast<std::uint8_t>(j*4)});dword(b,0);}
   b.push_back(0xe9);dword(b,s.va+static_cast<u32>(s.hex.size()/2)-(origin+static_cast<u32>(b.size())+4));
   c.writes.push_back(patch_transaction::relocation(s.va,"e9",true,origin,static_cast<unsigned>(s.hex.size()/2)-5,"eight-word bitmap clear"));
  }
 };
 if(which==pool::attached)clears(attached_clears);else clears(vf_clears);
 c.guards.push_back({owner(which),{0,0,0,0},"pool singleton must not exist"});
 return p;
}
}
