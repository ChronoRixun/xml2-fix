// Execute the production installer and both bridges in an owned image, with invented actors and moves.
// Optional retail executable input verifies guards; no game code or content is embedded in the test.
#include "stun_rule.hpp"
#include "stun_rules.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

extern "C" std::uint8_t fixture_space[0x400000];
namespace ini {
 std::optional<std::string> setting;
 std::optional<std::string> text(const wchar_t*, const wchar_t*) { return setting; }
}
namespace logger { int lines = 0; void write(const char*, ...) { ++lines; } }
namespace game_version { bool refused = false; void refuse_startup() { refused = true; } }
namespace {
 using u32 = std::uint32_t;
 int failures = 0, checks = 0;
 void check(bool value, const char* what) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", what); } }
 template<class T> T& field(void* p, unsigned o) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + o); }
 std::uint8_t* at(u32 a) { return reinterpret_cast<std::uint8_t*>(a); }
 std::vector<std::uint8_t> hex(std::string_view s) {
  std::vector<std::uint8_t> result;
  for (std::size_t i = 0; i < s.size(); i += 2) result.push_back(static_cast<std::uint8_t>(std::stoul(std::string(s.substr(i, 2)), nullptr, 16)));
  return result;
 }
 std::vector<std::uint8_t> binary;
 std::size_t file_offset(u32 va) {
  auto u16=[](std::size_t o){std::uint16_t n;std::memcpy(&n,binary.data()+o,2);return n;};
  auto u32at=[](std::size_t o){u32 n;std::memcpy(&n,binary.data()+o,4);return n;};
  const auto pe=u32at(0x3c), sections=pe+24+u16(pe+20);
  for(unsigned i=0;i<u16(pe+6);++i){const auto row=sections+i*40, rva=u32at(row+12);if(va-0x400000>=rva && va-0x400000<rva+u32at(row+16))return u32at(row+20)+va-0x400000-rva;}
  return binary.size();
 }
 void guards() { for (const auto& g : stun_rules::guards) { const auto bytes=hex(g.hex);std::memcpy(at(g.va),bytes.data(),bytes.size()); } }
 bool untouched() {
  for (const auto& g : stun_rules::guards) if (!limits_rules::matches(at(g.va),g.hex)) return false;
  return true;
 }
 alignas(16) std::array<std::uint8_t,0x900> actor{};
 alignas(16) std::array<std::uint8_t,0x160> dazed{}, tumble{}, ready{};
 void* drive_actor=actor.data(); void* drive_move=nullptr;
 u32 setter_site=stun_rules::set_move;
 u32 registers[8]{}, before_flags=0, after_flags=0;
 __declspec(naked) void drive_set() {
  __asm {
   pushad
   mov edi, drive_actor
   mov esi, drive_move
   mov eax, 0x12345678
   mov ebx, 0x23456789
   mov ecx, 0x3456789a
   mov edx, 0x456789ab
   mov ebp, 0x56789abc
   cmp eax, eax
   pushfd
   pop before_flags
   call dword ptr [setter_site]
   mov registers[0], eax
   mov registers[4], ebx
   mov registers[8], ecx
   mov registers[12], edx
   mov registers[16], ebp
   mov registers[20], esi
   mov registers[24], edi
   pushfd
   pop after_flags
   popad
   ret
  }
 }
 // Invoke the installed CALL target with the native stack layout and ECX.
 void drive_request() {
  u32 rel;std::memcpy(&rel,at(stun_rules::dispatch_call+1),4);
  using request_t=void(__fastcall*)(void*,void*,void*,u32,float,u32,u32,u32);
  reinterpret_cast<request_t>(stun_rules::dispatch_call+5+rel)(drive_actor,nullptr,drive_move,91,.75f,0,0,1);
 }
 int calls=0; bool accepts=true, arguments_ok=true;
 void __fastcall dispatch(void* who, void*, void* move, u32 animation, float speed, u32 a, u32 b, u32 c) {
  ++calls;
  arguments_ok &= who==actor.data() && move==dazed.data() && animation==91 && speed==.75f && a==0 && b==0 && c==1;
  if (accepts) { drive_move=move;drive_set(); }
 }
 void reset(bool hero, int controller=0) {
  actor.fill(0);field<u32>(actor.data(),4)=hero ? 1u<<20 : 0;field<int>(actor.data(),0x3d4)=controller;
  field<void*>(actor.data(),0x378)=tumble.data();actor[0x765]=0xa5;
  field<float>(actor.data(),0x600)=42.5f;field<float>(actor.data(),0x5e8)=12.25f;
  drive_move=dazed.data();
 }
}
namespace {
 float current_time=100, base_life=2, assigned_life=-1;
 bool competitive=false;
 int duration_writes=0;
 std::array<void*,80> table_vt{},power_vt{},move_vt{},actor_vt{},game_vt{},hud_vt{},manager_vt{};
 void** table_object=table_vt.data();void** power_object=power_vt.data();void** game_object=game_vt.data();void** hud_object=hud_vt.data();void** manager_object=manager_vt.data();
 void __fastcall select_index(void*,void*,u32){}
 void* __fastcall get_power(void*,void*,u32){return &power_object;}
 void __fastcall get_handle(void*,void*,u32* out){*out=77;}
 void* __cdecl resolve(u32){return actor.data();}
 void* __fastcall lookup(void*,void*,u32* name){return reinterpret_cast<u32>(name)==stun_rules::stun_name ? dazed.data() : ready.data();}
 float __fastcall speed(void*,void*,void*){return .75f;}
 u32 __fastcall animation(void*,void*){return 91;}
 bool __fastcall susceptible(void*,void*){return true;}
 void* __cdecl get_manager(){return &manager_object;}
 void* __cdecl get_hud(){return &hud_object;}
 void* __cdecl get_game(){return &game_object;}
 void* __fastcall coordinates(void*,void*,void* out){return out;}
 const char* __fastcall hud_label(void*,void*,u32,void*){return "synthetic";}
 void __fastcall notice(void*,void*,const char*,void*,float){}
 float __cdecl clock_value(){return current_time;}
 float __fastcall lifetime(void*,void*){return base_life;}
 void __fastcall set_lifetime(void*,void*,float value){assigned_life=value;++duration_writes;}
 bool __fastcall arena(void*,void*){return competitive;}
 void __fastcall expire_move(void*,void*,void* move,u32){drive_move=move;drive_set();}
 void redirect(u32 a,const void* f){auto bytes=stun_rules::branch(a,f,false,5);std::memcpy(at(a),bytes.data(),bytes.size());}
 struct result { std::array<std::uint8_t,0x900> bytes; float life; int requests,writes; };
 result trial(float factor,bool already_stunned,bool hero,bool in_arena){
  reset(hero);actor[0x765]=already_stunned?0xa5:0xa4;field<void*>(actor.data(),0)=actor_vt.data();
  field<float>(actor.data(),0x27c)=80;field<float>(actor.data(),0x520)=factor;field<void*>(actor.data(),0x378)=already_stunned?dazed.data():ready.data();
  accepts=true;competitive=in_arena;calls=0;duration_writes=0;assigned_life=-1;drive_move=dazed.data();
  using callback=void(__stdcall*)(u32,void*);
  // This is the actual retail entry, including susceptibility, refresh and lifetime calculation.
  reinterpret_cast<callback>(0x549fc0)(7,&table_object);
  const auto after=actor;const float life=assigned_life;const int requested=calls;
  // Exercise native expiration, not a synthetic flag clear.
  reinterpret_cast<callback>(0x54a1e0)(7,&table_object);
  check(!(actor[0x765]&1),"native expiry clears the flag");
  if(factor>0)check(field<void*>(actor.data(),0x378)==ready.data(),"native expiry changes accepted stun to idle");
  return {after,life,requested,duration_writes};
 }
}
void native_powerup_checks() {
 const std::vector<std::uint8_t> hook_call(at(stun_rules::dispatch_call),at(stun_rules::dispatch_call)+5),hook_set(at(stun_rules::set_move),at(stun_rules::set_move)+6);
 auto load=[&](u32 a,unsigned n){const auto o=file_offset(a);if(o+n>binary.size())ExitProcess(2);std::memcpy(at(a),binary.data()+o,n);};
 load(0x549fc0,0x2c6);load(0x680030,4);load(0x682a2c,4);load(0x699db4,4);
 table_vt[0]=reinterpret_cast<void*>(&select_index);table_vt[2]=reinterpret_cast<void*>(&get_power);
 power_vt[0x60/4]=reinterpret_cast<void*>(&get_handle);power_vt[0x50/4]=reinterpret_cast<void*>(&lifetime);power_vt[0x94/4]=reinterpret_cast<void*>(&set_lifetime);
 move_vt[0xf0/4]=reinterpret_cast<void*>(&speed);move_vt[0xe8/4]=reinterpret_cast<void*>(&animation);field<void*>(dazed.data(),0)=move_vt.data();
 actor_vt[0x24/4]=reinterpret_cast<void*>(&coordinates);manager_vt[2]=reinterpret_cast<void*>(&hud_label);hud_vt[0x44/4]=reinterpret_cast<void*>(&notice);game_vt[0x5c/4]=reinterpret_cast<void*>(&arena);
 redirect(0x537030,&resolve);redirect(0x427ca0,&lookup);redirect(0x427e60,&susceptible);redirect(0x4bdb30,&get_manager);redirect(0x59ee20,&get_hud);redirect(0x48de40,&get_game);redirect(0x45fe60,&clock_value);redirect(0x42c340,&expire_move);
 std::vector<result> control;
 const std::array factors{0.f,.01f,.25f,.5f,1.f,2.f,5.f};
 // Restore only the two native sites for the negative control; the test owns all mapped memory.
 load(stun_rules::set_move,6);load(stun_rules::dispatch_call,5);
 for(float factor:factors)for(bool refresh:{false,true})for(bool hero:{false,true})for(bool online_arena:{false,true})control.push_back(trial(factor,refresh,hero,online_arena));
 std::memcpy(at(stun_rules::dispatch_call),hook_call.data(),5);std::memcpy(at(stun_rules::set_move),hook_set.data(),6);
 std::size_t index=0;
 for(float factor:factors)for(bool refresh:{false,true})for(bool hero:{false,true})for(bool online_arena:{false,true}){
  auto actual=trial(factor,refresh,hero,online_arena);const auto& expected=control[index++];
  check(actual.bytes==expected.bytes,"uninterrupted native powerup leaves identical actor state before and after hook");
  check(actual.life==expected.life && actual.writes==expected.writes,"native assigned duration and expiry writes are unchanged");
  check(actual.requests==expected.requests,"native accepted request frequency is unchanged, including refresh and immunity");
 }
 std::printf("%zu native powerup scenarios compared\n",index);
}

