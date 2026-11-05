// Retail routines run inside a synthetic host. Never enters the game program.
#include "../src/lc_fonts.cpp"
#include "../src/pad_prompts.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <cstdarg>
extern "C" unsigned char union_space[];
static std::optional<std::string> option="1";
static std::filesystem::path asset_root;
namespace ini {std::optional<std::string> text(const wchar_t*,const wchar_t*) {return option;}}
namespace logger {
void write(const char* fmt,...) {va_list args;va_start(args,fmt);std::vprintf(fmt,args);va_end(args);std::puts("");}
const std::filesystem::path& module_dir() {return asset_root;}
}
using bytes=std::vector<std::uint8_t>;
static unsigned checks=0;
static void check(bool condition) {++checks;if(!condition)std::fprintf(stderr,"Failed check %u\n",checks);assert(condition);}
static DWORD addr(const void* p) {return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p));}
static void* ptr(DWORD va) {return reinterpret_cast<void*>(va);}
static bytes raw;
static void load() {
 DWORD old=0;check(VirtualProtect(union_space,0x700000,PAGE_EXECUTE_READWRITE,&old));
 std::memset(union_space,0,0x700000);
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data())->e_lfanew);
 const auto* sections=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
  std::memcpy(ptr(0x400000+sections[i].VirtualAddress),raw.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
 if(lc_fonts::trampoline)VirtualFree(lc_fonts::trampoline,0,MEM_RELEASE);
 lc_fonts::trampoline=nullptr;lc_fonts::installed=false;lc_fonts::scoped=false;
 lc_fonts::manager.fill(0);lc_fonts::scales.fill({});
}
static void jump(DWORD va,const void* target) {
 const auto b=patch_transaction::relocation(va,"e9",true,addr(target),0,"host dependency").bytes;
 std::memcpy(ptr(va),b.data(),b.size());
}
static void __cdecl no_atexit(void*) {}
__declspec(naked) static void cookie() {__asm ret}
__declspec(naked) static void integer() {
 __asm {sub esp,8
  fistp qword ptr [esp]
  pop eax
  add esp,4
  ret}
}
static void __fastcall concat(void*,void*,const char*) {}
static void __fastcall parser_empty(void*,void*) {}
static bool __fastcall parser_load(void*,void*,const char*,int,int,int) {return true;}
struct node {DWORD unused=0;node* next=nullptr;node* child=nullptr;int glyph=-1;};
static node table;
static std::array<node,256> glyphs;
static node* __fastcall find(void*,void*,const char*) {return &table;}
static const char* __fastcall attribute(node* p,void*,const char* key) {
 static std::string answer;
 std::string lower(key);std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});key=lower.c_str();
 if(p->glyph<0) {
  if(std::strcmp(key,"height")==0)return "40";
  if(std::strcmp(key,"ascender")==0)return "32";
  if(std::strcmp(key,"descender")==0)return "8";
  return "32";
 }
 const int i=p->glyph;
 if(std::strcmp(key,"width")==0)answer=std::to_string(i%17+1);
 else if(std::strcmp(key,"height")==0)answer="25";
 else if(std::strcmp(key,"horizoffset")==0)answer="-2";
 else if(std::strcmp(key,"baseline")==0)answer="24";
 else if(std::strcmp(key,"horizadvance")==0)answer=std::to_string(i%17+2);
 else if(std::strcmp(key,"s")==0)answer="0.1";
 else if(std::strcmp(key,"t")==0)answer="0.2";
 else if(std::strcmp(key,"s2")==0)answer="0.3";
 else answer="0.4";
 return answer.c_str();
}
static double __cdecl number(const char* s) {return std::atof(s);}
static int __cdecl whole(const char* s) {return std::atoi(s);}
static double __cdecl floor_value(double d) {return std::floor(d);}
static char* __cdecl copy_string(char* dest,const char* src,std::size_t n) {return std::strncpy(dest,src,n);}
static float __fastcall display_scale(void*,void*) {return 1;}
static std::array<void*,24> display_vtable{};
static void* display_object=display_vtable.data();
static void* __cdecl display() {return &display_object;}
static void dependencies() {
 jump(0x67211e,reinterpret_cast<void*>(&no_atexit));jump(0x672161,reinterpret_cast<void*>(&cookie));
 jump(0x67217c,reinterpret_cast<void*>(&integer));jump(0x425b10,reinterpret_cast<void*>(&concat));
 jump(0x564a80,reinterpret_cast<void*>(&parser_empty));jump(0x564d00,reinterpret_cast<void*>(&parser_empty));
 jump(0x564eb0,reinterpret_cast<void*>(&parser_load));jump(0x564ac0,reinterpret_cast<void*>(&find));
 jump(0x5649c0,reinterpret_cast<void*>(&attribute));jump(0x672210,reinterpret_cast<void*>(&number));
 jump(0x67233c,reinterpret_cast<void*>(&whole));jump(0x6721f8,reinterpret_cast<void*>(&floor_value));
 jump(0x672170,reinterpret_cast<void*>(&copy_string));
 display_vtable[23]=reinterpret_cast<void*>(&display_scale);jump(0x5f6e50,reinterpret_cast<void*>(&display));
 table.child=glyphs.data();
 for(unsigned i=0;i<256;++i) {glyphs[i].glyph=i;glyphs[i].next=i<255?&glyphs[i+1]:nullptr;}
}
static void refusals() {
 for(const auto value:{"","0","off","true","bad"}) {
  load();option=value;const bytes before(union_space,union_space+0x700000);
  lc_fonts::install(reinterpret_cast<HMODULE>(0x400000));
  check(std::memcmp(before.data(),union_space,before.size())==0);check(!lc_fonts::installed);
 }
 option="1";
 for(const auto& site:lc_fonts_plan::sites) {
  load();*static_cast<unsigned char*>(ptr(site.va))^=1;
  const bytes before(union_space,union_space+0x700000);
  lc_fonts::install(reinterpret_cast<HMODULE>(0x400000));
  check(!lc_fonts::installed);check(std::memcmp(before.data(),union_space,before.size())==0);
 }
 for(const auto& region:lc_fonts_plan::regions) {
  load();*static_cast<unsigned char*>(ptr(region.lo+7))^=1;
  const bytes before(union_space,union_space+0x700000);
  lc_fonts::install(reinterpret_cast<HMODULE>(0x400000));
  check(!lc_fonts::installed);check(std::memcmp(before.data(),union_space,before.size())==0);
 }
 for(const DWORD va:{0x6e4344,0x81d268,0x687fdc,0x69d624,0x69d620,0x69d618,0x672562}) {
  load();*static_cast<unsigned char*>(ptr(va))^=1;const bytes before(union_space,union_space+0x700000);
  lc_fonts::install(reinterpret_cast<HMODULE>(0x400000));check(!lc_fonts::installed);
  check(std::memcmp(before.data(),union_space,before.size())==0);
 }
 load();const bytes before(union_space,union_space+0x700000);
 const auto saved=asset_root;asset_root/="missing";
 lc_fonts::install(reinterpret_cast<HMODULE>(0x400000));asset_root=saved;
 check(!lc_fonts::installed);check(std::memcmp(before.data(),union_space,before.size())==0);
 load();lc_fonts::install(reinterpret_cast<HMODULE>(0x500000));check(!lc_fonts::installed);
}
static lc_fonts_plan::targets targets(DWORD manager) {
 return {manager,addr(lc_fonts::font_names),addr(reinterpret_cast<void*>(&lc_fonts::style_stub)),
  addr(reinterpret_cast<void*>(&lc_fonts::render_item)),addr(reinterpret_cast<void*>(&lc_fonts::draw_scale_stub)),
  addr(reinterpret_cast<void*>(&lc_fonts::clip_scale_stub)),addr(lc_fonts::map_pc),addr(lc_fonts::map_hd),addr(reinterpret_cast<void*>(&lc_fonts::list_clip_stub)),
  addr(reinterpret_cast<void*>(&lc_fonts::list_draw_stub)),addr(reinterpret_cast<void*>(&lc_fonts::list_height_stub))};
}
static void faults_and_canaries() {
 for(bool rollback:{false,true}) {
  load();const bytes before(union_space,union_space+0x700000);bool published=false,abandoned=false;
  auto c=lc_fonts_plan::make(targets(addr(lc_fonts::manager.data())));
  c.publish=[&]{published=true;};c.abandon=[&]{abandoned=true;};
  patch_transaction::transaction transaction;check(transaction.add(std::move(c)));
  patch_transaction::hooks hooks;
  if(rollback)hooks.after_apply=[] {*static_cast<unsigned char*>(ptr(0x597c41))^=1;};
  else hooks.protect=[](void*,std::size_t,DWORD,DWORD*){return false;};
  const auto result=transaction.commit(&hooks);
  check(result==(rollback?patch_transaction::outcome::rolled_back:patch_transaction::outcome::refused_protect));
  check(!published&&abandoned);check(std::memcmp(before.data(),union_space,before.size())==0);
 }
 load();bytes backing(lc_fonts_rules::manager_size+128,0xa5);
 auto* manager=backing.data()+64;std::memset(manager,0,lc_fonts_rules::manager_size);
 patch_transaction::transaction transaction;check(transaction.add(lc_fonts_plan::make(targets(addr(manager)))));
 check(transaction.commit()==patch_transaction::outcome::committed);dependencies();
 check(reinterpret_cast<void*(__cdecl*)()>(0x597c20)()==manager);
 using loader=bool(__fastcall*)(void*,void*,const char*,int);
 check(reinterpret_cast<loader>(0x596af0)(manager,nullptr,"synthetic",7));
 check(std::all_of(backing.begin(),backing.begin()+64,[](auto b){return b==0xa5;}));
 check(std::all_of(backing.end()-64,backing.end(),[](auto b){return b==0xa5;}));
}
static void readers() {
 load();option="1";lc_fonts::install(reinterpret_cast<HMODULE>(0x400000));check(lc_fonts::installed);
 dependencies();
 using getter=void*(__cdecl*)();auto* manager=static_cast<std::uint8_t*>(reinterpret_cast<getter>(0x597c20)());
 check(manager==lc_fonts::manager.data());
 using loader=bool(__fastcall*)(void*,void*,const char*,int);
 using read_int=int(__fastcall*)(void*,void*,int);
 using read_float=float(__fastcall*)(void*,void*,int);
 using glyph_table=void*(__fastcall*)(void*,void*,int);
 using texture_get=void*(__fastcall*)(void*,void*,void*,int);
 using measure=float(__fastcall*)(void*,void*,const char*,float,int);
 for(int index=0;index<8;++index) {
  std::printf("slot %d at check %u\n",index,checks);
  check(reinterpret_cast<loader>(0x596af0)(manager,nullptr,"synthetic",index));
  check(reinterpret_cast<read_int>(0x5971a0)(manager,nullptr,index)==40);
  check(reinterpret_cast<read_int>(0x5971c0)(manager,nullptr,index)==32);
  check(reinterpret_cast<read_int>(0x5971e0)(manager,nullptr,index)==8);
  check(reinterpret_cast<read_int>(0x597200)(manager,nullptr,index)==32);
  auto* records=static_cast<std::uint8_t*>(reinterpret_cast<glyph_table>(0x597280)(manager,nullptr,index));
  check(records==manager+index*lc_fonts_rules::stride+24);
  for(unsigned i=0;i<256;++i) {
   const auto code=i==160?32:i;
   check(*reinterpret_cast<short*>(records+i*28)==static_cast<short>(code%17+1));
   check(*reinterpret_cast<short*>(records+i*28+2)==25);
   check(*reinterpret_cast<short*>(records+i*28+4)==static_cast<short>(code%17+2));
   check(*reinterpret_cast<short*>(records+i*28+6)==-2);
   check(*reinterpret_cast<int*>(records+i*28+8)==24);
  }
  const DWORD texture=0x12340000+index;
  std::memcpy(manager+(index+1)*lc_fonts_rules::stride,&texture,4);
  DWORD result=0;reinterpret_cast<texture_get>(0x5972a0)(manager,nullptr,&result,index);check(result==texture);
  *reinterpret_cast<float*>(manager+index*lc_fonts_rules::stride+16)=.25f;
  *reinterpret_cast<float*>(manager+index*lc_fonts_rules::stride+20)=.5f;
  check(reinterpret_cast<read_float>(0x597220)(manager,nullptr,index)==.25f);
  check(reinterpret_cast<read_float>(0x597240)(manager,nullptr,index)==.5f);
  *reinterpret_cast<DWORD*>(manager+index*lc_fonts_rules::stride+12)=100+index;
  check(reinterpret_cast<read_int>(0x597260)(manager,nullptr,index)==100+index);
  reinterpret_cast<void(__fastcall*)(void*,void*,int)>(0x597190)(manager,nullptr,index);
  check(*reinterpret_cast<DWORD*>(manager+lc_fonts_rules::slots*lc_fonts_rules::stride+12)==static_cast<DWORD>(index));
  check(reinterpret_cast<measure>(0x596df0)(manager,nullptr,"AAA",.5f,index)==24);
  const char private_text[]{static_cast<char>(0x80),0};
  check(reinterpret_cast<measure>(0x596df0)(manager,nullptr,private_text,.5f,index)==5.5f);
 }
}
static std::string map_index;
static const char* __fastcall map_attribute(void*,void*,const char* key) {
 if(std::strcmp(key,"index")==0)return map_index.c_str();
 return "";
}
static int __cdecl compare(const char* a,const char* b) {return _stricmp(a,b);}
__declspec(naked) static void map_return() {__asm {mov eax,ebx
 ret}}
