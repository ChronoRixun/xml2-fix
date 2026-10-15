#include "lockon_effects.hpp"
#include "lockon_effects_rules.hpp"
#include "gameplay_capabilities.hpp"
#include "entity_capacity.hpp"
#include "ini.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

namespace {
std::optional<std::string> aim, paths;
bool fatal=false;
}
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t* key) {
 return std::wcscmp(key,L"Xml1LockOnAim")==0 ? aim : paths;
}
bool flag(const wchar_t*,const wchar_t*,bool fallback) { return fallback; }
}
namespace logger { void write(const char*,...) {} }
namespace game_version { void refuse_startup() { fatal=true; } }
namespace entity_capacity { std::uint32_t installed_target(std::uint32_t) { return 0; } }

int main(int argc,char** argv) {
 assert(argc==3);
 const std::string mode=argv[2];
 std::ifstream file(argv[1],std::ios::binary);
 const std::vector<unsigned char> image((std::istreambuf_iterator<char>(file)),{});
 assert(image.size()>0x1000);
 auto u16=[&](std::size_t p){std::uint16_t n;std::memcpy(&n,image.data()+p,2);return n;};
 auto u32=[&](std::size_t p){std::uint32_t n;std::memcpy(&n,image.data()+p,4);return n;};
 const auto pe=u32(0x3c),sections=pe+24+u16(pe+20);
 auto seed=[&](std::uint32_t va,std::size_t size) {
  for(unsigned i=0;i<u16(pe+6);++i) {
   const auto row=sections+40*i,rva=u32(row+12);
   if(va-0x400000>=rva && va-0x400000+size<=rva+u32(row+16)) {
    DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(va),size,PAGE_EXECUTE_READWRITE,&old));
    std::memcpy(reinterpret_cast<void*>(va),image.data()+u32(row+20)+va-0x400000-rva,size);return;
   }
  }
  assert(false);
 };
 for(const auto& g:lockon_effects_rules::guards)seed(g.va,g.hex.size()/2);
 for(const auto& s:lockon_effects_rules::sites)seed(s.address,s.before.size()/2);
 for(auto va:{0x4a95f0u,0x4a9600u,0x4a9590u})seed(va,6);
 seed(0x4eefb1,1);
 paths="invented/beam,invented/surface,invented/target";
 aim="1";
 if(mode=="off") {aim.reset();paths.reset();}
 if(mode=="visual")aim="0";
 if(mode=="no-effects")paths.reset();
 if(mode=="bad-paths")paths="../escape,invented/surface,invented/target";
 if(mode=="empty-effects")paths="";
 if(mode=="effects-off")paths="0";
 if(mode=="both-off") {aim="0";paths="0";}
 if(mode=="invalid")aim="typo";
 if(mode.starts_with("guard")) {
  const auto n=unsigned(mode.back()-'0');assert(n<5);
  const auto va=n<4 ? lockon_effects_rules::sites[n].address : 0x4eefb1;
  *reinterpret_cast<unsigned char*>(va)^=1;
 }
 if(mode=="dependency")*reinterpret_cast<unsigned char*>(0x4a95f0)^=1;
 std::vector<std::vector<unsigned char>> before;
 for(const auto& s:lockon_effects_rules::sites)
  before.emplace_back(reinterpret_cast<unsigned char*>(s.address),reinterpret_cast<unsigned char*>(s.address+s.before.size()/2));
 const auto head=*reinterpret_cast<unsigned char*>(0x4eefb1);
 lockon_effects::install(reinterpret_cast<HMODULE>(mode=="base" ? 0x500000 : 0x400000));
 const bool wanted=mode!="off" && mode!="visual" && mode!="both-off",success=mode=="on" || mode=="visual";
 assert(!fatal && lockon_effects::requested()==wanted);
 assert(lockon_effects::installed()==(mode=="on"));
 assert(lockon_effects::verified()==(!wanted || mode=="on"));
 for(std::size_t i=0;i<before.size();++i) {
  const auto va=lockon_effects_rules::sites[i].address;
  assert(success ? *reinterpret_cast<unsigned char*>(va)==0xe9 : std::memcmp(reinterpret_cast<void*>(va),before[i].data(),before[i].size())==0);
 }
 assert(*reinterpret_cast<unsigned char*>(0x4eefb1)==(mode=="on" ? 0 : head));
 gameplay_capabilities::record actual;
 if(wanted)actual.add(gameplay_capabilities::lockon_aim_id,1,{lockon_effects::installed() ? 1u : 0u,0,0,0},lockon_effects::verified());
 assert(actual.finalize()==(!wanted || mode=="on"));
 if(!wanted) {
  gameplay_capabilities::record stock;stock.finalize();
  assert(std::equal(actual.bytes().begin(),actual.bytes().end(),stock.bytes().begin(),stock.bytes().end()));
 }
 std::cout<<"production lock-on installer "<<mode<<": PASS\n";
}
