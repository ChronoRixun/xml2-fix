// Execute installed roller bridges against mapped owner-supplied retail bytes.
// Engine services are deterministic fixture edges; no game process is launched.
#include "xml1_roll.hpp"
#include "xml1_economy.hpp"
#include "item_capacity_rules.hpp"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <float.h>
#include <iterator>
#include <xmmintrin.h>

extern "C" std::uint8_t fixture_space[0x700000];
namespace item_capacity {void* prepared_definition_picker(){return nullptr;}}
namespace logger {
unsigned tripwire_logs=0,overflow_logs=0;
std::vector<std::string> roll_lines;
void write(const char* format,...){char line[1024];va_list args;va_start(args,format);std::vsnprintf(line,sizeof line,format,args);va_end(args);if(std::strstr(line,"generator tripwire"))++tripwire_logs;if(std::strstr(line,"nesting exceeds four"))++overflow_logs;if(std::strncmp(line,"roll ",5)==0)roll_lines.emplace_back(line);}
}
namespace xml1_economy {
bool enabled=true;
std::vector<xml1_economy_rules::record> rows;
bool installed(){return enabled;}
const std::vector<xml1_economy_rules::record>& records(){return rows;}
const void* economy_definition=nullptr;
bool is_economy(const void* p){return p==economy_definition;}
unsigned constructors=0;
bool constructor_arguments=false;
std::vector<unsigned short> constructor_controls;
std::vector<unsigned> constructor_mxcsr;
descriptor* __fastcall make_instance(void*,void*,descriptor* out,void* definition,int level,int prefixes,int suffixes,int a,int b){unsigned short control;__asm fnstcw control
 const auto mxcsr=_mm_getcsr();constructor_controls.push_back(control);constructor_mxcsr.push_back(mxcsr);++constructors;constructor_arguments=definition!=nullptr&&level==1&&prefixes==0&&suffixes==0&&a==0&&b==0;*out={777,reinterpret_cast<void*>(0x12340000)};return out;}
}
namespace {
using xml1_economy::descriptor;
int checks=0,failures=0;
void check(bool ok,const char* what){++checks;if(!ok)++failures;std::printf("%s %s\n",ok?"PASS":"FAIL",what);}
void* at(unsigned va){return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va));}
unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T read(const void* p,unsigned offset=0){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+offset,sizeof v);return v;}
template<class T>void put(void* p,unsigned offset,T v){std::memcpy(static_cast<unsigned char*>(p)+offset,&v,sizeof v);}
void jump(unsigned va,const void* p){auto w=patch_transaction::relocation(va,"e9",true,addr(p),0,"fixture service");std::memcpy(at(va),w.bytes.data(),w.bytes.size());FlushInstructionCache(GetCurrentProcess(),at(va),w.bytes.size());}
bool load(const char* path){
 std::ifstream f(path,std::ios::binary);std::vector<unsigned char> b{std::istreambuf_iterator<char>(f),{}};if(b.size()<0x1000)return false;
 const auto pe=read<unsigned>(b.data(),0x3c),sections=pe+24+read<unsigned short>(b.data(),pe+20);
 for(unsigned i=0;i<read<unsigned short>(b.data(),pe+6);++i){auto row=sections+i*40,va=0x400000+read<unsigned>(b.data(),row+12),size=read<unsigned>(b.data(),row+16),off=read<unsigned>(b.data(),row+20);if(off+std::uint64_t(size)>b.size()||va<addr(fixture_space)||va+std::uint64_t(size)>addr(fixture_space)+sizeof fixture_space)return false;std::memcpy(at(va),b.data()+off,size);}return true;
}
std::vector<unsigned char> manager(item_capacity_rules::manager_size);
void* manager_vt[0x140/4]{};void* game_vt[0x264/4]{};
struct object {void** vt;};object game{game_vt};
unsigned unit_calls=0,integer_calls=0,lookups=0,cache_calls=0,source_seen=0;
std::vector<float> units;std::vector<int> integers;
float __cdecl unit(char lockstep){check(lockstep==1,"unit uses lockstep stream");auto i=unit_calls++;return i<units.size()?units[i]:0.0f;}
int __cdecl integer(int lo,int hi,char lockstep){check(lockstep==1,"integer uses lockstep stream");auto i=integer_calls++;int n=i<integers.size()?integers[i]:lo;check(n>=lo&&n<=hi,"integer draw stays within requested bounds");return n;}
void* __cdecl get_manager(){return manager.data();}
void* __cdecl registry(){return manager.data();}
int highest_value=20;unsigned highest_calls=0;
int __fastcall highest(void*,void*){++highest_calls;return highest_value;}
bool actor_source=false;
void* __fastcall resolve(void* p,void*){source_seen=read<unsigned>(p);return actor_source?manager.data():nullptr;}
void* __cdecl actor(void* p){return actor_source?p:nullptr;}
int actor_level_value=10;
int __fastcall level(void*,void*){return actor_level_value;}
int __fastcall xtreme(void*,void*){return 0;}
int __fastcall maximum(void*,void*,int){return 10;}
std::vector<std::string> names;
unsigned short hardware_control(){unsigned short control;__asm fnstcw control
 return control;}
std::vector<unsigned short> cache_controls;
std::vector<unsigned> cache_mxcsr;
std::uint16_t __fastcall lookup(void*,void*,const char* name){++lookups;names.emplace_back(name);return static_cast<std::uint16_t>(names.size());}
descriptor* __fastcall cached(void*,void*,descriptor* out,unsigned index){const auto csr=_mm_getcsr();cache_controls.push_back(hardware_control());cache_mxcsr.push_back(csr);++cache_calls;*out={index,reinterpret_cast<void*>(0x12340000)};return out;}
unsigned special_calls=0,special_category=0,special_source=0,special_zero=1;
void (*event_roll)(int,unsigned,int)=nullptr;
void __fastcall special(void* self,void*,int category,unsigned source,int zero){check(self==&game,"special forwards game receiver");++special_calls;special_category=category;special_source=source;special_zero=zero;if(event_roll)event_roll(category,source,zero);}

