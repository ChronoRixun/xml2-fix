// Synthetic actors and a private retail executable as a read-only guard input.
// No game process, graphics, saves, registry or network calls.
#include "test_mode.cpp"
#include "xtraction_shop_rules.hpp"
#include <fstream>
#include <map>
#include <cstdarg>

namespace ini {
 bool forge=false,hotkeys=false;
 std::map<std::wstring,std::string> values;
 bool flag(const wchar_t* section,const wchar_t* key,bool fallback) {
  if(std::wcscmp(section,L"TestMode"))return fallback;
  return std::wcscmp(key,L"ForgeEverywhere")==0 ? forge : std::wcscmp(key,L"Hotkeys")==0 && hotkeys;
 }
 std::optional<std::string> text(const wchar_t*,const wchar_t* key) {
  auto i=values.find(key);return i==values.end()?std::nullopt:std::optional<std::string>(i->second);
 }
}
namespace logger { void write(const char*,...) {} }
namespace game_version {
 bool refused=false,testing=true,pending=false;
 void refuse_startup(){refused=true;}bool test_mode_active(){return testing;}
 bool take_test_mode_refusal(){const bool result=pending;pending=false;return result;}
 const char* test_mode_reason(){return "Invented [TestMode] refusal";}
}
namespace mold_query { bool supported(){return true;}bool pools_ready(){return false;} }
namespace entity_capacity { const entity_capacity_layout::descriptor& installed(){return entity_capacity_layout::stock_descriptor;} }
namespace test_input { bool queue_script_on_game_thread(const char*){return false;} }
extern "C" unsigned char fixture_space[0x400000];
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
  check(!killable(enemy|(1u<<29),20,false,0,0,0),"friendly team survives");
  check(!killable(1u<<30,20,false,0,0,0),"inactive enemy survives");
  check(!killable(enemy,0,false,0,0,0),"dead actor is never killed twice");
  check(party_member((1u<<20)|(1u<<29),0) && !party_member((1u<<20)|(1u<<29),-1),"seated hero distinguishes NPC double");
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
 void native(const char* path){
  std::ifstream f(path,std::ios::binary);binary.assign(std::istreambuf_iterator<char>(f),{});
  if(binary.size()<4096){check(false,"private executable readable");return;}
  check(reinterpret_cast<std::uintptr_t>(fixture_space)==0x401000,"fixture owns retail address range");
  constexpr std::uint32_t sites[]{0x6a3344,0x5e9e90,0x496a20,0x4654b0,0x499740,0x429860,0x4a2560,0x67aaa0,0x498dd7,0x498d9f,0x498dbb,0x467730,0x4676bc,0x467333,0x498ab5,0x4a88e0,0x610d20,0x5d8640,0x4a36f0,0x4a1ac0,0x474d43,0x59ee20,0x59cdb0};
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
 }
}
int main(int argc,char**argv){std::setbuf(stdout,nullptr);rules();if(argc>1)native(argv[1]);else std::puts("Native install checks skipped: pass private XMen2.exe.");std::printf("Test mode: %d checks, %d failures\n",checks,failures);return failures?1:0;}