static int invoke_map() {
 void* hook=ptr(0x5981c0);int result=-1;
 __asm {
  push ebx
  push esi
  push edi
  sub esp,40h
  xor esi,esi
  mov [esp+10h],esi
  call hook
  mov result,eax
  add esp,40h
  pop edi
  pop esi
  pop ebx
 }
 return result;
}
static void maps() {
 jump(0x5649c0,reinterpret_cast<void*>(&map_attribute));jump(0x672562,reinterpret_cast<void*>(&compare));
 jump(0x598220,reinterpret_cast<void*>(&map_return));
 for(unsigned i=0;i<8;++i) {map_index=lc_fonts::font_names[i];check(invoke_map()==static_cast<int>(i));}
 map_index="unknown";check(invoke_map()==0);
 map_index="BIG";check(invoke_map()==1);
 map_index="LC_TITLE";check(invoke_map()==4);
 map_index="MeDiUm";check(invoke_map()==0);
 const auto pc=*reinterpret_cast<const char**>(0x598143),hd=*reinterpret_cast<const char**>(0x598158);
 check(std::strcmp(pc,"ui/fonts/lc_fonts_pc.xmlb")==0);check(std::strcmp(hd,"ui/fonts/lc_fonts_hd.xmlb")==0);
}
static std::string font_name,scale_value;
static DWORD parser_tag=0;
static const char* __fastcall style_attribute(void* parser,void*,const char* key,const char*) {
 // Capture while the synthetic caller frame is live; do not retain/dereference
 // a pointer into that stack after invoke_style has returned.
 parser_tag=*static_cast<const DWORD*>(parser);
 return std::strcmp(key,"font")==0?font_name.c_str():scale_value.c_str();
}
__declspec(naked) static void style_return() {__asm ret}
__declspec(naked) static void scale_return() {__asm {pop eax
 ret}}
