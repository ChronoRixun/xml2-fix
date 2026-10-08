#pragma once

#include "save_format_rules.hpp"
#include <cctype>
#include <string>

// Core save recording/lifetime rules. Guards' atomic/refusal policy stays in save_format_rules.hpp.
namespace save_core_rules
{
 using save_format_rules::address;
 constexpr std::uint32_t capture_incomplete=0x00000001;
 constexpr std::uint32_t capture_unobserved=0x00000002;
 constexpr bool ordinal_fits(std::uint32_t ordinal){ return ordinal>=1 && ordinal<=256; }
 constexpr bool capacity_fits(std::uint32_t demand,std::uint32_t live,std::uint32_t cap)
 { return live<=cap && demand<=cap-live; }
 inline std::string folded(std::string text)
 { for(auto& c:text) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a'); return text; }
 inline bool placed_match(std::uint32_t saved_index,const std::string& saved_def,const std::string& saved_name,
                          std::uint32_t actual_index,const std::string& actual_def,const std::string& actual_name)
 { return saved_index==actual_index && folded(saved_def)==folded(actual_def) && folded(saved_name)==folded(actual_name); }
 // Entity names use the GENERAL4096-slot table; zone record names use a separate2048-slot table.
 template<class Read> bool general_name(std::uint32_t handle,Read&& read,std::string& text)
 {
  text.clear();if(!handle)return true;
  const auto slot=handle&0x00ffffff;
  std::uint32_t used=0,offset=0;
  if(slot>=4096 || !read(0x4004,&used,4) || used>0x7800 || !read(4+slot*4,&offset,4) || offset>=used)return false;
  for(std::uint32_t i=0;i<256 && i<used-offset;++i)
  {
   char c=0;if(!read(0x4008+offset+i,&c,1))return false;
   if(!c)return true;text.push_back(c);
  }
  text.clear();return false;
 }
 constexpr bool full_campaign_load(int mode){return mode!=1;}
 template<class T> const T* placement_source(const std::vector<T>& entries,std::uint32_t index,
                                             const std::string& definition,const std::string& name)
 {
  for(const auto& entry:entries)
   if(placed_match(entry.placement_index,entry.definition_name,entry.instance_name,index,definition,name))return &entry;
  return nullptr;
 }
 struct lifetime
 {
  std::uint64_t serial=0;
  std::uint32_t sequence=0;
  bool load_pending=false,write_blocked=false;
  void new_game(){serial=0;sequence=0;load_pending=false;write_blocked=false;}
  void stage_load(){load_pending=true;}
  void finish_load(bool ok){load_pending=false;write_blocked=write_blocked||!ok;}
  void commit_save(std::uint64_t new_serial,std::uint32_t new_sequence){serial=new_serial;sequence=new_sequence;}
  bool begin_native(int mode,bool has_pending)
  {if(!full_campaign_load(mode))return false;if(!has_pending)new_game();load_pending=true;write_blocked=false;return true;}
  void finish_native(int mode,bool ok)
  {if(full_campaign_load(mode))finish_load(ok);else if(!ok)write_blocked=true;}
  bool may_save()const{return !load_pending && !write_blocked;}
 };

 // These guards are independent of the card/atomic wrapper so Core and Guards can merge independently.
 inline constexpr std::array<save_format_rules::guard,9> guards{{
  {0x41a427,"81f9003800007d1d","zone-string arena: nonzero handle can name unwritten bytes on exhaustion"},
  {0x48624c,"6689476e4085c9","numbering: store ordinal, increment, test cached record"},
  {0x486260,"e8abf9fdff","numbered restore call before next placement"},
  {0x4664d0,"83ec0c5355","capture entry: stack and saved ebx/ebp"},
  {0x5f2e70,"51e81a1be9ff","actual resetgame entry"},
  {0x466659,"81f900030000745b","captured world state count"},
  {0x46668a,"3d2c0100007415","dynamic count"},
  {0x4666e7,"81fe000300000f84b0000000","captured smart state count"},
  {0x4666f3,"81bd74520000000300000f84a0000000","smart list count"},
 }};
 inline constexpr save_format_rules::guard ordinal_write_guard{
  0x46667a,"66898c450a480000","ordinal writer has no native bound"};
 inline void append(std::vector<std::uint8_t>& out,const std::vector<std::uint8_t>& bytes)
 {out.insert(out.end(),bytes.begin(),bytes.end());}
 inline void invoke(std::vector<std::uint8_t>& out,address base,address hook)
 {append(out,save_format_rules::call(base+static_cast<address>(out.size()),hook,5));}
 inline void resume(std::vector<std::uint8_t>& out,address base,address target)
 {append(out,save_format_rules::jump(base+static_cast<address>(out.size()),target,5));}
 inline std::vector<std::uint8_t> numbering_bridge(address base,address hook)
 {
  // Original loop index [esp+0x20]; pushfd/pushad add 36 bytes, hence [esp+0x44].
  std::vector<std::uint8_t> out{0x9c,0x60,0x8b,0x54,0x24,0x44,0x50,0x52,0x57};
  invoke(out,base,hook);
  out.insert(out.end(),{0x83,0xc4,0x0c,0x61,0x9d,0x66,0x89,0x47,0x6e,0x40,0x85,0xc9});
  resume(out,base,0x486253);return out;
 }
 inline std::vector<std::uint8_t> reset_bridge(address base,address hook)
 {
  std::vector<std::uint8_t> out{0x9c,0x60};invoke(out,base,hook);
  out.insert(out.end(),{0x61,0x9d,0x51});invoke(out,base,0x484990);
  resume(out,base,0x5f2e76);return out;
 }
 inline std::vector<std::uint8_t> capture_trampoline(address base)
 {
  std::vector<std::uint8_t> out{0x83,0xec,0x0c,0x53,0x55};resume(out,base,0x4664d5);return out;
 }
 struct capture_bound
 {address va;const char* compare;std::size_t length;address skip;};
 inline constexpr std::array<capture_bound,5> capture_bounds{{
  {0x41a427,"81f900380000",8,0x41a44c},
  {0x466659,"81f900030000",8,0x4666bc},{0x46668a,"3d2c010000",7,0x4666a6},
  {0x4666e7,"81fe00030000",12,0x4667a3},{0x4666f3,"81bd7452000000030000",16,0x4667a3}
 }};
 inline std::vector<std::uint8_t> bound_bridge(const capture_bound& site,address base,address hook)
 {
  std::vector<std::uint8_t> out;
  const auto size=limits_rules::hex_size(site.compare);
  for(std::size_t i=0;i<size;++i)out.push_back(limits_rules::hex_byte(site.compare,i));
  const auto at=base+static_cast<address>(out.size());out.insert(out.end(),{0x0f,0x82});
  save_format_rules::put32(out,site.va+static_cast<address>(site.length)-(at+6)); // unsigned below -> retail fit
  out.insert(out.end(),{0x9c,0x60,0x55});invoke(out,base,hook);
  out.insert(out.end(),{0x83,0xc4,0x04,0x61,0x9d});resume(out,base,site.skip);return out;
 }
 inline std::vector<std::uint8_t> ordinal_write_bridge(address base,address hook)
 {
  // EAX is the positive ordinal. Native world-handle availability is an independent limit.
  std::vector<std::uint8_t> out{0x3d,0x00,0x01,0x00,0x00,0x0f,0x86};
  save_format_rules::put32(out,0); // patched to the replay below
  out.insert(out.end(),{0x9c,0x60,0x55});invoke(out,base,hook);
  out.insert(out.end(),{0x83,0xc4,0x04,0x61,0x9d});resume(out,base,0x4666a6);
  const auto replay=base+static_cast<address>(out.size());
  const auto delta=replay-(base+11);std::memcpy(out.data()+7,&delta,4);
  out.insert(out.end(),{0x66,0x89,0x8c,0x45,0x0a,0x48,0x00,0x00});resume(out,base,0x466682);return out;
 }
}
