// Owned mapped-image fixture: retail instructions executed without a game.
#include "save_limits.hpp"
#include "save_limits_rules.hpp"
#include "save_limits_sites.hpp"
#include "objective_text.hpp"
#include "objective_text_rules.hpp"
#include "save_guard_rules.hpp"
#include "forced_teams_rules.hpp"
#include "test_state_rules.hpp"
#include "ini.hpp"
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>
extern "C" std::uint8_t fixture_space[0x700000];
namespace ini { bool descriptions=true; bool flag(const wchar_t*,const wchar_t*,bool){return descriptions;} }
namespace logger {void write(const char*,...){} }
namespace save_format {unsigned refusals=0;void latch_reconstruction_failure(const char*){++refusals;} }
namespace {
using namespace save_limits_sites;
int checks=0,failures=0;
void check(bool v,const std::string& s){++checks;failures+=!v;std::printf("%s %s\n",v?"PASS":"FAIL",s.c_str());}
unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
void* at(unsigned a){return reinterpret_cast<void*>(static_cast<std::uintptr_t>(a));}
template<class T>T native(unsigned a){return reinterpret_cast<T>(at(a));}
template<class T>T read(const void* p,unsigned o=0){T v;std::memcpy(&v,static_cast<const std::uint8_t*>(p)+o,sizeof v);return v;}
template<class T>void put(void* p,unsigned o,T v){std::memcpy(static_cast<std::uint8_t*>(p)+o,&v,sizeof v);}
void jump(unsigned a,const void* p){const auto w=patch_transaction::relocation(a,"e9",true,addr(p),0,"external fixture");std::memcpy(at(a),w.bytes.data(),w.bytes.size());}
bool load_image(const char* path){
 std::ifstream file(path,std::ios::binary);std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(file),{});if(bytes.size()<0x1000)return false;
 const auto pe=read<unsigned>(bytes.data(),0x3c),table=pe+24+read<std::uint16_t>(bytes.data(),pe+20);
 std::memset(fixture_space,0,sizeof fixture_space);
 for(unsigned i=0;i<read<std::uint16_t>(bytes.data(),pe+6);++i){const auto row=table+40*i,va=0x400000+read<unsigned>(bytes.data(),row+12),size=read<unsigned>(bytes.data(),row+16),off=read<unsigned>(bytes.data(),row+20);if(off+std::uint64_t(size)>bytes.size()||va<addr(fixture_space)||va+std::uint64_t(size)>addr(fixture_space)+sizeof fixture_space)return false;std::memcpy(at(va),bytes.data()+off,size);}
 return true;
}
__declspec(naked) void cookie(){__asm {ret}}
char* __cdecl copy_string(char* dst,const char* src,unsigned n){return std::strncpy(dst,src,n);}
unsigned __fastcall stats_index(void*,void*,unsigned){return 21;}
unsigned fake_vt[16]{};unsigned* fake=fake_vt;
void* __cdecl stats(){return &fake;}
void externals(){jump(0x672170,&copy_string);jump(0x672161,&cookie);fake_vt[0x3c/4]=addr(&stats_index);jump(0x44b8f0,&stats);}
unsigned random_state=0x5b71a304;
unsigned random(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
struct sample {unsigned n;std::vector<std::uint8_t> records;std::vector<unsigned> permutation;};
std::vector<sample> samples;
void retail_sorts(){
 externals();
 for(unsigned run=0;run<2500;++run){sample s;s.n=run%76;s.records.resize(s.n*0x1ac);
  for(unsigned i=0;i<s.n;++i){auto* r=s.records.data()+i*0x1ac;std::snprintf(reinterpret_cast<char*>(r),32,"objective-%03u",i);r[0x1a8]=static_cast<std::uint8_t>(run%4==0?i:run%4==1?127-i:run%4==2?7:random());r[0x1a9]=static_cast<std::uint8_t>(i);r[0x1aa]=static_cast<std::uint8_t>(random()&0x7f);put(r,0x1a0,i);}
  std::vector<std::uint8_t> pool(0x7d74);std::memcpy(pool.data(),s.records.data(),s.records.size());put(pool.data(),0x7d70,s.n);
  native<void(__fastcall*)(void*,void*)>(0x489680)(pool.data(),nullptr);
  for(unsigned i=0;i<s.n;++i)s.permutation.push_back(read<unsigned>(pool.data(),i*0x1ac+0x1a0));samples.push_back(std::move(s));
 }
 check(samples.size()==2500,"T10 2500 retail sort reference permutations (0..75, ascending/descending/equal/signed-random priorities)");
}
void register_index(void* record,unsigned value){
 // Execute the actual shared allocation-site cave, terminating immediately at
 // its continuation so this test need not parse a content file.
 const auto fn=at(0x489a01);
 __asm {
  mov eax,record
  mov edx,value
  call fn
 }
}
void* pool(){return at(save_layout_view::objectives().pool_address);}
struct memory {bool read(unsigned a,void* out,std::size_t n){return patch_transaction::read_memory(a,out,n);}};
bool inaccessible(const void* p){MEMORY_BASIC_INFORMATION info{};return VirtualQuery(p,&info,sizeof info)&&(info.State==MEM_RESERVE||info.Protect==PAGE_NOACCESS);}
__declspec(naked) bool __cdecl registrable(unsigned){
 __asm {
  mov ecx,[esp+4]
  sub esp,28
  mov [esp+18h],ecx
  mov eax,4885c0h
  call eax
  setb al
  movzx eax,al
  add esp,28
  ret
 }
}
bool ui_end(unsigned site,unsigned count){const auto fn=at(site);auto* p=pool();unsigned char equal=0;__asm {
 mov eax,p
 mov ecx,count
 mov edi,ecx
 call fn
 sete equal
}return equal!=0;}
void objective_tests(){
 const auto manager=native<void*(__cdecl*)()>(0x48a0e0)();auto* p=static_cast<std::uint8_t*>(pool());
 check(manager&&addr(p)==addr(manager)+8&&native<void*(__cdecl*)()>(0x48a0e0)()==manager,"T10 DLL manager getter publishes pool at +8 and is idempotent");
 *native<std::uint8_t*>(0x489a07)=0xc3;
 bool good=true;
 for(unsigned i=0;i<127;++i){auto* r=static_cast<std::uint8_t*>(native<void*(__fastcall*)(void*,void*)>(0x489620)(p,nullptr));good&=r==p+i*0x1ac&&save_layout_view::objectives().wide_index[i]==0xffff;r[0x1aa]=0x65;register_index(r,300+i);good&=r[0x1a9]==std::uint8_t(300+i)&&r[0x1aa]==0x65&&save_layout_view::objectives().wide_index[i]==300+i;}
 check(good&&read<unsigned>(p,objective_count)==127,"T10 allocate/register 127 objectives above 255, low byte and live +0x1aa preserved");
 bool ui=true;for(const auto site:{0x5cce8cu,0x5ccf4cu,0x5cd512u,0x5ce138u}){*native<std::uint8_t*>(site+6)=0xc3;ui&=ui_end(site,127)&&!ui_end(site,126);}
 check(ui,"T10 all four external journal/menu readers compare the relocated live count127");
 check(!native<void*(__fastcall*)(void*,void*)>(0x489620)(p,nullptr)&&read<unsigned>(p,objective_count)==127&&save_format::refusals==1,"T10 128th loaded objective refused without pool/count overflow");
 auto* states=native<std::uint8_t*>(save_layout_view::objectives().state_bytes);
 states[300]=0x40;states[301]=0x80;states[1023]=0x71;
 check(native<unsigned char(__fastcall*)(void*,void*)>(0x488ab0)(p,nullptr)==1&&native<unsigned char(__fastcall*)(void*,void*)>(0x488ab0)(p+0x1ac,nullptr)==0,"T10 retail completion readers use wide index 300/301");
 bool same=true,wide_ok=true;
 for(const auto& s:samples){std::memcpy(p,s.records.data(),s.records.size());put(p,objective_count,s.n);for(unsigned i=0;i<s.n;++i)register_index(p+i*0x1ac,700+i);
  native<void(__fastcall*)(void*,void*)>(0x489680)(p,nullptr);
  for(unsigned i=0;i<s.n;++i){const auto id=read<unsigned>(p,i*0x1ac+0x1a0);same&=id==s.permutation[i];wide_ok&=save_layout_view::objectives().wide_index[i]==700+id&&p[i*0x1ac+0x1a9]==std::uint8_t(700+id);}
 }
 check(same,"T10 2500 DLL heapsorts produce identical retail permutation including ties and signed priorities");
 check(wide_ok,"T10 wide indices above255 follow every record across heap-build/root-last/selected-child swaps");
 for(unsigned i=0;i<127;++i){auto* r=p+i*0x1ac;std::memset(r,0,0x1ac);r[0x1a8]=std::uint8_t(126-i);put(r,0x1a0,i);register_index(r,897+i);}
 put(p,objective_count,127u);native<void(__fastcall*)(void*,void*)>(0x489680)(p,nullptr);
 bool full=true;for(unsigned i=0;i<127;++i)full&=save_layout_view::objectives().wide_index[i]==1023-i;
 check(full,"T10 full127 record sort includes objective1023 and carries every wide side entry");
 register_index(p,1023);register_index(p+0x1ac,300);p[0x1aa]=0x35;(p+0x1ac)[0x1aa]=0x80;
 native<void*(__fastcall*)(void*,void*,void*)>(0x488430)(p+0x1ac,nullptr,p);
 check(save_layout_view::objectives().wide_index[1]==1023&&(p+0x1ac)[0x1aa]==0xb5,"T10 direct 0x488430 copy carries side entry and preserves retail destination flag bit7");
 native<void(__fastcall*)(void*,void*)>(0x488c20)(p,nullptr);bool empty=true;for(unsigned i=0;i<127;++i)empty&=save_layout_view::objectives().wide_index[i]==0xffff;
 check(empty&&read<unsigned>(p,objective_bitmap+12)==0,"T10 release clears four bitmap words and all side entries to FFFF");
 put(p,objective_count,0u);auto* reused=native<void*(__fastcall*)(void*,void*)>(0x489620)(p,nullptr);
 check(reused==p&&save_layout_view::objectives().wide_index[0]==0xffff,"T10 released slot reuses with invalid wide index until registration");
 for(unsigned i=0;i<1024;++i)states[i]=std::uint8_t(i*19);
 std::array<std::uint8_t,0x540+16> snapshot;snapshot.fill(0xa5);
 native<bool(__fastcall*)(void*,void*,void*)>(0x489a80)(manager,nullptr,snapshot.data());
 check(!std::memcmp(snapshot.data()+0x125,states,1024)&&snapshot[0x525]==0xa5&&snapshot.back()==0xa5,"T10 capture copies exactly 1024 objective bytes and preserves padding/canary");
 std::memset(states,0,1024);native<bool(__fastcall*)(void*,void*,void*)>(0x4892e0)(manager,nullptr,snapshot.data());
 bool restored=true;for(auto i:{255,256,300,1023})restored&=states[i]==std::uint8_t(i*19);
 check(restored,"T10 objective states 255/256/300/1023 survive native snapshot restore");
 register_index(p,300);states[300]=0x40;
 const auto attribute=0x489a2f+5+read<unsigned>(at(0x489a30));
 native<void(__fastcall*)(void*,void*,const char*,const char*)>(attribute)(p,nullptr,"updatedescription","wide completion");
 *native<std::uint8_t*>(0x5cd5ed)=0xc3;const auto journal=at(0x5cd5e7);const char* selected=nullptr;
 __asm {
  mov esi,p
  call journal
  mov selected,edx
 }
 check(selected&&std::strcmp(selected,"wide completion")==0,"T10 actual objective_text attribute and journal hooks show description300");
 put(at(0x729960),0,0x686e1cu);put(at(0x72a578),0,0x68878cu);put(p,objective_count,1u);p[0x1aa]=4;
 memory mem;test_state_rules::reader<memory> reader(mem);const auto observed=reader.read(0);
 check(observed.objectives&&observed.objectives->size()==1&&observed.objectives->front().complete,"T10 test_state reads relocated live count and wide completion state");
 check(save_layout_view::objectives().count_offset==8+objective_count&&inaccessible(static_cast<std::uint8_t*>(manager)+objective_manager_size)&&inaccessible(states+1024)&&inaccessible(save_layout_view::objectives().wide_index+127),"GP relocated manager/state/wide-index end guards and descriptor count offset");
 *native<std::uint8_t*>(0x4885c8)=0xc3;
 check(registrable(1023)&&!registrable(1024),"T10 actual global registrar admits index1023 and refuses1024");
}
void* __fastcall vector_zero(void* p,void*){std::memset(p,0,12);return p;}
void* __fastcall vector_assign(void* p,void*,const void* src){std::memcpy(p,src,12);return p;}
float zero_vector[3]{};
unsigned game_vt[130]{},zone_vt[24]{};unsigned* game=game_vt;unsigned* zone=zone_vt;
void* __cdecl game_get(){return &game;}
void* __cdecl zone_get(){return &zone;}
const char* __fastcall zone_name(void*,void*){return "synthetic-zone";}
void __fastcall no_action(void*,void*){}
int __cdecl as_int(const char*){return 0;}
unsigned frame_sequence=0;
void __fastcall capture_snapshot(void* snapshot,void*,unsigned){
 std::memset(snapshot,0,0x540);std::strcpy(static_cast<char*>(snapshot),"synthetic-zone");
 std::snprintf(static_cast<char*>(snapshot)+0xa4,32,"frame-%u",frame_sequence++);
 native<bool(__fastcall*)(void*,void*,void*)>(0x489a80)(nullptr,nullptr,snapshot);
 const unsigned vector[6]={0x3f000000,0x471c4000,0xc71c4000,0x80000000,0x3f800000,0x3fc00000};std::memcpy(static_cast<std::uint8_t*>(snapshot)+0x528,vector,24);
}
void side_tests(){
 put(at(0x67fa60),0,addr(&vector_zero));put(at(0x67fa2c),0,addr(&vector_assign));put(at(0x67fa54),0,addr(zero_vector));
 std::array<std::uint8_t,0x550> constructor;constructor.fill(0xa5);native<void*(__fastcall*)(void*,void*)>(0x4874b0)(constructor.data(),nullptr);
 check(constructor[0x524]==0&&constructor[0x525]==0xa5&&read<unsigned>(constructor.data(),0x528)==0&&constructor[0x540]==0xa5,"T10 native snapshot constructor clears1024 bytes, relocates vectors and preserves end canary");
 jump(0x67233c,&as_int);jump(0x484990,&zone_get);jump(0x46dce0,&game_get);jump(0x422520,&no_action);
 zone_vt[0x5c/4]=addr(&zone_name);game_vt[0x200/4]=addr(&no_action);
 jump(0x48a170,&capture_snapshot);
 auto* side=static_cast<std::uint8_t*>(native<void*(__cdecl*)()>(0x48a070)());
 auto* state=native<std::uint8_t*>(save_layout_view::objectives().state_bytes);
 const char* args[]={"0"};
 for(unsigned f=0;f<2;++f){for(unsigned i=0;i<1024;++i)state[i]=std::uint8_t(i+f*23);native<void(__cdecl*)(const char**)>(0x5f3630)(args);}
 bool frames=read<unsigned>(side,0xa84)==2;
 for(unsigned f=0;f<2;++f){auto* frame=side+f*0x540;for(auto i:{255,256,300,1023})frames&=frame[0x125+i]==std::uint8_t(i+f*23);frames&=read<unsigned>(frame,0x528)==0x3f000000&&read<unsigned>(frame,0x530)==0xc71c4000;}
 check(frames,"T10 native push with enlarged stack frames preserves two W2 snapshots, wide states and float bits");
 native<void(__cdecl*)(const char**)>(0x5f3630)(args);check(read<unsigned>(side,0xa84)==2,"T10 third side-mission push refuses at two frames");
 std::array<std::uint8_t,0x550> copied;copied.fill(0xa5);
 native<void*(__fastcall*)(void*,void*,void*)>(0x5f4400)(copied.data(),nullptr,side+0x540);
 bool copy=true;for(auto i:{255,256,300,1023})copy&=copied[0x125+i]==std::uint8_t(i+23);
 check(copy&&read<unsigned>(copied.data(),0x528)==0x3f000000&&copied[0x540]==0xa5,"T10 side snapshot copy helper carries1024 states and six relocated vector components");
 std::memset(state,0,1024);native<void(__cdecl*)()>(0x5f46f0)();bool last=true;for(auto i:{255,256,300,1023})last&=state[i]==std::uint8_t(i+23);
 check(last&&read<unsigned>(side,0xa84)==2,"T10 native top-frame restore executes enlarged pop frame and copies wide objective states");
 native<void(__cdecl*)()>(0x5f2ce0)();check(read<unsigned>(side,0xa84)==1,"T10 cancelsidemission decrements W2 count");
 native<void(__cdecl*)()>(0x48a130)();check(read<unsigned>(side,0xa84)==0,"T10 manager pop-all uses W2 count");
 check(inaccessible(side+0xa88),"GP side stack has a guard page immediately after two frames/pad/count");
}
void file_store(unsigned position,unsigned index){const auto fn=at(0x489252);__asm {
 mov edx,position
 mov ebx,index
 call fn
}}
unsigned file_read(unsigned position){const auto fn=at(0x489bdf);unsigned value=0;__asm {
 mov ebx,position
 call fn
 mov value,edx
}return value;}
bool file_available(unsigned count){const auto fn=at(0x48908a);const auto cell=read<unsigned>(at(0x48908c));put(at(cell),0,count);unsigned char available=0;__asm {
 call fn
 setne available
}return available!=0;}
void files_tests(){
 *native<std::uint8_t*>(0x489258)=0xc3;*native<std::uint8_t*>(0x489be6)=0xc3;*native<std::uint8_t*>(0x489091)=0xc3;
 bool all=true;for(unsigned i=0;i<64;++i)file_store(i,960+i);for(unsigned i=0;i<64;++i)all&=file_read(i)==960+i;
 check(all,"T10 64 file base entries store/read u16 indices960..1023 through actual retail sites");
 check(file_available(63)&&!file_available(64),"T10 file registrar admits64th file and refuses65th at native compare");
}
unsigned cap_observed=0,base_observed=0;void* ecx_observed=nullptr;
bool __fastcall cap_target(void* self,void*,void*,unsigned base,unsigned cap,unsigned,unsigned){ecx_observed=self;base_observed=base;cap_observed=cap;return true;}
bool passed_caps=false;
const char* __fastcall attr_name(void*,void*,const char*){return "synthetic-talent";}
unsigned __fastcall no_existing(void*,void*,const char*,unsigned){return 0x3fffffff;}
__declspec(naked) void caps_passed(){__asm {
 mov passed_caps,1
 push 4c011eh
 ret
}}
__declspec(naked) void endpoint_done(){__asm {
 pop eax
 mov base_observed,eax
 pop eax
 mov cap_observed,eax
 ret
}}
// 0x4be010 / 0x4be9c0 are ratl pool<16-byte, 100>::pop / clear, shared by
// every such pool. W2 a27495f widened both bodies in place, so the foreign
// owners below popped and cleared 256 entries at the talent offsets, writing
// up to 0xc38 bytes past their own 0x7f8-byte pool (three run as CRT static
// constructors at process start). Only the talent edges may reach 256 copies.
std::vector<std::uint8_t> retail_pool_pop,retail_pool_clear;
unsigned edge_target(unsigned call){return call+5+read<unsigned>(at(call+1));}
bool retail_pool_semantics(unsigned pop,unsigned clear){
 constexpr unsigned size=0x7f8; // ring +0x644, head +0x7d8, tail +0x7dc, count +0x7e0, buckets +0x7e4, live +0x7f4
 auto* m=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 std::memset(m,0xa5,0x3000);std::memset(m,0,size);
 native<void(__fastcall*)(void*,void*)>(clear)(m,nullptr);
 bool ok=read<unsigned>(m,0x7d8)==0&&read<unsigned>(m,0x7dc)==0&&read<unsigned>(m,0x7e0)==100;
 for(unsigned i=0;i<100;++i)ok&=read<unsigned>(m,0x644+4*i)==i;
 std::set<unsigned> ids;for(unsigned i=0;i<100;++i)ids.insert(native<unsigned(__fastcall*)(void*,void*)>(pop)(m,nullptr));
 ok&=ids.size()==100&&*ids.rbegin()==99&&read<unsigned>(m,0x7dc)==0&&read<unsigned>(m,0x7e0)==0&&read<unsigned>(m,0x7f4)==100;
 for(unsigned i=0;i<3;++i)ok&=read<unsigned>(m,0x7e4+4*i)==0xffffffff;
 ok&=read<unsigned>(m,0x7f0)==0xf;
 for(unsigned i=size;i<0x3000;++i)ok&=m[i]==0xa5;
 VirtualFree(m,0,MEM_RELEASE);return ok;
}
void shared_pool_tests(){
 check(!std::memcmp(at(0x4be010),retail_pool_pop.data(),retail_pool_pop.size())&&!std::memcmp(at(0x4be9c0),retail_pool_clear.data(),retail_pool_clear.size()),
       "shared ratl pool<16,100> pop 0x4be010 / clear 0x4be9c0 bodies byte-identical to retail after the W2 union");
 bool foreign=true;
 for(unsigned a:{0x4d6b09u,0x4fd8d9u})foreign&=*native<std::uint8_t*>(a)==0xe8&&edge_target(a)==0x4be010;
 for(unsigned a:{0x4605c2u,0x4d7020u,0x4d81feu})foreign&=*native<std::uint8_t*>(a)==0xe8&&edge_target(a)==0x4be9c0;
 check(foreign,"five foreign ratl pool edges (0x4d6b09, 0x4fd8d9 pop; 0x4605c2, 0x4d7020, 0x4d81fe clear) still call the retail helpers");
 const auto pop=edge_target(0x4bfce9),clear=edge_target(0x4bfe10);
 check(pop!=0x4be010&&clear!=0x4be9c0&&edge_target(0x4c043d)==clear,"talent C-pool edges 0x4bfce9 / 0x4bfe10 / 0x4c043d reach the private 256-entry copies");
 check(retail_pool_semantics(0x4be010,0x4be9c0),"foreign-layout ratl pool<16,100>: clear 100, pop 100 distinct, wrap, 100 bucket bits, nothing written past 0x7f8");
 check(!retail_pool_semantics(pop,clear),"negative control: the 256-entry instruction stream (the old in-place W2 body) fails the foreign-layout check");
}
void talent_tests(){
 auto* reg=static_cast<std::uint8_t*>(native<void*(__cdecl*)()>(0x4c05b0)());
 check(reg&&reg==*native<void**>(0x77339c)&&read<unsigned>(reg,talent_b+b_mask)==255&&read<unsigned>(reg,talent_b+b_shift)==8,"T11 native constructor on DLL registry, B mask FF / shift 8");
 check(inaccessible(reg+talent_size),"GP relocated entire talent registry ends at guard page");
 const unsigned offsets[]={talent_a,talent_b,talent_c},allocators[]={0x4bdfb0,0x4be7b0,edge_target(0x4bfce9)},counts[]={a_live,b_live,c_live},bits[]={a_buckets,b_buckets,c_buckets};
 bool all=true;
 for(unsigned k=0;k<3;++k){auto* p=reg+offsets[k];std::set<unsigned> ids;
  for(unsigned i=0;i<256;++i){const auto id=native<unsigned(__fastcall*)(void*,void*)>(allocators[k])(p,nullptr);ids.insert(id);all&=id<256;}
  all&=ids.size()==256&&read<unsigned>(p,counts[k])==256;
  for(unsigned i=0;i<8;++i)all&=read<unsigned>(p,bits[k]+i*4)==0xffffffff;
 }
 check(all,"T11 native A/B/C rings allocate all 256 distinct slots, wrap at 256, set all eight bitmap words");
 // Pool B's record bitmap drives destructors. Leave records empty so reset
 // executes the real scans without needing any talent-object implementation.
 auto* b=reg+talent_b;std::memset(b+b_bitmap,0,32);
 for(unsigned i=0;i<256;++i)put(b,b_generations+i*4,i%2?0x7fffff00u|i:0x100u|i);
 const unsigned canary=0x1badcafe;put(b,b_generations+256*4,canary); // mask field, restored after observing loop
 native<void(__fastcall*)(void*,void*)>(0x4bf9e0)(b,nullptr);
 bool generation=true;for(unsigned i=0;i<256;++i)generation&=read<unsigned>(b,b_generations+i*4)==(i%2?0x100u|i:0x200u|i);
 check(generation&&read<unsigned>(b,b_generations+256*4)==canary,"T11 generation loop updates exactly 256 entries, rollover semantics and table-end canary preserved");
 put(b,b_mask,255u);
 bool cleared=true;for(unsigned i=0;i<8;++i)cleared&=read<unsigned>(b,b_buckets+i*4)==0;
 check(cleared&&read<unsigned>(b,b_live)==0,"T11 B reset clears eight words and count");
 native<void*(__fastcall*)(void*,void*)>(0x4c0370)(reg,nullptr);
 bool all_clear=true;for(unsigned i=0;i<8;++i)all_clear&=read<unsigned>(reg,talent_a+a_buckets+4*i)==0&&read<unsigned>(reg,talent_c+c_buckets+4*i)==0;
 check(all_clear&&read<unsigned>(reg,talent_a+a_live)==0&&read<unsigned>(reg,talent_c+c_live)==0,"T11 whole-registry reconstruction clears all eight A/C bitmap words and counts");
 jump(0x5649c0,&attr_name);jump(0x501410,&no_existing);jump(0x4c0143,&caps_passed);
 const unsigned count_offsets[]={talent_a+a_live,talent_b+b_live,talent_c+c_live};
 for(auto o:count_offsets)put(reg,o,255u);
 passed_caps=false;native<bool(__fastcall*)(void*,void*,void*,unsigned,unsigned,unsigned)>(0x4c0080)(reg,nullptr,reg,0,0,0);
 check(passed_caps,"T11 actual common registrar admits pool counts255 to registration path");
 bool refused=true;for(auto o:count_offsets){put(reg,o,256u);passed_caps=false;const auto result=native<bool(__fastcall*)(void*,void*,void*,unsigned,unsigned,unsigned)>(0x4c0080)(reg,nullptr,reg,0,0,0);refused&=!result&&!passed_caps&&read<unsigned>(reg,o)==256;put(reg,o,255u);}
 check(refused,"T11 257th registration refused independently for full A/B/C pools; counts unchanged at256");
 const auto hero_base=native<unsigned(__fastcall*)(void*,void*,unsigned)>(0x4bdc00)(reg,nullptr,0);
 check(hero_base==4400&&hero_base+10==4410,"T11 actual 0x4bdc00 helper returns (21+1)*200; position10 id4410");
 check(save_limits_rules::position(22,199)&&!save_limits_rules::position(22,200)&&save_limits_rules::position(297,199)&&save_limits_rules::id(296,199)==59599&&298*200-1<=65535,"T11 position M-1 accepted, M refused, highest stats owner representable");
 check(save_limits_rules::position(0,198)&&!save_limits_rules::position(0,199),"T11 shared positions stop at M-2 (cap M-1)");
 jump(0x4c0460,&cap_target);native<bool(__fastcall*)(void*,void*)>(0x4c05d0)(reg,nullptr);
 check(cap_observed==199&&base_observed==0&&ecx_observed==reg,"T11 shared-cap call thunk changes third argument to199 and preserves ECX");
 // Execute the endpoint cave through its original entry, with native pushes
 // terminated before the rest of the file loader resumes.
 jump(0x4c0649,&endpoint_done);const auto endpoint=at(0x4c0644);
 __asm {
 mov eax,4400
 call endpoint
 }
 check(cap_observed==4600,"T11 endpoint cave computes base+200");
 check(!save_guard_rules::registry_full(255,255,255,256)&&save_guard_rules::registry_full(256,0,0,256)&&save_guard_rules::registry_full(0,256,0,256)&&save_guard_rules::registry_full(0,0,256,256),"T11 runtime fullness guard uses 256 independently for all three pools");
}
LONG CALLBACK fault(EXCEPTION_POINTERS* e){std::fprintf(stderr,"FAULT %08lx at %p\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress);ExitProcess(3);}
}
int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,&fault);std::setvbuf(stdout,nullptr,_IONBF,0);
 const auto* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");if(!path){std::puts("SKIP: retail exe required");return 2;}
 DWORD old=0;VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old);
 check(addr(fixture_space)==0x401000&&load_image(path),"retail PE mapped in owned fixture, no game launched");retail_sorts();
 // Restore dependencies before the complete instruction guards are prepared.
 check(load_image(path),"retail bytes restored before contribution guards");
 patch_transaction::contribution c;std::string why;check(save_limits::contribute_w2(c,why),"E14/E7 retail guards and owner hashes: "+why);if(c.writes.empty())return 1;
 for(auto a:{0x4885c0u,0x489a01u,0x489800u,0x4bfa50u,0x4c083cu}){auto& byte=*native<std::uint8_t*>(a);byte^=1;const auto before=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction bad;bad.add(c);const auto r=bad.commit();check((r==patch_transaction::outcome::refused_guard||r==patch_transaction::outcome::refused_region)&&std::equal(before.begin(),before.end(),fixture_space)&&!save_limits::installed(),"T3 mutated ledger/whole-owner byte refuses all writes at "+patch_transaction::hex(a));byte^=1;}
 retail_pool_pop.assign(native<const std::uint8_t*>(0x4be010),native<const std::uint8_t*>(0x4be06f));retail_pool_clear.assign(native<const std::uint8_t*>(0x4be9c0),native<const std::uint8_t*>(0x4bea1d));
 patch_transaction::transaction tx;tx.add(c);check(objective_text::contribute(tx,reinterpret_cast<HMODULE>(0x400000),true),"W2 objective_text joins same transaction with one allocation-site writer");const auto outcome=tx.commit();check(outcome==patch_transaction::outcome::committed,"combined E14/E7/descriptions commit: "+tx.report());if(outcome!=patch_transaction::outcome::committed)return 1;
 if(argc>2){const std::string prefix=argv[2];std::ofstream text(prefix+"-text.bin",std::ios::binary);text.write(native<const char*>(0x401000),0x27d7b5);std::ofstream manifest(prefix+"-caves.tsv");std::set<unsigned> pages;
  for(const auto& w:c.writes)for(unsigned a=w.va&~4095u;a<((w.va+w.bytes.size()+4095u)&~4095u);a+=4096)pages.insert(a);
  std::ofstream page_file(prefix+"-pages.txt");for(auto a:pages)page_file<<std::hex<<a<<'\n';
  for(const auto& block:blocks){const auto base=block.lo+5+read<unsigned>(at(block.lo+1));int size=block.hi-block.lo+5;for(const auto& e:block.edits)size+=static_cast<int>(patch_transaction::bytes_of(e.value).size())-e.size;const auto dump_path=prefix+"-code-"+patch_transaction::hex(block.lo)+".bin";std::ofstream dump(dump_path,std::ios::binary);dump.write(native<const char*>(base),size);manifest<<std::hex<<block.lo<<'\t'<<block.hi<<'\t'<<base<<'\t'<<size<<'\t'<<dump_path<<'\n';}
 }
 externals();objective_tests();files_tests();side_tests();shared_pool_tests();talent_tests();std::printf("W2 objectives/talents: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
}