// Switch to a complete synthetic engine stack and execute the installed E9.
// Continuations end at a fixture thunk, after the real naked bridge returns.
void* engine_stack=nullptr;unsigned target=0,saved_esp=0,returned_esp=0;
unsigned returned_eax=0,returned_ebx=0,returned_ecx=0,returned_edx=0,returned_esi=0,returned_edi=0,returned_ebp=0,returned_flags=0;
__declspec(naked) void entry(){__asm {
 pushad
 mov saved_esp,esp
 mov esp,engine_stack
 mov eax,11111111h
 mov ebx,22222222h
 mov ecx,33333333h
 mov edx,44444444h
 mov esi,55555555h
 mov edi,66666666h
 mov ebp,77777777h
 stc
 jmp target
}}
__declspec(naked) void continuation(){__asm {
 mov returned_esp,esp
 mov returned_eax,eax
 mov returned_ebx,ebx
 mov returned_ecx,ecx
 mov returned_edx,edx
 mov returned_esi,esi
 mov returned_edi,edi
 mov returned_ebp,ebp
 pushfd
 pop returned_flags
 mov esp,saved_esp
 popad
 ret
}}
struct stack {std::array<unsigned char,0x8000> bytes{};void* data(){return bytes.data()+0x7800;}};
void invoke(unsigned va,stack& s){engine_stack=s.data();target=va;entry();check(returned_esp==addr(s.data()),"naked bridge preserves engine ESP");check(returned_eax==0x11111111&&returned_ebx==0x22222222&&returned_edx==0x44444444&&returned_esi==0x55555555&&returned_edi==0x66666666&&returned_ebp==0x77777777&&(returned_flags&1),"naked bridge preserves registers and carry flag");check(returned_ecx==(va==0x46ea43?read<unsigned>(s.data(),0x60):0x33333333),"bridge ECX follows original SEH restore contract");}
void seed(stack& s,bool leader=false,int category=5){s={};put(s.data(),0x13,static_cast<unsigned char>(leader));put(s.data(),0x28,&game);put(s.data(),0x70,category);put(s.data(),0x74,0xabcdef01u);unsigned seh;__asm {
 mov eax,fs:[0]
 mov seh,eax
}put(s.data(),0x60,seh);}
void stream(std::initializer_list<float> u,std::initializer_list<int> i={}){units=u;integers=i;unit_calls=integer_calls=lookups=cache_calls=0;names.clear();}
void finish(stack& s){invoke(0x46ea43,s);}
void select(stack& s,unsigned i){put(s.data(),0x20,i);invoke(0x46e760,s);}
void services(){put(manager.data(),0,manager_vt);manager_vt[0x78/4]=reinterpret_cast<void*>(&lookup);manager_vt[0x12c/4]=reinterpret_cast<void*>(&cached);game_vt[0x194/4]=reinterpret_cast<void*>(&xtreme);game_vt[0x19c/4]=reinterpret_cast<void*>(&maximum);game_vt[0x25c/4]=reinterpret_cast<void*>(&special);
 jump(0x44b8f0,&registry);jump(0x449960,&highest);jump(0x4654b0,&resolve);jump(0x402b60,&actor);jump(0x41cfc0,&level);jump(0x480a00,&get_manager);jump(0x560190,&unit);jump(0x5601e0,&integer);
 jump(0x46e747,&continuation);jump(0x46e804,&continuation);jump(0x46ea4e,&continuation);
}
unsigned long long serial(const std::string& line){unsigned long long n=0;return std::sscanf(line.c_str(),"roll n=%llu",&n)==1?n:0;}
void serial_tests(){
 stack parent,child;const auto first=logger::roll_lines.size();
 for(unsigned roll=0;roll<2;++roll){
  seed(parent);stream({0,.8f,.9f,1},{2,4});invoke(0x46e5e2,parent);
  check(logger::roll_lines.size()==first+4*(roll+1),"serial fixture has four pickup log lines per money roller entry");
  const auto n=serial(logger::roll_lines[first+4*roll]);bool shared=n>0;
  for(unsigned i=0;i<4;++i)shared&=serial(logger::roll_lines[first+4*roll+i])==n;
  check(shared,"every split money pickup repeats its roller entry serial");
  const auto history=xml1_economy::roller::recent_rolls();const auto logged=logger::roll_lines.size();const auto draws=unit_calls+integer_calls;
  for(unsigned i=0;i<4;++i)select(parent,i);
  check(history==xml1_economy::roller::recent_rolls()&&logger::roll_lines.size()==logged&&unit_calls+integer_calls==draws,"per-pickup select bridge and pipe reads neither allocate serials nor consume RNG");finish(parent);
 }
 check(serial(logger::roll_lines[first+4])==serial(logger::roll_lines[first])+1,"adjacent identical money rolls have consecutive distinct serials");
 bool identical=true;for(unsigned i=0;i<4;++i){const auto& a=logger::roll_lines[first+i];const auto& b=logger::roll_lines[first+4+i];const auto pa=a.find(" cat="),pb=b.find(" cat=");identical&=pa!=std::string::npos&&pb!=std::string::npos&&a.substr(pa)==b.substr(pb);}
 check(identical,"serial disambiguates identical category leader group bucket definition amount and draws");
 const auto previous=serial(logger::roll_lines.back());const auto count=logger::roll_lines.size();
 xml1_economy::enabled=false;seed(parent);stream({});invoke(0x46e5e2,parent);finish(parent);xml1_economy::enabled=true;
 seed(parent,false,0);stream({.5f});invoke(0x46e5e2,parent);finish(parent);
 check(logger::roll_lines.size()==count+1&&serial(logger::roll_lines.back())==previous+1&&logger::roll_lines.back().find(" def=- amt=0 ")!=std::string::npos,"no-drop entry advances serial once; economy OFF entry does not advance it");
 seed(parent);stream({0,.8f,.9f,1},{2,2});invoke(0x46e5e2,parent);const auto outer=serial(logger::roll_lines.back());
 seed(child,false,0);stream({.5f});invoke(0x46e5e2,child);const auto inner=serial(logger::roll_lines.back());finish(child);
 select(parent,0);select(parent,1);finish(parent);
 seed(parent,false,0);stream({.5f});invoke(0x46e5e2,parent);finish(parent);
 check(inner==outer+1&&serial(logger::roll_lines.back())==inner+1,"nested roller entries get distinct serials; parent pickup selection never increments them");
 const auto before_seed=serial(logger::roll_lines.back());reinterpret_cast<void(__cdecl*)(unsigned,char)>(at(0x560150))(12345,1);
 seed(parent,false,0);stream({.5f});invoke(0x46e5e2,parent);finish(parent);
 check(serial(logger::roll_lines.back())==before_seed+1,"native lockstep RNG reseeding does not reset the process-lifetime roll serial");
 // The pipe returns exactly the log lines, preserving the serial on each pickup.
 const auto history=xml1_economy::roller::recent_rolls();std::string expected;
 for(std::size_t i=logger::roll_lines.size()>16?logger::roll_lines.size()-16:0;i<logger::roll_lines.size();++i){if(!expected.empty())expected+='\n';expected+=logger::roll_lines[i];}
 check(history==expected,"economy rolls history repeats exactly the emitted serial-bearing log lines");
 for(unsigned i=0;i<8;++i)std::printf("SERIAL-FIXTURE %s\n",logger::roll_lines[first+i].c_str());
}
unsigned native_continuations=0,observed_argument=0;
__declspec(naked) void gen_continue(){__asm {
 mov eax,[esp+3ch]
 mov observed_argument,eax
 inc native_continuations
 pop ebx
 add esp,34h
 mov eax,11111111h
 ret 1ch
}}
__declspec(naked) void special_continue(){__asm {
 inc native_continuations
 pop ebp
 pop ebx
 add esp,10h
 mov eax,22222222h
 ret 8
}}
__declspec(naked) void picker_continue(){__asm {
 inc native_continuations
 pop esi
 pop ebx
 add esp,20h
 mov eax,33333333h
 ret 0ch
}}
__declspec(naked) void alternate_continue(){__asm {
 mov eax,[esp+38h]
 mov observed_argument,eax
 inc native_continuations
 pop esi
 pop ebp
 add esp,2ch
 mov eax,44444444h
 ret 8
}}
__declspec(noinline) unsigned call_gen(){return reinterpret_cast<unsigned(__fastcall*)(void*,void*,unsigned,int,int,int,int,int,int)>(at(0x47d4b0))(manager.data(),nullptr,0x51515151,2,3,4,5,6,7);}
__declspec(noinline) unsigned call_special_gen(){return reinterpret_cast<unsigned(__fastcall*)(void*,void*,unsigned,int)>(at(0x47ecb0))(manager.data(),nullptr,1,2);}
__declspec(noinline) unsigned call_picker(){return reinterpret_cast<unsigned(__fastcall*)(void*,void*,int,int,int)>(at(0x47b590))(manager.data(),nullptr,1,2,3);}
__declspec(noinline) unsigned call_alternate(const void* definition){return reinterpret_cast<unsigned(__fastcall*)(void*,void*,const void*,int)>(at(0x481b80))(manager.data(),nullptr,definition,2);}
unsigned status_count(){unsigned total=0;const auto s=xml1_economy::roller::status();for(std::size_t i=0;(i=s.find(':',i))!=std::string::npos;++i)total+=std::strtoul(s.c_str()+i+1,nullptr,10);return total;}
bool three_callers_twice(){unsigned entries=0;const auto s=xml1_economy::roller::status();for(std::size_t i=0;(i=s.find(':',i))!=std::string::npos;++i){if(std::strtoul(s.c_str()+i+1,nullptr,10)!=2)return false;++entries;}return entries==3;}
void* __cdecl get_game(){return &game;}
unsigned nearby=0;
unsigned __fastcall nearby_items(void*,void*,void*){return nearby;}
void* __fastcall position(void*,void*,float* out){out[0]=out[1]=out[2]=0;return out;}
bool unique_value=true,obtained_value=false;unsigned unique_queries=0,obtained_queries=0,obtained_sets=0;
bool __fastcall unique(void*,void*){++unique_queries;return unique_value;}
bool __fastcall obtained(void*,void*){++obtained_queries;return obtained_value;}
void __fastcall set_obtained(void*,void*){++obtained_sets;}
void metadata_tests(){
 stack s;xml1_economy_rules::record r;r.name="fresh_highest";r.group=5;r.frequency=1;r.flags=0x40;r.definition=3;xml1_economy::rows={r};const auto getters=highest_calls;
 highest_value=20;seed(s);stream({0,0,0});invoke(0x46e5e2,s);check(names==std::vector<std::string>{"XTREME_PIP"},"G3 excludes group5 using current native roster highest");finish(s);
 highest_value=40;seed(s);stream({0,0,0});const auto made=xml1_economy::constructors;invoke(0x46e5e2,s);check(xml1_economy::constructors==made+1&&highest_calls==getters+2&&xml1_economy::roller::recent_rolls().find("G=5")!=std::string::npos,"next roll fetches new roster highest and admits group5");finish(s);highest_value=20;
 void* definition_vt[0x60/4]{};definition_vt[0x50/4]=reinterpret_cast<void*>(&unique);definition_vt[0x54/4]=reinterpret_cast<void*>(&obtained);definition_vt[0x5c/4]=reinterpret_cast<void*>(&set_obtained);auto* definition=manager.data()+item_capacity_rules::definitions+3*0x3c;put(definition,0,definition_vt);r.group=1;r.flags=0x41;xml1_economy::rows={r};
 for(bool already:{false,true}){obtained_value=already;seed(s);stream({0,0,0});const auto constructors=xml1_economy::constructors;invoke(0x46e5e2,s);check(already?names==std::vector<std::string>{"XTREME_PIP"}:xml1_economy::constructors==constructors+1,already?"obtained unique is excluded through definition vt54 getter":"unobtained unique remains eligible through native definition getters");finish(s);}
 check(unique_queries==2&&obtained_queries==2&&!obtained_sets,"unique eligibility calls vt50 and vt54 and never obtained setter vt5c");xml1_economy::rows.clear();
}

