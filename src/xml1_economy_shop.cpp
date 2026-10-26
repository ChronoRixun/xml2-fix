#include "xml1_economy_shop.hpp"
#include "xml1_economy.hpp"
#include "xml1_economy_digest.hpp"
#include "item_capacity_rules.hpp"
#include <algorithm>
#include <memory>
#include <atomic>

namespace xml1_economy::shop {
namespace {
template<class T>T native(unsigned va){return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T read(const void* p,unsigned off=0){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>T method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/4]);}
using unary_t=void(__fastcall*)(void*,void*);
using refill_t=void(__fastcall*)(void*,void*,int);
using switch_t=int(__fastcall*)(void*,void*,int);
using style_t=int(__fastcall*)(void*,void*);
refill_t original_refill=nullptr;
switch_t original_switch=nullptr;
unary_t original_training=nullptr;
style_t original_style=nullptr;
bool applied=false,forge=false,no_training=false;
std::atomic<unsigned> forge_count{0},forge_group{0};
void* opened_shop=nullptr;
const unsigned manager_getter=0x480a00,price_getter=0x5a9b00;
bool blocked(void* self){return xml1_economy::installed()&&no_training&&opened_shop==self&&!(read<unsigned char>(self,0x18e8)&1);}
void* definition(void* manager,unsigned index){return static_cast<unsigned char*>(manager)+item_capacity_rules::definitions+index*0x3c;}
void* instance(descriptor& d){return native<void*(__fastcall*)(void*,void*)>(0x480f30)(&d,nullptr);}
bool valid(descriptor& d){return native<bool(__fastcall*)(void*,void*)>(0x45e5d0)(&d,nullptr);}
struct stock {std::array<descriptor,1024> items{};};
// The native list can contain arbitrary carried/equipped/stashed legacy items.
// Snapshot it before mutation; unlinking/freeing stock must never invoke the
// bulk state's unique-obtained reset (0x47bd10).
stock reconcile(void* manager){
 stock result;
 auto* registry=native<void*(__cdecl*)()>(0x44b8f0)();
 const int highest=native<int(__fastcall*)(void*,void*)>(0x449960)(registry,nullptr);
 const int group=std::clamp(highest/10+1,1,5);
 std::array<bool,1024> catalog{};
 if(forge)for(const auto& r:records()){
  if(!(r.flags&0x40)||!r.group||*r.group<1||*r.group>group||(r.flags&2))continue;
  auto* d=definition(manager,r.definition);
  if(method<int(__fastcall*)(void*,void*)>(d,0x24)(d,nullptr)!=2)continue;
  if(method<bool(__fastcall*)(void*,void*)>(d,0x50)(d,nullptr)&&method<bool(__fastcall*)(void*,void*)>(d,0x54)(d,nullptr))continue;
  catalog[r.definition]=true;
 }
 std::vector<descriptor> previous;
 descriptor cursor{};method<descriptor*(__fastcall*)(void*,void*,descriptor*)>(manager,0xf8)(manager,nullptr,&cursor);
 for(unsigned n=0;n<1024&&valid(cursor);++n){previous.push_back(cursor);auto* p=instance(cursor);cursor=*method<descriptor*(__fastcall*)(void*,void*)>(p,0x74)(p,nullptr);}
 for(auto item:previous){
  auto* p=instance(item);
  const int state=method<int(__fastcall*)(void*,void*)>(p,0x18)(p,nullptr);
  if(state!=3&&state!=4)continue;
  auto* d=method<void*(__fastcall*)(void*,void*)>(p,8)(p,nullptr);
  const auto* r=record_for_definition(d);
  if(state==3&&r&&catalog[r->definition]&&!result.items[r->definition].pool){result.items[r->definition]=item;continue;}
  method<void(__fastcall*)(void*,void*,descriptor)>(manager,0xf0)(manager,nullptr,item);
  method<void(__fastcall*)(void*,void*,descriptor)>(manager,0xe8)(manager,nullptr,item);
 }
 for(const auto& r:records())if(catalog[r.definition]&&!result.items[r.definition].pool){
  descriptor item{};
  make_instance(manager,nullptr,&item,definition(manager,r.definition),1,0,0,0,0);
  if(!item.pool)continue; // constructor's W2 preflight accounts/logs exhaustion.
  method<bool(__fastcall*)(void*,void*,descriptor,int,int)>(manager,0xec)(manager,nullptr,item,1,3);
  result.items[r.definition]=item;
 }
 unsigned count=0;for(const auto& item:result.items)if(item.pool)++count;
 forge_count=count;forge_group=group;
 return result;
}
void __fastcall refill(void* self,void*,int reset){
 if(!xml1_economy::installed()){original_refill(self,nullptr,reset);return;}
 reconcile(native<void*(__cdecl*)()>(0x480a00)());
}
const char* __fastcall vendor_text(void* value,void*){
 const auto* text=method<const char*(__fastcall*)(void*,void*)>(value,0x14)(value,nullptr);
 if(xml1_economy::installed()){
  // ASCII case folding is independent of the peer's process locale.
  constexpr char equip[]="equip";forge=text!=nullptr;
  if(text)for(unsigned n=0;n<sizeof equip;++n){const unsigned char c=static_cast<unsigned char>(text[n]);if((c>='A'&&c<='Z'?c+('a'-'A'):c)!=static_cast<unsigned char>(equip[n])){forge=false;break;}if(!c)break;}
 }
 return text;
}
__declspec(naked) void vendor_bridge(){__asm {
 mov ecx,eax
 jmp vendor_text
}}
struct row {unsigned type=0;descriptor item{};void* owner=nullptr;unsigned detail=0;};
static_assert(sizeof(row)==20);
bool __fastcall forge_rows(void* self,void*){
 if(!xml1_economy::installed()||(read<unsigned char>(self,0x18e8)&1)||!forge)return false;
 auto* manager=native<void*(__cdecl*)()>(0x480a00)();const auto stock=reconcile(manager);
 for(const auto& r:records())if(stock.items[r.definition].pool){
  row entry{};auto item=stock.items[r.definition];
  native<row*(__fastcall*)(void*,void*,descriptor*)>(0x5a9840)(&entry,nullptr,&item);
  native<int(__cdecl*)(const row*)>(0x5aa4f0)(&entry);
 }
 return true;
}
__declspec(naked) void buy_bridge(){__asm {
 push ecx
 push edx
 mov ecx,esi
 call forge_rows
 pop edx
 pop ecx
 test al,al
 jnz finished
 // Healer retains the native synthetic pack, bag and course builders.
 push ebp
 push edi
 call dword ptr ds:[manager_getter]
 push 05d2a5ch
 ret
 finished:
 push 05d2b2dh
 ret
}}
int __fastcall switch_tabs(void* self,void*,int tab){
 if(blocked(self)&&(tab<0||tab>1))tab=0;
 return original_switch(self,nullptr,tab);
}
int __fastcall begin_tabs(void* self,void*,int tab){
 opened_shop=self;no_training=xml1_economy::installed()&&!(read<unsigned char>(self,0x18e8)&1);
 if(no_training)reconcile(native<void*(__cdecl*)()>(0x480a00)());
 return switch_tabs(self,nullptr,tab);
}
int __cdecl max_tab(void* self){return blocked(self)||(read<unsigned char>(self,0x18e8)&1)?1:2;}
__declspec(naked) void navigation_bridge(){__asm {
 push eax
 push edx
 push esi
 call max_tab
 add esp,4
 mov ecx,eax
 pop edx
 pop eax
 push 05d36d7h
 ret
}}
unsigned __fastcall label(void* self,unsigned offset){return blocked(self)&&offset==8?0x681968:read<unsigned>(native<void*>(0x6e6fd8),offset);}
__declspec(naked) void label_bridge(){__asm {
 mov ecx,esi
 mov edx,edi
 call label
 push 05d3b37h
 ret
}}
void __fastcall training(void* self,void*){
 if(blocked(self))native<void(__cdecl*)()>(0x5a9f80)();else original_training(self,nullptr);
}
template<unsigned Target>void __fastcall dispatch(void* self,void*,int detail){if(!blocked(self))native<void(__fastcall*)(void*,void*,int)>(Target)(self,nullptr,detail);}
bool __fastcall refuse_purchase(void* self,unsigned type){const bool refuse=blocked(self)&&type>=3&&type<=5;if(refuse)digest::bag_result("refused-training");return refuse;}
__declspec(naked) void purchase_bridge(){__asm {
 push ecx
 push edx
 mov ecx,esi
 mov edx,[esp+20h]
 call refuse_purchase
 pop edx
 pop ecx
 test al,al
 jnz refused
 lea ecx,[esp+18h]
 call dword ptr ds:[price_getter]
 push 05d3ff9h
 ret
 refused:
 push 05d4133h
 ret
}}
int __fastcall style(void* self,void*){
 if(xml1_economy::installed()){
  auto* d=read<void*>(self,4);
  if(const auto* r=record_for_definition(d)){
   if(method<bool(__fastcall*)(void*,void*)>(self,0x34)(self,nullptr))return 12;
   if((r->flags&0x40)&&r->frequency==2)return 11;
  }
 }
 return original_style(self,nullptr);
}
struct site {unsigned va;const char* bytes;const char* what;};
constexpr std::array<site,14> sites{{
 {0x49e64b,"8b108bc8ff5214","shopMenu argument text before native queue"},
 {0x46a1a0,"558bec83e4f8","stock reconcile before native refill prologue"},
 {0x5d2a55,"5557e8a4dfeaff","non-stash buy-list vendor filter"},
 {0x5d2b18,"536a03e8207afdff","Healer skips state-3/4 instance rows"},
 {0x5d35d0,"568bf18b86d8180000","tab selection entry"},
 {0x5d36c8,"f6c201b9020000007405b901000000","tab navigation upper bound"},
 {0x5d3a34,"e897fbffff","successful open initializes fix-owned Training state"},
 {0x5d3b2a,"8b87d86f6e00eb05","shop labels, empty third control"},
 {0x5d2b70,"a1a06f6e00","Training builder entry"},
 {0x5d3ff0,"8d4c2418e8075bfdff","Training refusal before common purchase debit"},
 {0x5d4104,"e827ecffff","Training level dispatch"},
 {0x5d4119,"e872e1ffff","Training skill dispatch"},
 {0x5d412e,"e8dde6ffff","Training respec dispatch"},
 {0x480e20,"568bf18b06","instance style entry"}
}};
struct storage {void* p=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);~storage(){if(p)VirtualFree(p,0,MEM_RELEASE);}};
std::vector<std::shared_ptr<storage>> retained;
}
bool installed(){return applied;}
const char* vendor(){return forge?"forge":"healer";}
std::string status(){return "; forge "+std::to_string(forge_count.load())+"/G"+std::to_string(forge_group.load());}
bool contribute(patch_transaction::transaction& tx,std::string& failure){
 patch_transaction::contribution c;c.owner="Xml1Economy Forge/Healer, two tabs and rare style";
 for(const auto& s:sites)c.guards.push_back({s.va,patch_transaction::bytes_of(s.bytes),s.what});
 if(!patch_transaction::verify_expectations(c,failure))return false;
 auto code=std::make_shared<storage>();if(!code->p){failure="shop trampoline allocation failed";return false;}
 std::array<unsigned,4> trampolines{};
 const std::array<unsigned,4> copied{1,4,8,13};
 for(unsigned n=0;n<copied.size();++n){
  const auto& s=sites[copied[n]];const auto bytes=patch_transaction::bytes_of(s.bytes);const auto dest=addr(code->p)+n*32;
  trampolines[n]=dest;std::memcpy(native<void*>(dest),bytes.data(),bytes.size());
  const auto tail=patch_transaction::relocation(dest+static_cast<unsigned>(bytes.size()),"e9",true,s.va+static_cast<unsigned>(bytes.size()),0,"native shop continuation");
  std::memcpy(native<void*>(tail.va),tail.bytes.data(),tail.bytes.size());
 }
 DWORD old=0;if(!VirtualProtect(code->p,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code->p,128)){failure="shop trampoline protection/flush failed";return false;}
 const std::array<unsigned,14> targets{addr(&vendor_bridge),addr(&refill),addr(&buy_bridge),0x5d2b2b,addr(&switch_tabs),addr(&navigation_bridge),addr(&begin_tabs),addr(&label_bridge),addr(&training),addr(&purchase_bridge),addr(&dispatch<0x5d2d30>),addr(&dispatch<0x5d2290>),addr(&dispatch<0x5d2810>),addr(&style)};
 for(unsigned n=0;n<sites.size();++n){const auto& s=sites[n];const bool call=n==0||n==6||(n>=10&&n<=12);c.writes.push_back(patch_transaction::relocation(s.va,call?"e8":"e9",true,targets[n],static_cast<unsigned>(patch_transaction::bytes_of(s.bytes).size())-5,s.what));}
 auto keep=[code,trampolines]{retained.push_back(code);original_refill=native<refill_t>(trampolines[0]);original_switch=native<switch_t>(trampolines[1]);original_training=native<unary_t>(trampolines[2]);original_style=native<style_t>(trampolines[3]);};
 c.publish=[keep]{keep();applied=true;};c.retain=[keep]{keep();applied=false;};c.abandon=[]{applied=false;};
 return tx.add(std::move(c));
}
}
