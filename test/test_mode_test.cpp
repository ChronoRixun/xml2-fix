// Synthetic actors and a private retail executable as a read-only guard input.
// No game process, graphics, saves, registry or network calls.
#include "test_mode.cpp"
#include "xtract.cpp"
#include "xtraction_shop_rules.hpp"
#include <fstream>
#include <map>
#include <cstdarg>

namespace ini {
 bool forge=false,hotkeys=false,shop=false;
 std::map<std::wstring,std::string> values;
 bool flag(const wchar_t* section,const wchar_t* key,bool fallback) {
  if(!std::wcscmp(section,L"Game") && !std::wcscmp(key,L"XtractionShop"))return shop;
  if(std::wcscmp(section,L"TestMode"))return fallback;
  return std::wcscmp(key,L"ForgeEverywhere")==0 ? forge : std::wcscmp(key,L"Hotkeys")==0 && hotkeys;
 }
 std::optional<std::string> text(const wchar_t*,const wchar_t* key) {
  auto i=values.find(key);return i==values.end()?std::nullopt:std::optional<std::string>(i->second);
 }
}
namespace logger { std::vector<std::string> lines;void write(const char* format,...){char text[1024];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);lines.emplace_back(text);} }
namespace game_version {
 bool refused=false,testing=true,pending=false;
 void refuse_startup(){refused=true;}bool test_mode_active(){return testing;}
 void clear_test_mode_refusal(){pending=false;}
 bool take_test_mode_refusal(){const bool result=pending;pending=false;return result;}
 const char* test_mode_reason(){return "Invented [TestMode] refusal";}
}
namespace mold_query { bool ready=false;bool supported(){return true;}bool pools_ready(){return ready;} }
namespace entity_capacity { std::uint32_t installed_target(std::uint32_t){return 0;} const entity_capacity_layout::descriptor& installed(){return entity_capacity_layout::stock_descriptor;} }
namespace test_input { std::string queued;bool accept=false;bool queue_script_on_game_thread(const char* s){queued=s;return accept;} }
extern "C" unsigned char fixture_space[0x700000];
namespace {
 int failures=0,checks=0;
 void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
 struct event_reader {
  std::map<std::uint32_t,std::uint32_t> values;
  template<class T> std::optional<T> get(std::uint32_t a) {
   auto i=values.find(a);return i==values.end()?std::nullopt:std::optional<T>(static_cast<T>(i->second));
  }
 };
 void rules(){
  using namespace test_mode_rules;
  check(function_key("F8")==0x42 && function_key("f12")==0x58,"function keys parse");
  for(const auto bad:{"", "F0", "F13", "F9junk", "9", "F-1"})check(function_key(bad)==0,"invalid key refuses");
  edges e;unsigned char state[256]{};e.disarm();
  state[default_keys[0]]=0x80;check(!e.read(state,default_keys)[0],"held at installation does not activate");
  state[default_keys[0]]=0;e.read(state,default_keys);state[default_keys[0]]=0x80;
  check(e.read(state,default_keys)[0],"new press activates");
  check(!e.read(state,default_keys)[0],"held key does not repeat");
  e.disarm();check(!e.read(state,default_keys)[0],"focus loss disarms held key");
  constexpr auto enemy=(1u<<30)|(1u<<20);
  check(killable(enemy,20,false,0,0,0),"invented ordinary enemy is eligible");
  check(!killable(enemy,20,true,0,0,0),"script protection survives");
  check(!killable(enemy,20,false,0x10,0,0),"undying actor survives");
  check(!killable(enemy,20,false,0,0x40,0),"boss phase survives");
  check(!killable(enemy,20,false,0,0,true),"pain-script actor survives");
  check(!killable(enemy,20,false,0,0,false,8),"mind-controlled enemy survives");
  check(!killable((1u<<20)|(1u<<28),20,false,0,0,false),"neutral escort survives");
  check(killable((1u<<20)|(1u<<31),20,false,0,0,false),"alternate enemy team remains hostile");
  check(!killable(enemy|(1u<<29),20,false,0,0,0),"friendly team survives");
  check(!killable(1u<<30,20,false,0,0,0),"inactive enemy survives");
  check(!killable(enemy,0,false,0,0,0),"dead actor is never killed twice");
  check(party_member((1u<<20)|(1u<<29),0) && !party_member((1u<<20)|(1u<<29),-1),"seated hero distinguishes NPC double");
  check(return_script(0x80000001u)=="moveHeroesToEnt('-2147483647')", "high-bit handle uses atoi-compatible signed decimal");
  check(return_offsets[0][2]==10 && return_offsets[3][1]==-40, "native destinations include ten-unit height and world-axis offsets");
  check(distance_squared({0,0,0},{1,2,2}) < distance_squared({0,0,0},{9,0,0}), "nearest nondefault start wins over distant default");
  event_reader event;
  check(!scripted_event(event,0),"absent pain event is ordinary");
  check(scripted_event(event,513),"unreadable event fails closed");
  constexpr std::uint32_t pool=0x10000;
  event.values={{0x72989c,pool},{pool+0x2724,511},{pool+0x1d10,2},{pool+0x224c+4,513},{pool+24+8,0}};
  check(!scripted_event(event,513),"sound/effect-only event does not protect ordinary enemy");
  event.values[pool+24+8]=73;check(scripted_event(event,513),"invented script token protects scripted enemy");
  event.values[pool+24+8]=0;event.values[pool+0x224c+4]=1025;
  check(scripted_event(event,513),"reused event slot fails closed");
  event.values[pool+0x224c+4]=513;event.values[pool+0x1d10]=0;
  check(scripted_event(event,513),"retired event slot fails closed");
  event.values[pool+0x1d10]=2;event.values.erase(pool+24+8);
  check(scripted_event(event,513),"unreadable script token fails closed");
  using namespace xtraction_shop_rules;
  for(int mask=0;mask<64;++mask){
   check(testing_services(mask,false)==mask,"testing off preserves every service mask");
   check(effective_services(mask,false,true)==forge,"Forge-only never enables Load, Healer, Danger Room or team suppression");
   check(effective_services(mask,false,false)==0,"both options off leaves native services alone");
   const auto effective=testing_services(mask,true);
   check((effective&~(forge|forge_campaign))==(mask&~(forge|forge_campaign)),"Forge override preserves other services");
   check(list_option(true,effective,options[3],false,false)==listing::shown,"Forge override works before campaign unlock");
  }
 }
 std::vector<unsigned char> binary;
 std::array<unsigned char,0x6c> observed_hit{};
 bool hit_called=false;
 std::string popup_text;
 void __fastcall popup_fixture(void* self,void*,const char* text){
  popup_text=text;check(self==reinterpret_cast<void*>(1234),"popup text adapter preserves native dialog object");
 }
 void* __fastcall attack_fixture(void* hit,void*,std::uint32_t source,float,short,std::uint32_t,std::uint32_t,std::uint32_t,unsigned char,const float*,const float*,const float*){
  std::memset(hit,0,0x6c);std::memcpy(hit,&source,4);static_cast<unsigned char*>(hit)[0x35]=0xf7;return hit;
 }
 void __fastcall hit_fixture(void*,void*,void* hit,std::uint32_t a,std::uint32_t b,std::uint32_t c){
  std::memcpy(observed_hit.data(),hit,observed_hit.size());hit_called=true;check(a==0&&b==1&&c==1,"credited kill retains native damage call arguments");
 }
 double __fastcall clock_fixture(void*,void*){return 10.;}
 std::array<void*,100> game_vtable{};
 void* game_object=game_vtable.data();
 void* __cdecl game_fixture(){return &game_object;}
 void redirect(std::uint32_t site,const void* target){
  DWORD old=0;VirtualProtect(reinterpret_cast<void*>(site),16,PAGE_EXECUTE_READWRITE,&old);
  auto* p=reinterpret_cast<unsigned char*>(site);p[0]=0xe9;
  const auto delta=reinterpret_cast<std::uint32_t>(target)-site-5;std::memcpy(p+1,&delta,4);
  FlushInstructionCache(GetCurrentProcess(),p,5);
 }
 void retail(std::uint32_t va,std::size_t n){
  const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(binary.data());
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(binary.data()+dos->e_lfanew);
  const auto* section=IMAGE_FIRST_SECTION(nt);
  for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
   const auto rva=va-0x400000;
   if(rva>=section[i].VirtualAddress && rva+n<=section[i].VirtualAddress+section[i].SizeOfRawData){
    DWORD old=0;VirtualProtect(reinterpret_cast<void*>(va),n,PAGE_EXECUTE_READWRITE,&old);
    std::memcpy(reinterpret_cast<void*>(va),binary.data()+section[i].PointerToRawData+rva-section[i].VirtualAddress,n);return;
   }
  }
  check(false,"retail guard has file backing");
 }
 std::array<unsigned char,0x900> fixture_hero{},fixture_enemy{},fixture_polled{},fixture_near{},fixture_far{};
 std::array<unsigned char,0x200> fixture_stats{};
 std::vector<unsigned char> fixture_menus(0x86100),fixture_conversation(0x21b30),fixture_popups(0x4040);
 template<class T> void put_at(void* p,unsigned offset,T value){std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof(value));}
 template<class T> void global(unsigned a,T value){put_at(reinterpret_cast<void*>(a),0,value);}
 void* __fastcall resolve_fixture(const std::uint32_t* h,void*) {
  switch(*h){case 257:return fixture_hero.data();case 258:return fixture_far.data();case 259:return fixture_near.data();case 260:return fixture_enemy.data();case 261:return fixture_polled.data();default:return nullptr;}
 }
 std::uint32_t* __cdecl lookup_fixture(std::uint32_t* out,const char*){*out=257;return out;}
 bool __fastcall valid_fixture(const std::uint32_t*,void*){return true;}
 bool __fastcall vulnerable_fixture(void*,void*){return false;}
 int __cdecl atoi_fixture(const char* s){return std::atoi(s);}
 void* __cdecl empty_getter(){return nullptr;}
 unsigned query_acquires=0,query_releases=0,trace_calls=0;
 bool ceiling=false;float fixture_floor=2.f,fixture_normal=1.f;
 std::array<void*,36> query_vtable{};void* query_object=query_vtable.data();
 std::array<unsigned char,64> contact_record{};
 std::vector<std::array<float,3>> sweep_starts,sweep_ends;
 void __fastcall query_create(void** out,void*,const std::uint32_t*){++query_acquires;*out=&query_object;}
 void __fastcall query_release(void**,void*){++query_releases;}
 void __fastcall filter_property(void*,void*,int,int){}
 void __fastcall filter_record(void*,void*,int,const void*){}
 void __fastcall trace_segment(void*,void*,const float* a,const float* b,const float* extent){
  sweep_starts.push_back({a[0],a[1],a[2]});sweep_ends.push_back({b[0],b[1],b[2]});
  check(extent[0]==16 && extent[1]==16 && extent[2]==1,"collision sweep covers the full footprint");
 }
 std::uint32_t* __fastcall trace_result(void*,void*,std::uint32_t* out){
  const bool floor=(trace_calls++%2)==0;
  put_at(contact_record.data(),0x28,fixture_normal);put_at(contact_record.data(),0x38,fixture_floor);
  *out=(floor||ceiling)?reinterpret_cast<std::uint32_t>(contact_record.data()):0;return out;
 }
 unsigned selected_hits=0;
 void __fastcall selection_hit(void* actor,void*,void*,std::uint32_t,std::uint32_t,std::uint32_t){++selected_hits;put_at(actor,0x27c,-1.f);}
 LRESULT CALLBACK fixture_window(HWND h,UINT m,WPARAM w,LPARAM l){return m==WM_APP?427:DefWindowProcA(h,m,w,l);}
 void selection_and_frame(){
  // Execute the original numeric resolver through its installed trampoline.
  redirect(0x67233c,reinterpret_cast<void*>(&atoi_fixture));redirect(0x4654d0,reinterpret_cast<void*>(&valid_fixture));
  std::uint32_t token=0;
  auto lookup=reinterpret_cast<test_mode::lookup>(0x4a1700);
  check(lookup(&token,"-2147483647")==&token && token==0x80000001u,"native atoi resolver accepts a signed high-bit token through the installed bridge");
  check(test_mode::script_references[token&255]==token,"script lookup protects a resolved health-polled token");
  lookup(&token,"257");check(test_mode::script_references[1]==257,"new handle generation replaces the old script reference");
  test_mode::script_references.fill(0);
  // Native headroom contract with synthetic query results (fresh acquisition per sweep).
  query_vtable[0x40/4]=reinterpret_cast<void*>(&filter_property);query_vtable[0x4c/4]=reinterpret_cast<void*>(&filter_record);
  query_vtable[0x74/4]=reinterpret_cast<void*>(&trace_segment);query_vtable[0x20/4]=reinterpret_cast<void*>(&trace_result);
  redirect(0x4ab670,reinterpret_cast<void*>(&query_create));redirect(0x4ab720,reinterpret_cast<void*>(&query_release));mold_query::ready=true;
  check(test_mode::landing_clear({10,20,12}),"floor below landing with clear standing volume is accepted");
  check(query_acquires==2 && query_releases==2 && sweep_starts[1][2]==5 && sweep_ends[1][2]==84,"headroom uses a fresh released query from floor to top of landing body");
  ceiling=true;check(!test_mode::landing_clear({10,20,12}),"low ceiling refuses teleport");ceiling=false;
  fixture_floor=13;trace_calls=0;check(!test_mode::landing_clear({10,20,12}),"floor above the native destination refuses teleport");
  fixture_floor=2;fixture_normal=.3f;trace_calls=0;check(!test_mode::landing_clear({10,20,12}),"steep floor refuses teleport");fixture_normal=1;trace_calls=0;
  // Build a normal-play reader fixture, without a test pipe or any real game.
  DWORD previous=0;check(VirtualProtect(fixture_space,sizeof(fixture_space),PAGE_EXECUTE_READWRITE,&previous)!=0,"fixture owns writable high reader globals");
  std::memset(reinterpret_cast<void*>(0x778b70),0,0xc44);
  std::memset(reinterpret_cast<void*>(0x729960),0,0x600);
  std::memset(reinterpret_cast<void*>(0x72a578),0,0x230);
  global(0x729960,0x686e1cu);global(0x72a578,0x68878cu);std::strcpy(reinterpret_cast<char*>(0x72a758),"invented/room");
  global(0x729d48,10.f);global(0x729d54,0.f);global(0x729974,1u);
  global(0xa0a828,0u);std::strcpy(reinterpret_cast<char*>(0xa12828),"InventedHero");
  put_at(fixture_conversation.data(),0,0x685e04u);global(0x717aac,reinterpret_cast<unsigned>(fixture_conversation.data()));
  put_at(fixture_menus.data(),0,0x6a236cu);global(0x8aff18,reinterpret_cast<unsigned>(fixture_menus.data()));
  global(0x8b13ec,reinterpret_cast<unsigned>(fixture_popups.data()));
  global(0x682ccc,0x470000u);global(0x470000,static_cast<unsigned char>(0xb8));global(0x470001,0x684000u);global(0x470005,static_cast<unsigned char>(0xc3));
  global(0x70b840,0u);global(0x684018,16u);
  std::strcpy(reinterpret_cast<char*>(fixture_stats.data()+0x150),"InventedHero");
  auto actor=[&](auto& a,unsigned handle,unsigned flags,int controller){
   put_at(a.data(),0,0x682cccu);put_at(a.data(),4,flags);put_at(a.data(),0x1c,handle);put_at(a.data(),0x3d4,controller);
   put_at(a.data(),0x158,test_mode::vec{-16,-16,0});put_at(a.data(),0x164,test_mode::vec{16,16,72});
   put_at(a.data(),0x27c,40.f);put_at(a.data(),0x284,40.f);put_at(a.data(),0x35c,reinterpret_cast<unsigned>(fixture_stats.data()));
  };
  actor(fixture_hero,257,(1u<<29)|(1u<<20),0);actor(fixture_enemy,260,(1u<<30)|(1u<<20),-1);actor(fixture_polled,261,(1u<<30)|(1u<<20),-1);
  put_at(fixture_hero.data(),0x20,test_mode::vec{0,0,2});
  put_at(fixture_far.data(),0,0x689e5cu);put_at(fixture_far.data(),0x20,test_mode::vec{500,0,2});put_at(fixture_far.data(),0xda,static_cast<unsigned char>(2));
  put_at(fixture_near.data(),0,0x689e5cu);put_at(fixture_near.data(),0x20,test_mode::vec{20,0,2});
  unsigned ix=0;for(auto* p:{fixture_hero.data(),fixture_far.data(),fixture_near.data(),fixture_enemy.data(),fixture_polled.data()}){
   global(0x778b74+4*ix,reinterpret_cast<unsigned>(p));global(0x7793ac+4*ix,257u+ix);++ix;
  }
  global(0x779388,31u);
  redirect(0x4654b0,reinterpret_cast<void*>(&resolve_fixture));redirect(0x59ee20,reinterpret_cast<void*>(&empty_getter));redirect(0x55c890,reinterpret_cast<void*>(&empty_getter));
  test_mode::original_lookup=&lookup_fixture;test_mode::original_invulnerable=&vulnerable_fixture;
  global(0x682ccc+0xb0,reinterpret_cast<unsigned>(&selection_hit));
  test_mode::script_references[261&255]=261;test_mode::invulnerable=false;test_mode::pending_action=-1;test_mode::window_keys=true;
  check(test_mode::context_ready(),"normal-play context fixture is a living offline party");
  check(test_mode::window_key(WM_KEYDOWN,VK_F8,0) && selected_hits==0 && test_mode::pending_action==0,"window F8 only queues; input callback cannot run damage");
  check(test_mode::window_key(WM_KEYDOWN,VK_F8,1u<<30) && selected_hits==0,"held WM key does not repeat");
  game_version::pending=true;
  std::int32_t delta=0;std::memcpy(&delta,reinterpret_cast<void*>(0x402206),4);
  auto frame=reinterpret_cast<void*(__cdecl*)()>(0x40220a+delta);
  frame();
  check(!game_version::pending && selected_hits==1 && test_mode::at<float>(fixture_polled.data(),0x27c)==40,"installed frame consumes refusal and kills ordinary enemy while protecting script-polled actor");
  frame();check(selected_hits==1,"subsequent frames do not repeat the consumed action");
  test_mode::window_key(WM_KEYDOWN,VK_F9,0);check(!test_mode::invulnerable,"F9 also waits for the frame");frame();check(test_mode::invulnerable,"frame applies queued F9 with no pipe");
  test_mode::window_key(WM_KEYDOWN,VK_F9,0);std::strcpy(reinterpret_cast<char*>(0x72a758),"invented/other");frame();check(test_mode::invulnerable && test_mode::pending_action==-1,"zone change drops a queued action instead of applying it to another room");
  std::strcpy(reinterpret_cast<char*>(0x72a758),"invented/room");test_input::accept=true;trace_calls=0;ceiling=false;
  test_mode::window_key(WM_SYSKEYDOWN,VK_F10,0);frame();
  check(test_input::queued=="moveHeroesToEnt('259')","F10 chooses nearest nondefault start over first/default far start");
  test_input::queued.clear();ceiling=true;trace_calls=0;
  test_mode::window_key(WM_SYSKEYDOWN,VK_F10,0);frame();check(test_input::queued.empty(),"obstructed nearest start refuses rather than escaping to farther default");
  check(!test_mode::window_key(WM_SYSKEYDOWN,VK_F10,1u<<29),"Alt combinations are forwarded to the native window procedure");
  WNDCLASSA wc{};wc.lpfnWndProc=fixture_window;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="InventedTestModeWindow";
  RegisterClassA(&wc);HWND window=CreateWindowExA(0,wc.lpszClassName,"",0,0,0,1,1,nullptr,nullptr,wc.hInstance,nullptr);
  check(window!=nullptr,"hidden fixture window exists without activation");
  if(window){test_mode::watch_window(window);check(test_mode::watched_window==window && SendMessageA(window,WM_APP,0,0)==427,"pipe-free observer installs independently of display mode and preserves the original procedure");
   test_mode::invulnerable=false;SendMessageA(window,WM_KEYDOWN,VK_F9,0);check(!test_mode::invulnerable && test_mode::pending_action==1,"real window message is deferred until the frame");frame();check(test_mode::invulnerable,"installed observer and frame execute a pipe-free hotkey");
   SendMessageA(window,WM_KEYDOWN,VK_F9,0);SendMessageA(window,WM_KILLFOCUS,0,0);frame();check(test_mode::invulnerable,"focus loss cancels a queued action");DestroyWindow(window);check(!test_mode::watched_window && !test_mode::original_window_proc,"window destruction permits observing a recreated game window");}
  test_mode::enabled=false;check(!test_mode::window_key(WM_KEYDOWN,VK_F8,0),"option off leaves window input untouched");test_mode::enabled=true;

 }

 unsigned fixture_text_object=0x68e990;
 std::array<void*,32> fixture_dialog_vtable{};void* fixture_dialog_object=fixture_dialog_vtable.data();
 std::vector<std::string> offered_services;
 void* __cdecl texts_fixture(){return &fixture_text_object;}
 void* __cdecl dialog_fixture(){return &fixture_dialog_object;}
 const char* __fastcall label_fixture(void*,void*,int){return "Invented service label";}
 void __fastcall option_fixture(void*,void*,const char*,const char* script,const char*,int,int,int){offered_services.emplace_back(script);}
 void forge_only(){
  auto seed=[](){for(const auto& g:xtraction_shop_rules::guards)retail(g.va,g.hex.size()/2);for(const auto& g:xtraction_shop_rules::net_guards)retail(g.va,g.hex.size()/2);for(const auto& g:xtraction_shop_rules::change_team_guards)retail(g.va,g.hex.size()/2);};
  seed();ini::forge=false;ini::shop=false;xtract::shop_installed=xtract::services_installed=false;
  xtract::install(reinterpret_cast<HMODULE>(0x400000));check(!xtract::services_installed,"both shop switches off leave finalize callback untouched");
  ini::forge=true;logger::lines.clear();xtract::install(reinterpret_cast<HMODULE>(0x400000));
  check(xtract::services_installed && !xtract::shop_installed && xtract::capability(false)==std::array<std::uint32_t,4>{},"Forge-only installs its option without advertising the XtractionShop capability");
  check(std::none_of(logger::lines.begin(),logger::lines.end(),[](const auto& line){return line.find("XtractionShop=1")!=std::string::npos;}),"Forge-only never logs that disabled XtractionShop is enabled");
  xtract::online_guarded=false;
  redirect(0x4bdb30,reinterpret_cast<void*>(&texts_fixture));redirect(0x5eb300,reinterpret_cast<void*>(&dialog_fixture));
  global(0x68e998,reinterpret_cast<unsigned>(&label_fixture));fixture_dialog_vtable[7]=reinterpret_cast<void*>(&option_fixture);
  for(int mask=0;mask<64;++mask){xtract::point_services=mask;offered_services.clear();check(xtract::shop_dialog()==&fixture_dialog_object && offered_services==std::vector<std::string>{"shopMenu('equip')"},"actual Forge-only wrapper offers exactly Forge for every source mask");}
  global(xtract_rules::choice_block+1,xtraction_shop_rules::to_change_team);xtract::xtract_installed=true;
  xtract::set_services(xtraction_shop_rules::no_change_team);
  check(*reinterpret_cast<unsigned char*>(xtract_rules::choice_block+1)==xtraction_shop_rules::to_change_team,"Forge-only leaves Change Team available despite lite-point service bits");
  seed();ini::shop=true;xtract::shop_installed=xtract::services_installed=false;xtract::install(reinterpret_cast<HMODULE>(0x400000));
  check(xtract::shop_installed && xtract::capability(false)[0]==1,"explicit XtractionShop still installs and advertises all services");
  ini::forge=ini::shop=false;
 }

 void native(const char* path){
  std::ifstream f(path,std::ios::binary);binary.assign(std::istreambuf_iterator<char>(f),{});
  if(binary.size()<4096){check(false,"private executable readable");return;}
  check(reinterpret_cast<std::uintptr_t>(fixture_space)==0x401000,"fixture owns retail address range");
  constexpr std::uint32_t sites[]{0x41c320,0x682d04,0x402205,0x55c890,0x4a1700,0x41a220,0x41a254,0x4ab670,0x4ab720,0x6a3344,0x5e9e90,0x496a20,0x4654b0,0x499740,0x429860,0x4a2560,0x67aaa0,0x498dd7,0x498d9f,0x498dbb,0x467730,0x4676bc,0x467333,0x498ab5,0x4a88e0,0x610d20,0x5d8640,0x4a36f0,0x4a1ac0,0x474d43,0x59ee20,0x59cdb0};
  auto seed=[&]{test_mode::notice_installed=false;for(auto a:sites)retail(a,64);};
  seed();ini::values[L"KillEnemiesKey"]="F6";test_mode::install(reinterpret_cast<HMODULE>(0x400000));
  check(!test_mode::hotkeys_enabled() && *reinterpret_cast<unsigned char*>(0x496a20)==0x56,"all off never patches predicate");
  check(*reinterpret_cast<std::uint32_t*>(0x6a3344)==0x5e9e90,"key assignments alone never change the native popup slot");
  ini::values.clear();ini::forge=true;test_mode::install(reinterpret_cast<HMODULE>(0x400000));
  check(test_mode::notice_installed && !test_mode::hotkeys_enabled() && *reinterpret_cast<unsigned char*>(0x496a20)==0x56,"Forge-only installs named refusal without enabling hotkeys or protection");
  ini::forge=false;seed();
  ini::hotkeys=true;
  game_version::testing=false;test_mode::install(reinterpret_cast<HMODULE>(0x400000));
  check(!test_mode::hotkeys_enabled() && *reinterpret_cast<unsigned char*>(0x496a20)==0x56,"late INI enable without offline exclusion refuses");
  game_version::testing=true;
  for(auto a:sites){
   seed();*reinterpret_cast<unsigned char*>(a)^=1;
   const auto before=*reinterpret_cast<unsigned char*>(0x496a20);
   test_mode::install(reinterpret_cast<HMODULE>(0x400000));
   check(!test_mode::hotkeys_enabled() && *reinterpret_cast<unsigned char*>(0x496a20)==before,"each guard mismatch refuses without patching");
  }
  seed();ini::values[L"KillEnemiesKey"]="F9";
  test_mode::install(reinterpret_cast<HMODULE>(0x400000));check(!test_mode::hotkeys_enabled(),"duplicate keys refuse installation");
  ini::values.clear();test_mode::keys=test_mode_rules::default_keys;
  seed();test_mode::install(reinterpret_cast<HMODULE>(0x400000));
  check(test_mode::hotkeys_enabled() && *reinterpret_cast<unsigned char*>(0x496a20)==0xe9,"production installer publishes complete predicate hook");
  check(test_mode::console_frame_hook_matches(),"console accepts only the installed frame getter hook");
  *reinterpret_cast<unsigned char*>(0x402206)^=1;check(!test_mode::console_frame_hook_matches(),"foreign frame target is rejected");*reinterpret_cast<unsigned char*>(0x402206)^=1;
  if(!test_mode::hotkeys_enabled())return;
  redirect(0x5e9e90,reinterpret_cast<void*>(&popup_fixture));
  const auto popup=reinterpret_cast<test_mode::popup_text>(*reinterpret_cast<std::uint32_t*>(0x6a3344));
  popup(reinterpret_cast<void*>(1234),nullptr,"Invented normal popup");check(popup_text=="Invented normal popup","normal popup text is unchanged");
  game_version::pending=true;popup(reinterpret_cast<void*>(1234),nullptr,"Invented generic network error");
  check(popup_text==game_version::test_mode_reason(),"the actual installed popup slot displays the testing reason");
  popup(reinterpret_cast<void*>(1234),nullptr,"Invented later popup");check(popup_text=="Invented later popup","unrelated later popups are unchanged");
  game_vtable[0x160/4]=reinterpret_cast<void*>(&clock_fixture);redirect(0x46dce0,reinterpret_cast<void*>(&game_fixture));
  std::array<unsigned char,0x900> hero{};
  auto put=[&](unsigned offset,auto value){std::memcpy(hero.data()+offset,&value,sizeof(value));};
  put(0,std::uint32_t(0x682ccc));put(4,std::uint32_t((1u<<20)|(1u<<29)));put(0x3d4,0);put(0x294,-1.f);
  const auto predicate=reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x496a20);
  check(predicate(hero.data(),nullptr),"trampoline retains preexisting scripted invulnerability");
  test_mode::invulnerable=true;put(0x294,0.f);
  check(predicate(hero.data(),nullptr),"testing toggle protects a seated invented hero");
  check(test_mode::at<float>(hero.data(),0x294)==0.f,"testing protection never changes saved actor deadline");
  test_mode::invulnerable=false;put(0x294,-1.f);
  check(predicate(hero.data(),nullptr),"switching testing off preserves scripted protection");
  put(0x294,0.f);check(!predicate(hero.data(),nullptr),"switching testing off restores normal vulnerability");
  put(0x294,12.f);check(predicate(hero.data(),nullptr),"native temporary protection before deadline survives trampoline");
  put(0x294,10.f);check(!predicate(hero.data(),nullptr),"native protection expires at exact deadline");
  put(0x294,-1.f);
  put(4,std::uint32_t(1u<<30));test_mode::invulnerable=true;
  check(predicate(hero.data(),nullptr),"nonparty actor falls through complete native prologue");
  put(0x294,0.f);check(!predicate(hero.data(),nullptr),"testing never protects a hostile actor");
  redirect(0x429860,reinterpret_cast<void*>(&attack_fixture));
  std::array<void*,64> actor_vtable{};actor_vtable[0xb0/4]=reinterpret_cast<void*>(&hit_fixture);
  void* invented_actor=actor_vtable.data();test_mode::kill_actor(&invented_actor,1234);
  check(hit_called && test_mode::at<std::uint32_t>(observed_hit.data(),0)==1234,"kill damage retains party attacker for native XP credit");
  check(test_mode::at<float>(observed_hit.data(),4)==1000000.f && test_mode::at<std::uint32_t>(observed_hit.data(),0x10)==0x10000000 && observed_hit[0x35]==0x8f,"kill damage retains engine killEntity recipe");
  selection_and_frame();
  forge_only();
 }
}
int main(int argc,char**argv){std::setbuf(stdout,nullptr);rules();if(argc>1)native(argv[1]);else std::puts("Native install checks skipped: pass private XMen2.exe.");std::printf("Test mode: %d checks, %d failures\n",checks,failures);return failures?1:0;}
