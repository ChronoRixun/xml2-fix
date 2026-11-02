#include "lc_fonts.hpp"
#include "lc_fonts_plan.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>

namespace lc_fonts {
namespace {
using namespace lc_fonts_rules;
alignas(16) std::array<std::uint8_t,manager_size> manager{};
const char* font_names[]{"medium","big","hud","digital","lc_title","lc_row","lc_body","lc_label"};
const char map_pc[]="ui/fonts/lc_fonts_pc.xmlb",map_hd[]="ui/fonts/lc_fonts_hd.xmlb";
struct style_scale {void* style=nullptr;float value=1;};
std::array<style_scale,32> scales{};
bool installed=false;
thread_local bool scoped=false;
void* trampoline=nullptr;
DWORD style_resume=0x5d7ea8,draw_resume=0x5c6373,clip_resume=0x5c62fa;
DWORD address(const void* p) {return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p));}
bool ready(int index) {
 if(index<4||index>=8)return false;
 const auto* p=manager.data()+index*stride;
 std::uint32_t texture=0;std::int16_t height=0;
 std::memcpy(&texture,p+stride,4);std::memcpy(&height,p+6,2);
 return texture!=0&&height>0;
}
// Parser is the retail style node; the style record remains its original size.
void __fastcall read_style(void* style,void*,void* parser) {
 using get_t=const char*(__fastcall*)(void*,void*,const char*,const char*);
 const auto get=reinterpret_cast<get_t>(0x564b70);
 const char* name=get(parser,nullptr,"font","");
 int index=font_index(name?name:"");
 // Preserve the game's own CRT comparison/locale for all legacy values.
 if(index<4)index=reinterpret_cast<int(__cdecl*)(const char*,const char*)>(0x672562)(name?name:"","big")==0?1:0;
 const bool requested=index>=4;
 const bool custom=index>=4&&ready(index);
 if(index>=4&&!custom)index=0;
 *reinterpret_cast<short*>(static_cast<std::uint8_t*>(style)+8)=static_cast<short>(index);
 for(auto& entry:scales)if(entry.style==style||!entry.style) {
  const char* value=custom?get(parser,nullptr,"textscale","1"):"1";
  entry={style,scale(value?value:"")};
  if(requested)logger::write("lc fonts: font=%s slot=%d textscale=%.6g %s",name,index,static_cast<double>(entry.value),custom?"ready":"classic fallback (slot not ready)");
  break;
 }
}
// The engine's screen height (0x5fad5c stores it, 0x5fa066 divides by it; both guarded).
constexpr std::uintptr_t screen_height_va=0xa0a000;
std::uint32_t __cdecl scale_bits(const void* style) {
 for(const auto& entry:scales)if(entry.style==style) {
  const bool custom=style&&ready(*reinterpret_cast<const short*>(static_cast<const std::uint8_t*>(style)+8));
  if(!custom)return std::bit_cast<std::uint32_t>(entry.value);
  return std::bit_cast<std::uint32_t>(glyph_scale(entry.value,*reinterpret_cast<const volatile int*>(screen_height_va)));
 }
 return 0x3f800000;
}
__declspec(naked) void style_stub() {
 __asm {
  pushfd
  pushad
  lea eax,[esp+48h]
  push eax
  mov ecx,esi
  call read_style
  popad
  popfd
  jmp dword ptr [style_resume]
 }
}
// Replace a PUSH, preserving every register and EFLAGS. The result occupies the
// exact original argument slot. The clipping path must use the same glyph scale.
__declspec(naked) void draw_scale_stub() {
 __asm {
  lea esp,[esp-4]
  pushfd
  pushad
  push dword ptr [esi+94h]
  call scale_bits
  add esp,4
  mov [esp+24h],eax
  popad
  popfd
  jmp dword ptr [draw_resume]
 }
}
__declspec(naked) void clip_scale_stub() {
 __asm {
  lea esp,[esp-4]
  pushfd
  pushad
  push dword ptr [esi+94h]
  call scale_bits
  add esp,4
  mov [esp+24h],eax
  popad
  popfd
  jmp dword ptr [clip_resume]
 }
}
void __fastcall render_item(void* item,void* edx) {
 auto* bytes=static_cast<std::uint8_t*>(item);
 const auto* style=*reinterpret_cast<std::uint8_t**>(bytes+0x94);
 const bool previous=scoped;
 scoped=style&&ready(*reinterpret_cast<const short*>(style+8));
 reinterpret_cast<void(__fastcall*)(void*,void*)>(trampoline)(item,edx);
 scoped=previous;
}
}
bool prompt_scope() {return installed&&scoped;}
bool active() {return installed;}
void install(HMODULE game) {
 if(!enabled(ini::text(L"Game",L"LegendsClassicFonts").value_or("")))return;
 if(reinterpret_cast<std::uintptr_t>(game)!=0x400000)return;
 // Refuse before changing paths when the build lacks a complete private family.
 for(const char* name:{"UI/fonts/lc_fonts_pc.XMLB","UI/fonts/lc_fonts_hd.XMLB",
    "UI/fonts/lc_online_title.XMLB","UI/fonts/lc_online_row.XMLB","UI/fonts/lc_online_body.XMLB","UI/fonts/lc_online_label.XMLB",
    "Textures/fonts/lc_online_title.IGB","Textures/fonts/lc_online_row.IGB","Textures/fonts/lc_online_body.IGB","Textures/fonts/lc_online_label.IGB"}) {
  std::error_code error;const auto size=std::filesystem::file_size(logger::module_dir()/name,error);
  if(error||!size) {logger::write("lc fonts: missing private font asset %s; classic fonts retained",name);return;}
 }
 auto c=lc_fonts_plan::make({address(manager.data()),address(font_names),address(reinterpret_cast<void*>(&style_stub)),
  address(reinterpret_cast<void*>(&render_item)),address(reinterpret_cast<void*>(&draw_scale_stub)),address(reinterpret_cast<void*>(&clip_scale_stub)),address(map_pc),address(map_hd)});
 std::string why;
 if(!patch_transaction::verify_expectations(c,why)) {logger::write("lc fonts: %s; classic fonts retained",why.c_str());return;}
 // The original entry's single stolen instruction has no relative operands.
 auto code=patch_transaction::bytes_of("81ec18010000");
 trampoline=VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!trampoline) {logger::write("lc fonts: trampoline allocation refused; classic fonts retained");return;}
 const auto tail=patch_transaction::relocation(address(trampoline)+6,"e9",true,0x5c61d6,0,"render trampoline").bytes;
 code.insert(code.end(),tail.begin(),tail.end());std::memcpy(trampoline,code.data(),code.size());
 DWORD old=0;
 if(!VirtualProtect(trampoline,32,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),trampoline,32)) {
  VirtualFree(trampoline,0,MEM_RELEASE);trampoline=nullptr;return;
 }
 c.publish=[] {installed=true;};
 c.abandon=[] {VirtualFree(trampoline,0,MEM_RELEASE);trampoline=nullptr;};
 c.retain=[] {logger::write("lc fonts: rollback uncertified; stopping before engine startup");TerminateProcess(GetCurrentProcess(),1);};
 patch_transaction::transaction transaction;transaction.add(std::move(c));
 const auto result=transaction.commit();
 logger::write("lc fonts: %s (%s); eight slots, private style names and text scale per screen pixel (lc-fonts-screen/1)",patch_transaction::name(result),transaction.report().c_str());
}
}
