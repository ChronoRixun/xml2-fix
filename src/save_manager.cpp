#include "save_manager.hpp"
#include "save_manager_rules.hpp"
#include "entity_capacity_rules.hpp"
#include "loader_placements_rules.hpp"
#include "state_records.hpp"
#include "smart_entities.hpp"
#include "zone_strings.hpp"
#include "save_format.hpp"
#include "log.hpp"
#include <utility>

namespace save_manager {
namespace {
using namespace save_format_rules;
template<class T> T native(std::uint32_t va) {return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
std::uint32_t address(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
template<class T> T read(const void* p,unsigned off){T v;std::memcpy(&v,static_cast<const std::uint8_t*>(p)+off,sizeof v);return v;}
void __fastcall clear_zone(std::uint8_t* zone,void*,bool dispose) {
 const auto count=read<std::uint32_t>(zone,w2_profile.state_count);
 for(std::uint32_t i=0;i<count;++i) {
  auto* r=zone+4+i*w2_profile.state_record_size;
  if(dispose && (r[0x11]&1))native<void(__cdecl*)(void*)>(0x479c10)(r);
  // Native clear preserves payload bytes 0xb..0xf, clears flags and name,
  // and writes the non-owning slot independently of the payload sentinel.
  std::memset(r,0,8);r[8]=0xff;r[9]=r[10]=r[0x10]=r[0x11]=0;
  const std::uint16_t none=0xffff;std::memcpy(r+0x12,&none,2);std::memset(r+0x14,0,12);
 }
 std::memset(zone,0,4);std::memset(zone+w2_profile.state_count,0,4);
 std::memset(zone+w2_profile.ordinal_map,0,w2_profile.ordinals*2);
 std::memset(zone+dynamic_count(w2_profile),0,4);
 std::memset(zone+smart_count(w2_profile),0,4);
 std::memset(zone+save_manager_rules::fog(w2_profile),0,0x34c+20);
 std::memset(zone+w2_profile.time_stamp,0,4);
}
// The vector API returns component addresses at vt import 0x67f9c4. Raw
// dword copies avoid x87 precision, rounding, NaN and signed-zero changes.
void* __fastcall position_read(const std::uint8_t* r,void*,void* output) {
 using component_t=void*(__fastcall*)(void*,void*,int);
 const auto component=*native<component_t*>(0x67f9c4);
 for(int i=0;i<3;++i)std::memcpy(component(output,nullptr,i),r+0x14+i*4,4);
 return output;
}
bool __fastcall position_pack(std::uint8_t* r,void*,void* input) {
 if(input) {
  using component_t=void*(__fastcall*)(void*,void*,int);
  const auto component=*native<component_t*>(0x67f9c4);
  for(int i=0;i<3;++i)std::memcpy(r+0x14+i*4,component(input,nullptr,i),4);
 }
 return true;
}
// Preserve the native payload cursor start/end side effects, but pass the
// record's own u16 to the disposal chain instead of consuming payload byte 0.
void __cdecl dispose_record(std::uint8_t* r) {
 if(!(r[0x10]&1))return;
 native<bool(__fastcall*)(void*,void*)>(0x465570)(r,nullptr);
 const auto slot=read<std::uint16_t>(r,0x12);
 native<void(__fastcall*)(void*,void*)>(0x465620)(r,nullptr);
 if(slot!=0xffff)native<void(__cdecl*)(std::uint32_t)>(0x479ad0)(slot);
}
bool __cdecl preflight(void* root,const char* zone) {
 using child_t=void*(__fastcall*)(void*,void*,void*);
 using same_t=bool(__fastcall*)(void*,void*,const char*);
 auto child=native<child_t>(0x564a20);auto same=native<same_t>(0x5649a0);
 std::uint32_t count=0;
 const bool ok=loader_placements_rules::fits(root,
  [child](void* n){return child(n,nullptr,nullptr);},
  [child](void* n,void* p){return child(n,nullptr,p);},
  [same](void* n){return same(*static_cast<void**>(n),nullptr,"inst");},count);
 if(!ok) {
  logger::write("W2 zone load refused: %.64s exceeds loader placements limit (1024); observed %lu",zone,static_cast<unsigned long>(count));
  save_format::latch_reconstruction_failure("zone exceeds loader placements limit (1024)");
 }
 return ok;
}
// 0x485724 has nine native arguments on the stack; root is argument 2.
// Refusal destroys the XML reader, then uses the native array-cleanup path.
__declspec(naked) void placement_preflight() {
 __asm {
  pushfd
  pushad
  lea eax,[ebp+1e0h]
  push eax
  push dword ptr [esp+2ch]
  call preflight
  add esp,8
  test al,al
  jz refused
  popad
  popfd
  call dword ptr [ebx+8]
  mov eax,46dce0h
  call eax
  push 48572ch
  ret
 refused:
  popad
  popfd
  add esp,24h
  lea ecx,[esp+30h]
  mov eax,564d00h
  call eax
  mov byte ptr [esp+0f3f0h],9
  mov edi,0ffffffffh
  mov ebx,7
  push 4854bbh
  ret
 }
}
void append(patch_transaction::contribution& dst,patch_transaction::contribution part) {
 dst.guards.insert(dst.guards.end(),part.guards.begin(),part.guards.end());
 dst.regions.insert(dst.regions.end(),part.regions.begin(),part.regions.end());
 dst.writes.insert(dst.writes.end(),part.writes.begin(),part.writes.end());
 dst.requires_written.insert(dst.requires_written.end(),part.requires_written.begin(),part.requires_written.end());
 auto join=[](auto a,auto b){return [a=std::move(a),b=std::move(b)]{if(a)a();if(b)b();};};
 dst.publish=join(std::move(dst.publish),std::move(part.publish));
 dst.abandon=join(std::move(dst.abandon),std::move(part.abandon));
 dst.retain=join(std::move(dst.retain),std::move(part.retain));
}
}
bool contribute_w2(patch_transaction::contribution& owner,std::string& why) try {
 patch_transaction::contribution c; c.owner="save manager W2";
 using namespace patch_transaction;
 for(const auto& s:entity_capacity_sites::literal_sites)if(s.owner=="save") {
  c.guards.push_back({s.va,bytes_of(s.old_hex),"Phase A save literal"});
  if(!save_manager_rules::replaced_body(s.va))c.writes.push_back({s.va,save_manager_rules::literal(s,w2_profile),"regenerated W2 save operand"});
 }
 const auto& extra=entity_capacity_rules::extra_literals[0];
 c.guards.push_back({extra.va,bytes_of(extra.old_hex),"manager party names"});
 c.writes.push_back({extra.va,save_manager_rules::party_names(w2_profile),"W2 manager party names"});
 struct small {std::uint32_t va;const char* old;const char* wide;};
 for(const auto& s:std::initializer_list<small>{{0x46699c,"83f805","83f808"},{0x487461,"6a05","6a08"},{0x487685,"6a05","6a08"},{0x479aed,"0fb64c2410","0fb74c2410"}}) {
  c.guards.push_back({s.va,bytes_of(s.old),"W2 zone count / disposal width"});
  c.writes.push_back({s.va,bytes_of(s.wide),"W2 zone count / disposal width"});
 }
 struct function {std::uint32_t lo,hi;const char* entry;const char* hash;const void* target;const char* what;};
 for(const auto& f:std::initializer_list<function>{
  {0x465ec0,0x465f89,"53558be98b","b3c41d7981d1df45238dbda3548115e2f86c65c84cf2f7a664ab5c626f23e5d2",&clear_zone,"W2 zone record clear"},
  {0x4654f0,0x465568,"83ec10568b","2f8704422265ac027542a0901d0b9e1081adbf36259b12a3c898572c437d7671",&position_read,"W2 float position read"},
  {0x4656c0,0x4657a3,"568b742408","1768073f7c22c3d19f8139507141810205ff6f9d68c84076413aa050af0a3a0b",&position_pack,"W2 float position pack"},
  {0x479c10,0x479c46,"568b742408","386b208768f3d2f6524c458fd3c01779483601182d21a2097244942777e2ee20",&dispose_record,"W2 disposal adapter"},
  {0x485724,0x48572c,"ff5308e8b485feff","9a3363ddcddcb99fc5c58c23959cdfea8bfeefeb41516ce27fe979bdfb926321",&placement_preflight,"W2 placement preflight before parser"}}) {
  c.guards.push_back({f.lo,bytes_of(f.entry),f.what});
  c.regions.push_back({f.what,f.lo,f.hi,sha256_of(f.hash)});
  c.writes.push_back(relocation(f.lo,"e9",true,address(f.target),0,f.what));
 }
 // The preflight also depends on CMap's pending arguments, EH state and
 // native cleanup frame, well beyond the eight overwritten hook bytes.
 c.regions.push_back({"CMap loader frame and refusal cleanup",0x484ce0,0x486353,
  sha256_of("a470ed0141aa06011b8ecf549fbf7c46f9d0f20345b6d64f62e50670793dd460")});
 if(!verify_expectations(c,why))return false;
 patch_transaction::contribution part;
 if(!state_records::contribute_w2(part,why))return false;append(c,std::move(part));part={};
 if(!smart_entities::contribute_w2(part,why))return false;append(c,std::move(part));part={};
 if(!zone_strings::contribute_w2(part,why))return false;append(c,std::move(part));
 // Replace any Phase A writes already in the owner (including the byte at
 // clear entry+4); every other contributor retains its original guards.
 owner.writes.erase(std::remove_if(owner.writes.begin(),owner.writes.end(),[](const auto& w){
  for(const auto& s:entity_capacity_sites::literal_sites)if(s.owner=="save" && w.va==s.va)return true;
  return w.va==0x484c76;
 }),owner.writes.end());
 append(owner,std::move(c));why.clear();return true;
} catch(const std::exception& e){why=std::string("W2 save manager: ")+e.what();return false;}
}
