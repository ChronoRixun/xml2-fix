#pragma once
#include "lc_fonts_rules.hpp"
#include "patch_transaction.hpp"

namespace lc_fonts_plan {
using namespace patch_transaction;
enum class kind {manager,slots,count,counter,current,counter_offset,names,count8,style,render,scale_draw,scale_clip,list_clip,list_draw,list_height,map_pc,map_hd};
struct site {address va;const char* hex;unsigned offset;kind type;};
struct region_guard {address lo,hi;const char* hash;};
#include "lc_fonts_sites.hpp"
struct targets {address manager,names,style,render,scale_draw,scale_clip,map_pc,map_hd,list_clip,list_draw,list_height;};
inline contribution make(const targets& t) {
 contribution c;c.owner="scoped Legends Classic fonts";
 c.guards.push_back({0x81d268,{0,0,0,0},"font manager must not exist"});
 for(const auto& r:regions)c.regions.push_back({"font contract",r.lo,r.hi,sha256_of(r.hash)});
 // The table cannot move independently of the guarded loader.
 c.guards.push_back({0x6e4344,bytes_of("dc7f680024d6690020d6690018d66900"),"retail font names"});
 c.guards.push_back({0x687fdc,bytes_of("6d656469756d00"),"medium name"});
 c.guards.push_back({0x69d624,bytes_of("62696700"),"big name"});
 c.guards.push_back({0x69d620,bytes_of("68756400"),"hud name"});
 c.guards.push_back({0x69d618,bytes_of("6469676974616c00"),"digital name"});
 c.guards.push_back({0x672562,bytes_of("ff2580f16700"),"retail case-insensitive comparison thunk"});
 // The glyph scale reads the engine's screen height at 0xa0a000: its store and its reader.
 c.guards.push_back({0x5fad5c,bytes_of("a300a0a000"),"screen height store"});
 c.guards.push_back({0x5fa066,bytes_of("f73d00a0a000"),"screen height reader"});
 for(const auto& s:sites) {
  auto original=bytes_of(s.hex);c.guards.push_back({s.va,original,"font site"});
  address value=0;bool jump=false;
  switch(s.type) {
   case kind::manager:value=t.manager;break;
   case kind::slots:value=t.manager+4;break;
   case kind::counter:value=t.manager+lc_fonts_rules::slots*lc_fonts_rules::stride+8;break;
   case kind::counter_offset:value=lc_fonts_rules::slots*lc_fonts_rules::stride+8;break;
   case kind::current:value=lc_fonts_rules::slots*lc_fonts_rules::stride+12;break;
   case kind::count:case kind::count8:value=lc_fonts_rules::slots;break;
   case kind::names:value=t.names;break;
   case kind::map_pc:value=t.map_pc;break;
   case kind::map_hd:value=t.map_hd;break;
   case kind::style:value=t.style;jump=true;break;
   case kind::render:value=t.render;jump=true;break;
   case kind::scale_draw:value=t.scale_draw;jump=true;break;
   case kind::scale_clip:value=t.scale_clip;jump=true;break;
   case kind::list_clip:value=t.list_clip;jump=true;break;
   case kind::list_draw:value=t.list_draw;jump=true;break;
   case kind::list_height:value=t.list_height;jump=true;break;
  }
  if(jump)c.writes.push_back(relocation(s.va,"e9",true,value,static_cast<unsigned>(original.size())-5,"font hook"));
  else {
   const unsigned n=s.type==kind::count8?1:4;
   std::vector<std::uint8_t> b;for(unsigned i=0;i<n;++i)b.push_back(static_cast<std::uint8_t>(value>>(8*i)));
   c.writes.push_back({s.va+s.offset,b,"font operand"});
  }
 }
 return c;
}
}
