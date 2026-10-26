#include "resident_stats.hpp"
#include "patch_transaction.hpp"
#include "resident_stats_sites.hpp"
#include "resident_payload.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
namespace resident_stats {
namespace {
using u32=std::uint32_t;
constexpr auto payload=resident_payload::layout(320,136);
bool applied=false,requested=false;
std::array<std::atomic<u32>,3> counts{},peaks{},refusals{};
std::array<bool,297> logged{};
std::atomic<u32> violations{0},epoch{0};
std::atomic<bool> census_requested{false};
std::mutex cache_lock;std::string census="{\"ok\":false,\"error\":\"awaiting game-thread sample\"}";
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
u32 read(u32 a){u32 n=0;patch_transaction::read_memory(a,&n,4);return n;}
bool enabled(const std::optional<std::string>& k){return k&&*k!="31";}
void settle(bool ok){gameplay_agreement::register_rule(gameplay_capabilities::resident_stats_id,1,{320,22,320,1},ok);}
unsigned char __fastcall owns(void* p,void*,void* probe){return resident_payload::owns(p,probe,payload);}
void __fastcall destroy(void* p,void*){resident_payload::destroy(p,payload);}
u32 __fastcall pop(void* p,void*){return resident_payload::pop(p,payload);}
void __fastcall release(void* p,void*,u32 i){resident_payload::release(p,i,payload);}
void __fastcall initialize(void* p,void*){resident_payload::initialize_constructor(p,payload);}
u32 replacement(u32 va){switch(va){case 0x448ff0:return address(reinterpret_cast<void*>(&owns));case 0x449150:return address(reinterpret_cast<void*>(&destroy));case 0x4497d0:return address(reinterpret_cast<void*>(&pop));case 0x44a120:return address(reinterpret_cast<void*>(&release));case 0x44a300:return address(reinterpret_cast<void*>(&initialize));default:return 0;}}
// Only the refusal path calls this logger; every admission retains AL.
void __cdecl refused(u32 reason,u32 definition,u32 manager){
 if(reason>2)return;++refusals[reason];
 if(definition>=logged.size()||logged[definition])return;logged[definition]=true;
 const char* labels[]={"outer pool","hero payload","npc payload"};
 // Resolve the pooled definition name without retaining pointers across zones.
 char name[128]{};const auto handle=read(manager+stats_layout::raised.entries+definition*28)&0xffffff;
 const auto pool=reinterpret_cast<u32(__cdecl*)()>(0x602140)();
 const auto offset=read(pool+4+handle*4);
 for(unsigned i=0;i+1<sizeof name;++i){if(!patch_transaction::read_memory(pool+0x8008+offset+i,name+i,1)||!name[i])break;}
 logger::write("R2: residency refused (definition %u '%s', %s)",definition,name,labels[reason]);
}
u32 __fastcall attached(void* stats,void*,u32 file_class){
 const auto result=reinterpret_cast<u32(__fastcall*)(void*,void*,u32)>(0x4b7c50)(stats,nullptr,file_class);sample();return result;
}
__declspec(naked) void zone_begin_bridge(){__asm {
 call dword ptr[edx+1ch]
 pushfd
 pushad
 call begin_zone
 popad
 popfd
 mov eax,ds:[0782dc0h]
 push 04d0ca8h
 ret
}}
__declspec(naked) void zone_end_bridge(){__asm {
 call dword ptr[edx+10h]
 pushfd
 pushad
 call end_zone
 popad
 popfd
 mov ecx,[esp+0ch]
 push 04d0d7eh
 ret
}}
struct arena {unsigned char* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));bool retained=false;~arena(){if(p&&!retained)VirtualFree(p,0,MEM_RELEASE);}};
struct emitter {
 std::vector<unsigned char> b;u32 base;
 void byte(unsigned x){b.push_back(static_cast<unsigned char>(x));}
 void word(u32 n){for(unsigned i=0;i<4;++i)byte(n>>(8*i));}
 void hex(std::string_view h){auto v=patch_transaction::bytes_of(h);b.insert(b.end(),v.begin(),v.end());}
 void jump(u32 to){byte(0xe9);word(to-(base+static_cast<u32>(b.size())+4));}
 void call(u32 to){byte(0xe8);word(to-(base+static_cast<u32>(b.size())+4));}
 void condition(unsigned cc,u32 to){byte(0x0f);byte(cc);word(to-(base+static_cast<u32>(b.size())+4));}
 std::size_t local(unsigned cc){byte(0x0f);byte(cc);auto n=b.size();word(0);return n;}
 void bind(std::size_t n){u32 delta=static_cast<u32>(b.size()-n-4);std::memcpy(b.data()+n,&delta,4);}
};
std::vector<unsigned char> emit(const resident_stats_sites::site& s,u32 base){
 emitter e{{},base};const auto old=patch_transaction::bytes_of(s.hex);
 if(s.role=="capacity"){
  e.byte(0x81);e.byte(old[1]);e.word(320);
  if(old[3]>=0x70&&old[3]<=0x7f){e.condition(0x80+(old[3]&15),s.va+5+static_cast<signed char>(old[4]));}
  else if(old[3]==0x0f){u32 delta;std::memcpy(&delta,old.data()+5,4);e.condition(old[4],s.va+9+delta);}
  else {
   auto store=old;for(const auto& i:resident_stats_sites::sites)if(i.type==resident_stats_sites::kind::incave&&i.va>=s.va&&i.va<s.va+s.span)for(unsigned n=0;n<i.size;++n)store[i.va-s.va+i.offset+n]=static_cast<unsigned char>(i.new_value>>(8*n));
   e.b.insert(e.b.end(),store.begin()+3,store.end()); // MOV preserves widened CMP flags.
  }
  e.jump(s.va+s.span);
 }else if(s.va==0x44b46d){e.hex("81be");e.word(0x777dc);e.word(320);e.condition(0x84,0x44b492);e.jump(0x44b476);}
 else if(s.va==0x44c3fb){
  e.hex("81bd");e.word(0x63b64);e.word(320);auto outer=e.local(0x84);
  e.hex("8b4c242484c9");auto npc=e.local(0x84);
  e.hex("81bd");e.word(0x6bda8);e.word(22);auto hero_ref=e.local(0x83);e.jump(0x44c404);
  e.bind(npc);e.hex("81bd");e.word(0x777dc);e.word(320);auto npc_ref=e.local(0x83);e.jump(0x44c404);
  const std::array<std::size_t,3> branches{outer,hero_ref,npc_ref};
  for(u32 i=0;i<3;++i){e.bind(branches[i]);e.hex("9c600fb7cb55516a");e.byte(i);e.call(address(reinterpret_cast<void*>(&refused)));e.hex("83c40c619d");e.jump(0x44c47d);}
 }else { // Four constructed/active bitmap clears, all ten words, all flags preserved.
  e.hex("9c60fc");if(s.va==0x449145)e.hex("8bff");else{e.hex("8dbe");e.word(s.va==0x44ab56?0x63600:0x63b38);}
  e.hex("33c0b90a000000f3ab619d");e.jump(s.va+s.span);
 }
 return e.b;
}
void reset_publication(){applied=false;stats_layout::publish(stats_layout::stock);settle(false);}
}
void require_agreement(HMODULE game){if(enabled(ini::text(L"Limits",L"ResidentStats")))game_version::require_gameplay_agreement(game);}
void contribute(patch_transaction::transaction& tx,HMODULE game,const stats_layout::descriptor& d){
 const auto key=ini::text(L"Limits",L"ResidentStats");if(!enabled(key))return;requested=true;require_agreement(game);
 u32 manager=0;
 if(*key!="320"||address(game)!=0x400000||d!=stats_layout::raised||d.npc.capacity<d.capacity||d.capacity<297||!stats_layout::registered()||!patch_transaction::read_memory(0x71770c,&manager,4)||manager){
  logger::write("ResidentStats: refused configuration, manager 0x%08x or missing consumer; rule 50 unverified",manager);reset_publication();return;
 }
 auto storage=std::make_shared<arena>();if(!storage->p){reset_publication();return;}
 patch_transaction::contribution c;c.owner="ResidentStats";unsigned slot=0;
 for(const auto& s:resident_stats_sites::sites){
  if(s.type==resident_stats_sites::kind::incave)continue;
  const auto old=patch_transaction::bytes_of(s.hex);const auto label="ResidentStats "+patch_transaction::hex(s.va);
  if(s.type==resident_stats_sites::kind::operand){
   auto i=std::find_if(c.writes.begin(),c.writes.end(),[&](const auto& w){return w.va==s.va;});
   if(i==c.writes.end()){c.guards.push_back({s.va,old,label});c.writes.push_back({s.va,old,label});i=std::prev(c.writes.end());}
   for(unsigned n=0;n<s.size;++n)i->bytes[s.offset+n]=static_cast<unsigned char>(s.new_value>>(8*n));
  }else{
   c.guards.push_back({s.va,old,label});c.regions.push_back({label,s.va,s.va+s.span,patch_transaction::sha256_of(s.hash)});
   if(s.type==resident_stats_sites::kind::rewrite)c.writes.push_back({s.va,patch_transaction::bytes_of(s.replacement),label});
   else {u32 target=0,pad=0;if(s.type==resident_stats_sites::kind::replace_)target=replacement(s.va);
    else {target=address(storage->p+slot*256);auto bytes=emit(s,target);if(bytes.size()>256){reset_publication();return;}std::memcpy(storage->p+slot++*256,bytes.data(),bytes.size());pad=s.span-5;}
    c.writes.push_back(patch_transaction::relocation(s.va,"e9",true,target,pad,label));
   }
  }
 }
 c.guards.push_back({0x71770c,{0,0,0,0},"ResidentStats manager not created"});
 c.guards.push_back({0x44c1a7,patch_transaction::bytes_of("3d29010000"),"ResidentStats retail definition cap"});
 c.guards.push_back({0x44b451,{22},"ResidentStats hero payload remains 22"});
 auto hook=[&](u32 va,std::string_view hex,const void* target,bool call){c.guards.push_back({va,patch_transaction::bytes_of(hex),"ResidentStats observer"});c.writes.push_back(patch_transaction::relocation(va,call?"e8":"e9",true,address(target),static_cast<unsigned>(hex.size()/2-5),"ResidentStats observer"));};
 hook(0x44c44d,"e8feb70600",reinterpret_cast<void*>(&attached),true);
 hook(0x4d0ca0,"ff521ca1c02d7800",reinterpret_cast<void*>(&zone_begin_bridge),false);
 hook(0x4d0d77,"ff52108b4c240c",reinterpret_cast<void*>(&zone_end_bridge),false);
 DWORD prior=0;if(!VirtualProtect(storage->p,8192,PAGE_EXECUTE_READ,&prior)||!FlushInstructionCache(GetCurrentProcess(),storage->p,8192)){reset_publication();return;}
 c.publish=[storage,d]{storage->retained=true;applied=true;stats_layout::publish(d);for(auto& n:counts)n=0;for(auto& n:peaks)n=0;for(auto& n:refusals)n=0;logged.fill(false);epoch=0;violations=0;settle(true);logger::write("ResidentStats: rule 50 revision 1 installed {320,22,320,1}; 275 ledger rows; residency guard; manager 0x777e0");};
 c.abandon=[] {reset_publication();logger::write("ResidentStats: refused transaction; rule 50 unverified");};
 c.retain=[storage]{storage->retained=true;reset_publication();game_version::refuse_startup();logger::write("ResidentStats: uncertified rollback; caves retained, startup refused");};
 if(!tx.add(std::move(c)))reset_publication();
}
void sample(){
 if(!applied)return;const auto& d=stats_layout::current();const auto manager=read(0x71770c);
 const std::array<u32,3> offsets{d.pool_live,d.hero.live,d.npc.live};for(unsigned i=0;i<3;++i){const auto n=manager?read(manager+offsets[i]):0;counts[i]=n;if(n>peaks[i])peaks[i]=n;}
 if(!census_requested.exchange(false))return;
 std::string out="{\"ok\":true,\"epoch\":"+std::to_string(epoch.load())+",\"definitions\":[";bool comma=false;
 const auto total=manager?std::min(read(manager+d.definition_count),297u):0;
 for(u32 i=0;i<total;++i){const auto entry=manager+d.entries+i*28,handle=read(entry+16);if(!handle)continue;unsigned char flags=0;patch_transaction::read_memory(entry+24,&flags,1);
  const auto record=manager+4+(handle&read(manager+d.pool_mask))*0x4f8,p=read(record+0xc0);const char* cls=p>=manager+d.hero.base&&p<manager+d.hero.base+22*68?"hero":p>=manager+d.npc.base&&p<manager+d.npc.base+320*136?"npc":"invalid";
  if(comma)out+=",";comma=true;out+="{\"index\":"+std::to_string(i)+",\"handle\":"+std::to_string(handle)+",\"flags\":"+std::to_string(flags)+",\"payload\":\""+cls+"\"}";
 }
 std::lock_guard lock(cache_lock);census=out+"]}";
}
void begin_zone(){if(!applied)return;logged.fill(false);++epoch;end_zone();}
void end_zone(){if(!applied)return;sample();if(counts[1]+counts[2]!=counts[0]){++violations;logger::write("R2: residency invariant failed (resident %u, hero %u, npc %u)",counts[0].load(),counts[1].load(),counts[2].load());}}
std::string status(){if(!applied)return {};return "resident "+std::to_string(counts[0].load())+"/"+std::to_string(stats_layout::current().capacity)+" hero "+std::to_string(counts[1].load())+"/22 npc "+std::to_string(counts[2].load())+"/"+std::to_string(stats_layout::current().npc.capacity);}
std::string telemetry_json(){std::string out="{\"ok\":true,\"installed\":"+std::string(applied?"true":"false")+",\"requested\":"+(requested?"true":"false")+",\"status\":\""+status()+"\",\"counts\":[";
 for(unsigned i=0;i<3;++i){if(i)out+=",";out+=std::to_string(counts[i].load());}out+="],\"peaks\":[";for(unsigned i=0;i<3;++i){if(i)out+=",";out+=std::to_string(peaks[i].load());}out+="],\"refusals\":[";for(unsigned i=0;i<3;++i){if(i)out+=",";out+=std::to_string(refusals[i].load());}return out+"],\"invariant_failures\":"+std::to_string(violations.load())+"}";
}
std::string census_json(){census_requested=true;std::lock_guard lock(cache_lock);return census;}
}
