// Union host: map the retail image at its own addresses inside a reserved
// fixture, commit the production step-3 contribution, execute retail callers.
// No game process or game DLL is loaded.
#include "save_manager.hpp"
#include "item_capacity.hpp"
#include "save_limits.hpp"
#include "save_manager_rules.hpp"
#include "loader_placements_rules.hpp"
#include "save_core_rules.hpp"
#include "smart_entities.hpp"
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>
extern "C" std::uint8_t fixture_space[0x700000];
namespace ini { bool flag(const wchar_t*,const wchar_t*,bool){return false;} }
namespace save_format {
 std::uint32_t strings=0, refusals=0;
 void publish_zone_strings(std::uint32_t a){strings=a;}
 void latch_reconstruction_failure(const char*){++refusals;}
}
namespace logger { std::string last; void write(const char* fmt,...) {
 char buffer[512];va_list args;va_start(args,fmt);std::vsnprintf(buffer,sizeof buffer,fmt,args);va_end(args);last=buffer;
} }
namespace {
 int checks=0,failures=0;
 void check(bool v,const std::string& s){++checks;failures+=!v;std::printf("%s %s\n",v?"PASS":"FAIL",s.c_str());}
 void* at(std::uint32_t v){return reinterpret_cast<void*>(static_cast<std::uintptr_t>(v));}
 std::uint32_t addr(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
 template<class T>T read(const void* p,unsigned off=0){T v;std::memcpy(&v,static_cast<const std::uint8_t*>(p)+off,sizeof v);return v;}
 template<class T>void put(void* p,unsigned off,T v){std::memcpy(static_cast<std::uint8_t*>(p)+off,&v,sizeof v);}
 template<class T>T native(std::uint32_t v){return reinterpret_cast<T>(at(v));}
 void jump(std::uint32_t va,const void* target){auto w=patch_transaction::relocation(va,"e9",true,addr(target),0,"test external dependency");std::memcpy(at(va),w.bytes.data(),w.bytes.size());}
 std::vector<std::uint8_t> binary;
 bool load_image(const char* path) {
  std::ifstream f(path,std::ios::binary);binary.assign(std::istreambuf_iterator<char>(f),{});
  if(binary.size()<0x1000)return false;
  const auto pe=read<std::uint32_t>(binary.data(),0x3c);
  const auto sections=pe+24+read<std::uint16_t>(binary.data(),pe+20);
  std::memset(fixture_space,0,sizeof fixture_space);
  for(unsigned i=0;i<read<std::uint16_t>(binary.data(),pe+6);++i) {
   const auto row=sections+i*40,va=0x400000+read<std::uint32_t>(binary.data(),row+12),size=read<std::uint32_t>(binary.data(),row+16),off=read<std::uint32_t>(binary.data(),row+20);
   if(off+std::uint64_t(size)>binary.size() || va<addr(fixture_space) || va+std::uint64_t(size)>addr(fixture_space)+sizeof fixture_space)return false;
   std::memcpy(at(va),binary.data()+off,size);
  }
  return true;
 }

 void __cdecl capture_overflow(void*) {}
 void add_capture_bridges(patch_transaction::contribution& c) {
  static auto* memory=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
  const auto block=save_core_rules::capture_bridges(addr(memory),addr(&capture_overflow),save_format_rules::w2_profile);
  std::memcpy(memory,block.bytes.data(),block.bytes.size());
  for(const auto& g:save_core_rules::guards)for(const auto& j:block.jumps)if(j.va==g.va)c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),g.what});
  const auto& g=save_core_rules::ordinal_write_guard;c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),g.what});
  for(const auto& j:block.jumps)c.writes.push_back(patch_transaction::relocation(j.va,"e9",true,j.target,static_cast<unsigned>(j.length)-5,"composed production save-core bound bridge"));
 }
 void generator_tests() {
  using namespace save_format_rules;
  unsigned rows=0;bool same=true;
  for(const auto& s:entity_capacity_sites::literal_sites)if(s.owner=="save") {
   ++rows;same&=save_manager_rules::literal(s,e10a2_profile)==patch_transaction::bytes_of(s.new_hex);
   if(s.va==0x48733d)check(save_manager_rules::literal(s,w2_profile)==patch_transaction::bytes_of("6890211200"),"W2 prepared manager allocation is 0x122190");
   if(s.va==0x4669b0)check(save_manager_rules::literal(s,w2_profile)==patch_transaction::bytes_of("8d9608870400"),"W2 eviction record 1 timestamp is 0x48708");
   // The serializer copies the widened side-mission block (0x46bcea/0x46bcfa: 0xa88 bytes); its cursor advance shares
   // this instruction with the payload operand and must move over the same 0xa88 bytes, as the loader does (0x46e423).
   if(s.va==0x46bd01)check(save_manager_rules::literal(s,w2_profile)==patch_transaction::bytes_of("818300002000880a0000"),"W2 serializer advances the stream cursor over the whole 0xa88-byte side-mission block");
   if(s.va==0x46bcca)check(save_manager_rules::literal(s,w2_profile)==patch_transaction::bytes_of("818300002000dc000000"),"W2 game block cursor advance stays 0xdc");
   if(s.va==0x4666bd)check(save_manager_rules::literal(s,w2_profile)==patch_transaction::bytes_of("81fb00040000"),"W2 scans exactly 1024 World handles");
  }
  check(rows==178&&same,"T1 all 178 regenerated E10A2 save rows equal reviewed Phase A bytes");
  check(save_manager_rules::party_names(e10a2_profile)==patch_transaction::bytes_of("8d8417540a0800")&&save_manager_rules::party_names(w2_profile)==patch_transaction::bytes_of("8d8417ec1c1200"),"T1 extra 0x484c76 regenerated for both layouts");
 }
 std::array<bool,1024> allocated;
 std::vector<unsigned> released;
 bool __fastcall slot_lookup(void*,void*,unsigned slot,void*) {
  released.push_back(slot);if(slot<allocated.size())allocated[slot]=false;
  return false; // only the slot lookup/release observation; no inventory body needed
 }
 void* manager_vt[73]{};void** manager=manager_vt;
 void* __cdecl item_manager(){return &manager;}
 void* __fastcall component(void* v,void*,int i){return static_cast<std::uint8_t*>(v)+i*4;}
 void install_external_fakes() {
  manager_vt[0x120/4]=reinterpret_cast<void*>(&slot_lookup);jump(0x480a00,&item_manager);
  put(at(0x67f9c4),0,addr(&component));
 }
 void positions() {
  std::array<std::uint8_t,40> record;record.fill(0xa5);
  const std::array<std::array<std::uint32_t,3>,3> cases{{{{0x3f000000,0x471c4000,0xc71c4000}},{{0x80000000,0x7fc12345,0x00000001}},{{0x3f800000,0xbf800000,0x7f800000}}}};
  bool exact=true;
  for(auto input:cases) {
   auto before=record;std::array<std::uint32_t,3> output{};
   exact&=native<bool(__fastcall*)(void*,void*,void*)>(0x4656c0)(record.data(),nullptr,input.data());
   exact&=native<void*(__fastcall*)(void*,void*,void*)>(0x4654f0)(record.data(),nullptr,output.data())==output.data();
   exact&=input==output&&!std::memcmp(record.data()+0x14,input.data(),12);
   exact&=!std::memcmp(before.data(),record.data(),0x14)&&!std::memcmp(before.data()+32,record.data()+32,8);
  }
  check(exact,"T9 float positions 0.5, +/-40000, signed zero, subnormal and NaN payload are bit-exact; slot/canaries preserved");
  auto before=record;check(native<bool(__fastcall*)(void*,void*,void*)>(0x4656c0)(record.data(),nullptr,nullptr)&&record==before,"null position input preserves retail no-op success");
 }
 void disposal() {
  using namespace save_format_rules;
  std::vector<std::uint8_t> zone(w2_profile.record_size+32,0xa5);
  const unsigned owned[]={0,255,300,1023};
  put(zone.data(),w2_profile.state_count,4095u);
  for(unsigned i=0;i<4095;++i) {
   auto* r=zone.data()+4+i*32;r[0x10]=r[0x11]=0;
   put(r,0x12,std::uint16_t(0xffff));
  }
  for(unsigned i=0;i<4;++i) {
   auto* r=zone.data()+4+i*32;r[0x10]=r[0x11]=1;
   put(r,0x12,std::uint16_t(owned[i]));r[8]=7; // poisoned payload byte cannot supply the slot
  }
  auto* r=zone.data()+4+4*32;r[0x10]=r[0x11]=1; // non-owning FFFF
  r+=32;r[0x10]=0;r[0x11]=1;put(r,0x12,std::uint16_t(500));
  r+=32;r[0x10]=1;r[0x11]=0;put(r,0x12,std::uint16_t(501));
  allocated.fill(true);released.clear();
  native<void(__fastcall*)(void*,void*,bool)>(0x465ec0)(zone.data(),nullptr,true);
  check(released==std::vector<unsigned>(std::begin(owned),std::end(owned)),"T9 native disposal chain releases exactly slots 0,255,300,1023 (both flags set), ignores FFFF and mismatched flags");
  bool untouched=true;for(unsigned i=0;i<1024;++i)untouched&=allocated[i]==(std::find(std::begin(owned),std::end(owned),i)==std::end(owned));
  check(untouched,"disposal leaves every other world slot allocated");
  bool fields=true;for(unsigned i=0;i<4095;++i) {
   r=zone.data()+4+i*32;fields&=read<std::uint16_t>(r,0x12)==0xffff;
   fields&=read<std::uint32_t>(r,0x14)==0&&read<std::uint32_t>(r,0x18)==0&&read<std::uint32_t>(r,0x1c)==0&&r[8]==0xff&&r[0x10]==0&&r[0x11]==0;
  }
  check(fields,"T9 4095 states clear with non-owning u16 sentinel and zero float fields");
  bool map=true;for(unsigned i=0;i<4096;++i)map&=read<std::uint16_t>(zone.data(),w2_profile.ordinal_map+i*2)==0;
  check(map&&read<unsigned>(zone.data(),w2_profile.state_count)==0&&read<unsigned>(zone.data(),dynamic_count(w2_profile))==0&&read<unsigned>(zone.data(),smart_count(w2_profile))==0,"T9 all 4096 ordinal entries and list counts clear");
  check(std::all_of(zone.end()-32,zone.end(),[](auto b){return b==0xa5;}),"zone clear preserves canary past 0x24388");
  put(zone.data(),w2_profile.state_count,1u);r=zone.data()+4;r[0x10]=r[0x11]=1;put(r,0x12,std::uint16_t(300));released.clear();
  native<void(__fastcall*)(void*,void*,bool)>(0x465ec0)(zone.data(),nullptr,false);
  check(released.empty()&&read<std::uint16_t>(r,0x12)==0xffff,"non-disposing reset never releases a slot and writes FFFF");
 }

 void* definitions_vt[9]{};void** definitions=definitions_vt;
 void* __cdecl definitions_get(){return &definitions;}
 void* __fastcall find_definition(void*,void*,const char*){return nullptr;}
 unsigned cookie_calls=0;
 void __fastcall check_cookie(unsigned,void*){++cookie_calls;}
 void full_restore() {
  using namespace save_format_rules;
  jump(0x4625c0,&definitions_get);definitions_vt[8]=reinterpret_cast<void*>(&find_definition);jump(0x672161,&check_cookie);
  std::vector<std::uint8_t> zone(w2_profile.record_size,0);
  put(zone.data(),smart_count(w2_profile),2048u);
  std::vector<std::array<std::uint8_t,32>> want;
  for(unsigned i=0;i<2048;++i) {
   const auto index=i==2047?4095:i+1;
   put(zone.data(),smart_list(w2_profile)+i*2,static_cast<std::uint16_t>(index));
   auto* r=zone.data()+4+index*32;
   put(r,8,0x12340000u+i);put(r,0x12,static_cast<std::uint16_t>(i%1024));
   put(r,0x14,0x3f000000u+i);put(r,0x18,0xc71c4000u);put(r,0x1c,0x80000000u);
   std::array<std::uint8_t,32> w;std::memcpy(w.data(),r,32);want.push_back(w);
  }
  const auto before=zone;
  native<void(__fastcall*)(void*,void*)>(0x4667e0)(zone.data(),nullptr);
  auto* object=static_cast<std::uint8_t*>(smart_entities::object());
  bool exact=read<unsigned>(object,0x10108)==2048&&cookie_calls==1&&zone==before;
  for(unsigned i=0;i<2048;++i)exact&=!std::memcmp(object+8+i*32,want[i].data(),32);
  check(exact,"T9 full native restore of 2048 smart entries including state index 4095; u16 slot and float bits copied; source unchanged; native cookie epilogue runs");
 }

 std::uint8_t world_entities[1024][0x80]{};std::uint8_t class_bits[0x30]{};
 void* entity_vt[4]{};void* world_vt[10]{};void** world_object=world_vt;
 void* fog_vt[3]{};void** fog_object=fog_vt;std::uint8_t fog_bytes[0x34c]{};
 void* conversation_vt[19]{};void** conversation_object=conversation_vt;std::uint32_t conversation_bytes[5]{};
 void* __cdecl world_get(){return &world_object;}
 void __fastcall world_end(void*,void*,unsigned* end){*end=0xffffffffu;}
 void __fastcall world_at(void*,void*,unsigned* value,unsigned index){*value=index+1;}
 void* __fastcall entity_at(unsigned* value,void*) {return world_entities[*value-1];}
 void* __fastcall entity_class(void*,void*) {return class_bits;}
 bool __fastcall entity_capture(std::uint8_t* entity,void*,void* dst) {
  const auto index=static_cast<unsigned>((entity-world_entities[0])/0x80);
  std::array<std::uint8_t,32> record{};put(record.data(),8,index);put(record.data(),0x12,static_cast<std::uint16_t>(index));
  put(record.data(),0x14,0x3f000000u);put(record.data(),0x18,0x471c4000u);put(record.data(),0x1c,0xc71c4000u);
  std::memcpy(dst,record.data(),32);return true;
 }
 void* __cdecl fog_get(){return &fog_object;}
 void* __fastcall fog_data(void*,void*){return fog_bytes;}
 void* __cdecl conversations_get(){return &conversation_object;}
 void* __fastcall conversations_data(void*,void*){return conversation_bytes;}
 void* __cdecl not_inventory(void*){return nullptr;}
 unsigned __cdecl not_filtered(void*){return 0;}
 void full_capture() {
  using namespace save_format_rules;
  jump(0x4c7f20,&world_get);world_vt[0x24/4]=reinterpret_cast<void*>(&world_end);world_vt[0x18/4]=reinterpret_cast<void*>(&world_at);
  jump(0x4654b0,&entity_at);entity_vt[0]=reinterpret_cast<void*>(&entity_class);entity_vt[3]=reinterpret_cast<void*>(&entity_capture);
  jump(0x402b60,&not_inventory);jump(0x4500f0,&not_filtered);put(at(0x718448),0,0u);put(class_bits,0x18,0x10u);
  for(unsigned i=0;i<1024;++i) {put(world_entities[i],0,addr(entity_vt));put(world_entities[i],0x14,1u);world_entities[i][0x6c]=0x20;put(world_entities[i],0x6e,std::uint16_t(i==1023?4096:0));}
  jump(0x596890,&fog_get);fog_vt[2]=reinterpret_cast<void*>(&fog_data);
  jump(0x4583f0,&conversations_get);conversation_vt[0x48/4]=reinterpret_cast<void*>(&conversations_data);
  std::vector<std::uint8_t> zone(w2_profile.record_size+32,0);
  std::fill(zone.end()-32,zone.end(),std::uint8_t{0xa5});
  const auto ok=native<bool(__fastcall*)(void*,void*,const char*,unsigned,unsigned)>(0x4664d0)(zone.data(),nullptr,"",1,0x3f000000);
  check(ok&&read<unsigned>(zone.data(),w2_profile.state_count)==3073&&read<unsigned>(zone.data(),smart_count(w2_profile))==2048&&read<unsigned>(zone.data(),dynamic_count(w2_profile))==1023,
        "T9 full native capture: all 1024 World handles plus 2048 smart records, 3073 states including sentinel");
  bool exact=read<std::uint16_t>(zone.data(),w2_profile.ordinal_map+4095*2)==1024;
  auto* smart=static_cast<std::uint8_t*>(smart_entities::object());
  for(unsigned i=0;i<2048;++i)exact&=read<std::uint16_t>(zone.data(),smart_list(w2_profile)+i*2)==1025+i&&!std::memcmp(zone.data()+4+(1025+i)*32,smart+8+i*32,32);
  check(exact&&std::all_of(zone.end()-32,zone.end(),[](auto b){return b==0xa5;}),"full native capture writes ordinal 4096 and complete 32-byte smart records without crossing the zone backing");
 }

 void* clock_vt[0x164/4]{};void** clock_object=clock_vt;
 void* save_vt[0xa8/4]{};void** save_object=save_vt;
 void* __cdecl clock_get(){return &clock_object;}
 void* __cdecl save_get(){return &save_object;}
 float __fastcall clock_time(void*,void*){return 1.0f;}
 unsigned __fastcall save_token(void*,void*){return 0;}
 int __fastcall no_zone(void*,void*,const char*){return -1;}
 std::uint8_t* eviction_manager=nullptr;std::vector<unsigned> captured;unsigned snapshots=0;
 bool __fastcall capture_zone(std::uint8_t* zone,void*,const char*,unsigned,unsigned) {
  captured.push_back(static_cast<unsigned>((zone-eviction_manager)/save_format_rules::w2_profile.record_size));
  put(zone,save_format_rules::w2_profile.time_stamp,static_cast<float>(captured.size()));return true;
 }
 void __fastcall capture_snapshot(void* snapshot,void*,unsigned) {
  if(snapshot==eviction_manager+0x121c48)++snapshots;
 }
 void eviction() {
  using namespace save_format_rules;
  // External clock, snapshot and capture are observations. LRU selection,
  // clear/disposal, string GC, the 8-record loop and manager writes are retail.
  jump(0x465d80,&no_zone);jump(0x46dce0,&clock_get);jump(0x484990,&save_get);
  clock_vt[0x160/4]=reinterpret_cast<void*>(&clock_time);save_vt[0xa4/4]=reinterpret_cast<void*>(&save_token);
  jump(0x4664d0,&capture_zone);jump(0x48a170,&capture_snapshot);
  std::vector<std::uint8_t> manager_bytes(w2_profile.manager_size+32,0);
  std::fill(manager_bytes.end()-32,manager_bytes.end(),std::uint8_t{0xa5});eviction_manager=manager_bytes.data();
  using take_t=bool(__fastcall*)(void*,void*,const char*);
  auto take=native<take_t>(0x466960);bool ok=true;
  for(unsigned i=0;i<8;++i) {const auto name="zone"+std::to_string(i);ok&=take(eviction_manager,nullptr,name.c_str());}
  check(ok&&captured==std::vector<unsigned>({0,1,2,3,4,5,6,7})&&read<unsigned>(eviction_manager,0x121c44)==8,"T9 retail capture manager admits eight native records in order");
  auto* zone=eviction_manager+3*w2_profile.record_size;
  put(zone,w2_profile.time_stamp,-3.0f);put(zone,w2_profile.state_count,1u);
  auto* r=zone+4;r[0x10]=r[0x11]=1;put(r,0x12,std::uint16_t(1023));released.clear();
  ok=take(eviction_manager,nullptr,"zone8");
  check(ok&&captured.back()==3&&released==std::vector<unsigned>({1023})&&read<unsigned>(eviction_manager,0x121c44)==8&&read<std::uint16_t>(r,0x12)==0xffff,"T9 ninth zone evicts LRU record 3 through native comparison loop and releases its u16 slot exactly once");
  check(snapshots==9&&std::all_of(manager_bytes.end()-32,manager_bytes.end(),[](auto b){return b==0xa5;}),"W2 manager snapshot address and 0x122190 backing canary survive eight captures plus eviction");
 }

 struct node {const char* name;std::vector<node*> children;};
 node root{"world",{}},group{"entinst",{}};std::vector<node> instances;
 void* __fastcall next_child(void* n,void*,void* previous) {
  auto& children=static_cast<node*>(n)->children;
  if(!previous)return children.empty()?nullptr:children[0];
  const auto found=std::find(children.begin(),children.end(),previous);
  return found!=children.end()&&found+1!=children.end()?*(found+1):nullptr;
 }
 bool __fastcall name_is(void* n,void*,const char* text){return std::strcmp(static_cast<const char*>(n),text)==0;}
 void tree(unsigned count) {
  instances.assign(count,{"inst",{}});group.children.clear();root.children={&group};
  for(auto& n:instances)group.children.push_back(&n);
 }
 unsigned parser_calls=0,reader_destructs=0;
 void __fastcall parser(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*){++parser_calls;}
 void __fastcall reader_destruct(void*,void*){++reader_destructs;}
 void* __cdecl game_get(){return nullptr;}
 std::uint32_t call_saved=0,call_before=0,call_after=0;bool loader_ok=false;
 std::uint8_t loader_stack[0x20000]{};
 std::uint8_t zone_object[0x240]{};void* parser_vt[3]{};
 void loader_done();
 __declspec(naked) void loader_success() {__asm {
mov loader_ok,1
mov call_after,esp
jmp loader_done
}}
 __declspec(naked) void loader_failure() {__asm {
mov loader_ok,0
mov call_after,esp
jmp loader_done
}}
 // Shared exit is reached by a jump; restore the harness's own stack.
 __declspec(naked) void loader_done() {__asm {
mov esp,call_saved
pop edi
pop esi
pop ebx
pop ebp
ret
}}
 __declspec(naked) void call_loader() {
  __asm {
   push ebp
   push ebx
   push esi
   push edi
   mov call_saved,esp
   mov esp,offset loader_stack
   add esp,20000h
   sub esp,0f500h
   mov ebp,offset zone_object
   mov ebx,offset parser_vt
   mov edi,0
   mov ecx,0
   sub esp,24h
   mov eax,offset root
   mov [esp+4],eax
   mov call_before,esp
   push 485724h
   ret
  }
 }
 void loader_tests() {
  jump(0x564a20,&next_child);jump(0x5649a0,&name_is);jump(0x564d00,&reader_destruct);jump(0x46dce0,&game_get);
  jump(0x48572c,&loader_success);jump(0x4854bb,&loader_failure);parser_vt[2]=reinterpret_cast<void*>(&parser);
  std::strcpy(reinterpret_cast<char*>(zone_object)+0x1e0,"synthetic-1025-zone");
  tree(1024);parser_calls=reader_destructs=0;call_loader();
  check(loader_ok&&parser_calls==1&&reader_destructs==0&&call_after==call_before+36,"T9 loader accepts 1024 placements, calls native parser, preserves stack cleanup");
  tree(1025);parser_calls=reader_destructs=0;const auto before=save_format::refusals;call_loader();
  check(!loader_ok&&parser_calls==0&&reader_destructs==1&&call_after==call_before+36&&save_format::refusals==before+1,"T9 1025-placement synthetic zone refused before parser; reader destroyed; native failure cleanup selected");
  check(logger::last.find("loader placements limit (1024)")!=std::string::npos&&logger::last.find("synthetic-1025-zone")!=std::string::npos,"runtime placement refusal names the limit and zone");
 }
}
LONG WINAPI diagnostic(EXCEPTION_POINTERS* e) {
 std::printf("FAULT code=%08lx EIP=%08lx ESP=%08lx EAX=%08lx EBX=%08lx EBP=%08lx\n",e->ExceptionRecord->ExceptionCode,e->ContextRecord->Eip,e->ContextRecord->Esp,e->ContextRecord->Eax,e->ContextRecord->Ebx,e->ContextRecord->Ebp);
 return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char** argv) {
 AddVectoredExceptionHandler(1,&diagnostic);
 std::setvbuf(stdout,nullptr,_IONBF,0);
 generator_tests();
 const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");
 if(!path){std::puts("SKIP retail union host: supply XMen2.exe");return failures?1:0;}
 check(addr(fixture_space)==0x401000,"fixed-base union fixture starts at 0x401000");
 DWORD old=0;check(!!VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old),"union image pages writable/executable");
 if(!load_image(path)){check(false,"read-only retail PE mapping");return 1;}
 check(true,"read-only retail PE mapped; game never launched");
 const auto stock=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);
 patch_transaction::contribution c;std::string why;
 check((save_manager::contribute_w2(c,why)&&item_capacity::contribute_w2(c,why)&&save_limits::contribute_w2(c,why)),"build all step-3 contributions: "+why);
 if(c.writes.empty())return 1;
 check(std::equal(stock.begin(),stock.end(),fixture_space)&&save_format::strings==0,"T2 preparing W2 changes zero retail bytes and publishes nothing");
 check(patch_transaction::verify_expectations(c,why),"all W2 row guard bytes and region hashes match retail: "+why);
 // Refusal must leave every image byte, including unrelated pages, unchanged.
 *static_cast<std::uint8_t*>(at(0x465f88))^=1;
 const auto mutated=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);
 patch_transaction::transaction bad;bad.add(c);const auto refused=bad.commit();
 check(refused==patch_transaction::outcome::refused_region&&std::equal(mutated.begin(),mutated.end(),fixture_space)&&save_format::strings==0,"T3 one region-tail byte mutated: entire union refused, zero writes/publication");
 c={};check(load_image(path),"restore retail image for loader frame mutation");
 check((save_manager::contribute_w2(c,why)&&item_capacity::contribute_w2(c,why)&&save_limits::contribute_w2(c,why)),"build union for independent loader dependency mutation");
 *static_cast<std::uint8_t*>(at(0x484cf0))^=1;
 const auto changed_frame=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);
 patch_transaction::transaction bad_frame;bad_frame.add(c);
 check(bad_frame.commit()==patch_transaction::outcome::refused_region&&std::equal(changed_frame.begin(),changed_frame.end(),fixture_space)&&save_format::strings==0,
       "T3 loader frame byte outside hook mutated: entire union refused, zero writes/publication");
 // The failed transaction abandoned its executable storage; rebuild it.
 c={};check(load_image(path),"restore pristine retail image after mutation");
 check((save_manager::contribute_w2(c,why)&&item_capacity::contribute_w2(c,why)&&save_limits::contribute_w2(c,why)),"rebuild step-3 union after refusal: "+why);
 add_capture_bridges(c);
 if(argc>2) {
  std::set<unsigned> pages;for(const auto& w:c.writes)for(unsigned i=0;i<w.bytes.size();++i)pages.insert((w.va+i)&~0xfffu);
  std::ofstream out(std::string(argv[2])+"-pages.txt");for(auto page:pages)out<<std::hex<<page<<'\n';
 }
 patch_transaction::transaction tx;tx.add(std::move(c));const auto result=tx.commit();
 check(result==patch_transaction::outcome::committed,"W2 combined contribution is conflict-free and commits: "+tx.report());
 if(result!=patch_transaction::outcome::committed)return 1;
 check(save_format::strings!=0,"zone-string backing published only after verified union commit");
 {
  // The stream must carry the W2 blocks at the sizes lcsave's W2 layout reads and writes (limits-wave2.md 3.4):
  // CMap's [MAP: MISSION STATE] snapshot write/read and the serializer's side-mission cursor advance.
  auto installed=[](unsigned va,const char* hex){const auto b=patch_transaction::bytes_of(hex);return std::memcmp(at(va),b.data(),b.size())==0;};
  check(save_format_rules::w2_profile.snapshot_size==0x540&&installed(0x484b49,"6840050000")&&installed(0x484c20,"6840050000"),
        "W2 CMap writes and reads the whole 0x540-byte mission snapshot (0x484b49 / 0x484c20 push 0x540)");
  check(installed(0x46bd01,"818300002000880a0000")&&installed(0x46bcfa,"b9a2020000")&&installed(0x46e423,"05880a0000"),
        "W2 serializer and loader advance the stream over the same 0xa88-byte side-mission block it copies");
 }
 if(argc>2) {
  const std::string prefix=argv[2];
  std::ofstream text(prefix+"-text.bin",std::ios::binary);text.write(static_cast<const char*>(at(0x401000)),0x27d7b5);
  const auto gc=0x466025+read<std::uint32_t>(at(0x466021));
  std::ofstream clone(prefix+"-gc.bin",std::ios::binary);clone.write(static_cast<const char*>(at(gc)),0x47f);
 }

 install_external_fakes();positions();disposal();full_restore();full_capture();eviction();loader_tests();
 std::printf("Save manager union: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
}
