#include "xml1_economy_digest.hpp"
#include "xml1_economy_digest_rules.hpp"
#include "xml1_economy.hpp"
#include "xml1_economy_shop.hpp"
#include "item_capacity_rules.hpp"
#include "log.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <memory>
#include <mutex>

namespace xml1_economy::digest {
namespace {
using namespace digest_rules;
template<class T>T native(unsigned va){return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T read(const void* p,unsigned off=0){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>T method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/4]);}
struct native_row {unsigned type;descriptor item;void* owner;unsigned detail;};
static_assert(sizeof(native_row)==20);
using open_t=bool(__fastcall*)(void*,void*,void*,void*);
using update_t=void(__fastcall*)(void*,void*);
open_t original_open=nullptr;update_t original_update=nullptr;
bool applied=false;std::atomic<bool> enabled{false};
std::mutex mutex;history events;std::uint64_t sequence=0,last_view=0;
std::string view="{\"error\":\"no shop sample\"}";
struct context {void* shop=nullptr;event action;bool pressed=false;std::string result;};
thread_local context* current=nullptr;

std::string name(unsigned id){
 if(!id)return "-";
 auto* names=native<unsigned char*(__cdecl*)()>(0x602140)();
 const char* text=reinterpret_cast<const char*>(names+0x8008+read<unsigned>(names,4+(id&0xffffff)*4));
 const auto n=strnlen(text,128);if(n==128)return "invalid-name";return std::string(text,n);
}
// The captured owner is a hero stats pointer, not a world entity handle. Roster
// order is synchronized; addresses and descriptor pool pointers are not wire IDs.
int owner_id(void* owner){
 if(!owner)return -1;
 auto* roster=native<void*(__cdecl*)()>(0x44b8f0)();
 const int count=method<int(__fastcall*)(void*,void*)>(roster,0x48)(roster,nullptr);
 for(int i=0;i<std::clamp(count,0,128);++i){
  const auto handle=method<unsigned(__fastcall*)(void*,void*,int)>(roster,0x4c)(roster,nullptr,i);
  if(method<void*(__fastcall*)(void*,void*,unsigned)>(roster,0x74)(roster,nullptr,handle)==owner)return i;
 }
 return -2; // explicit invalid identity; never collapse unknown equipped owners
}
row describe(const native_row& r,void* manager){
 row out;out.type=r.type;out.owner=owner_id(r.owner);
 if(r.type==0){
  out.name="invalid-descriptor";
  if(!r.item.pool)return out;
  auto d=r.item;
  if(!native<bool(__fastcall*)(void*,void*)>(0x45e5d0)(&d,nullptr))return out;
  auto* instance=native<void*(__fastcall*)(void*,void*)>(0x480f30)(&d,nullptr);
  if(!instance)return out;
  auto* def=read<void*>(instance,4);
  const auto delta=addr(def)-addr(manager)-item_capacity_rules::definitions;
  if(delta%0x3c||delta/0x3c>=read<unsigned short>(manager,0x2588)){out.name="invalid-definition";return out;}
  out.index=delta/0x3c;out.name=name(read<unsigned>(def,8));
  out.quantity=method<unsigned char(__fastcall*)(void*,void*)>(def,0x40)(def,nullptr);
  out.state=method<int(__fastcall*)(void*,void*)>(instance,0x18)(instance,nullptr);
 }else if(r.type==1)out.name="bag";
 else if(r.type==2){out.detail=r.detail&0xff;out.name="course";}
 else {out.detail=r.detail&0xff;out.name=r.type<=5?"type"+std::to_string(r.type):"invalid-type";}
 return out;
}
bool invalid(const row& r){return r.owner==-2||r.name.starts_with("invalid-");}
struct sample {int money=0,tab=-1,selected=-1;std::string list="-",vendor,error;std::uint64_t defs=0,stock=0;unsigned rng=0;std::vector<row> rows;std::vector<unsigned> visible;};
void capture(sample& out,void* shop,bool hashes){
 auto* manager=native<void*(__cdecl*)()>(0x480a00)();
 const unsigned count=read<unsigned short>(manager,0x2588);
 if(count>1024){out.error="definition-count";return;}
 out.tab=read<int>(shop,0x18d8);const auto focus=read<unsigned>(shop,0x324);out.list=name(focus);
 out.vendor=(read<unsigned char>(shop,0x18e8)&1)?"stash":shop::vendor();
 const unsigned n=read<unsigned>(native<void*>(0x8a6b1c));
 if(n>100){out.error="row-count";return;}
 for(unsigned i=0;i<n;++i){auto r=read<native_row>(native<void*>(0x8a6348),i*20);auto value=describe(r,manager);
  if(invalid(value)){out.error="row-identity";return;}
  value.price=(read<unsigned char>(shop,0x18e8)&1)?0:native<int(__fastcall*)(void*,void*)>(out.tab==1?0x5a9cc0:0x5a9b00)(&r,nullptr);out.rows.push_back(std::move(value));}
 // Exact native focused-list lookup and visible-index -> row-index mapping.
 auto* widget=native<void*(__cdecl*)(unsigned,int,unsigned,unsigned,int)>(0x67234e)(focus,0,0x6e4b34,0x6e5d60,0);
 if(widget){
  const int visible=native<int(__fastcall*)(void*,void*)>(0x5bf750)(widget,nullptr);
  if(visible<0||visible>100){out.error="visible-count";return;}
  for(int i=0;i<visible;++i){const int index=native<int(__fastcall*)(void*,void*,int)>(0x5bfc20)(widget,nullptr,i);
   if(index<0||static_cast<unsigned>(index)>=n){out.error="visible-map";return;}out.visible.push_back(static_cast<unsigned>(index));}
  const int selected=method<int(__fastcall*)(void*,void*)>(widget,0xc8)(widget,nullptr);
  if(selected>=0&&selected<visible)out.selected=static_cast<int>(out.visible[selected]);
 }
 out.money=method<int(__fastcall*)(void*,void*)>(manager,0x24)(manager,nullptr);
 out.rng=read<unsigned>(native<void*>(0x7ad8b4)); // 0x560190(1)'s native draw counter
 if(hashes){
  std::vector<definition> defs;defs.reserve(count);
  for(unsigned i=0;i<count;++i){auto* d=static_cast<unsigned char*>(manager)+item_capacity_rules::definitions+i*0x3c;
   auto text=name(read<unsigned>(d,8));if(text=="invalid-name"){out.error="definition-name";return;}
   defs.push_back({i,std::move(text),method<unsigned char(__fastcall*)(void*,void*)>(d,0x40)(d,nullptr),method<bool(__fastcall*)(void*,void*)>(d,0x54)(d,nullptr)});
  }
  out.defs=definitions_hash(defs);
  std::vector<row> stock;descriptor cursor{};
  method<descriptor*(__fastcall*)(void*,void*,descriptor*)>(manager,0xf8)(manager,nullptr,&cursor);
  unsigned walked=0;
  while(cursor.pool){
   if(!native<bool(__fastcall*)(void*,void*)>(0x45e5d0)(&cursor,nullptr)){out.error="stock-descriptor";return;}
   if(walked++==1024){out.error="stock-cycle";return;}
   auto* p=native<void*(__fastcall*)(void*,void*)>(0x480f30)(&cursor,nullptr);
   const int state=method<int(__fastcall*)(void*,void*)>(p,0x18)(p,nullptr);
   if(state==3||state==4){auto r=describe({0,cursor,nullptr,0},manager);if(invalid(r)){out.error="stock-identity";return;}stock.push_back(std::move(r));}
   cursor=*method<descriptor*(__fastcall*)(void*,void*)>(p,0x74)(p,nullptr);
  }
  std::sort(stock.begin(),stock.end(),[](const row& a,const row& b){return a.index!=b.index?a.index<b.index:a.state<b.state;});
  const auto base=stock_hash(stock,out.rows);hash h;h.text("shop-v1");h.text(out.vendor);h.text(out.list);h.word(static_cast<unsigned>(base));h.word(static_cast<unsigned>(base>>32));h.word(static_cast<unsigned>(out.visible.size()));for(unsigned index:out.visible)h.word(index);out.stock=h.value();
 }
}
bool safe_capture(sample* out,void* shop,bool hashes){
 __try {capture(*out,shop,hashes);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::string row_json(const row& r,unsigned i){return "{\"index\":"+std::to_string(i)+",\"type\":"+std::to_string(r.type)+",\"definition\":"+json_string(r.name)+",\"owner\":"+std::to_string(r.owner)+",\"detail\":"+std::to_string(r.detail)+",\"price\":"+std::to_string(r.price)+"}";}
void observe(void* shop,event* e){
 sample s;if(!safe_capture(&s,shop,e!=nullptr))s.error="snapshot-fault";
 std::string json="{\"vendor\":"+json_string(s.vendor)+",\"tab\":"+std::to_string(s.tab)+",\"list\":"+json_string(s.list)+",\"selected\":"+std::to_string(s.selected)+",\"error\":"+json_string(s.error)+",\"rows\":[";
 for(unsigned i=0;i<s.visible.size();++i){if(i)json+=',';json+=row_json(s.rows[s.visible[i]],s.visible[i]);}json+="]}";
 std::lock_guard lock(mutex);view=std::move(json);last_view=GetTickCount64();
 if(!e)return;
 e->seq=++sequence;e->money=s.money;e->defs=s.defs;e->stock=s.stock;e->rng=s.rng;
 if(e->tab<0){e->tab=s.tab;e->list=s.list;e->row_index=s.selected;if(s.selected>=0&&static_cast<unsigned>(s.selected)<s.rows.size()){const auto& r=s.rows[s.selected];e->definition=r.name;e->owner=r.owner;}}
 if(!s.error.empty())e->result="error-"+s.error;
 if(e->owner==-2)e->result="error-owner-identity";
 if(e->definition.starts_with("invalid-"))e->result="error-captured-row-identity";
 events.append(*e);logger::write("%s",format(*e).c_str());
}
bool __fastcall open(void* shop,void*,void* a,void* b){
 const bool result=original_open(shop,nullptr,a,b);
 if(enabled&&xml1_economy::installed()&&result){event e;e.result="open";observe(shop,&e);}return result;
}
void __fastcall update(void* shop,void*){
 if(!enabled||!xml1_economy::installed()){original_update(shop,nullptr);return;}
 context c;c.shop=shop;c.action.tab=read<int>(shop,0x18d8);c.action.list=name(read<unsigned>(shop,0x324));
 auto* previous=current;current=&c;original_update(shop,nullptr);current=previous;
 if(c.pressed){c.action.result=c.result.empty()?"refused":c.result;observe(shop,&c.action);}
 else {bool due=false;{std::lock_guard lock(mutex);due=GetTickCount64()-last_view>=100;}if(due)observe(shop,nullptr);}
}
bool __fastcall gate(void* manager,void*){
 const bool result=method<bool(__fastcall*)(void*,void*)>(manager,0xdc)(manager,nullptr);
 if(current)current->action.gate=result?1:0;return result;
}
template<unsigned Slot>unsigned __fastcall input(void* manager,void*,int* player){
 const unsigned result=method<unsigned(__fastcall*)(void*,void*,int*)>(manager,Slot)(manager,nullptr,player);
 if(current){current->action.player=*player;current->pressed=(result&16)!=0;}return result;
}
native_row* __cdecl selected_row(int index){
 auto* result=native<native_row*(__cdecl*)(int)>(0x5a9f90)(index);
 if(current){const auto r=describe(*result,native<void*(__cdecl*)()>(0x480a00)());
  current->action.row_index=index;current->action.definition=r.name;current->action.owner=r.owner;
  current->action.tab=read<int>(current->shop,0x18d8);current->action.list=name(read<unsigned>(current->shop,0x324));
 }return result;
}
// Completion is before the native UI rebuild (which can switch tab/row for bags).
// It covers equipment, pack quantities, course purchases and empty bag outcomes.
void __fastcall completed(void* shop,void*){
 if(current&&current->result.empty()){
  const bool stash=(read<unsigned char>(shop,0x18e8)&1)!=0;
  const auto& e=current->action;
  current->result=stash?(e.tab==1?"deposit":"withdraw"):(e.tab==1?"sell":"buy");
 }
 native<void(__fastcall*)(void*,void*)>(0x5d1fa0)(shop,nullptr);
}
void* __cdecl empty_sale(){if(current)current->result="refused-empty";return native<void*(__cdecl*)()>(0x5d8920)();}
struct site {unsigned va;const char* bytes;const char* what;};
constexpr std::array<site,8> sites{{
 {0x5d39c0,"8b44240883ec18","shop open entry, ret 8"},
 {0x5d3ba0,"81ec30010000","shop update entry"},
 {0x5d3c31,"ff92dc000000","native menu-state gate result"},
 {0x5d3c81,"ff923c010000","alternate routed input and action player"},
 {0x5d3c97,"ff9230010000","routed input and action player at 0x5d4a00"},
 {0x5d3dd7,"e8b461fdff","selected native row before owner removal"},
 {0x5d4135,"e866deffff","transaction complete before native UI rebuild"},
 {0x5d3fda,"e841490000","empty plain sale refusal"}
}};
struct storage {void* p=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);~storage(){if(p)VirtualFree(p,0,MEM_RELEASE);}};
std::vector<std::shared_ptr<storage>> retained;
}
bool installed(){return applied;}
void enable(){enabled=true;}
void bag_result(const char* result){if(current)current->result=result;}
std::string pipe(const std::string& command){
 if(!xml1_economy::installed())return "error economy is not installed";
 std::lock_guard lock(mutex);
 if(command=="shop")return "{\"active\":"+std::string(last_view&&GetTickCount64()-last_view<1000?"true":"false")+",\"sample\":"+view+"}";
 if(command=="events"||command.starts_with("events ")){
  std::uint64_t after=0;if(command.size()>6){const char* p=command.data()+7;const auto end=command.data()+command.size();auto r=std::from_chars(p,end,after);if(r.ec!=std::errc{}||r.ptr!=end)return "error events takes an unsigned sequence cursor";}return events.json(after);
 }
 return "error economy takes status, rolls, shop or events [after]";
}
bool contribute(patch_transaction::transaction& tx,std::string& failure){
 patch_transaction::contribution c;c.owner="Xml1Economy native shop digest observation";
 for(const auto& s:sites)c.guards.push_back({s.va,patch_transaction::bytes_of(s.bytes),s.what});
 if(!patch_transaction::verify_expectations(c,failure))return false;
 auto code=std::make_shared<storage>();if(!code->p){failure="digest trampoline allocation failed";return false;}
 std::array<unsigned,2> originals{};
 for(unsigned n=0;n<2;++n){const auto& s=sites[n];const auto bytes=patch_transaction::bytes_of(s.bytes);const auto dest=addr(code->p)+32*n;originals[n]=dest;std::memcpy(native<void*>(dest),bytes.data(),bytes.size());const auto tail=patch_transaction::relocation(dest+static_cast<unsigned>(bytes.size()),"e9",true,s.va+static_cast<unsigned>(bytes.size()),0,"native observer continuation");std::memcpy(native<void*>(tail.va),tail.bytes.data(),tail.bytes.size());}
 DWORD old=0;if(!VirtualProtect(code->p,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code->p,64)){failure="digest trampoline protection/flush failed";return false;}
 const std::array<unsigned,8> targets{addr(&open),addr(&update),addr(&gate),addr(&input<0x13c>),addr(&input<0x130>),addr(&selected_row),addr(&completed),addr(&empty_sale)};
 for(unsigned n=0;n<targets.size();++n){const auto& s=sites[n];c.writes.push_back(patch_transaction::relocation(s.va,n<2?"e9":"e8",true,targets[n],static_cast<unsigned>(patch_transaction::bytes_of(s.bytes).size())-5,s.what));}
 auto keep=[code,originals]{retained.push_back(code);original_open=native<open_t>(originals[0]);original_update=native<update_t>(originals[1]);};
 c.publish=[keep]{keep();applied=true;};c.retain=[keep]{keep();applied=false;};c.abandon=[]{applied=false;};
 return tx.add(std::move(c));
}
}