struct spawned {descriptor item;int amount;unsigned index;unsigned short control;unsigned mxcsr;};
std::vector<spawned> spawned_items;
void __cdecl observe_spawn(void* s){const auto csr=_mm_getcsr();spawned_items.push_back({read<descriptor>(s,0x14),read<int>(s,0x70),read<unsigned>(s,0x20),hardware_control(),csr});}
__declspec(naked) void spawn_edge(){__asm {
 pushfd
 pushad
 lea eax,[esp+36]
 push eax
 call observe_spawn
 add esp,4
 popad
 popfd
 push 046e977h
 ret
}}
void __fastcall ignore_cookie(void*,void*){}
void native_loop(stack& s){const auto incoming_mxcsr=_mm_getcsr();const auto incoming=hardware_control();const auto first_construct=xml1_economy::constructor_controls.size(),first_cache=cache_controls.size();spawned_items.clear();put(s.data(),0,0x66666666u);put(s.data(),4,0x55555555u);put(s.data(),8,0x77777777u);put(s.data(),0xc,0x22222222u);put(s.data(),0x6c,addr(&continuation));engine_stack=s.data();target=0x46e5e2;entry();const auto outgoing_control=hardware_control();const auto outgoing_mxcsr=_mm_getcsr();check(returned_esp==addr(s.data())+0x7c,"native loop head tail exit epilogue and ret c balance original engine frame");check(returned_ebx==0x22222222&&returned_esi==0x55555555&&returned_edi==0x66666666&&returned_ebp==0x77777777,"native epilogue restores original callee-saved registers");check(outgoing_control==incoming&&std::all_of(spawned_items.begin(),spawned_items.end(),[&](const spawned& d){return d.control==incoming;})&&std::all_of(xml1_economy::constructor_controls.begin()+first_construct,xml1_economy::constructor_controls.end(),[&](unsigned short c){return c==incoming;})&&std::all_of(cache_controls.begin()+first_cache,cache_controls.end(),[&](unsigned short c){return c==incoming;}),"selection restores exact incoming x87 control before construction cache spawn and native return");check(outgoing_mxcsr==incoming_mxcsr&&std::all_of(spawned_items.begin(),spawned_items.end(),[&](const spawned& d){return d.mxcsr==incoming_mxcsr;})&&std::all_of(xml1_economy::constructor_mxcsr.begin()+first_construct,xml1_economy::constructor_mxcsr.end(),[&](unsigned c){return c==incoming_mxcsr;})&&std::all_of(cache_mxcsr.begin()+first_cache,cache_mxcsr.end(),[&](unsigned c){return c==incoming_mxcsr;}),"selection restores exact incoming MXCSR before construction cache spawn and native return");}
void ordinary_event(int category,unsigned source,int argument3){stack s;seed(s,false,category);put(s.data(),0x74,source);put(s.data(),0x78,argument3);native_loop(s);}
void loop_tests(const std::vector<unsigned char>& original){
 // Retain the native loop head, native EBP increment/count compare/backedge,
 // and installed finish. Only physical-spawn work is a fixture service edge.
 std::memcpy(at(0x46e747),original.data()+0x46e747-addr(fixture_space),5);std::memcpy(at(0x46ea4e),original.data()+0x46ea4e-addr(fixture_space),5);FlushInstructionCache(GetCurrentProcess(),at(0x46e747),5);FlushInstructionCache(GetCurrentProcess(),at(0x46ea4e),5);jump(0x672161,&ignore_cookie);jump(0x46e804,&spawn_edge);
 stack s;seed(s,false,0);stream({.5f});native_loop(s);check(spawned_items.empty()&&read<unsigned>(s.data(),0x20)==0,"native loop zero-count branch goes directly through finish");
 seed(s);stream({0,.5f,0});native_loop(s);check(spawned_items.size()==1&&spawned_items[0].index==0&&spawned_items[0].item.handle==1&&read<unsigned>(s.data(),0x20)==1,"native loop selects and emits one descriptor once");
 for(unsigned count=2;count<=4;++count){seed(s);stream({0,.8f,.9f,1},{2,static_cast<int>(count)});native_loop(s);bool ok=spawned_items.size()==count&&read<unsigned>(s.data(),0x20)==count;for(unsigned i=0;i<spawned_items.size();++i)ok=ok&&spawned_items[i].index==i&&spawned_items[i].item.handle==i+1&&spawned_items[i].amount==234/static_cast<int>(count);check(ok,"native tail increments loops and exits exactly at split count 2 through 4");}
 xml1_economy_rules::record r;r.name="treasure_equipment";r.group=1;r.frequency=1;r.flags=0x40;r.definition=3;xml1_economy::rows={r};
 const auto previous=xml1_economy::constructors;stream({0,0,0});ordinary_event(5,0x10101010,1);check(spawned_items.size()==1&&spawned_items[0].item.handle==777&&xml1_economy::constructors==previous+1&&unit_calls==3&&!integer_calls,"ordinary treasure argument3=1 retains equipment bucket through full native loop");
 event_roll=&ordinary_event;stream({0,0,0});reinterpret_cast<void(__fastcall*)(void*,void*,unsigned)>(at(0x469c80))(&game,nullptr,0x10101010);check(spawned_items.size()==1&&spawned_items[0].item.handle==777&&xml1_economy::constructors==previous+2&&unit_calls==3&&!integer_calls,"special treasure dispatches same full equipment roll and native loop");xml1_economy::rows.clear();
 for(unsigned count=2;count<=4;++count){stream({0,.8f,.9f,1},{2,static_cast<int>(count)});ordinary_event(5,0x10101010,1);check(spawned_items.size()==count&&unit_calls==4&&integer_calls==2,"ordinary treasure argument3=1 keeps money split 2 through 4");stream({0,.8f,.9f,1},{2,static_cast<int>(count)});reinterpret_cast<void(__fastcall*)(void*,void*,unsigned)>(at(0x469c80))(&game,nullptr,0x10101010);check(spawned_items.size()==count&&unit_calls==4&&integer_calls==2,"special treasure keeps same money splits and draw counts");}
 event_roll=nullptr;
}
struct queued_event {unsigned short kind;float delay;};
std::vector<queued_event> queued;
void* treasure_receiver=nullptr;
bool __fastcall treasure_eligible(void*,void*,int,int){return true;}
void __fastcall queue_event(void* self,void*,void* event,float delay){check(self==treasure_receiver&&addr(event)==0x6da238,"native treasure queue retains receiver and event template");queued.push_back({read<unsigned short>(event,4),delay});}
void treasure_tests(){
 jump(0x4753c0,&treasure_eligible);jump(0x41a730,&queue_event);game_vt[0x260/4]=at(0x469c80);
 void* vt[0x28/4]{};vt[0x24/4]=reinterpret_cast<void*>(&position);std::array<unsigned char,0x26c> chest{};put(chest.data(),0,vt);put(chest.data(),0x1c,0x31313131u);treasure_receiver=chest.data();
 auto activate=[&](){return reinterpret_cast<bool(__fastcall*)(void*,void*,int,int)>(at(0x4c44c0))(chest.data(),nullptr,0,0);};
 for(int random:{0,7}){queued.clear();stream({},{random});check(activate(),"native treasure activation accepts eligible chest");bool ok=queued.size()==unsigned(random+6)&&integer_calls==1&&!unit_calls;for(unsigned i=0;i<queued.size();++i){ok=ok&&queued[i].kind==(i+1==queued.size()?1:0);if(i)ok=ok&&queued[i].delay>queued[i-1].delay;}check(ok,"native activation queues 5 or 12 ordinary events followed by exactly one special");}
 put(chest.data(),0x268,static_cast<unsigned char>(1));queued.clear();stream({});check(activate()&&queued.empty()&&!integer_calls&&!unit_calls,"native activated flag prevents another cadence without a random draw");put(chest.data(),0x268,static_cast<unsigned char>(0));
 auto event=[&](unsigned short kind){reinterpret_cast<void(__fastcall*)(void*,void*,int,unsigned short)>(at(0x4c4420))(chest.data(),nullptr,0,kind);};
 nearby=0;event_roll=&ordinary_event;xml1_economy_rules::record r;r.name="native_treasure_equipment";r.group=1;r.frequency=1;r.flags=0x40;r.definition=3;xml1_economy::rows={r};
 for(unsigned short kind:std::array<unsigned short,2>{0,1}){stream({0,0,0});const auto constructed=xml1_economy::constructors;event(kind);check(special_category==5&&special_source==0x31313131&&special_zero==(kind?0u:1u)&&spawned_items.size()==1&&spawned_items[0].item.handle==777&&xml1_economy::constructors==constructed+1,kind?"native special treasure event dispatches469c80 then full equipment roll":"native ordinary treasure event passes arg3=1 through full equipment roll");}
 xml1_economy::rows.clear();for(unsigned short kind:std::array<unsigned short,2>{0,1})for(int count=2;count<=4;++count){stream({0,.8f,.9f,1},{2,count});event(kind);check(spawned_items.size()==unsigned(count)&&unit_calls==4&&integer_calls==2,kind?"native special treasure event retains money splits 2 through 4":"native ordinary treasure event retains money splits 2 through 4");}
 nearby=20;for(unsigned short kind:std::array<unsigned short,2>{0,1}){queued.clear();stream({});const auto calls=special_calls;event(kind);check(queued.size()==1&&queued[0].kind==kind&&queued[0].delay>0&&special_calls==calls&&!unit_calls&&!integer_calls,"native nearby-item gate requeues the same treasure event without rolling");}event_roll=nullptr;nearby=0;
}
double __cdecl raw_money_unit(char lockstep){
 check(lockstep==1,"raw money draw requests lockstep stream");
 const auto index=unit_calls++;
 constexpr double scripted[]{0,.8,.9};
 if(index<3)return scripted[index];
 // Execute the original native core, leaving its 31-bit value in ST0. The
 // hooked public wrapper cannot call itself; 560090 is its real char=1 edge.
 return reinterpret_cast<double(__cdecl*)()>(at(0x560090))();
}
double __cdecl staged_money_unit(char lockstep){check(lockstep==1,"staged money draw requests lockstep stream");constexpr double scripted[]{0,.8,.9,static_cast<double>(.021189f)};const auto i=unit_calls++;return scripted[std::min(i,3u)];}
double __cdecl fixed_money_unit(char lockstep){check(lockstep==1,"integer-boundary fixture requests lockstep unit stream");constexpr double scripted[]{0,.8,.9,.5};const auto i=unit_calls++;return scripted[std::min(i,3u)];}
void ftol();
int native_money_amount(float u,int actor_level){
 const float minus_ten=-10.0f;unsigned short saved_control=0,chop_control=0;__int64 truncated=0;
 // Native x87 stages, including precision-control rounding at each multiply.
 // Only integer conversion changes to chop; restore the exact prior control.
 __asm {
  fild dword ptr actor_level
  fmul dword ptr u
  fmul dword ptr minus_ten
  fnstcw saved_control
  mov ax,saved_control
  or ax,0c00h
  mov chop_control,ax
  fldcw chop_control
  fistp qword ptr truncated
  fldcw saved_control
 }
 return actor_level-static_cast<int>(truncated);
}
void raw_rng_tests(const std::vector<unsigned char>& original){
 const auto original_control=_control87(0,0),original_mxcsr=_mm_getcsr();const auto original_hardware=hardware_control();
 for(unsigned precision:{_PC_53,_PC_24}){
  _control87(precision,_MCW_PC);const bool single=precision==_PC_24;
  std::memcpy(at(0x560190),original.data()+0x560190-addr(fixture_space),0x12);FlushInstructionCache(GetCurrentProcess(),at(0x560190),0x12);
  put(at(0x6e0874),0,309741327u);put(at(0x7ad8b4),0,0u);const auto gate_raw=reinterpret_cast<double(__cdecl*)(char)>(at(0x560190))(1);check((single?gate_raw==static_cast<double>(.15f):gate_raw>static_cast<double>(.15f))&&static_cast<float>(gate_raw)==.15f,"native outer-gate seed exposes active 24/53-bit x87 precision");
  put(at(0x6e0874),0,309741327u);put(at(0x7ad8b4),0,0u);stack s;seed(s,false,1);stream({});native_loop(s);
  check(spawned_items.empty()&&read<unsigned>(at(0x7ad8b4))==1&&read<unsigned>(at(0x6e0874))==322122561u,"actual native category1 gate uses normalized PC53 under either incoming precision");
  put(at(0x6e0874),0,739806647u);put(at(0x7ad8b4),0,0u);const auto money_raw=reinterpret_cast<double(__cdecl*)()>(at(0x560090))();check((single?money_raw==1.0:money_raw<1.0)&&static_cast<float>(money_raw)==1.0f,"native near-one money draw retains precision-dependent ST0 result");
  jump(0x560190,&raw_money_unit);put(at(0x6e0874),0,739806647u);put(at(0x7ad8b4),0,0u);actor_source=true;actor_level_value=10;seed(s);stream({},{1,2});native_loop(s);const int expected=54;
  check(spawned_items.size()==2&&spawned_items[0].amount==expected&&spawned_items[1].amount==expected&&unit_calls==4&&integer_calls==2&&read<unsigned>(at(0x7ad8b4))==1&&read<unsigned>(at(0x6e0874))==2147483646u,"actual native near-one actor money normalizes to split54 under either incoming precision");
  const int native_amount=native_money_amount(.021189f,32767);check(native_amount==(single?39710:39709),"native x87 two-multiply oracle exposes per-stage PC24 rounding");jump(0x560190,&staged_money_unit);actor_level_value=32767;seed(s);stream({},{1,2});native_loop(s);check(spawned_items.size()==2&&spawned_items[0].amount==39709/2&&spawned_items[1].amount==39709/2&&unit_calls==4&&integer_calls==2,"runtime actor money follows normalized PC53 stages regardless of incoming PC24 negative control");actor_source=false;
 }
 // Contrasting incoming RC modes verify normalization and full-state restore,
 // while the real native integer implementation exercises its near-one bound.
 _control87(_PC_24|_RC_UP,_MCW_PC|_MCW_RC);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_DOWN);
 std::memcpy(at(0x5601e0),original.data()+0x5601e0-addr(fixture_space),5);FlushInstructionCache(GetCurrentProcess(),at(0x5601e0),5);jump(0x67217c,&ftol);jump(0x560190,&fixed_money_unit);put(at(0x6e0874),0,739806647u);put(at(0x7ad8b4),0,0u);actor_source=true;actor_level_value=10;stack s;seed(s);stream({});native_loop(s);
 check(spawned_items.size()==4&&names==std::vector<std::string>(4,"MONEY3")&&unit_calls==4&&read<unsigned>(at(0x7ad8b4))==2&&read<unsigned>(at(0x6e0874))==2147466840u,"actual native randint near-one returns inclusive upper3 rather than4 under incoming PC24 upward and SSE downward modes");
 _control87(original_control,_MCW_PC|_MCW_RC);_mm_setcsr(original_mxcsr);check(hardware_control()==original_hardware&&_mm_getcsr()==original_mxcsr,"RNG precision fixture restores exact prior x87 control and MXCSR");actor_source=false;actor_level_value=10;jump(0x560190,&unit);jump(0x5601e0,&integer);
}

