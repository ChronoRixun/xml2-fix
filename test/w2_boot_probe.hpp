#pragma once
#include <Windows.h>
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include "../src/item_capacity_rules.hpp"
namespace w2_boot_probe {
inline auto* image=reinterpret_cast<unsigned char*>(0x401000);
inline int failures=0;
inline unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
inline void* at(unsigned a){return reinterpret_cast<void*>(a);}
template<class T>T native(unsigned a){return reinterpret_cast<T>(a);}
template<class T>T read(const void* p,unsigned o=0){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof v);return v;}
template<class T>void put(void* p,unsigned o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof v);}
inline void jump(unsigned a,const void* p){auto* b=static_cast<unsigned char*>(at(a));b[0]=0xe9;put(b,1,addr(p)-a-5);FlushInstructionCache(GetCurrentProcess(),b,5);}
inline void check(bool ok,const std::string& s){failures+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",s.c_str());}
struct protected_buffer {
 void* allocation;unsigned char* data;
 protected_buffer(unsigned n){const auto pages=(n+4095)&~4095u;allocation=VirtualAlloc(nullptr,pages+4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD old;VirtualProtect(static_cast<unsigned char*>(allocation)+pages,4096,PAGE_NOACCESS,&old);data=static_cast<unsigned char*>(allocation)+pages-n;std::memset(data,0,n);}
 ~protected_buffer(){VirtualFree(allocation,0,MEM_RELEASE);}
};
inline unsigned __fastcall zero2(void*,void*,unsigned,unsigned){return 0;}
inline int __cdecl no_atexit(void*){return 0;}
// The real file-buffer cache stores {buffer, length} at an index allocated by
// 0x58a1d0. Keep two documents live, as native XML loading does. A widened
// shared ring incorrectly hands both documents index zero.
struct cached_input {
 protected_buffer memory{4+0x105c+0x1000};
 unsigned char* pool=memory.data+4;
 unsigned first=0,second=0;
 cached_input(void* a,unsigned asize,void* b,unsigned bsize){
  for(unsigned i=0;i<256;++i){put(pool,0x820+4*i,i);put(pool,0xc54+4*i,256u|i);}
  put(pool,0xc28,0u);put(pool,0xc2c,256u);put(pool,0x1054,255u);put(pool,0x1058,8u);
  unsigned pair[2]{addr(a),asize};
  first=native<unsigned(__fastcall*)(void*,void*)>(0x58a1d0)(pool,nullptr);
  native<void(__fastcall*)(void*,void*,unsigned,void*)>(0x58a190)(pool,nullptr,first,pair);
  pair[0]=addr(b);pair[1]=bsize;
  second=native<unsigned(__fastcall*)(void*,void*)>(0x58a1d0)(pool,nullptr);
  native<void(__fastcall*)(void*,void*,unsigned,void*)>(0x58a190)(pool,nullptr,second,pair);
 }
 void* first_buffer()const{return at(read<unsigned>(pool,first*8));}
};
inline std::vector<void*> released_buffers;
inline void __cdecl observe_file_free(void* p,unsigned){released_buffers.push_back(p);}
inline bool release_cached_inputs(cached_input& c,void* first,void* second){
 unsigned char saved[5];std::memcpy(saved,at(0x5606a0),5);
 jump(0x5606a0,&observe_file_free);released_buffers.clear();
 native<void(__fastcall*)(void*,void*,unsigned,unsigned)>(0x58a220)(c.memory.data,nullptr,256u|c.first,14);
 native<void(__fastcall*)(void*,void*,unsigned,unsigned)>(0x58a220)(c.memory.data,nullptr,256u|c.second,14);
 std::memcpy(at(0x5606a0),saved,5);FlushInstructionCache(GetCurrentProcess(),at(0x5606a0),5);
 return released_buffers.size()==2&&released_buffers[0]==first&&released_buffers[1]==second;
}

inline unsigned resource_vtable[2]{};
inline unsigned* resource_object=resource_vtable;
inline unsigned xml_owner[3]{};
inline void* __cdecl resource_get(){return &resource_object;}
inline unsigned __fastcall bind_xml(void* manager,void*){put(manager,0x258c,addr(xml_owner));return 1;}

// A controlled native boot path: real item XMLB, constructors, name interning,
// registration, economy callback, inventory resets and cached-buffer release.
// Resource notifications, disk I/O and CRT exit registration are environment
// edges. No game entry point, renderer, input or game window is run.
inline int run(const char* path){
 failures=0;
 std::ifstream file(path,std::ios::binary);
 std::vector<unsigned char> xml(std::istreambuf_iterator<char>(file),{});
 if(xml.size()<24||read<unsigned>(xml.data())!=0x11b1){std::puts("invalid boot-probe XMLB");return 9;}
 unsigned item_count=0,remaining=unsigned(xml.size()/16);
 auto offset=[&](unsigned at){if(at>xml.size()-4)throw std::runtime_error("XMLB field out of range");return read<unsigned>(xml.data(),at);};
 auto string_at=[&](unsigned at)->const char* {if(at>=xml.size()||!std::memchr(xml.data()+at,0,xml.size()-at))throw std::runtime_error("XMLB string out of range");return reinterpret_cast<const char*>(xml.data()+at);};
 std::function<void(unsigned,bool)> fix=[&](unsigned off,bool inventory){
  if(off==0xffffffff)return;
  if(!remaining--)throw std::runtime_error("XMLB cycle");
  const auto name=offset(off),sibling=offset(off+4),child=offset(off+8),fields=offset(off+12);
  const bool is_inventory=std::strcmp(string_at(name),"inventory")==0;
  if(inventory&&std::strcmp(string_at(name),"item")==0)++item_count;
  if(fields>(xml.size()-off-16)/8)throw std::runtime_error("XMLB fields out of range");
  for(unsigned at:{off,off+4,off+8}){const auto v=offset(at);if(v!=0xffffffff&&v>=xml.size())throw std::runtime_error("XMLB link out of range");put(xml.data(),at,v==0xffffffff?0u:addr(xml.data()+v));}
  for(unsigned i=0;i<fields*2;++i){const auto at=off+16+i*4,v=offset(at);string_at(v);put(xml.data(),at,addr(xml.data()+v));}
  fix(child,is_inventory);fix(sibling,inventory);
 };
 fix(8,false);
 std::array<unsigned char,64> empty{};
 put(empty.data(),0,0x11b1u);put(empty.data(),4,1u);
 put(empty.data(),8,addr("inventory"));
 cached_input input(xml.data(),unsigned(xml.size()),empty.data(),unsigned(empty.size()));
 check(input.first!=input.second,"two live native file buffers have distinct cache slots");
 xml_owner[0]=addr(input.first_buffer());
 resource_vtable[1]=addr(&zero2);
 jump(0x563070,&resource_get);jump(0x47b4e0,&bind_xml);jump(0x67211e,&no_atexit);
 const auto manager=native<void*(__cdecl*)()>(0x480a00)();
 const auto money=native<unsigned(__fastcall*)(void*,void*,const char*)>(0x47e320)(manager,nullptr,"MONEY1");
 std::printf("native boot: definitions=%u expected=%u MONEY1=%08x\n",read<unsigned short>(manager,0x2588),item_count,money);
 check(read<unsigned short>(manager,0x2588)==item_count&&money!=0xffffffff,"real XMLB registration resolves MONEY1 under W2");
 if(money!=0xffffffff){
  for(unsigned i=0;i<3;++i){
   native<void(__fastcall*)(void*,void*)>(0x47f5d0)(manager,nullptr);
   check(native<unsigned(__fastcall*)(void*,void*,const char*)>(0x47e320)(manager,nullptr,"MONEY1")==money,"MONEY1 survives native resetgame inventory path");
  }
 }
 check(release_cached_inputs(input,xml.data(),empty.data()),"native menu file-buffer release frees each source once");
 std::printf("W2 boot probe: %d failure(s)\n",failures);
 return failures?8:0;
}
}
