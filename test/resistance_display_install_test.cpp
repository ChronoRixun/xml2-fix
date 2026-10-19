#include "resistance_display.hpp"
#include "resistance_display_rules.hpp"
#include "ini.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

namespace
{
 std::optional<std::string> setting="xml1";
 bool fatal=false;
 unsigned calls=0;
 unsigned native_calls=0;
 bool original=true;
 unsigned char actor[0x580]{};
 unsigned char __fastcall query(void* who,void*,int attribute,float* low,float* high,const void* context,int mode)
 {
  assert(who==actor && native_calls==7);++calls;
  const auto* words=static_cast<const std::uint32_t*>(context);
  for(unsigned i=0;i<0x64/4;++i)if(i!=4)assert(words[i]==0);
  *low=*high=mode==1 ? 1.f : 0.f;
  if(original && attribute==56 && words[4]==0x80){*low=*high=.2f;return 1;}
  return 0;
 }
 const char* __fastcall get_text(void*,void*,int id)
 {
  if(id==202)return "reduced %s";
  if(id==210)return "amber";
  assert(false);return "";
 }
 std::uintptr_t table[]{0,0,reinterpret_cast<std::uintptr_t>(&get_text)};
 std::uintptr_t* manager=table;
 void* __cdecl get_manager(){return &manager;}
 const char* __fastcall native_formatter(void*,void*,void* who,int attribute,const void* context)
 {
  assert(who==actor && !context && attribute==13+int(native_calls));++native_calls;
  if(attribute==16)return "extra ";
  return !original && attribute==13 ? "native " : "";
 }
 void jump(std::uint32_t at,const void* to)
 {
  auto* p=reinterpret_cast<unsigned char*>(at);p[0]=0xe9;
  const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(to))-(at+5);
  std::memcpy(p+1,&delta,4);
 }
}
namespace ini
{
 std::optional<std::string> text(const wchar_t*,const wchar_t*){return setting;}
 bool flag(const wchar_t*,const wchar_t*,bool fallback){return fallback;}
}
namespace logger {void write(const char*,...) {}}
namespace game_version {void refuse_startup(){fatal=true;}}

int main(int argc,char** argv)
{
 assert(argc==3);const std::string mode=argv[2];
 std::ifstream file(argv[1],std::ios::binary);
 const std::vector<unsigned char> image((std::istreambuf_iterator<char>(file)),{});assert(image.size()>0x1000);
 auto u16=[&](std::size_t p){std::uint16_t n;std::memcpy(&n,image.data()+p,2);return n;};
 auto u32=[&](std::size_t p){std::uint32_t n;std::memcpy(&n,image.data()+p,4);return n;};
 const auto pe=u32(0x3c),sections=pe+24+u16(pe+20);
 auto seed=[&](std::uint32_t va,std::size_t size)
 {
  for(unsigned i=0;i<u16(pe+6);++i)
  {
   const auto row=sections+40*i,rva=u32(row+12);
   if(va-0x400000>=rva && va-0x400000+size<=rva+u32(row+16))
   {
    DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(va),size,PAGE_EXECUTE_READWRITE,&old));
    std::memcpy(reinterpret_cast<void*>(va),image.data()+u32(row+20)+va-0x400000-rva,size);return;
   }
  }
  assert(false);
 };
 seed(0x54ff80,16);seed(0x4bdb30,16);seed(0x543f80,16);seed(0x53d9c0,16);seed(0x69786c,4);
 original=mode!="on-native";
 const auto plan=resistance_display_rules::patches(nullptr);
 seed(resistance_display_rules::site,plan[0].before.size()+1);
 if(mode=="off")setting.reset();if(mode=="xml2")setting="xml2";if(mode=="invalid")setting="bad";
 if(mode=="guard0")*reinterpret_cast<unsigned char*>(resistance_display_rules::site)^=1;
 if(mode=="guard1")*reinterpret_cast<unsigned char*>(0x54ff80)^=1;
 if(mode=="guard2")*reinterpret_cast<unsigned char*>(0x4bdb30)^=1;
 if(mode=="guard3")*reinterpret_cast<unsigned char*>(0x543f80)^=1;
 if(mode=="guard4")*reinterpret_cast<unsigned char*>(0x53d9c0)^=1;
 if(mode=="guard5")*reinterpret_cast<unsigned char*>(0x69786c)^=1;
 const std::vector<unsigned char> before(reinterpret_cast<unsigned char*>(resistance_display_rules::site),reinterpret_cast<unsigned char*>(resistance_display_rules::resume));
 resistance_display::install(reinterpret_cast<HMODULE>(mode=="base" ? 0x500000 : 0x400000));
 const bool success=mode=="on" || mode=="on-native";
 assert(!fatal && resistance_display::installed()==success);
 if(success)
 {
  jump(0x54ff80,reinterpret_cast<const void*>(&query));jump(0x4bdb30,reinterpret_cast<const void*>(&get_manager));
  jump(0x543f80,reinterpret_cast<const void*>(&get_manager));jump(0x53d9c0,reinterpret_cast<const void*>(&native_formatter));
  *reinterpret_cast<unsigned char*>(resistance_display_rules::resume)=0xc3;
  // Run the emitted fragment at its real address. The caller recreates its
  // native stack-relative subtitle slot, with EDI holding the selected actor.
  char* buffer=nullptr;std::uintptr_t before_stack=0,after_stack=0;
  __asm {
   push ebx
   push edi
   mov ebx,esp
   mov before_stack,esp
   sub esp,200h
   lea eax,[esp+128h]
   mov buffer,eax
   mov dword ptr[eax],20676174h
   mov byte ptr[eax+4],0
   lea edi,actor
   mov eax,5a8dech
   call eax
   add esp,200h
   mov after_stack,esp
   mov esp,ebx
   pop edi
   pop ebx
  }
  assert(before_stack==after_stack && calls==9 && native_calls==7);
  assert(std::strcmp(buffer,original ? "tag ~22reduced amber\nextra " : "tag native extra ")==0);
 }
 else assert(std::memcmp(reinterpret_cast<void*>(resistance_display_rules::site),before.data(),before.size())==0);
 std::cout<<"production resistance display "<<mode<<": PASS\n";
}