// T-REGEN fixture. Execute full retail level-adjustment, bulk, factory and XP
// callers. Stats/inventory/registry services supply the surrounding engine.
std::vector<unsigned char> roster(0xbc00);
void* roster_vt[0x100/4]{};void* inventory_vt[0xc0/4]{};void* settings_vt[0x60/4]{};void* bag_vt[0x14/4]{};void* pool_vt[3]{};
object settings{settings_vt},bag{bag_vt},item_pool{pool_vt};
struct inventory {void** vt;std::array<descriptor,3> equipment;unsigned xp=1000;} equipped{inventory_vt};
struct saved_gear {
 std::array<std::array<unsigned char,0x3c>,3> definitions{};
 std::array<std::array<unsigned char,0x34>,3> instances{};
 std::array<std::array<unsigned char,0x14>,3> attached{};
};
saved_gear gear{},gear_before{};
bool gear_intact(){return gear.definitions==gear_before.definitions&&gear.instances==gear_before.instances&&gear.attached==gear_before.attached;}
unsigned regenerations=0,adjustments=0,removals=0,releases=0,generated=0,mode_queries=0;
int target_level=30;
bool eligible_character=true,registry_flag=true;
unsigned char* stats(){return roster.data()+4+0x4f8+0x150;}
void* __cdecl get_roster(){return roster.data();}
void* __fastcall resolve_stats(void*,void*,const char*){return stats();}
int __fastcall stats_level(void* p,void*){return read<unsigned char>(p,0x1c);}
bool __cdecl difficulty(){return false;}
void __fastcall adjust_stats(void*,void*,void* p,int delta,int one,float ratio,bool hard){check(p==stats()&&one==1&&ratio==-1.0f&&!hard,"native level adjustment forwards its computed delta");put(p,0x1c,static_cast<unsigned char>(stats_level(p,nullptr)+delta));++adjustments;}
void __fastcall regenerate(void* p,void*,void* s){++regenerations;reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(at(0x448dc0))(p,nullptr,s);}
void* __cdecl mode_manager(){++mode_queries;return &settings;}
bool __fastcall no_mode(void*,void*){return false;}
bool __fastcall flag(void*,void*,unsigned){return registry_flag;}
bool __cdecl is_character(void*){return eligible_character;}
const char* __fastcall property(void*,void*,const char* key,int){return std::strcmp(key,"character")==0?"fixture_hero":nullptr;}
void __fastcall register_hero(void*,void*,const char*,int){}
void* __fastcall game_settings(void*,void*){return &settings;}
int __fastcall desired_level(void*,void*){return target_level;}
unsigned __fastcall hero_handle(void*,void*,const char*){return 1;}
void* __cdecl context(){return &settings;}
void __fastcall context_noop(void*,void*){}
descriptor* __fastcall equipment(void*,void*,descriptor* out,int slot){*out=equipped.equipment[slot-1];return out;}
bool __fastcall descriptor_valid(void*,void*,unsigned handle){return handle!=0;}
void* __fastcall instance_get(void*,void*,unsigned handle){const unsigned i=handle>=1000?handle-1100:handle-100;return i<gear.instances.size()?gear.instances[i].data():nullptr;}
void __fastcall remove_equipment(void*,void*,unsigned handle,void* pool){check(pool==&item_pool,"native regeneration removes actual equipped descriptor");for(auto& d:equipped.equipment)if(d.handle==handle)d={};++removals;}
void __fastcall unequip(void*,void*,unsigned,void*){}
void __fastcall release(void*,void*,unsigned,void*){++releases;}
descriptor* __fastcall generate(void*,void*,descriptor* out,int,int,int,int,int,int){++generated;*out={};return out;}
unsigned __fastcall xp_threshold(void*,void*,int level){return level*100;}
unsigned __fastcall xp_current(void*,void*){return equipped.xp;}
void __fastcall xp_add(void*,void*,int amount,int){equipped.xp+=amount;}
void __fastcall noop(void*,void*){}
int __fastcall xp_game_level(void*,void*,void*){return -1;}
__declspec(naked) void ftol(){__asm {
 sub esp,10h
 fnstcw word ptr [esp+8]
 mov ax,word ptr [esp+8]
 or ax,0c00h
 mov word ptr [esp+0ah],ax
 fldcw word ptr [esp+0ah]
 fistp qword ptr [esp]
 fldcw word ptr [esp+8]
 mov eax,[esp]
 mov edx,[esp+4]
 add esp,10h
 ret
}}
void regen_services(){
 put(roster.data(),0,roster_vt);put(roster.data(),0xbba8,1);put(roster.data(),0x9b20,0xffu);put(roster.data(),0x9b38,1u);put(roster.data(),0x9b40,1u);put(roster.data(),4+0x4f8+0x2a0,0x1d);put(stats(),0xc0,&equipped);put(stats(),0x28e,static_cast<unsigned short>(1));
 roster_vt[0x40/4]=reinterpret_cast<void*>(&resolve_stats);roster_vt[0xdc/4]=reinterpret_cast<void*>(&adjust_stats);roster_vt[0xe0/4]=reinterpret_cast<void*>(&regenerate);roster_vt[0xe4/4]=at(0x448b10);roster_vt[0xb0/4]=reinterpret_cast<void*>(&register_hero);roster_vt[0x3c/4]=reinterpret_cast<void*>(&hero_handle);roster_vt[0x7c/4]=reinterpret_cast<void*>(&flag);roster_vt[0xd0/4]=reinterpret_cast<void*>(&xp_threshold);
 inventory_vt[0x5c/4]=reinterpret_cast<void*>(&equipment);inventory_vt[0x64/4]=reinterpret_cast<void*>(&remove_equipment);inventory_vt[0xc/4]=reinterpret_cast<void*>(&xp_current);pool_vt[0]=reinterpret_cast<void*>(&descriptor_valid);pool_vt[2]=reinterpret_cast<void*>(&instance_get);
 settings_vt[0x5c/4]=reinterpret_cast<void*>(&no_mode);settings_vt[0x2c/4]=reinterpret_cast<void*>(&desired_level);bag_vt[0x10/4]=reinterpret_cast<void*>(&property);game_vt[8/4]=reinterpret_cast<void*>(&game_settings);game_vt[0x114/4]=reinterpret_cast<void*>(&xp_game_level);
 manager_vt[0xf0/4]=reinterpret_cast<void*>(&unequip);manager_vt[0xe8/4]=reinterpret_cast<void*>(&release);manager_vt[0x138/4]=reinterpret_cast<void*>(&generate);
 jump(0x44b8f0,&get_roster);jump(0x4b87c0,&stats_level);jump(0x4c87c0,&difficulty);jump(0x48de40,&mode_manager);jump(0x460140,&is_character);jump(0x612be0,&context);jump(0x610d20,&context_noop);jump(0x67217c,&ftol);jump(0x4b79b0,&xp_add);jump(0x4bbae0,&noop);
}
void seed_equipment(bool legacy=false){put(stats(),0x1c,static_cast<unsigned char>(10));equipped.xp=1000;for(unsigned i=0;i<3;++i){equipped.equipment[i]={100+i+(legacy?1000:0),&item_pool};gear.definitions[i].fill(static_cast<unsigned char>(0x40+i));gear.instances[i].fill(static_cast<unsigned char>(0x70+i));gear.attached[i].fill(static_cast<unsigned char>(0xa0+i));put(gear.instances[i].data(),4,gear.definitions[i].data());put(gear.instances[i].data(),0x10,descriptor{500+i,&item_pool});put(gear.attached[i].data(),8,legacy?.75f:0.0f);}gear_before=gear;regenerations=adjustments=removals=releases=generated=mode_queries=0;stream({});}
void adjustment(int desired){reinterpret_cast<void(__fastcall*)(void*,void*,const char*,int,float)>(at(0x448b10))(roster.data(),nullptr,"fixture_hero",desired,1.0f);}
void regen_checks(const std::array<descriptor,3>& saved,const char* label){check(std::memcmp(saved.data(),equipped.equipment.data(),sizeof saved)==0&&gear_intact(),label);check(regenerations==1&&adjustments==1&&stats_level(stats(),nullptr)==target_level,"native caller changes level and dispatches regeneration once");check(!generated&&!removals&&!releases&&!mode_queries&&!unit_calls&&!integer_calls,"entry bypass precedes mode query removal generation and RNG");}
void* segment_actor=nullptr;void* segment_spawn=nullptr;
__declspec(naked) void segment_entry(){__asm {
 pushad
 mov saved_esp,esp
 mov esp,engine_stack
 mov esi,segment_actor
 mov edi,segment_spawn
 jmp target
}}
void segment(unsigned start,unsigned end){stack s;std::array<unsigned char,0x360> a{};std::array<unsigned char,0x60> spawn{};put(a.data(),0x35c,stats());put(spawn.data(),0x50,static_cast<unsigned char>(2));put(at(0x782728),0,static_cast<unsigned char>(0xfe));segment_actor=a.data();segment_spawn=spawn.data();engine_stack=s.data();target=start;jump(end,&continuation);segment_entry();check(returned_esp==addr(s.data()),"native arena call-edge segment balances stack");}
void regen_tests(const std::vector<unsigned char>& original){
 const auto census_before=xml1_economy::roller::status();unsigned initial_esp=0,final_esp=0;__asm mov initial_esp,esp
 regen_services();seed_equipment();target_level=30;auto saved=equipped.equipment;adjustment(target_level);regen_checks(saved,"full 448b10 level-delta caller preserves equipped gear");
 seed_equipment();saved=equipped.equipment;reinterpret_cast<void(__fastcall*)(void*,void*,int)>(at(0x449f50))(roster.data(),nullptr,target_level);regen_checks(saved,"full 449f50 bulk caller preserves equipped gear");
 seed_equipment();saved=equipped.equipment;check(reinterpret_cast<bool(__cdecl*)(void*)>(at(0x41c850))(&bag),"full actor factory property/setup routine returns character");regen_checks(saved,"full factory 41c850 through 41c91f preserves equipped gear");
 seed_equipment();saved=equipped.equipment;target_level=20;segment(0x4d09a0,0x4d09e8);regen_checks(saved,"arena 4d09e2 native call-edge preserves equipped gear");
 seed_equipment();saved=equipped.equipment;target_level=30;segment(0x4d0a77,0x4d0aa6);regen_checks(saved,"spawn 4d0aa0 native level-30 call-edge preserves equipped gear");
 seed_equipment();saved=equipped.equipment;segment(0x4d0ac8,0x4d0b17);regen_checks(saved,"spawn 4d0b11 native gated level-30 call-edge preserves equipped gear");
 // Lifecycle states are injected; this proves state preservation at the
 // unconditional bypass, not engine new-game/unlock/save-load reachability.
 for(unsigned state=0;state<3;++state){seed_equipment(state==2);if(state==0)equipped.equipment={};saved=equipped.equipment;regenerate(reinterpret_cast<void*>(1),nullptr,reinterpret_cast<void*>(1));check(std::memcmp(saved.data(),equipped.equipment.data(),sizeof saved)==0&&gear_intact()&&regenerations==1&&!adjustments&&!mode_queries&&!generated&&!removals&&!integer_calls&&!unit_calls,state==0?"new-game empty equipment fixture preserved before stats access":state==1?"hero join economy equipment fixture preserved before stats access":"loaded XML2 legacy equipment fixture preserved before stats access");}
 seed_equipment(true);saved=equipped.equipment;reinterpret_cast<void(__fastcall*)(void*,void*,void*,int,int,float,int)>(at(0x44be40))(roster.data(),nullptr,stats(),1,0,0.0f,0);check(stats_level(stats(),nullptr)==11&&regenerations==0&&!integer_calls&&!unit_calls&&!generated&&std::memcmp(saved.data(),equipped.equipment.data(),sizeof saved)==0&&gear_intact(),"native ordinary XP caller and native threshold-level loop advance level without regeneration or gear changes");
 // Restore just the original entry to demonstrate native OFF behavior.
 std::array<unsigned char,6> patched_entry{};std::memcpy(patched_entry.data(),at(0x448dc0),patched_entry.size());
 std::memcpy(at(0x448dc0),original.data()+0x448dc0-addr(fixture_space),6);FlushInstructionCache(GetCurrentProcess(),at(0x448dc0),6);seed_equipment(true);stream({},{0,0,0});xml1_economy::enabled=false;regenerate(roster.data(),nullptr,stats());check(mode_queries==1&&removals==3&&generated==3&&integer_calls==3&&std::all_of(equipped.equipment.begin(),equipped.equipment.end(),[](const descriptor& d){return !d.pool;}),"rule OFF executes native regeneration removal generation and three draws");xml1_economy::enabled=true;
 __asm mov final_esp,esp
 check(initial_esp==final_esp,"all full native trigger and lifecycle entry calls balance the caller stack");check(xml1_economy::roller::status()==census_before,"all regeneration trigger cases leave native generator census unchanged");
 std::memcpy(at(0x448dc0),patched_entry.data(),patched_entry.size());FlushInstructionCache(GetCurrentProcess(),at(0x448dc0),patched_entry.size());
}
}
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");if(!path)return 2;
 DWORD old;VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old);
 check(addr(fixture_space)==0x401000&&load(path),"map owner retail image without launching game");if(failures)return 1;
 const auto before=std::vector<unsigned char>(fixture_space,fixture_space+sizeof fixture_space);
 // Each guarded ownership site must refuse before any patch is contributed.
 for(unsigned va:{0x46e5e2u,0x46e760u,0x46ea43u,0x421f3fu,0x469c80u,0x448dc0u,0x4c908au,0x47d4b0u,0x47ecb0u,0x47b590u,0x481b80u,0x46e735u,0x46e774u}){
  auto byte=read<unsigned char>(at(va));put(at(va),0,static_cast<unsigned char>(byte^1));patch_transaction::transaction tx;std::string why;
  check(!xml1_economy::roller::contribute(tx,why)&&!tx.contributions()&&!why.empty()&&!xml1_economy::roller::installed(),"modified retail/leader ownership guard refuses roller");put(at(va),0,byte);
 }
 {patch_transaction::transaction tx;std::string why;check(xml1_economy::roller::contribute(tx,why),"valid retail guards contribute");check(!xml1_economy::roller::installed(),"contribution has no early publication");patch_transaction::hooks h;h.after_apply=[]{put(at(0x421f3f),0,static_cast<unsigned char>(0xee));};check(tx.commit(&h)==patch_transaction::outcome::rolled_back,"forced readback error certifies rollback");check(!xml1_economy::roller::installed()&&std::equal(before.begin(),before.end(),fixture_space),"rollback restores every mapped byte and OFF publication");}
 {patch_transaction::transaction tx;std::string why;check(xml1_economy::roller::contribute(tx,why)&&tx.commit()==patch_transaction::outcome::committed&&xml1_economy::roller::installed(),"roller hooks publish after verified commit");}
 if(failures)return 1;
 check(read<unsigned char>(at(0x421f3f))==3,"actor default category is three");
 check(read<unsigned char>(at(0x4c908a))==0xe9&&0x4c908f+read<int>(at(0x4c908b))==0x4c926b,"DR jump skips only generated gear block");
 auto unchanged=[&](unsigned va,unsigned size){return std::equal(static_cast<unsigned char*>(at(va)),static_cast<unsigned char*>(at(va))+size,before.begin()+va-addr(fixture_space));};
 check(unchanged(0x4c8fc7,0x4c908a-0x4c8fc7)&&unchanged(0x4c911c,0x4c926b-0x4c911c),"DR named character points costume and other reward instructions remain byte-identical");
 check(unchanged(0x46e735,6)&&unchanged(0x46e774,8),"legacy leader sites are guarded but never patched twice");
 services();check(xml1_economy::roller::recent_rolls().empty(),"recent roll history starts empty");
 stack s;seed(s);xml1_economy::enabled=false;stream({});invoke(0x46e5e2,s);check(read<unsigned>(s.data(),0x1c)==0&&!unit_calls&&!lookups&&xml1_economy::roller::recent_rolls().empty(),"economy OFF produces zero drops draws or history");finish(s);xml1_economy::enabled=true;
 seed(s,false,0);stream({.5f});invoke(0x46e5e2,s);check(read<unsigned>(s.data(),0x1c)==0&&unit_calls==1&&!lookups&&xml1_economy::roller::recent_rolls()=="roll n=1 cat=0 lead=0 G=3 bucket=0 def=- amt=0 draws=1","failed chance publishes zero count and exact serial-bearing history starting at one");finish(s);
 seed(s);stream({0,.5f,0});invoke(0x46e5e2,s);check(read<unsigned>(s.data(),0x1c)==1&&names==std::vector<std::string>{"XTREME_PIP"},"single fallback descriptor uses cache");select(s,0);check(read<descriptor>(s.data(),0x14).handle==1&&read<int>(s.data(),0x70)==1&&source_seen==0xabcdef01,"first selection copies descriptor amount and source");finish(s);
 for(int count=2;count<=4;++count){seed(s);stream({0,.8f,.9f,1},{2,count});invoke(0x46e5e2,s);check(read<unsigned>(s.data(),0x1c)==unsigned(count)&&cache_calls==unsigned(count),"money split publishes requested count 2 through 4");for(int i=0;i<count;++i){select(s,i);check(read<descriptor>(s.data(),0x14).handle==unsigned(i+1)&&read<int>(s.data(),0x70)==234/count,"each split selects matching descriptor and truncated amount");}finish(s);}
 xml1_economy_rules::record r;r.name="fixture_equipment";r.group=1;r.frequency=1;r.flags=0x40;r.definition=3;xml1_economy::rows={r};
 seed(s,true);stream({0,0},{0,1,3});invoke(0x46e5e2,s);check(read<unsigned>(s.data(),0x1c)==4&&xml1_economy::constructors==1&&xml1_economy::constructor_arguments&&names==std::vector<std::string>{"HEALTH_ITEM","ENERGY_ITEM","XTREME_PIP"},"leader retains equipment first and exactly three extras");select(s,0);check(read<descriptor>(s.data(),0x14).handle==777,"leader first selection is constructed equipment");finish(s);xml1_economy::rows.clear();
 std::array<stack,5> nested;stream({});for(auto& n:nested){seed(n);invoke(0x46e5e2,n);}check(read<unsigned>(nested[4].data(),0x1c)==0,"fifth nested roll refuses drops");invoke(0x46e5e2,nested[4]);check(logger::overflow_logs==1,"repeated depth overflow logs only once");finish(nested[4]);for(int i=3;i>=0;--i){select(nested[i],0);check(read<descriptor>(nested[i].data(),0x14).pool!=nullptr,"nested parent frame survives overflow release");finish(nested[i]);}
 seed(s);stream({0,.5f,0});invoke(0x46e5e2,s);check(read<unsigned>(s.data(),0x1c)==1,"all frames reusable after nested unwind");finish(s);
 serial_tests();
 unsigned esp_before=0,esp_after=0;const auto draws=unit_calls+integer_calls,allocations=xml1_economy::constructors;__asm {
 mov esp_before,esp
 push 123
 mov eax,0448dc0h
 call eax
 mov esp_after,esp
}check(esp_before==esp_after&&unit_calls+integer_calls==draws&&xml1_economy::constructors==allocations,"regeneration entry ret 4 preserves stack without draws or allocation");
 reinterpret_cast<void(__fastcall*)(void*,void*,unsigned)>(at(0x469c80))(&game,nullptr,0x76543210);check(special_calls==1&&special_category==5&&special_source==0x76543210&&special_zero==0,"special native entry forwards (5,source,0) exactly once");
 // Unmodified retail breakable function: its nearby-count gate and category
 // clear execute natively; only receiver services are fixture edges.
 jump(0x46dce0,&get_game);game_vt[0x250/4]=reinterpret_cast<void*>(&nearby_items);void* entity_vt[0x28/4]{};entity_vt[0x24/4]=reinterpret_cast<void*>(&position);std::array<unsigned char,0x304> entity{};put(entity.data(),0,entity_vt);put(entity.data(),0x1c,0x19191919u);put(entity.data(),0x300,4);
 reinterpret_cast<void(__fastcall*)(void*,void*,int)>(at(0x496c70))(entity.data(),nullptr,0);check(special_calls==2&&special_category==4&&special_source==0x19191919&&special_zero==0&&read<unsigned>(entity.data(),0x300)==0,"native breakable calls roller once with authored category then clears +300");nearby=20;put(entity.data(),0x300,5);reinterpret_cast<void(__fastcall*)(void*,void*,int)>(at(0x496c70))(entity.data(),nullptr,0);check(special_calls==2&&read<unsigned>(entity.data(),0x300)==0,"native breakable nearby gate still clears category without roll");
 metadata_tests();loop_tests(before);treasure_tests();raw_rng_tests(before);check(xml1_economy::roller::status()=="; gen 0","roller ordinary special and breakable paths explicitly report zero native-generator tripwire calls");
 // Replay each displaced prologue before a matching native-ABI continuation.
 jump(0x47d4b8,&gen_continue);jump(0x47ecb5,&special_continue);jump(0x47b595,&picker_continue);jump(0x481b85,&alternate_continue);
 for(unsigned i=0;i<2;++i){check(call_gen()==0x11111111&&observed_argument==0x51515151,"generator tripwire preserves arguments and native ret 1c");check(call_special_gen()==0x22222222,"special generator displaced prologue and ret 8 execute");check(call_picker()==0x33333333,"picker displaced prologue and ret c execute");}
 check(native_continuations==6&&status_count()==6,"tripwires count every native call by return address");check(three_callers_twice()&&logger::tripwire_logs==3,"each of three caller return addresses counts twice and logs once");
 int economy=0,legacy=0;xml1_economy::economy_definition=&economy;check(call_alternate(&legacy)==0x44444444&&observed_argument==addr(&legacy)&&status_count()==6,"legacy alternate initializer continues without economy census");check(call_alternate(&economy)==0x44444444&&observed_argument==addr(&economy)&&status_count()==7&&logger::tripwire_logs==4,"economy alternate initializer counts definition entry argument");xml1_economy::enabled=false;check(call_gen()==0x11111111&&status_count()==7&&native_continuations==9,"economy OFF tripwire preserves native continuation without census");
 xml1_economy::enabled=true;regen_tests(before);const auto history=xml1_economy::roller::recent_rolls();const auto prior_draws=unit_calls+integer_calls;check(std::count(history.begin(),history.end(),'\n')==15&&history==xml1_economy::roller::recent_rolls()&&unit_calls+integer_calls==prior_draws,"history is bounded to sixteen lines and read-only queries consume no RNG");
 std::printf("Roll execution: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
}
