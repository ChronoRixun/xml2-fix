#include "xml1_roll.hpp"
#include "xml1_economy.hpp"
#include "xml1_roll_rules.hpp"
#include "item_capacity_rules.hpp"
#include "item_capacity.hpp"
#include "log.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <xmmintrin.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace xml1_economy::roller {
namespace {
template<class T>T native(unsigned va){return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T read(const void* p,unsigned off=0){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>void put(void* p,unsigned off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T>T method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/4]);}
struct drop {descriptor item{};int amount=0;};
struct frame {void* stack=nullptr;std::array<drop,4> drops{};unsigned count=0;std::uint64_t serial=0;};
std::array<frame,4> frames{};
unsigned depth=0;
bool applied=false,overflow_logged=false;
std::map<unsigned,unsigned> calls;
std::mutex calls_mutex;
std::array<std::string,16> roll_history;
unsigned roll_count=0,roll_next=0;
// Game thread only. Allocate once at the entry bridge, never per pickup or
// history query, and never from RNG/time/addresses. Keep each frame's identity
// across nested rolls and native loop iterations. No resets at zone/seed loads.
std::uint64_t roll_serial=0;
void roll_log(std::uint64_t serial,const xml1_roll_rules::input& input,const xml1_roll_rules::result& result,const char* name,int amount){
 char line[512];std::snprintf(line,sizeof line,"roll n=%llu cat=%d lead=%d G=%d bucket=%d def=%s amt=%d draws=%u",static_cast<unsigned long long>(serial),input.category,input.leader,result.G,result.bucket,name,amount,result.draws);
 logger::write("%s",line);
 std::lock_guard lock(calls_mutex);
 roll_history[roll_next]=line;roll_next=(roll_next+1)%static_cast<unsigned>(roll_history.size());
 roll_count=std::min(roll_count+1,static_cast<unsigned>(roll_history.size()));
}
struct storage {void* p=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);~storage(){if(p)VirtualFree(p,0,MEM_RELEASE);}};
std::vector<std::shared_ptr<storage>> retained;
// The owned XBE CRT selects 53-bit x87 precision. PC D3D8 may leave 24-bit
// precision behind, which rounds the native RNG's highest states to 1.0 and
// can even make its integer helper return hi+1. Match XML1 during selection,
// then restore both caller control states before construction/native spawning.
struct selection_precision {
 unsigned short saved=0;
 unsigned sse=0;
 selection_precision(){
  unsigned short current=0;__asm fnstcw current
  saved=current;sse=_mm_getcsr();
  const unsigned short selected=static_cast<unsigned short>((current&~0x0f00u)|0x0200u);
  __asm fldcw selected
  _mm_setcsr(sse&~0x6000u);
 }
 ~selection_precision(){const unsigned short current=saved;__asm fldcw current
  _mm_setcsr(sse);
 }
};
struct services {
 void* game;
 // The native RNG leaves its product in ST0. A double-return declaration uses
 // the same ABI and preserves all 31 state bits when x87 precision permits it;
 // declaring float here would prematurely round gate/reservoir/money draws.
 double unit(){return native<double(__cdecl*)(char)>(0x560190)(1);}
 int integer(int lo,int hi){return native<int(__cdecl*)(int,int,char)>(0x5601e0)(lo,hi,1);}
 bool xtreme_full(){return method<int(__fastcall*)(void*,void*)>(game,0x194)(game,nullptr)>=method<int(__fastcall*)(void*,void*,int)>(game,0x19c)(game,nullptr,-1);}
 bool obtained(const xml1_economy_rules::record& r){
  auto* manager=native<void*(__cdecl*)()>(0x480a00)();
  auto* definition=static_cast<unsigned char*>(manager)+item_capacity_rules::definitions+r.definition*0x3c;
  // Definition vt+0x50 is unique; vt+0x54 is the obtained getter. vt+0x5c
  // sets obtained and must never be called during selection.
  return method<bool(__fastcall*)(void*,void*)>(definition,0x50)(definition,nullptr)&&
         method<bool(__fastcall*)(void*,void*)>(definition,0x54)(definition,nullptr);
 }
};
descriptor construct(const xml1_roll_rules::drop& d){
 auto* manager=native<void*(__cdecl*)()>(0x480a00)();descriptor out{};
 if(d.row&&(d.row->flags&0x40)){
  auto* definition=static_cast<unsigned char*>(manager)+item_capacity_rules::definitions+d.row->definition*0x3c;
  make_instance(manager,nullptr,&out,definition,1,0,0,0,0);
 }else{
  const auto index=method<std::uint16_t(__fastcall*)(void*,void*,const char*)>(manager,0x78)(manager,nullptr,d.name.c_str());
  if(index!=0xffff)method<descriptor*(__fastcall*)(void*,void*,descriptor*,unsigned)>(manager,0x12c)(manager,nullptr,&out,index);
 }
 return out;
}
void __fastcall special(void* game,void*,unsigned source){method<void(__fastcall*)(void*,void*,int,unsigned,int)>(game,0x25c)(game,nullptr,5,source,0);}
void __cdecl tripwire(void* stack,unsigned kind){
 if(!xml1_economy::installed())return;
 if(kind==3&&!is_economy(read<void*>(stack,4)))return;
 const auto caller=read<unsigned>(stack);std::lock_guard lock(calls_mutex);auto& n=calls[caller];
 if(n++==0)logger::write("economy: generator tripwire %08x (entry %08x)",caller,std::array<unsigned,4>{0x47d4b0,0x47ecb0,0x47b590,0x481b80}[kind]);
}
__declspec(naked) void bridge_begin(){__asm {
 pushfd
 pushad
 lea eax,[esp+36]
 push eax
 call begin
 add esp,4
 popad
 popfd
 push 046e747h
 ret
}}
__declspec(naked) void bridge_select(){__asm {
 pushfd
 pushad
 lea eax,[esp+36]
 push eax
 call select
 add esp,4
 popad
 popfd
 push 046e804h
 ret
}}
__declspec(naked) void bridge_finish(){__asm {
 pushfd
 pushad
 lea eax,[esp+36]
 push eax
 call finish
 add esp,4
 popad
 popfd
 mov ecx,[esp+60h]
 mov fs:[0],ecx
 push 046ea4eh
 ret
}}
struct site {unsigned va;const char* bytes;const char* what;};
constexpr std::array<site,12> sites{{
 {0x46e5e2,"85ed7e148b542474","roller selection and party-extra bypass"},
 {0x46e760,"8a44241384c0","roller descriptor selection"},
 {0x46ea43,"8b4c246064890d00000000","roller frame release and SEH restore"},
 {0x421f3f,"04","actor default category"},
 {0x469c80,"83ec145355","special treasure event"},
 {0x448dc0,"83ec10568bf1","level adjustment regeneration entry"},
 {0x4c908a,"895c2414895c2418","generated DR gear block only"},
 {0x47d4b0,"83ec34538a5c2444","generator tripwire"},
 {0x47ecb0,"83ec105355","special generator tripwire"},
 {0x47b590,"83ec205356","picker tripwire"},
 {0x481b80,"83ec2c5556","alternate initializer tripwire"},
 {0x46e735,"6a016a056a02","exclusive legacy leader count ownership"}
}};
}
void __cdecl begin(void* stack){
 put(stack,0x1c,0u);
 if(!xml1_economy::installed())return;
 const auto serial=++roll_serial;
 if(depth==frames.size()){if(!overflow_logged){overflow_logged=true;logger::write("economy: roller n=%llu nesting exceeds four; drop refused",static_cast<unsigned long long>(serial));}return;}
 auto& frame=frames[depth++];frame={};frame.stack=stack;frame.serial=serial;
 auto* game=read<void*>(stack,0x28);
 auto* registry=native<void*(__cdecl*)()>(0x44b8f0)();
 const int highest=native<int(__fastcall*)(void*,void*)>(0x449960)(registry,nullptr);
 xml1_roll_rules::input input;input.category=read<int>(stack,0x70);input.leader=read<unsigned char>(stack,0x13)!=0;input.highest_level=highest;
 const auto source=read<unsigned>(stack,0x74);auto copy=source;
 auto* entity=native<void*(__fastcall*)(void*,void*)>(0x4654b0)(&copy,nullptr);
 auto* actor=native<void*(__cdecl*)(void*)>(0x402b60)(entity);
 if(actor)input.actor_level=native<int(__fastcall*)(void*,void*)>(0x41cfc0)(actor,nullptr);
 services env{game};xml1_roll_rules::result result;
 {selection_precision precision;result=xml1_roll_rules::x1_roll(records(),input,env);}
 for(unsigned i=0;i<result.count;++i){const auto item=construct(result.drops[i]);if(item.pool)frame.drops[frame.count++]={item,result.drops[i].amount};
  roll_log(frame.serial,input,result,result.drops[i].name.c_str(),result.drops[i].amount);
 }
 if(!result.count)roll_log(frame.serial,input,result,"-",0);
 put(stack,0x1c,frame.count);
}
void __cdecl select(void* stack){
 if(!depth||frames[depth-1].stack!=stack)return;
 const auto i=read<unsigned>(stack,0x20);const auto& f=frames[depth-1];if(i>=f.count)return;
 put(stack,0x14,f.drops[i].item);put(stack,0x70,f.drops[i].amount);
}
void __cdecl finish(void* stack){if(depth&&frames[depth-1].stack==stack)frames[--depth]={};}
bool installed(){return applied;}
std::string status(){std::lock_guard lock(calls_mutex);unsigned total=0;for(const auto& [caller,n]:calls)total+=n;std::string s="; gen "+std::to_string(total);for(const auto& [caller,n]:calls){char text[48];std::snprintf(text,sizeof text,"; gen %08x:%u",caller,n);s+=text;}return s;}
std::string recent_rolls(){std::lock_guard lock(calls_mutex);std::string s;for(unsigned i=0;i<roll_count;++i){if(!s.empty())s+='\n';s+=roll_history[(roll_next+roll_history.size()-roll_count+i)%roll_history.size()];}return s;}
bool contribute(patch_transaction::transaction& tx,std::string& failure){
 patch_transaction::contribution c;c.owner="Xml1Economy roller, treasure, DR and regeneration";
 for(const auto& s:sites)c.guards.push_back({s.va,patch_transaction::bytes_of(s.bytes),s.what});
 c.guards.push_back({0x46e774,patch_transaction::bytes_of("8b176aff8bcfb301"),"exclusive legacy leader pick ownership"});
 if(!patch_transaction::verify_expectations(c,failure))return false;
 auto code=std::make_shared<storage>();if(!code->p){failure="roller tripwire allocation failed";return false;}
 auto jump=[&](unsigned va,const void* target,unsigned pad,const char* why){c.writes.push_back(patch_transaction::relocation(va,"e9",true,addr(target),pad,why));};
 jump(0x46e5e2,&bridge_begin,3,"XML1 roll selection");jump(0x46e760,&bridge_select,1,"XML1 per-entry descriptor");jump(0x46ea43,&bridge_finish,6,"release matching nested frame");
 c.writes.push_back({0x421f3f,{3},"XML1 category default"});jump(0x469c80,&special,0,"special event full XML1 roll");
 c.writes.push_back({0x448dc0,{0xc2,4,0,0x90,0x90,0x90},"preserve equipped gear before regeneration prologue"});
 c.writes.push_back(patch_transaction::relocation(0x4c908a,"e9",true,0x4c926b,3,"skip generated gear; preserve remaining DR rewards"));
 unsigned used=0;
 for(unsigned i=0;i<4;++i){const auto& s=sites[7+i];const auto original=patch_transaction::bytes_of(s.bytes);
  unsigned entry=s.va;
  if(i==2)if(auto* copy=item_capacity::prepared_definition_picker()){
   entry=addr(copy);
   c.guards.push_back({entry,original,"W2 widened definition picker copied entry"});
   c.requires_written.push_back({s.va,"W2 owns the retail picker redirect"});
  }
  std::vector<unsigned char> stub={0x9c,0x60,0x8d,0x44,0x24,0x24,0x6a,static_cast<unsigned char>(i),0x50};
  auto append=[&](const std::vector<unsigned char>& bytes){stub.insert(stub.end(),bytes.begin(),bytes.end());};
  append(patch_transaction::relocation(addr(code->p)+used+static_cast<unsigned>(stub.size()),"e8",true,addr(&tripwire),0,"tripwire callback").bytes);
  append({0x83,0xc4,8,0x61,0x9d});append(original);
  append(patch_transaction::relocation(addr(code->p)+used+static_cast<unsigned>(stub.size()),"e9",true,entry+static_cast<unsigned>(original.size()),0,"native tripwire continuation").bytes);
  std::memcpy(static_cast<unsigned char*>(code->p)+used,stub.data(),stub.size());jump(entry,static_cast<unsigned char*>(code->p)+used,static_cast<unsigned>(original.size())-5,s.what);used+=static_cast<unsigned>(stub.size());
 }
 DWORD old=0;if(!VirtualProtect(code->p,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code->p,used)){failure="roller tripwire protection/flush failed";return false;}
 c.publish=[code]{retained.push_back(code);applied=true;};c.retain=[code]{retained.push_back(code);applied=false;};c.abandon=[]{applied=false;};
 return tx.add(std::move(c));
}
}