namespace {
 std::array<std::uint8_t,0x4100> interned{};
 u32 alternate_name=0x70000012;
 bool primary_applies=false, moves_available=true;
 int alternate_requests=0;
 void* __cdecl intern_table(){return interned.data();}
 void* __fastcall find_move(void*,void*,const u32* name){
  if(!moves_available)return nullptr;
  return *name==alternate_name ? tumble.data() : dazed.data();
 }
 bool __fastcall applicable(void* move,void*,void*,u32){return move==tumble.data() || primary_applies;}
 const u32* __fastcall alternate(void*,void*){++alternate_requests;return &alternate_name;}
}
void native_resolution_checks() {
 // Execute the retail resolver and applicability/alternate traversal with synthetic style tables.
 // This establishes why name-field equality is weaker than the native pointer comparison.
 auto load=[&](u32 a,unsigned n){const auto o=file_offset(a);if(o+n>binary.size())ExitProcess(2);std::memcpy(at(a),binary.data()+o,n);};
 load(stun_rules::resolve_move,0x15);load(0x41d0d0,0x10);load(0x501a50,0x99);
 redirect(0x602280,&intern_table);
 interned[0x4008]='x';
 table_vt.fill(nullptr);table_vt[0x14/4]=reinterpret_cast<void*>(&find_move);
 move_vt[8/4]=reinterpret_cast<void*>(&applicable);move_vt[0xd4/4]=reinterpret_cast<void*>(&alternate);
 field<void*>(dazed.data(),0)=move_vt.data();field<void*>(tumble.data(),0)=move_vt.data();
 field<u32>(dazed.data(),8)=0x70000011;field<u32>(tumble.data(),8)=alternate_name;
 *reinterpret_cast<u32*>(stun_rules::stun_name)=0x70000011;
 reset(true);field<void*>(actor.data(),0x360+3*4)=&table_object;
 using resolve_t=void*(__thiscall*)(void*,const u32*);
 const auto resolve_native=reinterpret_cast<resolve_t>(stun_rules::resolve_move);
 const auto* name=reinterpret_cast<const u32*>(stun_rules::stun_name);
 check(resolve_native(actor.data(),name)==tumble.data() && alternate_requests==1,"retail resolver chooses applicable alternate with a different name");
 drive_move=tumble.data();drive_set();
 check(actor[0x765]==0xa5,"production bridge keeps native-resolved alternate stun");
 drive_move=dazed.data();drive_set();
 check(actor[0x765]==0xa4,"production bridge clears same-name primary rejected by retail applicability");
 primary_applies=true;actor[0x765]=0xa5;drive_move=dazed.data();drive_set();
 check(actor[0x765]==0xa5 && resolve_native(actor.data(),name)==dazed.data(),"applicable primary stun still retains flag");
 moves_available=false;drive_set();
 check(actor[0x765]==0xa4,"missing resolved stun clears the party flag");
}