static void invoke_style(void* style) {
 void* hook=reinterpret_cast<void*>(&lc_fonts::style_stub);
 __asm {
  push esi
  mov esi,style
  sub esp,40h
  mov dword ptr [esp+20h],12345678h
  call hook
  add esp,40h
  pop esi
 }
}
static DWORD invoke_scale(void* item,bool clip,bool list=false) {
 void* hook=list ? (clip?reinterpret_cast<void*>(&lc_fonts::list_clip_stub):reinterpret_cast<void*>(&lc_fonts::list_draw_stub)) :
  (clip?reinterpret_cast<void*>(&lc_fonts::clip_scale_stub):reinterpret_cast<void*>(&lc_fonts::draw_scale_stub));
 DWORD result=0;
 __asm {
  push esi
  mov esi,item
  call hook
  mov result,eax
  pop esi
 }
 return result;
}
static int invoke_list_height(void* item,int measured) {
 int result;
 void* hook=reinterpret_cast<void*>(&lc_fonts::list_height_stub);
 __asm {
  push esi
  mov esi,item
  mov eax,measured
  call hook
  mov result,eax
  pop esi
 }
 return result;
}
static bool observed=false;
static void __fastcall fake_render(void*,void*) {observed=lc_fonts::prompt_scope();}
static void styles() {
 jump(0x564b70,reinterpret_cast<void*>(&style_attribute));
 lc_fonts::style_resume=addr(reinterpret_cast<void*>(&style_return));
 lc_fonts::draw_resume=lc_fonts::clip_resume=addr(reinterpret_cast<void*>(&scale_return));
 lc_fonts::list_clip_resume=lc_fonts::list_draw_resume=addr(reinterpret_cast<void*>(&scale_return));
 lc_fonts::list_height_resume=addr(reinterpret_cast<void*>(&style_return));
 std::array<std::uint8_t,12> style{};
 std::array<std::uint8_t,0xf0> item{};void* style_pointer=style.data();std::memcpy(item.data()+0x94,&style_pointer,4);
 for(const auto name:{"big","BIG","BiG","medium","hud","HUD","unknown","lc_title","LC_TITLE","lc_row","lc_body","lc_label"}) {
  font_name=name;scale_value="0.375";invoke_style(style.data());
  const auto index=lc_fonts_rules::font_index(name);
  check(*reinterpret_cast<short*>(style.data()+8)==index);
  check(parser_tag==0x12345678);
  const auto wanted=index>=4?std::bit_cast<DWORD>(.375f):0x3f800000;
  check(invoke_scale(item.data(),false)==wanted);check(invoke_scale(item.data(),true)==wanted);
  lc_fonts::trampoline=reinterpret_cast<void*>(&fake_render);lc_fonts::render_item(item.data(),nullptr);
  check(observed==(index>=4));check(!lc_fonts::prompt_scope());lc_fonts::trampoline=nullptr;
 }
 // Live 060p round: private text stayed the same pixel size at 720p and 1080p because the scale was
 // authored in 512 x 384 view units but the renderer draws glyphs in back-buffer pixels. The scale
 // the retail draw/clip pushes now follows the engine's screen height; classic styles stay at 1.
 for(const int height:{720,1080,2160,768,0,100,100000}) {
  *reinterpret_cast<volatile int*>(0xa0a000)=height;
  const float factor=height>=240&&height<=16384?height/384.0f:1.0f;
  for(const auto name:{"lc_row","lc_title","medium","big"}) {
   font_name=name;scale_value="0.1875";invoke_style(style.data());
   const bool custom=lc_fonts_rules::font_index(name)>=4;
   const auto wanted=custom?std::bit_cast<DWORD>(.1875f*factor):0x3f800000;
   check(invoke_scale(item.data(),false)==wanted);check(invoke_scale(item.data(),true)==wanted);
   check(invoke_scale(item.data(),false,true)==wanted);check(invoke_scale(item.data(),true,true)==wanted);
   // Use the actual prepared atlas height; each viewport recomputes from it,
   // never rescales a stored row. Stock styles preserve even a caller override.
   const auto atlas_height=*reinterpret_cast<short*>(lc_fonts::manager.data()+lc_fonts_rules::font_index(name)*lc_fonts_rules::stride+6);
   const int expected_height=custom ? std::max(1,int((atlas_height*.1875f+4)*factor+.5f)) : 27;
   check(invoke_list_height(item.data(),27)==expected_height);
   check(*reinterpret_cast<int*>(item.data()+0xe0)==expected_height);
   check(invoke_list_height(item.data(),27)==expected_height);
  }
 }
 font_name="lc_row";scale_value="0.1875";invoke_style(style.data());*reinterpret_cast<volatile int*>(0xa0a000)=720;
 // 15-unit row text (design grid 640 x 480 -> 12 view units) is 22.5 px at 720p and 33.75 px at 1080p.
 check(std::abs(std::bit_cast<float>(invoke_scale(item.data(),false))*64-22.5f)<.01f);
 *reinterpret_cast<volatile int*>(0xa0a000)=1080;
 check(std::abs(std::bit_cast<float>(invoke_scale(item.data(),false))*64-33.75f)<.01f);
 *reinterpret_cast<volatile int*>(0xa0a000)=0;
 for(const auto value:{"nan","inf","-1","0","5","0.375garbage"}) {
  font_name="lc_row";scale_value=value;invoke_style(style.data());check(invoke_scale(item.data(),false)==0x3f800000);
 }
 std::memset(lc_fonts::manager.data()+5*lc_fonts_rules::stride+lc_fonts_rules::stride,0,4);
 font_name="lc_row";scale_value=".5";invoke_style(style.data());
 check(*reinterpret_cast<short*>(style.data()+8)==0);check(invoke_scale(item.data(),false)==0x3f800000);
}
static std::string bound_key="Enter";
static const char* __fastcall key_name(const void*,void*,std::uint32_t,std::uint32_t) {return bound_key.c_str();}
static void prompts() {
 using namespace pad_prompts_rules;
 std::array<std::uint8_t,input_size> input{};
 std::array<std::uint8_t,0x990> configuration{};
 auto* map=configuration.data()+map_in_configuration;
 const auto put=[](void* p,DWORD value){std::memcpy(p,&value,4);};
 put(map,50);put(map+map_pad_device,first_pad_device);
 auto* record=map+map_records+action_of(4)*slot_count*record_size;
 put(record,first_pad_device);put(record+4,22);put(record+record_size,keyboard_device);put(record+record_size+4,28);
 *reinterpret_cast<void**>(input_pointer)=input.data();*reinterpret_cast<DWORD*>(row_global)=0;
 *reinterpret_cast<void**>(maps_global)=configuration.data();
 jump(key_name_function,reinterpret_cast<void*>(&key_name));
 pad_prompts::chosen_mode=mode::automatic;pad_prompts::seen.pads_present=1;
 pad_prompts::seen.pads[0]=2;pad_prompts::seen.keyboard=1;
 lc_fonts::scoped=true;check(lc_fonts::prompt_scope());
 auto label=[] {return std::string(pad_prompts::label_for_target({4,0,false},4));};
 check(label()==std::string(1,static_cast<char>(0x80)));
 put(record+4,21);check(label()==std::string(1,static_cast<char>(0x82)));
 pad_prompts::seen.keyboard=3;check(label()==std::string(1,static_cast<char>(0x90)));
 bound_key="V";check(label()==std::string(1,static_cast<char>(0xd5)));
 bound_key="unrecognized";check(label()=="[unrecognized]");
 // The same binding/device state retains the existing labels outside our scope.
 lc_fonts::scoped=false;bound_key="Enter";check(label()=="[Enter]");
 pad_prompts::seen.pads[0]=4;check(label()=="X");
 lc_fonts::scoped=true;lc_fonts::installed=false;check(label()=="X");
 lc_fonts::scoped=false;lc_fonts::installed=true;
 // The co-op screens (060p live round, failure 8): the game draws $MENU_OTHER as the binding of Jump (the pad's Y) and
 // $MENU_NEXT as the binding of Power (RT); inside the scope of the Hub/Host/Code/Browse/Party screens the pad shows X and Y,
 // the buttons the screens read (online_menus_rules.hpp pad_menu_mask). Real label chain, fake maps for row 1 (the menus).
 {
  lc_fonts::scoped=false;lc_fonts::installed=true;
  *reinterpret_cast<DWORD*>(row_global)=1;reinterpret_cast<void**>(maps_global)[4]=configuration.data();
  const auto bind=[&](int code,DWORD device,DWORD control,int slot){
   auto* r=map+map_records+action_of(code)*slot_count*record_size+slot*record_size;put(r,device);put(r+4,control);};
  bind(8,first_pad_device,21,0);   // Guard = pad X
  bind(0x18,first_pad_device,24,0); // Jump = pad Y
  bind(0x18,keyboard_device,0xd3,2); // the menus' third slot: Delete
  bind(0x16,first_pad_device,28,0); // Power = RT
  bind(0x16,keyboard_device,0x4c,2); // KP5
  bound_key="Del";
  pad_prompts::seen.pads_present=1;pad_prompts::seen.pads[0]=9;pad_prompts::seen.keyboard=1; // the pad was last used
  const auto token=[](int code){return std::string(pad_prompts::prompt_label(code));};
  pad_prompts::menu_scope=nullptr;
  check(token(0x18)=="Y" && token(0x16)=="[RT]");                   // the live round's prompts, no scope hook
  pad_prompts::menu_scope=[]{return false;};
  check(token(0x18)=="Y" && token(0x16)=="[RT]");                   // a scope that says no: the same
  check(!pad_prompts::pad_shown(0));                                 // pad_shown is false until the patch is installed
  pad_prompts::menu_scope=[]{return true;};
  check(token(0x18)=="X" && token(0x16)=="Y" && token(0x15)!="X");  // the scope: Change name / Clear = X, Rejoin / Paste = Y
  check(token(4)==std::string(pad_prompts::label_for_target({4,0,false},4)));                                           // every other token is the game's
  pad_prompts::seen.keyboard=20;                                     // the keyboard was last used: the engine's own keys
  check(token(0x18)=="[Del]");                                       // not remapped: Guard has no keyboard binding here
  pad_prompts::menu_scope=nullptr;pad_prompts::seen.keyboard=1;
  *reinterpret_cast<DWORD*>(row_global)=0;
 }
}
int main(int argc,char** argv) {
 std::setvbuf(stdout,nullptr,_IONBF,0);
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);assert(argc==2||argc==3);
 std::ifstream file(argv[1],std::ios::binary);raw=bytes(std::istreambuf_iterator<char>(file),{});check(raw.size()>0x1000);
 if(argc==3)asset_root=argv[2];
 else {
  asset_root=std::filesystem::temp_directory_path()/"lc-font-host-assets";
  for(const char* name:{"UI/fonts/lc_fonts_pc.XMLB","UI/fonts/lc_fonts_hd.XMLB",
     "UI/fonts/lc_online_title.XMLB","UI/fonts/lc_online_row.XMLB","UI/fonts/lc_online_body.XMLB","UI/fonts/lc_online_label.XMLB",
     "Textures/fonts/lc_online_title.IGB","Textures/fonts/lc_online_row.IGB","Textures/fonts/lc_online_body.IGB","Textures/fonts/lc_online_label.IGB"}) {
   const auto path=asset_root/name;std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<"synthetic";
  }
 }
 std::puts("refusals");refusals();std::puts("faults/canaries");faults_and_canaries();
 std::puts("readers");readers();std::puts("maps");maps();std::puts("styles");styles();
 std::puts("device prompts");prompts();
 check(lc_fonts_rules::pad_glyph("X")==0x82);check(lc_fonts_rules::key_glyph("Enter")==0x90);
 check(lc_fonts_rules::key_glyph("Z")==0xd9);check(lc_fonts_rules::key_glyph("unrecognized")==0);
 std::cout<<"lc fonts host: "<<checks<<" checks, 0 failures\n";
}
