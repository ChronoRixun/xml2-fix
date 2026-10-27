#include "save_limits.hpp"
#include "save_limits_rules.hpp"
#include "save_limits_sites.hpp"
#include "save_layout_view.hpp"
#include "objective_text.hpp"
#include "save_format.hpp"
#include "log.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace save_limits {
namespace {
using namespace patch_transaction;
using namespace save_limits_sites;
unsigned address_of(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T native(unsigned a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}
template<class T>T read(const void* p,unsigned o){T v;std::memcpy(&v,static_cast<const std::uint8_t*>(p)+o,sizeof v);return v;}
template<class T>void put(void* p,unsigned o,T v){std::memcpy(static_cast<std::uint8_t*>(p)+o,&v,sizeof v);}
struct storage {
 struct block {void* allocation;std::uint8_t* bytes;unsigned size;bool code;};
 std::vector<block> blocks;
 std::uint8_t* allocate(unsigned size,bool code=false){
  const unsigned committed=(size+4095)&~4095u;
  auto* base=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,committed+8192,MEM_RESERVE,PAGE_NOACCESS));
  if(!base)throw std::runtime_error("reserve guarded W2 storage failed");
  if(!VirtualAlloc(base+4096,committed,MEM_COMMIT,PAGE_READWRITE)){VirtualFree(base,0,MEM_RELEASE);throw std::runtime_error("commit W2 storage failed");}
  auto* bytes=code?base+4096:base+4096+committed-size;
  blocks.push_back({base,bytes,size,code});return bytes;
 }
 ~storage(){for(auto& b:blocks)VirtualFree(b.allocation,0,MEM_RELEASE);}
};
std::vector<std::shared_ptr<storage>> retained;
bool active=false;
std::uint8_t* manager=nullptr;
std::uint8_t* registry=nullptr;
std::uint16_t* wide=nullptr;
save_layout_view::objectives_view view=save_layout_view::w2_objectives_shape;
unsigned copy_original=0;
std::atomic<unsigned> objective_total{0};
void __cdecl registered_total(unsigned count){if(count<=1024)objective_total.store(count,std::memory_order_relaxed);}
unsigned generation_return=0x4bfafe,shared_cap_target=0x4c0460;
static_assert(save_layout_view::w2_objectives_shape.count_offset==8+objective_count);
int slot(const void* record){
 const auto p=address_of(record),base=address_of(manager)+8;
 if(!manager||p<base||(p-base)%0x1ac||(p-base)/0x1ac>=127)return -1;
 return static_cast<int>((p-base)/0x1ac);
}
unsigned __cdecl record_index(const void* record){const auto s=slot(record);return s<0?0xffff:wide[s];}
void __cdecl registered(void* record,unsigned index){
 const auto s=slot(record);if(s<0||index>=1024)return;
 wide[s]=static_cast<std::uint16_t>(index);objective_text::reset(index);
}
void* __fastcall allocate_objective(std::uint8_t* pool,void*){
 const auto n=read<unsigned>(pool,objective_count);
 if(n>=127){save_format::latch_reconstruction_failure("objective pool full (127)");return nullptr;}
 auto* record=pool+n*0x1ac;
 native<void*(__fastcall*)(void*,void*)>(0x488670)(record,nullptr);
 auto bitmap=read<unsigned>(pool,objective_bitmap+n/32*4);bitmap|=1u<<(n%32);
 put(pool,objective_bitmap+n/32*4,bitmap);put(pool,objective_count,n+1);
 const auto s=slot(record);if(s>=0)wide[s]=0xffff;
 return record;
}
void __fastcall reset_objectives(void* pool,void*){
 std::memset(static_cast<std::uint8_t*>(pool)+objective_bitmap,0,16);
 if(wide)std::fill_n(wide,127,std::uint16_t(0xffff));
}
// Copying to a sort temporary must use retail's string/flag semantics. A direct
// pool-to-pool copy also moves the wide index; no global temporary shadow exists.
void* __fastcall copy_record(void* dst,void*,const void* src){
 const auto index=record_index(src);
 native<void*(__fastcall*)(void*,void*,const void*)>(copy_original)(dst,nullptr,src);
 const auto d=slot(dst);if(d>=0)wide[d]=static_cast<std::uint16_t>(index);
 return dst;
}
void __fastcall sort_objectives(std::uint8_t* pool,void*){
 const auto n=read<unsigned>(pool,objective_count);if(n>127)return;
 auto record=[pool](unsigned i){return pool+i*0x1ac;};
 auto key=[&](unsigned i){return static_cast<std::int8_t>(record(i)[0x1a8]);};
 std::array<std::uint8_t,0x1ac> tempA{},tempB{};
 auto swap=[&](unsigned parent,unsigned child,auto& temp){
  const auto pi=record_index(record(parent)),ci=record_index(record(child));
  // Retail calls the field-wise helper only on pool -> temporary; both
  // copies back into the pool are exact 0x1ac-byte copies.
  native<void*(__fastcall*)(void*,void*,const void*)>(copy_original)(temp.data(),nullptr,record(parent));
  std::memcpy(record(parent),record(child),0x1ac);std::memcpy(record(child),temp.data(),0x1ac);
  const auto p=slot(record(parent)),c=slot(record(child));
  if(p>=0)wide[p]=static_cast<std::uint16_t>(ci);
  if(c>=0)wide[c]=static_cast<std::uint16_t>(pi);
 };
 // Incremental max heap, with signed priorities and retail's right-child tie.
 for(unsigned i=1;i<n;++i){unsigned child=i;while(child){const auto parent=(child-1)/2;if(key(parent)>=key(child))break;swap(parent,child,tempA);child=parent;}}
 for(unsigned end=n? n-1:0;end>0;--end){
  swap(0,end,tempA);unsigned parent=0;
  for(;;){const auto left=2*parent+1,right=left+1;if(left>=end)break;
   const auto child=right<end&&key(right)>=key(left)?right:left;
   if(key(parent)>=key(child))break;
   // Native's third swap copies selected child -> temporary B first.
   swap(child,parent,tempB);parent=child;
  }
 }
}
void* __cdecl manager_get(){
 auto*& cell=*native<void**>(0x72b108);
 if(!cell){std::memset(manager,0,objective_manager_size);std::fill_n(wide,127,std::uint16_t(0xffff));cell=native<void*(__fastcall*)(void*,void*)>(0x489ff0)(manager,nullptr);}
 return cell;
}
void __cdecl manager_stop(){
 auto*& cell=*native<void**>(0x72b108);if(cell){native<void(__fastcall*)(void*,void*)>(0x489fe0)(cell,nullptr);cell=nullptr;std::fill_n(wide,127,std::uint16_t(0xffff));}
}
void* __fastcall ring_reset(void* ring,void*){
 put(ring,256*4+4,0u);put(ring,256*4+8,0u);put(ring,256*4+12,0u);return ring;
}
void __fastcall ring_push(void* ring,void*,const unsigned* value){
 auto head=read<unsigned>(ring,256*4+4),count=read<unsigned>(ring,256*4+12);
 put(ring,head*4,*value);put(ring,256*4+4,(head+1)%256);put(ring,256*4+12,count+1);
}
// 0x4be010 / 0x4be9c0 are the ratl pool<16-byte, 100>::pop / clear shared
// (identical-code folded) by every such pool; 0x4d6b00, 0x4fd8d0, 0x460580,
// 0x4d6ff0 and 0x4d8170 are foreign owners, so both retail bodies stay intact.
// Only the talent registry's C-pool edges (0x4bfce9 pop; 0x4bfe10, 0x4c043d
// clear) call these 256-entry versions at the W2 C-pool offsets. The callers
// use only EAX (the popped id) afterwards, as for any thiscall.
using save_limits_sites::c_ring;using save_limits_sites::c_head;using save_limits_sites::c_tail;
using save_limits_sites::c_ringcount;using save_limits_sites::c_buckets;using save_limits_sites::c_live;
unsigned __fastcall talent_pool_pop(std::uint8_t* pool,void*){
 const auto tail=read<unsigned>(pool,c_tail);
 const auto id=read<unsigned>(pool,c_ring+tail*4);
 const auto word=c_buckets+static_cast<unsigned>(static_cast<int>(id)>>5)*4;
 put(pool,word,read<unsigned>(pool,word)|1u<<(id&31));
 put(pool,c_tail,static_cast<int>(tail+1)<256?tail+1:0u);
 put(pool,c_ringcount,read<unsigned>(pool,c_ringcount)-1);
 put(pool,c_live,read<unsigned>(pool,c_live)+1);
 return id;
}
// Head, tail and count start at zero; ids 0..255 are pushed in order, each at
// the slot before the advanced head (the 256th wraps the head to 0, slot 255).
void __fastcall talent_pool_clear(std::uint8_t* pool,void*){
 put(pool,c_head,0u);put(pool,c_tail,0u);put(pool,c_ringcount,0u);
 for(unsigned id=0;id<256;++id){
  const auto head=read<unsigned>(pool,c_head)+1;
  put(pool,c_ringcount,read<unsigned>(pool,c_ringcount)+1);
  unsigned slot=head-1;
  if(static_cast<int>(head)<256)put(pool,c_head,head);else{put(pool,c_head,0u);slot=255;}
  put(pool,c_ring+slot*4,id);
 }
}
void* __cdecl registry_allocate(unsigned,unsigned,unsigned){std::memset(registry,0,talent_size);return registry;}
void __cdecl registry_free(void*,unsigned){} // DLL backing lives through process shutdown.
void* __fastcall acts_construct(void* p,void*){put(p,64,0u);return p;}
void* __fastcall bases_construct(void* p,void*){put(p,128,0u);return p;}
void __cdecl generations(void* pool){
 for(unsigned i=0;i<256;++i){const auto old=read<unsigned>(pool,b_generations+i*4);auto next=old+256u;if(next&0x80000000u)next=256u|i;put(pool,b_generations+i*4,next);}
}
__declspec(naked) void generation_stub(){__asm {
 pushfd
 pushad
 push esi
 call generations
 add esp,4
 popad
 popfd
 jmp dword ptr [generation_return]
}}
__declspec(naked) void multiplier_stub(){__asm {
 imul eax,eax,200
 ret 4
}}
__declspec(naked) void cap_stub(){__asm {
 mov dword ptr [esp+0xc],199
 jmp dword ptr [shared_cap_target]
}}
struct built {unsigned lo,hi,base;std::vector<save_limits_sites::change> edits;};
unsigned mapped(const built& b,unsigned a){
 int delta=0;for(const auto& e:b.edits){if(a<b.lo+e.offset)break;if(a==b.lo+e.offset)return b.base+e.offset+delta;if(a<b.lo+e.offset+e.size)throw std::runtime_error("branch enters transformed instruction");delta+=static_cast<int>(bytes_of(e.value).size())-e.size;}
 return b.base+a-b.lo+delta;
}
void append(contribution& owner,contribution c){
 owner.guards.insert(owner.guards.end(),c.guards.begin(),c.guards.end());owner.regions.insert(owner.regions.end(),c.regions.begin(),c.regions.end());owner.writes.insert(owner.writes.end(),c.writes.begin(),c.writes.end());
 auto chain=[](auto a,auto b){return [a=std::move(a),b=std::move(b)]{if(a)a();if(b)b();};};
 owner.publish=chain(std::move(owner.publish),std::move(c.publish));owner.abandon=chain(std::move(owner.abandon),std::move(c.abandon));owner.retain=chain(std::move(owner.retain),std::move(c.retain));
}
}
bool installed(){return active;}
std::optional<std::string> expected(unsigned va,std::string_view hex){
 if(!active)return std::string(hex);
 if(hex.size()%2)return std::nullopt;
 auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
 std::vector<std::uint8_t> bytes;
 for(std::size_t i=0;i<hex.size();i+=2){const int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)return std::nullopt;bytes.push_back(static_cast<std::uint8_t>(a<<4|b));}
 const std::uint64_t end=std::uint64_t(va)+bytes.size();
 // one W2-owned span: [lo, lo+size); `old` = W2's retail bytes for it (rows), nullptr for caves / replacements
 auto take=[&](unsigned lo,unsigned size,const char* old)->bool{
  if(!(std::uint64_t(lo)<end&&std::uint64_t(va)<std::uint64_t(lo)+size))return true;
  for(unsigned i=0;i<size;++i){
   const std::uint64_t at=std::uint64_t(lo)+i;if(at<va||at>=end)continue;
   if(old){const int h=digit(old[2*i]),l=digit(old[2*i+1]);if(h<0||l<0||bytes[at-va]!=static_cast<std::uint8_t>(h<<4|l))return false;}
   bytes[at-va]=*native<const std::uint8_t*>(static_cast<unsigned>(at));
  }
  return true;
 };
 for(const auto& r:direct)if(!take(r.va,static_cast<unsigned>(std::strlen(r.old)/2),r.old))return std::nullopt;
 for(const auto& b:blocks)if(!take(b.lo,b.hi-b.lo,nullptr))return std::nullopt;
 for(const auto& r:replacements)if(!take(r.lo,r.hi-r.lo,nullptr))return std::nullopt;
 constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(hex.size());
 for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}
 return out;
}
unsigned objective_registered_count(){return active?objective_total.load(std::memory_order_relaxed):0;}
unsigned talent_capacity(){return active?256:100;}
unsigned talent_count_offset(unsigned pool){const unsigned old[]={0x1654,0x26f4,0x3090},raised[]={talent_a+a_live,talent_b+b_live,talent_c+c_live};return pool<3?(active?raised[pool]:old[pool]):0;}
bool contribute_w2(contribution& owner,std::string& why) try {
 contribution c;c.owner="W2 objectives / talents";auto store=std::make_shared<storage>();
 auto* manager_bytes=store->allocate(objective_manager_size);auto* indices=store->allocate(127*2);
 auto* registry_bytes=store->allocate(talent_size);
 auto* states=store->allocate(1024);auto* names=store->allocate(64*32+8);auto* acts=store->allocate(64+4);auto* bases=store->allocate(64*2+4);auto* side=store->allocate(0xa88);
 const std::array<unsigned,10> data{0,address_of(states),address_of(names),address_of(names)+64*32+4,address_of(acts),address_of(acts)+64,address_of(bases),address_of(bases)+128,address_of(side),address_of(side)+0xa84};
 const std::array<unsigned,21> target{0,address_of(&record_index),address_of(&registered),address_of(&allocate_objective),address_of(&reset_objectives),address_of(&sort_objectives),address_of(&manager_get),address_of(&manager_stop),address_of(&ring_reset),address_of(&ring_push),address_of(&generation_stub),address_of(&multiplier_stub),address_of(&cap_stub),address_of(&copy_record),address_of(&registry_allocate),address_of(&registry_free),address_of(&acts_construct),address_of(&bases_construct),address_of(&registered_total),address_of(&talent_pool_pop),address_of(&talent_pool_clear)};
 auto resolve=[&](std::vector<std::uint8_t>& bytes){for(unsigned i=0;i+4<=bytes.size();++i){unsigned v=read<unsigned>(bytes.data(),i);if(v>0xfeed0000&&v<0xfeed0000+target.size())put(bytes.data(),i,target[v-0xfeed0000]);if(v>0xfeed1000&&v<0xfeed1000+data.size())put(bytes.data(),i,data[v-0xfeed1000]);}};
 for(const auto& g:save_limits_sites::ledger)c.guards.push_back({g.va,bytes_of(g.bytes),"E14/E7 reviewed retail ledger"});
 for(const auto cell:{0x72b108u,0x77339cu})c.guards.push_back({cell,{0,0,0,0},"W2 objectives/talents startup quiescence"});
 for(const auto& r:direct){c.guards.push_back({r.va,bytes_of(r.old),r.kind});auto bytes=bytes_of(r.value);resolve(bytes);if(r.target){const auto rel=target[r.target]-r.va-5;put(bytes.data(),1,rel);}c.writes.push_back({r.va,std::move(bytes),r.kind});}
 // Copy the complete guarded retail record-copy helper before replacing its
 // entry; this preserves its unusual field-wise string and flag semantics.
 auto* copy=store->allocate(0x1000,true);
 std::memcpy(copy,native<void*>(0x488430),0xe4);
 // Its only relative edges are five CRT strncpy calls, at reviewed offsets.
 for(const auto o:{0xeu,0x22u,0x37u,0x55u,0x70u}){
  const unsigned a=0x488430+o,destination=a+5+read<unsigned>(copy,o+1);
  put(copy,o+1,destination-address_of(copy)-o-5);
 }
 for(const auto& block:blocks){
  auto* memory=store->allocate(block.hi-block.lo+0x4000,true);built b{block.lo,block.hi,address_of(memory),block.edits};
  c.regions.push_back({"W2 save limits displaced owner",block.lo,block.hi,sha256_of(block.hash)});
  c.guards.push_back({block.lo,std::vector<std::uint8_t>(native<const std::uint8_t*>(block.lo),native<const std::uint8_t*>(block.hi)),"hash-checked W2 cave bytes"});
  unsigned old=0;std::vector<std::uint8_t> bytes;
  for(const auto& e:block.edits){const auto* from=native<const std::uint8_t*>(block.lo+old);bytes.insert(bytes.end(),from,from+e.offset-old);auto value=bytes_of(e.value);resolve(value);
   if(e.target){auto dest=e.target<target.size()?target[e.target]:e.target;if(block.lo<=dest&&dest<block.hi)dest=mapped(b,dest);const unsigned rel=dest-b.base-static_cast<unsigned>(bytes.size()+value.size());put(value.data(),static_cast<unsigned>(value.size()-4),rel);}
   bytes.insert(bytes.end(),value.begin(),value.end());old=e.offset+e.size;
  }
  const auto* from=native<const std::uint8_t*>(block.lo+old);bytes.insert(bytes.end(),from,from+block.hi-block.lo-old);
  const auto tail=relocation(b.base+static_cast<unsigned>(bytes.size()),"e9",true,block.hi,0,"W2 cave return");bytes.insert(bytes.end(),tail.bytes.begin(),tail.bytes.end());std::memcpy(memory,bytes.data(),bytes.size());
  c.writes.push_back(relocation(block.lo,"e9",true,b.base,0,"W2 save limits cave"));
 }
 for(const auto& r:replacements){c.regions.push_back({r.kind,r.lo,r.hi,sha256_of(r.hash)});c.guards.push_back({r.lo,std::vector<std::uint8_t>(native<const std::uint8_t*>(r.lo),native<const std::uint8_t*>(r.hi)),"hash-checked W2 replacement bytes"});if(r.target)c.writes.push_back(relocation(r.lo,"e9",true,target[r.target],0,r.kind));}
 for(const auto& block:store->blocks)if(block.code){DWORD old=0;if(!VirtualProtect(block.bytes,(block.size+4095)&~4095u,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),block.bytes,block.size))throw std::runtime_error("W2 cave protection/flush failed");}
 if(!verify_expectations(c,why))return false;
 c.publish=[store,manager_bytes,indices,states,copy,registry_bytes]{
  retained.push_back(store);manager=manager_bytes;wide=reinterpret_cast<std::uint16_t*>(indices);std::fill_n(wide,127,std::uint16_t(0xffff));copy_original=address_of(copy);active=true;
  view.state_bytes=address_of(states);view.wide_index=wide;view.pool_address=address_of(manager)+8;save_layout_view::publish(view);
  registry=registry_bytes;
 };
 c.abandon=[store]{};c.retain=[store]{retained.push_back(store);};append(owner,std::move(c));why.clear();return true;
}catch(const std::exception& e){why=std::string("W2 objectives/talents: ")+e.what();return false;}
}