int main(int argc,char** argv) {
 if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000 || reinterpret_cast<std::uintptr_t>(fixture_space)>0x41d890) return 2;
 DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old))return 2;
 if(argc>1) {
  std::ifstream input(argv[1],std::ios::binary);binary.assign(std::istreambuf_iterator<char>(input),{});
  if(binary.size()<0x1000)return 2;
  for(const auto& g:stun_rules::guards){const auto bytes=hex(g.hex);const auto o=file_offset(g.va);check(o+bytes.size()<=binary.size() && std::memcmp(binary.data()+o,bytes.data(),bytes.size())==0,"guard matches retail input");}
  if(failures)return 1;
 } else std::puts("SKIPPED retail guard comparison (no executable argument)");
 guards();stun_rule::install(GetModuleHandleW(nullptr));check(!stun_rule::requested() && stun_rule::verified() && untouched(),"absent key is verified native");
 ini::setting=" XML2 ; explicit native";stun_rule::install(GetModuleHandleW(nullptr));check(!stun_rule::requested() && untouched(),"explicit xml2 makes no writes");
 ini::setting="invented";stun_rule::install(GetModuleHandleW(nullptr));check(stun_rule::requested() && !stun_rule::verified() && untouched(),"invalid key refuses without writes");
 ini::setting=" XML1 ; original reaction ownership";
 for(const auto& g:stun_rules::guards) for(std::size_t byte=0;byte<g.hex.size()/2;++byte) {
  guards();at(g.va)[byte]^=1;const auto before=logger::lines;stun_rule::install(GetModuleHandleW(nullptr));
  check(!stun_rule::installed() && !stun_rule::verified() && logger::lines==before+1,"each bad guard refuses once");
  at(g.va)[byte]^=1;check(untouched(),"guard refusal leaves every other site native");
 }
 guards();stun_rule::install(GetModuleHandleW(nullptr));check(stun_rule::installed() && stun_rule::verified() && !game_version::refused,"production transaction installs");
 check(stun_rule::values()==std::array<u32,4>{1,0,0,0},"effective capability is on");
 const auto redirect=stun_rules::branch(stun_rules::native_dispatch,&dispatch,false,5);std::memcpy(at(stun_rules::native_dispatch),redirect.data(),redirect.size());
 const auto lookup_redirect=stun_rules::branch(stun_rules::resolve_move,&lookup,false,5);std::memcpy(at(stun_rules::resolve_move),lookup_redirect.data(),lookup_redirect.size());
 *at(stun_rules::set_move_resume)=0xc3;*at(stun_rules::dispatch_call+5)=0xc3;
 field<u32>(dazed.data(),8)=0x70000011;field<u32>(tumble.data(),8)=0x70000012;field<u32>(ready.data(),8)=0x70000013;
 *reinterpret_cast<u32*>(stun_rules::stun_name)=field<u32>(dazed.data(),8);
 for(int controller=0;controller<4;++controller) {
  reset(true,controller);accepts=false;drive_request();
  check(actor[0x765]==0xa4 && field<void*>(actor.data(),0x378)==tumble.data(),"rejected party stun is discarded");
  drive_move=ready.data();drive_set();check(!(actor[0x765]&1),"get-up completion cannot reenter stale stun");
  reset(true,controller);accepts=true;drive_request();
  check(actor[0x765]==0xa5 && field<void*>(actor.data(),0x378)==dazed.data(),"accepted party stun retains flag");
  const auto after_first=actor;drive_request();check(actor==after_first,"same-stun refresh does not mutate reaction state");
  drive_move=tumble.data();drive_set();check(actor[0x765]==0xa4,"interruption clears party stun");
  check(field<float>(actor.data(),0x600)==42.5f && field<float>(actor.data(),0x5e8)==12.25f,"native clocks are untouched");
  check(registers[0]==0x12345678 && registers[1]==0x23456789 && registers[2]==0x3456789a && registers[3]==0x456789ab && registers[4]==0x56789abc && registers[5]==reinterpret_cast<u32>(tumble.data()) && registers[6]==reinterpret_cast<u32>(actor.data()) && before_flags==after_flags,"setter bridge preserves registers and flags");
 }
 for(bool member:{false,true})for(int controller:{-1,0,3,4}) {
  if(member && controller>=0 && controller<4)continue;
  reset(member,controller);const auto before=actor;accepts=false;drive_request();check(actor==before,"non-party rejected stun remains byte-for-byte native");
  drive_move=ready.data();drive_set();auto expected=before;field<void*>(expected.data(),0x378)=ready.data();check(actor==expected,"non-party move change touches only native current move");
 }
 reset(true);actor[0x765]=0xa4;drive_move=tumble.data();drive_set();check(actor[0x765]==0xa4,"unstunned hero is unchanged");
 check(arguments_ok && calls==18,"every request forwarded exactly once with all native arguments");
 // V-TBD: a resolved alternate need not have the requested name; an unselected move may share it.
 reset(true);field<u32>(dazed.data(),8)=0x70000021;drive_move=dazed.data();drive_set();
 check(actor[0x765]==0xa5,"resolved alternate stun retains flag despite a different interned name");
 field<u32>(tumble.data(),8)=*reinterpret_cast<u32*>(stun_rules::stun_name);drive_move=tumble.data();drive_set();
 check(actor[0x765]==0xa4,"same-name move not selected by actor resolver does not retain stun flag");
 if(argc>1){native_powerup_checks();native_resolution_checks();}
 std::printf("%d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
