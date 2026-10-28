#include "xml1_economy_prices.hpp"
#include "xml1_economy.hpp"
#include "item_capacity_rules.hpp"
#include <intrin.h>
#include <memory>

namespace xml1_economy::prices {
namespace {
template<class T>T native(unsigned va){return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T read(const void* p,unsigned off=0){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>void put(void* p,unsigned off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T>T method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/4]);}
const float ordinary_fraction=0.25f,unique_fraction=0.33f;
bool applied=false;
using value_t=void(__fastcall*)(void*,void*);
value_t original_value=nullptr;
struct storage {void* p=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);~storage(){if(p)VirtualFree(p,0,MEM_RELEASE);}};
std::vector<std::shared_ptr<storage>> retained;

void __fastcall cache_value(void* instance,void*){
 auto* definition=read<void*>(instance,4);
 if(const auto* r=record_for_definition(definition)){
  // The builder mirrors the authored XML1 cost in the agreed sidecar; unlike
  // the native value calculator, it never includes enhancement/affix costs.
  put(instance,0x28,r->cost.value_or(0));
  put(instance,0x30,static_cast<unsigned char>(read<unsigned char>(instance,0x30)|2));
 }else original_value(instance,nullptr);
}

// This slot belongs to the current native transaction frame. Consume it before
// invoking vt+0x104, so a nested transaction cannot overwrite the outer owner.
struct capture {void* frame=nullptr;void* owner=nullptr;};
thread_local capture captured;
void __cdecl capture_owner(void* stack){captured={stack,read<void*>(stack,0x24)};}
void __fastcall state_changed(void* manager,void* shop,descriptor item,int state){
 auto* frame=static_cast<unsigned char*>(_AddressOfReturnAddress())+16;
 const bool has_owner=captured.frame==frame;
 auto* owner=has_owner?captured.owner:nullptr;
 if(has_owner)captured={};
 method<void(__fastcall*)(void*,void*,descriptor,int)>(manager,0x104)(manager,nullptr,item,state);
 if(!xml1_economy::installed()||(read<unsigned char>(shop,0x18e8)&1))return;
 if(state!=2&&(state!=4||!has_owner||owner))return;
 auto* instance=native<void*(__fastcall*)(void*,void*)>(0x480f30)(&item,nullptr);
 if(!instance)return;
 auto* definition=read<void*>(instance,4);
 if(!record_for_definition(definition)||!method<bool(__fastcall*)(void*,void*)>(definition,0x50)(definition,nullptr))return;
 method<void(__fastcall*)(void*,void*,int)>(definition,0x5c)(definition,nullptr,state==2?1:0);
}
__declspec(naked) void state_bridge(){__asm {
 mov edx,esi
 jmp state_changed
}}
__declspec(naked) void owner_bridge(){__asm {
 pushfd
 pushad
 lea eax,[esp+36]
 push eax
 call capture_owner
 add esp,4
 popad
 popfd
 mov eax,[esp+24h]
 cmp eax,ebx
 push 05d3eefh
 ret
}}
__declspec(naked) void pack_bridge(){__asm {
 imul eax,eax,225
 sub eax,125
 pop ebx
 pop esi
 push 0480ddeh
 ret
}}

struct row {unsigned type=0;descriptor item{};void* owner=nullptr;unsigned detail=0;};
static_assert(sizeof(row)==20);
void append_plain(void* manager,unsigned index){
 if(read<unsigned>(native<void*>(0x8a6b1c))>=100)return;
 auto* definition=static_cast<unsigned char*>(manager)+item_capacity_rules::definitions+index*0x3c;
 if(method<int(__fastcall*)(void*,void*)>(definition,0x24)(definition,nullptr)==2||
    !method<unsigned char(__fastcall*)(void*,void*)>(definition,0x40)(definition,nullptr))return;
 descriptor item{};
 method<descriptor*(__fastcall*)(void*,void*,descriptor*,unsigned)>(manager,0x12c)(manager,nullptr,&item,index);
 if(!item.pool)return;
 row entry{};
 native<row*(__fastcall*)(void*,void*,descriptor*)>(0x5a9840)(&entry,nullptr,&item);
 // The shared native append owns the same 100-row capacity check as equipment.
 native<int(__cdecl*)(const row*)>(0x5aa4f0)(&entry);
}
void __cdecl append_sellables(){
 auto* manager=native<void*(__cdecl*)()>(0x480a00)();
 // Keep native pack order, including packs in a sidecar that omits them.
 const auto lookup=method<std::uint16_t(__fastcall*)(void*,void*,const char*)>(manager,0x78);
 const unsigned health=lookup(manager,nullptr,"HEALTH_ITEM"),energy=lookup(manager,nullptr,"ENERGY_ITEM");
 if(health!=0xffff)append_plain(manager,health);
 if(energy!=0xffff&&energy!=health)append_plain(manager,energy);
 for(const auto& r:records())if((r.flags&0x10)&&r.definition!=health&&r.definition!=energy)append_plain(manager,r.definition);
}
__declspec(naked) void sell_bridge(){__asm {
 pushfd
 pushad
 call append_sellables
 popad
 popfd
 push 05d26d3h
 ret
}}

struct site {unsigned va;const char* bytes;const char* what;};
constexpr std::array<site,7> sites{{
 {0x5a9cd8,"e4e06900","ordinary sale multiplier operand"},
 {0x5a9d2c,"e8e06900","unique sale multiplier operand"},
 {0x481170,"83ec14568bf1","definition-cost cache entry"},
 {0x480dd8,"405b6bc0325e","pack result with native register pops"},
 {0x5a9b8d,"e84e4fecff","course level call"},
 {0x5d3ee9,"8b4424243bc3","owner capture before equipped removal"},
 {0x5d404b,"ff9204010000","shared equipment state call"}
}};
}
bool installed(){return applied;}
bool contribute(patch_transaction::transaction& tx,std::string& failure){
 patch_transaction::contribution c;c.owner="Xml1Economy sales, prices, obtained and sell list";
 for(const auto& s:sites)c.guards.push_back({s.va,patch_transaction::bytes_of(s.bytes),s.what});
 // Stash branches around this entry; final presentation and sorting stay native.
 c.guards.push_back({0x5d2639,patch_transaction::bytes_of("5357e8c0e3eaff"),"non-stash sell-list entry"});
 if(!patch_transaction::verify_expectations(c,failure))return false;
 auto code=std::make_shared<storage>();if(!code->p){failure="price cache trampoline allocation failed";return false;}
 const auto displaced=patch_transaction::bytes_of(sites[2].bytes);
 std::memcpy(code->p,displaced.data(),displaced.size());
 const auto tail=patch_transaction::relocation(addr(code->p)+6,"e9",true,0x481176,0,"native legacy value continuation");
 std::memcpy(static_cast<unsigned char*>(code->p)+6,tail.bytes.data(),tail.bytes.size());
 DWORD old=0;if(!VirtualProtect(code->p,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code->p,11)){failure="price trampoline protection/flush failed";return false;}
 c.writes.push_back(patch_transaction::relocation(0x5a9cd8,"",false,addr(&ordinary_fraction),0,"XML1 ordinary fraction"));
 c.writes.push_back(patch_transaction::relocation(0x5a9d2c,"",false,addr(&unique_fraction),0,"XML1 unique fraction"));
 c.writes.push_back(patch_transaction::relocation(0x481170,"e9",true,addr(&cache_value),1,"economy definition cost cache"));
 c.writes.push_back(patch_transaction::relocation(0x480dd8,"e9",true,addr(&pack_bridge),1,"225G-125 pack result"));
 c.writes.push_back({0x5a9b8d,{0x0f,0xb6,0x01,0x90,0x90},"raw course level byte"});
 c.writes.push_back(patch_transaction::relocation(0x5d3ee9,"e9",true,addr(&owner_bridge),1,"capture equipped owner"));
 c.writes.push_back(patch_transaction::relocation(0x5d404b,"e8",true,addr(&state_bridge),1,"state-guarded obtained bridge"));
 c.writes.push_back(patch_transaction::relocation(0x5d2639,"e9",true,addr(&sell_bridge),2,"positive-quantity non-equipment sell rows"));
 c.publish=[code]{retained.push_back(code);original_value=reinterpret_cast<value_t>(code->p);applied=true;};
 c.retain=[code]{retained.push_back(code);original_value=reinterpret_cast<value_t>(code->p);applied=false;};
 c.abandon=[]{applied=false;};
 return tx.add(std::move(c));
}
}
