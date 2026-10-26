#pragma once
#include <Windows.h>
#include "sha256.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace handler_fixture {
using u32=std::uint32_t;
inline int checks=0, failures=0;
inline void check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);} }
template<class T> T& field(void* p,unsigned offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset); }
inline unsigned char* at(u32 a) { return reinterpret_cast<unsigned char*>(a); }
struct image {
 std::vector<unsigned char> bytes;
 struct section { u32 va,offset,size; };
 std::vector<section> sections;
 u32 word(std::size_t a) const { u32 v=0;std::memcpy(&v,bytes.data()+a,4);return v; }
 unsigned short half(std::size_t a) const { unsigned short v=0;std::memcpy(&v,bytes.data()+a,2);return v; }
 image(const char* path,const char* expected) {
  std::ifstream f(path,std::ios::binary);bytes={std::istreambuf_iterator<char>(f),{}};
  if(bytes.size()<0x200){check(false,"owner binary is required");return;}
  auto hex=[](const std::vector<unsigned char>& b){const auto hash=sha256_fips::digest(b.data(),b.size());std::string s;for(auto x:hash){char v[3];std::snprintf(v,sizeof v,"%02x",x);s+=v;}return s;};
  std::string actual=hex(bytes);
  // A Legends Classic build's own XMen2.exe is the retail exe with the large-address-aware flag (E12): compared
  // with the flag cleared, so the audited bytes are still the retail ones.
  if(actual!=expected && word(0)!=0x48454258 && word(0x3c)+0x18<bytes.size()){auto copy=bytes;copy[word(0x3c)+0x16]&=~0x20;actual=hex(copy);}
  check(actual==expected,"owner executable SHA-256");
  if(actual!=expected)return;
  if(word(0)==0x48454258){
   const auto headers=word(0x120)-word(0x104);
   for(u32 i=0;i<word(0x11c);++i){auto row=headers+0x38*i;sections.push_back({word(row+4),word(row+12),word(row+16)});}
  } else {
   const auto pe=word(0x3c),headers=pe+24+half(pe+20),base=word(pe+24+28);
   for(unsigned i=0;i<half(pe+6);++i){auto row=headers+40*i;sections.push_back({base+word(row+12),word(row+20),word(row+16)});}
  }
 }
 const unsigned char* read(u32 va,std::size_t n) const {
  for(auto s:sections)if(va>=s.va && va-s.va+n<=s.size && s.offset+va-s.va+n<=bytes.size())return bytes.data()+s.offset+va-s.va;
  return nullptr;
 }
 void load(u32 va,std::size_t n,u32 bias=0) const {
  auto p=read(va,n);check(p!=nullptr,"binary range exists");if(!p)return;std::memcpy(at(va+bias),p,n);
  // Uniform code relocation leaves relative branches intact. Only these
  // absolute data references occur in the executed original callback slices.
  if(bias)for(std::size_t i=0;i+4<=n;++i) {
   u32 v;std::memcpy(&v,at(va+bias)+i,4);
   if(v==0x3d640c||v==0x3d6410||v==0x3d6414||v==0x3d641c||v==0x3caa50||v==0x3c6de4||v==0x3dd054){v+=bias;std::memcpy(at(va+bias)+i,&v,4);i+=3;}
  }
 }
};
inline void jump(u32 va,const void* target) { at(va)[0]=0xe9;const auto d=static_cast<u32>(reinterpret_cast<std::uintptr_t>(target))-(va+5);std::memcpy(at(va)+1,&d,4); }
inline constexpr const char* pc_hash="146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b";
inline constexpr const char* x1_hash="2ea531f11e0b5b7ca651485012b871ef99a5099c566a6d7cb8ec327ee73d62ac";
}
