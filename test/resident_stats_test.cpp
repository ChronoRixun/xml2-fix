// TR2-TR13 production transaction and retail instructions in an owned image.
#include <Windows.h>
#include "resident_stats.hpp"
#include "resident_payload.hpp"
#include "resident_stats_sites.hpp"
#include <algorithm>
#include <numeric>
#include <random>
#include <memory>
#include "patch_transaction.hpp"
#include "gameplay_capabilities.hpp"
#include <fstream>
#include <iterator>
#include <vector>
#include <array>
#include <optional>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstring>
namespace {
using u32=std::uint32_t;unsigned checks=0;std::optional<std::string> key="320";bool required=false,unsafe=false;
gameplay_capabilities::record agreement;std::vector<std::string> logs;
void check(bool x,const char* s){++checks;if(!x){std::printf("FAIL %s\n",s);std::fflush(stdout);std::abort();}}
void* ptr(u32 x){return reinterpret_cast<void*>(x);}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T get(const void* p,u32 off=0){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T>void jump(u32 a,T f){put<unsigned char>(ptr(a),0,0xe9);put<u32>(ptr(a),1,address(reinterpret_cast<void*>(f))-a-5);}
std::vector<unsigned char> raw;
void load(){const auto* h=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+get<u32>(raw.data(),0x3c));for(unsigned i=0;i<h->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(h)[i];if(s.VirtualAddress>=0x1000&&s.VirtualAddress+s.Misc.VirtualSize<=0x401000){std::memset(ptr(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);std::memcpy(ptr(0x400000+s.VirtualAddress),raw.data()+s.PointerToRawData,s.SizeOfRawData);}}}
}
namespace ini {std::optional<std::string> text(const wchar_t* s,const wchar_t* k){return std::wcscmp(s,L"Limits")==0&&std::wcscmp(k,L"ResidentStats")==0?key:std::nullopt;}}
namespace logger {void write(const char* f,...){char b[1024]{};va_list a;va_start(a,f);std::vsnprintf(b,sizeof b,f,a);va_end(a);logs.emplace_back(b);}}
namespace game_version {bool require_gameplay_agreement(HMODULE){required=true;return true;}void refuse_startup(){unsafe=true;}}
namespace gameplay_agreement {bool register_rule(std::uint16_t id,std::uint16_t rev,std::array<u32,4> values,bool verified){return agreement.add(id,rev,values,verified);}}

namespace {
using method=void*(__fastcall*)(void*,void*);
std::vector<unsigned char> manager_storage;
void* manager=nullptr;
void __fastcall noop(void*,void*){}
void __fastcall noop_arg(void*,void*,u32){}
void* __fastcall stats_ctor(void* p,void*){std::memset(p,0,0x4f8);put<u32>(p,0,0x68e950);return p;}
void __fastcall stats_destructor(void* p,void*){reinterpret_cast<void(__fastcall*)(void*,void*)>(0x4b7750)(p,nullptr);}
void __stdcall vector_ctor(void* p,u32 stride,u32 n,u32 ctor,u32){for(u32 i=0;i<n;++i){void* x=static_cast<unsigned char*>(p)+i*stride;
 if(stride==0x1e44){std::memset(x,0,stride);for(u32 j=0;j<22;++j)put<u32>(x,j*0x160+0xbc,0x685294);}
 else reinterpret_cast<method>(ctor)(x,nullptr);
}}
void* __cdecl current_manager(){return manager;}
void __fastcall cookie(u32,void*){}
int __cdecl compare(const char* a,const char* b){return std::strcmp(a,b);}
u32 __cdecl length(const char* p){return static_cast<u32>(std::strlen(p));}
char* __cdecl copy(char* d,const char* s,u32 n){return std::strncpy(d,s,n);}
char* __cdecl lower(char* p){for(char* q=p;*q;++q)if(*q>='A'&&*q<='Z')*q+=32;return p;}
struct node{u32 self,index;char name[32];};std::array<node,297> nodes{};node root{};unsigned first=1,last=0;
std::vector<unsigned char> strings(0x8008+32768);
u32 __cdecl string_pool(){return address(strings.data());}
short __fastcall find_name(void*,void*,const char* name){for(u32 i=1;i<nodes.size();++i)if(std::strcmp(name,nodes[i].name)==0)return static_cast<short>(i);return 0;}
bool __fastcall xml_open(void*,void*,const char*,int,int,int){return true;}
void* __fastcall xml_root(void*,void*){return &root;}
bool __fastcall xml_equals(void*,void*,const char*){return true;}
void* __fastcall xml_child(void*,void*,const char*){return nullptr;}
void* __fastcall xml_next(void*,void*,const char*,node* prev){const auto i=prev?prev->index+1:first;return i<=last?&nodes[i]:nullptr;}
const char* __fastcall xml_attr(node* n,void*,const char* key){return std::strcmp(key,"name")==0?n->name:nullptr;}
bool __fastcall details(void*,void*,void*,u32,void*,void*){return true;}
void* __cdecl cast(void* p,int,u32,u32 to,int){const auto vt=get<u32>(p);return (to==0x6d6970&&vt==0x685294)||(to==0x6d6958&&vt==0x68511c)?p:nullptr;}
struct archive {std::vector<unsigned char> data;std::size_t cursor=0;};
u32 __fastcall write(archive* a,void*,const void* p,u32 n){const auto* b=static_cast<const unsigned char*>(p);a->data.insert(a->data.end(),b,b+n);return n;}
u32 __fastcall read_archive(archive* a,void*,void* p,u32 n){check(a->cursor+n<=a->data.size(),"TR6 stream bounds");std::memcpy(p,a->data.data()+a->cursor,n);a->cursor+=n;return n;}
void __cdecl marker(archive* a,const char* s){write(a,nullptr,s,length(s)+1);}
void __fastcall read_marker(archive* a,void*){while(a->cursor<a->data.size()&&a->data[a->cursor++]){}}
void __fastcall neutralize(void*,void*,const char*){}
void services(){
 jump(0x6722d4,&vector_ctor);jump(0x44a3d0,&noop);jump(0x4bcd50,&stats_ctor);jump(0x448d60,&noop);
 jump(0x4bd2d0,&stats_destructor);jump(0x44b8f0,&current_manager);jump(0x672161,&cookie);jump(0x672562,&compare);
 jump(0x602120,&length);jump(0x672170,&copy);jump(0x602080,&lower);jump(0x602140,&string_pool);
 jump(0x564a80,&noop);jump(0x564d00,&noop);jump(0x564eb0,&xml_open);jump(0x564aa0,&xml_root);jump(0x5649a0,&xml_equals);jump(0x564a40,&xml_child);jump(0x564cb0,&xml_next);jump(0x5649c0,&xml_attr);
 jump(0x44bda0,&details);jump(0x4b8290,&noop);jump(0x67234e,&cast);
 jump(0x4688f0,&write);jump(0x468940,&read_archive);jump(0x468990,&marker);jump(0x468a30,&read_marker);
 put<u32>(ptr(0x68544c),0x3c,address(reinterpret_cast<void*>(&find_name)));
 put<u32>(ptr(0x68544c),0xb4,address(reinterpret_cast<void*>(&neutralize)));
 put<u32>(ptr(0x68544c),0xb8,address(reinterpret_cast<void*>(&noop)));
 put<u32>(ptr(0x68544c),0x30,address(reinterpret_cast<void*>(&noop)));
 // Stop the UI smoke immediately after its two native payload calls, before unrelated GUI work.
 const unsigned char epilogue[]={0x5f,0x5e,0x5d,0x5b,0x83,0xc4,0x1c,0xc2,4,0};std::memcpy(ptr(0x5d2d66),epilogue,sizeof epilogue);
}
void create(const stats_layout::descriptor& d){
 manager_storage.assign(d.manager_size+64,0xa5);manager=manager_storage.data()+32;std::memset(manager,0,d.manager_size);
 reinterpret_cast<method>(0x44b520)(manager,nullptr);put<u32>(ptr(0x71770c),0,address(manager));
 check(get<u32>(manager,d.pool_live)==0&&get<u32>(manager,d.hero.live)==0&&get<u32>(manager,d.npc.live)==0,"TR2 constructor live fields");
 const auto n=d.capacity,w=(n+31)/32,c=n*0x4f8,r=c+4*w,q=r+4*(n+1),a=q+12,l=a+4*w;
 auto* pool=static_cast<unsigned char*>(manager)+4;
 for(u32 i=0;i<w;++i)check(get<u32>(pool,c+4*i)==0&&get<u32>(pool,a+4*i)==0,"TR2 all outer bitmap words clear");
 for(u32 i=0;i<n;++i)check(get<u32>(pool,r+4*i)==i&&get<u32>(pool,l+4+i*4)==((d.installed?512:32)|i),"TR2 native constructor ring/generation");
 check(get<u32>(pool,q)==0&&get<u32>(pool,q+4)==0&&get<u32>(pool,q+8)==n,"TR2 native queue fields");
 check(get<u32>(manager,d.pool_mask)==(d.installed?511:31),"TR2 manager mask");
 for(u32 i=0;i<nodes.size();++i){nodes[i].self=address(&nodes[i]);nodes[i].index=i;std::snprintf(nodes[i].name,32,"synthetic_%03u",i);put<u32>(strings.data(),4+i*4,i*32);std::memcpy(strings.data()+0x8008+i*32,nodes[i].name,32);}
 root.self=address(&root);put<u32>(manager,d.definition_count,297);
 for(u32 i=1;i<297;++i)put<u32>(manager,d.entries+i*28,i);
}
void canaries(const stats_layout::descriptor& d){for(unsigned i=0;i<32;++i)check(manager_storage[i]==0xa5&&manager_storage[d.manager_size+32+i]==0xa5,"TR2 allocation canaries");}
void* record(const stats_layout::descriptor& d,u32 index){const auto h=get<u32>(manager,d.entries+index*28+16);return static_cast<unsigned char*>(manager)+4+(h&get<u32>(manager,d.pool_mask))*0x4f8;}
void load_definitions(const stats_layout::descriptor& d,unsigned begin,unsigned end,bool hero){
 first=begin;last=end;for(unsigned i=begin;i<=end;++i)put<unsigned char>(manager,d.entries+i*28+24,hero?1:8);
 const auto file=hero?static_cast<const char*>(ptr(0x685578)):"synthetic/npcstat";
 check(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,const char*,u32)>(0x44c030)(manager,nullptr,file,1)!=0,"TR12 native loader success");
 resident_stats::end_zone();check(get<u32>(manager,d.hero.live)+get<u32>(manager,d.npc.live)==get<u32>(manager,d.pool_live),"TR12 hero+npc==resident after load");
}
void getter(const stats_layout::descriptor& d){for(u32 n=0;n<65536;++n){const auto i=static_cast<short>(n);auto* p=reinterpret_cast<void*(__fastcall*)(void*,void*,int)>(0x44b760)(manager,nullptr,i);check(address(p)==address(manager)+d.entries+12+static_cast<int>(i)*28,"TR5 all signed16 getters, flags dead");}}
void seed(const stats_layout::descriptor& d){
 for(u32 i=1;i<=21;++i){auto* p=static_cast<unsigned char*>(record(d,i));for(u32 j=8;j<0xbc;++j)p[j]=static_cast<unsigned char>(i*17+j);for(u32 j=0xc4;j<0x119;++j)p[j]=static_cast<unsigned char>(i+j);put<u32>(p,0x11c,0x107000+i);p[0x120]=static_cast<unsigned char>(i);
  auto* q=ptr(get<u32>(p,0xc0));put<u32>(q,0x20,0);for(u32 j=0x28;j<0x44;++j)put<unsigned char>(q,j,static_cast<unsigned char>(i+j));
 }
 for(u32 i=0;i<22;++i){auto* p=static_cast<unsigned char*>(manager)+(0xf488+d.trailing_delta)+i*0x160;put<u32>(p,0x20,0);for(u32 j=0x28;j<0x44;++j)p[j]=static_cast<unsigned char>(i*3+j);}
}
archive save(){archive a;reinterpret_cast<void(__fastcall*)(void*,void*,archive*)>(0x449a60)(manager,nullptr,&a);return a;}
void roundtrip(const archive& a){auto input=a;reinterpret_cast<void(__fastcall*)(void*,void*,archive*)>(0x44b970)(manager,nullptr,&input);check(input.cursor==input.data.size(),"TR6 full stream consumed");check(save().data==a.data,"TR6 native manager/CStats/hero payload save reload reserialize");}

u32 probe_target=0,probe_sp=0,probe_saved=0,probe_flags=0,route=0;
u32 in_eax=0,in_ecx=0,in_edx=0,in_ebx=0,in_ebp=0,in_esi=0,in_edi=0;
u32 out_eax=0,out_ecx=0,out_edx=0,out_ebx=0,out_ebp=0,out_esi=0,out_edi=0;
__declspec(naked) void probe(){__asm {
 pushfd
 pushad
 mov probe_saved,esp
 mov esp,probe_sp
 mov eax,in_eax
 mov ecx,in_ecx
 mov edx,in_edx
 mov ebx,in_ebx
 mov ebp,in_ebp
 mov esi,in_esi
 mov edi,in_edi
 jmp dword ptr[probe_target]
}}
__declspec(naked) void capture(){__asm {
 mov out_eax,eax
 mov out_ecx,ecx
 mov out_edx,edx
 mov out_ebx,ebx
 mov out_ebp,ebp
 mov out_esi,esi
 mov out_edi,edi
 pushfd
 pop probe_flags
 mov esp,probe_saved
 popad
 popfd
 ret
}}
void stop(u32 a){static auto* stubs=static_cast<unsigned char*>(VirtualAlloc(nullptr,65536,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));static unsigned used=0;check(stubs&&used+16<65536,"probe stub capacity");auto* b=stubs+used;used+=16;put<unsigned char>(b,0,0xc7);put<unsigned char>(b,1,5);put<u32>(b,2,address(&route));put<u32>(b,6,a);jump(address(b)+10,&capture);jump(a,b);}
void caves(){
 const std::vector<unsigned char> code(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
 std::array<unsigned char,16384> stack{};probe_sp=address(stack.data()+8192);
 std::vector<unsigned char> object(0x77820,0x71);const auto base=address(object.data());
 for(const auto& row:resident_stats_sites::sites)if(row.type==resident_stats_sites::kind::cave&&row.role=="capacity"){
  const auto old=patch_transaction::bytes_of(row.hex);u32 branch=0;bool signed_less=false;
  if(old[3]>=0x70&&old[3]<=0x7f){branch=row.va+5+static_cast<signed char>(old[4]);signed_less=old[3]==0x7c;}
  else if(old[3]==15){u32 delta;std::memcpy(&delta,old.data()+5,4);branch=row.va+9+delta;}
  for(int value:{0,319,320,321,0x7fffffff,static_cast<int>(0x80000000u)}){
   std::memcpy(ptr(0x401000),code.data(),code.size());stop(row.va+row.span);if(branch)stop(branch);
   in_eax=in_ecx=in_edx=static_cast<u32>(value);in_ebx=0x12345678;in_ebp=base;in_esi=123;in_edi=base;const auto reg=old[1]&7;
   if(!branch){if(reg==1)in_edx=base;else if(row.va==0x44a1ce)in_ecx=base;else in_esi=base;}
   probe_target=row.va;probe();
   if(branch)check(route==((signed_less?(value<320):(value>=320))?branch:row.va+row.span),"TR4 signed compare cave both outcomes");
   else {const bool sf=((static_cast<u32>(value)-320)&0x80000000u)!=0,of=value<0&&static_cast<int>(static_cast<u32>(value)-320)>=0;
    check(((probe_flags>>7)&1)==sf&&((probe_flags>>11)&1)==of,"TR4 replayed MOV preserves CMP SF/OF");
    const auto disp=row.va==0x449704?0x63b30:0x63b2c;check(get<u32>(object.data(),disp)==(row.va==0x449704?in_esi:in_eax),"TR4 displaced store targets moved queue");
   }
   check(out_ebx==in_ebx&&out_ebp==in_ebp&&out_esi==in_esi&&out_edi==in_edi,"TR4 capacity cave callee-saved registers");
  }
 }
 for(const auto& row:resident_stats_sites::sites)if(row.type==resident_stats_sites::kind::cave&&row.role=="clear-cave"){
  std::memcpy(ptr(0x401000),code.data(),code.size());std::fill(object.begin(),object.end(),0x71);stop(row.va+row.span);in_eax=17;in_ecx=19;in_edx=23;in_ebx=29;in_ebp=31;in_esi=base;in_edi=base+(row.va==0x449145?0x63600:0);probe_target=row.va;probe();
  const u32 offset=(row.va==0x449145||row.va==0x44ab56)?0x63600:0x63b38;for(u32 i=0;i<10;++i)check(get<u32>(object.data(),offset+i*4)==0,"TR4 clear all10 bitmap words");
  check(get<u32>(object.data(),offset-4)==0x71717171&&get<u32>(object.data(),offset+40)==0x71717171,"TR4 clear adjacent canaries");
  check(out_eax==in_eax&&out_ecx==in_ecx&&out_edx==in_edx&&out_esi==in_esi&&out_edi==in_edi,"TR4 clear register preservation");
 }
 // Residency guard ABI: only CL of the stack dword selects class, AL survives admission.
 std::memcpy(ptr(0x401000),code.data(),code.size());stop(0x44c404);stop(0x44c47d);
 for(u32 cls:{0u,0x12340000u,1u,0xff000001u})for(u32 h:{21u,22u}){
  std::fill(object.begin(),object.end(),0);put<u32>(object.data(),0x6bda8,h);put<u32>(object.data(),0x777dc,275);put<u32>(stack.data()+8192,0x24,cls);in_eax=0xaabbcc09;in_ebp=base;in_ebx=1;probe_target=0x44c3fb;probe();
  check(route==((cls&255)&&h>=22?0x44c47d:0x44c404),"TR12 file-class low byte and payload refusal");if(route==0x44c404)check((out_eax&255)==9,"TR12 AL preserved on admission");
 }
 std::memcpy(ptr(0x401000),code.data(),code.size());
}


void release_calls(const stats_layout::descriptor& d){
 create(d);load_definitions(d,1,1,false);auto* p=record(d,1);
 reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4b7c50)(p,nullptr,0);
 check(get<u32>(manager,d.npc.live)==1&&get<u32>(p,0xc0)!=0,"TR11 reattach release4b7c6d retains one payload");
 reinterpret_cast<void(__fastcall*)(void*,void*)>(0x4b7750)(p,nullptr);
 check(get<u32>(manager,d.npc.live)==0&&get<u32>(p,0xc0)==0,"TR11 reset release4b776d");
 reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4b7c50)(p,nullptr,0);
 std::array<unsigned char,16384> stack{};probe_sp=address(stack.data()+8192);
 std::array<unsigned char,5> original{};std::memcpy(original.data(),ptr(0x4bd363),5);stop(0x4bd363);
 in_esi=address(p);probe_target=0x4bd339;probe();std::memcpy(ptr(0x4bd363),original.data(),5);
 check(get<u32>(manager,d.npc.live)==0&&get<u32>(p,0xc0)==0,"TR11 native destructor release4bd353");
}
void outer_stress(const stats_layout::descriptor& d){
 create(d);auto* pool=static_cast<unsigned char*>(manager)+4;
 std::vector<u32> handles;for(u32 i=0;i<320;++i){auto h=reinterpret_cast<u32(__fastcall*)(void*,void*)>(0x44ab00)(pool,nullptr);handles.push_back(h);check((h&511)==i&&h!=0,"TR4 outer native allocate320");}
 check(get<u32>(manager,d.pool_live)==320,"TR4 outer full live320");
 first=last=1;put<unsigned char>(manager,d.entries+28+24,8);check(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,const char*,u32)>(0x44c030)(manager,nullptr,"synthetic/npcstat",1)!=0,"TR4 forced-full loader succeeds");check(get<u32>(manager,d.entries+28+16)==0,"TR4 full outer guard takes refusal handle0");
 check(reinterpret_cast<void*(__fastcall*)(void*,void*,const char*)>(0x4498d0)(manager,nullptr,nodes[1].name)==pool,"TR4 refused name resolves first active resident");
 check(reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x44b890)(manager,nullptr,1)==pool,"TR4 zero handle indexed lookup masks to slot0");
 const auto stale=handles.back();std::mt19937 random(0x132a);std::shuffle(handles.begin(),handles.end(),random);
 for(auto h:handles)reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x44ae00)(pool,nullptr,h);
 check(get<u32>(manager,d.pool_live)==0,"TR4 all native outer releases");
 put<u32>(manager,d.entries+28+16,stale);check(reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x44b890)(manager,nullptr,1)==pool+319*0x4f8,"TR4 released handle keeps retail masking, no generation rejection");
 for(u32 i=0;i<320;++i)check(reinterpret_cast<u32(__fastcall*)(void*,void*)>(0x44ab00)(pool,nullptr)!=0,"TR4 outer reuse random queue");
 reinterpret_cast<void(__fastcall*)(void*,void*)>(0x44ada0)(pool,nullptr);check(get<u32>(manager,d.pool_live)==0,"TR4 native reset covers all320 constructed entries");
 constexpr u32 active=0x63b38;std::array<u32,2> iterator{};
 for(u32 i=0;i<=320;++i){std::memset(pool+active,0,40);if(i<320)put<u32>(pool,active+(i/32)*4,1u<<(i%32));
  reinterpret_cast<void*(__fastcall*)(void*,void*,void*)>(0x448c60)(pool,nullptr,iterator.data());check(iterator[0]==i&&iterator[1]==address(pool),"TR4 native iterator indices0..319 and sentinel320");}
 canaries(d);
}
void smoke(const stats_layout::descriptor& d,unsigned n){
 create(d);load_definitions(d,1,n,false);
 for(u32 i=1;i<=n;++i){void* p=record(d,i);check(get<u32>(p,0xc0)!=0,"TR13 precondition non-null payload");
  check(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,const char*,const char*)>(0x4b9c40)(p,nullptr,"leadername","synthetic_leader")!=0,"TR13 native leadername path");
  reinterpret_cast<void(__stdcall*)(u32)>(0x5d2d30)(i);
  archive a;reinterpret_cast<void(__fastcall*)(void*,void*,archive*)>(0x4b7e50)(p,nullptr,&a);check(!a.data.empty(),"TR13 native indexed payload prefix and CStats save");
 }
 canaries(d);
}
void install(){agreement={};put<u32>(ptr(0x71770c),0,0);patch_transaction::transaction tx;resident_stats::contribute(tx,GetModuleHandleW(nullptr));check(tx.contributions()==1,"TR4 production contribution exists");auto result=tx.commit();if(result!=patch_transaction::outcome::committed)std::printf("%s\n",tx.report().c_str());check(result==patch_transaction::outcome::committed,"TR4 R2 transaction commits");}
}
int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);check(argc==2||argc==3,"retail argument");std::ifstream f(argv[1],std::ios::binary);raw.assign(std::istreambuf_iterator<char>(f),{});check(!raw.empty(),"retail read");DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"owned image writable");load();stats_layout::consumers=15;
 const std::vector<unsigned char> original(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
 gameplay_capabilities::record empty;check(empty.finalize(),"empty agreement");
 for(auto k:{std::optional<std::string>{},std::optional<std::string>{"31"}}){key=k;agreement={};patch_transaction::transaction tx;resident_stats::contribute(tx,GetModuleHandleW(nullptr));check(tx.contributions()==0&&agreement.finalize()&&std::equal(empty.bytes().begin(),empty.bytes().end(),agreement.bytes().begin()),"TR3 OFF no rule");}
 check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"TR3 OFF zero bytes");check(resident_stats::status().empty(),"TR3 OFF status unchanged");key="320";
 for(auto va:{0x71770cu,0x44c1a7u,0x44b451u,0x448c62u,0x448ff0u+80}){auto* p=static_cast<unsigned char*>(ptr(va));*p^=1;agreement={};patch_transaction::transaction tx;resident_stats::contribute(tx,GetModuleHandleW(nullptr));const auto result=tx.commit();check(result!=patch_transaction::outcome::committed&&!agreement.finalize(),"TR8 manager/cap/hero/operand/region refusal");*p^=1;check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"TR8 refusal zero bytes");}
 for(auto mask:{0u,1u,3u,7u}){stats_layout::consumers=mask;agreement={};patch_transaction::transaction tx;resident_stats::contribute(tx,GetModuleHandleW(nullptr));check(!tx.contributions()&&!agreement.finalize(),"TR8 missing consumer");}stats_layout::consumers=15;
 {auto d=stats_layout::raised;d.npc.capacity=319;agreement={};patch_transaction::transaction tx;resident_stats::contribute(tx,GetModuleHandleW(nullptr),d);check(!tx.contributions()&&!agreement.finalize(),"TR8 NPC capacity less than outer");}
 {agreement={};patch_transaction::transaction tx;resident_stats::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::contribution conflict;conflict.owner="conflict";conflict.guards.push_back({0x448cbc,patch_transaction::bytes_of("ba1f000000"),"conflict"});conflict.writes.push_back({0x448cbc,{0x90},"conflict"});tx.add(std::move(conflict));check(tx.commit()==patch_transaction::outcome::refused_conflict&&!agreement.finalize(),"TR8 overlapping rule fails closed");}

 {agreement={};patch_transaction::transaction tx;resident_stats::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::hooks hooks;u32 arena_target=0;
  hooks.after_apply=[&]{arena_target=0x448c76+5+get<u32>(ptr(0x448c76),1);*static_cast<unsigned char*>(ptr(0x448cbd))^=1;};
  const bool uncertified=argc==3&&std::strcmp(argv[2],"retain")==0;
  if(uncertified)hooks.after_rollback=[]{*static_cast<unsigned char*>(ptr(0x448cbd))^=1;};
  const auto result=tx.commit(&hooks);
  if(uncertified){MEMORY_BASIC_INFORMATION region{};check(result==patch_transaction::outcome::rollback_failed&&unsafe&&!agreement.finalize(),"TR8 uncertified rollback closes startup and online");check(VirtualQuery(ptr(arena_target),&region,sizeof region)==sizeof region&&region.State==MEM_COMMIT&&(region.Protect&PAGE_EXECUTE_READ),"TR8 uncertified rollback retains RX caves");std::printf("PASS ResidentStats uncertified rollback: %u checks, RX caves retained, startup refused\n",checks);return 0;}
  check(result==patch_transaction::outcome::rolled_back&&!agreement.finalize()&&!stats_layout::current().installed,"TR8 certified rollback leaves stock descriptor and unverified rule");
  check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"TR8 rollback restores every image byte");
 }
 // Stock baseline and save: same twenty-one resident heroes as beta 8.
 services();create(stats_layout::stock);getter(stats_layout::stock);load_definitions(stats_layout::stock,1,21,true);seed(stats_layout::stock);const auto stock_save=save();roundtrip(stock_save);canaries(stats_layout::stock);
 load();install();check(agreement.finalize()&&agreement.rule(1).id==50&&agreement.rule(1).values==std::array<u32,4>{320,22,320,1},"TR9 rule 50 exact effective values");
 for(bool reverse:{false,true}){const auto& a=reverse?agreement:empty;const auto& b=reverse?empty:agreement;gameplay_capabilities::difference diff;char message[256]{};check(a.compare(b.bytes(),&diff)!=gameplay_capabilities::result::match,"TR9 bidirectional mismatch");a.describe(diff.buckets,diff.local_only,true,message,sizeof message);check(std::strstr(message,"50 ResidentStats")!=nullptr,"TR9 names rule 50");}
 services();caves();create(stats_layout::raised);getter(stats_layout::raised);load_definitions(stats_layout::raised,1,21,true);seed(stats_layout::raised);const auto raised_save=save();check(stock_save.data==raised_save.data,"TR6 OFF/ON streams byte identical, same 21 hero resident set");roundtrip(stock_save);canaries(stats_layout::raised);
 // Test the raised stream loading on the stock engine, then reinstall for stress.
 load();stats_layout::publish(stats_layout::stock);services();create(stats_layout::stock);load_definitions(stats_layout::stock,1,21,true);seed(stats_layout::stock);roundtrip(raised_save);
 load();install();services();create(stats_layout::raised);
 std::puts("PASS TR2/3/5/6/8/9; beginning raised pool stress");std::fflush(stdout);
 const auto& d=stats_layout::raised;outer_stress(d);create(d);
 const auto old_npc=std::vector<unsigned char>(static_cast<unsigned char*>(manager)+0x11864+d.trailing_delta,static_cast<unsigned char*>(manager)+0x11e84+d.trailing_delta);
 auto payload_alloc=reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x44b440);
 std::vector<void*> payloads;for(u32 i=0;i<320;++i){auto* p=payload_alloc(manager,nullptr,0);check(p==static_cast<unsigned char*>(manager)+d.npc.base+i*136&&get<u32>(p)==0x68511c,"TR11 distinct native CStatsNPC payload");payloads.push_back(p);}
 check(payload_alloc(manager,nullptr,0)==nullptr,"TR11 321st payload refused by cave");
 std::mt19937 rng(0x93c220);std::shuffle(payloads.begin(),payloads.end(),rng);
 for(auto* p:payloads)check(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,void*)>(0x44b320)(manager,nullptr,p)!=0,"TR11 native payload release owns/free");
 check(get<u32>(manager,d.npc.live)==0,"TR11 payload free live zero");for(u32 i=0;i<320;++i)check(payload_alloc(manager,nullptr,0)!=nullptr,"TR11 random queue reuse");canaries(d);if(argc==3&&std::strcmp(argv[2],"mutate-hole")==0)put<unsigned char>(manager,0x11864+d.trailing_delta,0x5a);check(std::equal(old_npc.begin(),old_npc.end(),static_cast<unsigned char*>(manager)+0x11864+d.trailing_delta),"TR11 old NPC storage untouched by payload operations");
 auto refusal_logs=[&]{return std::count_if(logs.begin(),logs.end(),[](const auto& x){return x.find("R2: residency refused")!=std::string::npos;});};
 for(unsigned heroes:{21u,22u,23u,30u}){create(d);resident_stats::begin_zone();const auto before=refusal_logs();load_definitions(d,1,heroes,true);const auto logged=refusal_logs();
  check(logged-before==(heroes>22?heroes-22:0),"TR12 reason logged once per refused definition in zone");
  load_definitions(d,1,heroes,true);check(refusal_logs()==logged,"TR12 repeated same-zone refusals do not log twice"); // sharing existing definitions allocates no second CStats
  check(get<u32>(manager,d.hero.live)==std::min(heroes,22u),"TR12 hero payload ceiling22");
  for(u32 i=1;i<=heroes;++i)check((get<u32>(manager,d.entries+i*28+16)!=0)==(i<=22),"TR12 heroes23+ fallback handle zero");
  if(heroes==21){load_definitions(d,22,296,false);check(get<u32>(manager,d.npc.live)==275&&get<u32>(manager,d.pool_live)==296,"TR12 21 heroes plus275 NPCs");
   for(u32 i=1;i<=296;++i){auto* p=record(d,i);auto q=get<u32>(p,0xc0);check(q!=0&&get<u32>(ptr(q))==(i<=21?0x685294u:0x68511cu),"TR12 every resident has correct non-null payload");}
   reinterpret_cast<void(__fastcall*)(void*,void*)>(0x44b090)(manager,nullptr);resident_stats::end_zone();check(get<u32>(manager,d.npc.live)==0&&get<u32>(manager,d.pool_live)==21,"TR12 native zone unload frees all NPCs");
  }canaries(d);
 }
 const auto counters=resident_stats::telemetry_json();check(counters.find("\"refusals\":[1,18,0]")!=std::string::npos&&counters.find("\"invariant_failures\":0")!=std::string::npos,"TR12 exact reason counters and zone invariant");
 resident_stats::census_json();resident_stats::sample();const auto census=resident_stats::census_json();std::size_t count=0,pos=0;while((pos=census.find("\"payload\":\"hero\"",pos))!=std::string::npos){++count;++pos;}
 check(count==22&&census.find("\"payload\":\"invalid\"")==std::string::npos,"TR12 cached game-thread census reports22 correct hero payloads");
 release_calls(d);for(unsigned n:{12u,64u,275u})smoke(d,n);
 std::printf("PASS ResidentStats native fixture: %u checks; TR2-TR13 (TR7/TR10 separate); same21-hero save precondition\n",checks);return 0;
}
