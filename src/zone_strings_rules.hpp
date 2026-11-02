#pragma once

// W2-E15-14a..k. Retail XMen2.exe SHA-1 7d95cdb4a9a599b982147a3389ea7aff211cb82b.
// Pure layouts, instruction ledger and bounded intern; no Windows or live process dependency.
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace zone_strings_rules
{
 struct layout
 {
  std::uint32_t slots, arena;
  constexpr std::uint32_t used_offset() const { return 4 + slots * 4; }
  constexpr std::uint32_t arena_offset() const { return used_offset() + 4; }
  constexpr std::uint32_t size() const { return arena_offset() + arena; }
 };
 inline constexpr layout retail{0x800,0x3800}, w2{0x2000,0x20000};
 static_assert(retail.size()==0x5808 && w2.size()==0x28008);
 struct site { std::uint32_t va; const char* retail; const char* w2; const char* row; };
 inline constexpr site in_place[]{
  {0x419d70,"81e3ff070000","81e3ff1f0000","14b"},
  {0x41a3fb,"25ff070000","25ff1f0000","14b"},
  {0x465816,"25ff070000","25ff1f0000","14b"},
  {0x46582d,"81e2ff070000","81e2ff1f0000","14b"},
  {0x4658d1,"25ff070000","25ff1f0000","14b"},
  {0x4658df,"81e6ff070000","81e6ff1f0000","14b"},
  {0x46568d,"b900080000","b900200000","14c"},
  {0x465fed,"b900080000","b900200000","14c"},
  {0x4661d8,"b900080000","b900200000","14c"},
  {0x466394,"b900080000","b900200000","14c"},
  {0x6021b9,"b900080000","b900200000","14c"},
  {0x466258,"81fe00080000","81fe00200000","14c"},
  {0x41a414,"8b8304200000","8b8304800000","14e"},
  {0x41a41e,"8b8304200000","8b8304800000","14e"},
  {0x41a446,"01ab04200000","01ab04800000","14e"},
  {0x4656a7,"c7820420000002000000","c7820480000002000000","14e"},
  {0x465ac5,"39ae04200000","39ae04800000","14e"},
  {0x465b2f,"8b9604200000","8b9604800000","14e"},
  {0x465b47,"8b8e04200000","8b8e04800000","14e"},
  {0x465bb5,"8b8e04200000","8b8e04800000","14e"},
  {0x465bc9,"898e04200000","898e04800000","14e"},
  {0x46600a,"c7820420000002000000","c7820480000002000000","14e"},
  {0x4661f4,"c7820420000002000000","c7820480000002000000","14e"},
  {0x419d62,"8db40608200000","8db40608800000","14f"},
  {0x41a42f,"8dbc1808200000","8dbc1808800000","14f"},
  {0x46569b,"888208200000","888208800000","14f"},
  {0x4657d7,"8dbc0b08200000","8dbc0b08800000","14f"},
  {0x465882,"8d8c2b08200000","8d8c2b08800000","14f"},
  {0x4658aa,"8db42b08200000","8db42b08800000","14f"},
  {0x4658f2,"8dac2808200000","8dac2808800000","14f"},
  {0x465af0,"80bc3e0820000000","80bc3e0880000000","14f"},
  {0x465bfd,"8d840108200000","8d840108800000","14f"},
  {0x465cc3,"8dac0208200000","8dac0208800000","14f"},
  {0x465ce6,"8d840108200000","8d840108800000","14f"},
  {0x465dd4,"8d840108200000","8d840108800000","14f"},
  {0x465ffe,"889a08200000","889a08800000","14f"},
  {0x4661e8,"888208200000","888208800000","14f"},
  {0x466234,"8a940108200000","8a940108800000","14f"},
  {0x46623d,"8d8c0108200000","8d8c0108800000","14f"},
  {0x4668b0,"8d840108200000","8d840108800000","14f"},
  {0x4656a1,"888209200000","888209800000","14f"},
  {0x466004,"889a09200000","889a09800000","14f"},
  {0x4661ee,"888209200000","888209800000","14f"},
  {0x484ac1,"6808580000","6808800200","14h"},
  {0x484ba6,"6808580000","6808800200","14h"},
 };
 inline constexpr std::uint32_t compact_lo=0x466020, compact_hi=0x46649f;
 inline constexpr site compact_edits[]{
  {0x466020,"81ec10010000","81ec10040000","14j"},
  {0x4661ff,"81c410010000","81c410040000","14j"},
  {0x466496,"81c410010000","81c410040000","14j"},
  {0x46602e,"b940000000","b900010000","14j"},
  {0x466052,"3b842424010000","3b842424040000","14j"},
  {0x466292,"3b842424010000","3b842424040000","14j"},
  {0x4663b7,"3b842424010000","3b842424040000","14j"},
  {0x4660ee,"83c718","83c720","14j"},
  {0x4662fd,"83c618","83c620","14j"},
  {0x466415,"83c618","83c620","14j"},
  {0x4661b7,"83f840","80fc01","14j"},
 };
 // All relative branches stay inside the copy; only these 32 rel32 calls leave it.
 inline constexpr std::uint32_t compact_calls[]{0x466062,0x46609a,0x4660c5,0x466114,0x466130,0x46613f,0x466167,0x46617a,0x4661bc,0x466227,0x466268,0x46626f,0x4662a1,0x4662ce,0x4662e6,0x466331,0x466346,0x466357,0x466370,0x46638a,0x4663c8,0x4663cf,0x4663ef,0x4663f6,0x466405,0x46640c,0x466441,0x466457,0x46645e,0x466465,0x46647f,0x466486};
 struct region {std::uint32_t lo,hi; const char* hash; const char* name; const char* entry;};
 inline constexpr region regions[]{
  {0x419d40,0x419d94,"c51a28f0f9e0a5a0c77771248f3a47ea5eb38421afe9cf2726795900f3ac27f3","lookup","8b54240453"},
  {0x41a3c0,0x41a45a,"3e47aedf4b6bb5f85f3492c07244150fedfd660db5b38796cc80422c784f5bff","add-name","53558b6c24"},
  {0x466020,0x46649f,"111358f2b673a7634a260da885676a9b00d1910854a87a1f78d916b51edbf866","compaction","81ec100100"},
  {0x6021a0,0x6021f4,"087dd1a1b2c9ccc88b42a2cb32198e5c77d4b4a5a7fa8bb26ac1a924884dcac4","getter","f60538c4a2"},
 };

 enum class refusal { none, slot_limit, arena_limit, invalid_name };
 inline const char* name(refusal r)
 {
  switch(r) {case refusal::none:return "none";case refusal::slot_limit:return "zone-string slot limit (8191 non-empty names)";
   case refusal::arena_limit:return "zone-string arena limit (0x20000 bytes)";default:return "invalid zone-string name";}
 }
 inline std::uint32_t read32(const std::uint8_t* p) {std::uint32_t v;std::memcpy(&v,p,4);return v;}
 inline void write32(std::uint8_t* p,std::uint32_t v) {std::memcpy(p,&v,4);}
 inline void initialize(std::uint8_t* object,layout p)
 {
  write32(object,0);std::memset(object+4,0,p.slots*4);write32(object+4,1);
  write32(object+p.used_offset(),2);object[p.arena_offset()]=object[p.arena_offset()+1]=0;
 }
 inline std::uint32_t hash(const char* text,std::uint32_t length)
 {
  std::uint32_t h=0;
  for(std::uint32_t i=0;i<length;++i)h^=static_cast<std::uint8_t>(text[i])*(0x77u+i);
  return (h>>10)^h;
 }
 struct probe_result {std::uint32_t slot,tries;bool found,available;};
 inline probe_result probe(const std::uint8_t* object,layout p,const char* text,std::uint32_t length,std::uint32_t first)
 {
  auto slot=first&(p.slots-1);
  for(std::uint32_t tries=1;tries<=p.slots;++tries,slot=(slot+1)&(p.slots-1))
  {
   const auto offset=read32(object+4+slot*4);
   if(!offset)return {slot,tries,false,true};
   // The retail comparison includes the supplied NUL. Valid stored names always lie in the arena.
   if(offset<p.arena && length<=p.arena-offset && !std::memcmp(object+p.arena_offset()+offset,text,length))return {slot,tries,true,true};
  }
  return {0,p.slots,false,false};
 }
 struct add_result {std::uint32_t slot;refusal error;std::uint32_t tries;};
 inline add_result add(std::uint8_t* object,layout p,const char* text,std::uint32_t length)
 {
  if(!text || !length || text[length-1]!=0)return {0,refusal::invalid_name,0};
  const auto q=probe(object,p,text,length,hash(text,length));
  if(q.found)return {q.slot,refusal::none,q.tries};
  if(!q.available)return {0,refusal::slot_limit,q.tries};
  const auto used=read32(object+p.used_offset());
  // Keep the retail strict bound (used + length < arena), without addition overflow.
  if(used>=p.arena || length>=p.arena-used)return {0,refusal::arena_limit,q.tries};
  std::memcpy(object+p.arena_offset()+used,text,length);
  write32(object+4+q.slot*4,used);write32(object+p.used_offset(),used+length);
  return {q.slot,refusal::none,q.tries};
 }
 inline std::uint32_t handle(const std::uint8_t* object,std::uint32_t slot) {return (read32(object)<<24)|slot;}
}
