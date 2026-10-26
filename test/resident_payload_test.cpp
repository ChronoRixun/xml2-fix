// TR10: the five payload operations against owner-loaded retail code at N=11.
#include <Windows.h>
#include "resident_payload.hpp"
#include <array>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdio>
namespace {
using u32=std::uint32_t;
unsigned checks=0;
void check(bool x,const char* s){++checks;if(!x){std::printf("FAIL %s\n",s);std::fflush(stdout);std::abort();}}
void* ptr(u32 x){return reinterpret_cast<void*>(x);}
template<class T>T get(const void* p,u32 off=0){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
void load(const std::vector<unsigned char>& raw){const auto* h=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+get<u32>(raw.data(),0x3c));
 for(unsigned i=0;i<h->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(h)[i];if(s.VirtualAddress>=0x1000&&s.VirtualAddress+s.Misc.VirtualSize<=0x401000){std::memset(ptr(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);std::memcpy(ptr(0x400000+s.VirtualAddress),raw.data()+s.PointerToRawData,s.SizeOfRawData);}}}
}
int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);check(argc==2,"retail input");std::ifstream f(argv[1],std::ios::binary);const std::vector<unsigned char> raw(std::istreambuf_iterator<char>(f),{});check(!raw.empty(),"input read");DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"owned image writable");load(raw);
 const auto l=resident_payload::layout(11,0x88);check(l.size==0x620&&l.constructed==0x5d8&&l.live==0x61c,"retail NPC layout");
 std::array<unsigned char,0x660> a{},b{};a.fill(0x5a);b=a;auto* pa=a.data()+32;auto* pb=b.data()+32;
 auto init=reinterpret_cast<void(__fastcall*)(void*,void*)>(0x44a300);auto pop=reinterpret_cast<u32(__fastcall*)(void*,void*)>(0x4497d0);auto free=reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x44a120);auto owns=reinterpret_cast<unsigned char(__fastcall*)(void*,void*,void*)>(0x448ff0);auto destroy=reinterpret_cast<void(__fastcall*)(void*,void*)>(0x449150);
 auto initialize=[&]{for(auto* p:{pa,pb}){put<u32>(p,l.constructed,0);put<u32>(p,l.active,0);put<u32>(p,l.live,0);}init(pa,nullptr);resident_payload::ring_init(pb,l);check(a==b,"TR10 ring initializer in constructor context");check(get<u32>(pb,l.ring+11*4)==0x5a5a5a5a,"ring[N] untouched");};
 initialize();for(unsigned i=0;i<12;++i)check(pop(pa,nullptr)==resident_payload::pop(pb,l),"TR10 pop, including empty edge");check(get<u32>(pb,l.free)==0xffffffff&&get<u32>(pb,l.live)==12,"empty pop has no extra guard");
 u32 rng=0x731cd9a1;auto random=[&]{rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
 for(unsigned n=0;n<100000;++n){const auto op=random()%5;
  if(op==0)check(pop(pa,nullptr)==resident_payload::pop(pb,l),"TR10 random pop return");
  else if(op==1){auto i=random()%11;put<u32>(pa,l.constructed,0x7ff);put<u32>(pb,l.constructed,0x7ff);free(pa,nullptr,i);resident_payload::release(pb,i,l);}
  else if(op==2){const auto d=static_cast<std::int32_t>(random());auto* qa=ptr(static_cast<u32>(reinterpret_cast<std::uintptr_t>(pa))+static_cast<u32>(d));auto* qb=ptr(static_cast<u32>(reinterpret_cast<std::uintptr_t>(pb))+static_cast<u32>(d));check(owns(pa,nullptr,qa)==resident_payload::owns(pb,qb,l),"TR10 owns AL and wrapped signed difference");}
  else if(op==3){put<u32>(pa,l.constructed,random());put<u32>(pb,l.constructed,get<u32>(pa,l.constructed));destroy(pa,nullptr);resident_payload::destroy(pb,l);}
  else initialize();
  check(a==b,"TR10 all memory/canaries identical; void incidental registers ignored");
 }
 for(int delta:{-137,-136,-135,-1,0,1,135,136,1495,1496}){put<u32>(pa,l.active,0x7ff);put<u32>(pb,l.active,0x7ff);auto* qa=ptr(static_cast<u32>(reinterpret_cast<std::uintptr_t>(pa))+delta);auto* qb=ptr(static_cast<u32>(reinterpret_cast<std::uintptr_t>(pb))+delta);check(owns(pa,nullptr,qa)==resident_payload::owns(pb,qb,l),"TR10 owns exact below-base/misaligned bounds");}
 check(owns(pa,nullptr,nullptr)==resident_payload::owns(pb,nullptr,l),"TR10 null owns AL");
 std::printf("PASS ResidentStats TR10: %u checks, 100000 random operations, AL-only owns, constructor-context init\n",checks);return 0;
}
