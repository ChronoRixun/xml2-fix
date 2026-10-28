#include "item_capacity.hpp"
#include "item_capacity_rules.hpp"
#include "item_capacity_sites.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include "save_format.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
namespace item_capacity {
namespace {
using namespace patch_transaction;
std::uint32_t address_of(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T native(std::uint32_t a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}
template<class T>T read(const void* p,unsigned off=0){T v;std::memcpy(&v,static_cast<const std::uint8_t*>(p)+off,sizeof v);return v;}
template<class T>void put(void* p,unsigned off,T v){std::memcpy(static_cast<std::uint8_t*>(p)+off,&v,sizeof v);}
template<class T>T method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/4]);}
using get0=void*(__fastcall*)(void*,void*);
using get_int=int(__fastcall*)(void*,void*);
using get_bool=bool(__fastcall*)(void*,void*);
using get_key=std::uint32_t*(__fastcall*)(void*,void*,std::uint32_t*);
using valid=bool(__fastcall*)(void*,void*,std::uint32_t);
using get_object=void*(__fastcall*)(void*,void*,std::uint32_t);
struct link {std::uint32_t handle;void* owner;};
bool duplicate(void* record,std::uint32_t handle,void* chain) {
 std::uint32_t key=0;const auto* key_result=method<get_key>(record,0xc)(record,nullptr,&key);
 key=*key_result;if(!key)return false;
 while(chain && method<valid>(chain,0)(chain,nullptr,handle)) {
  void* element=method<get_object>(chain,8)(chain,nullptr,handle);
  void* enhancement=method<get0>(element,0x38)(element,nullptr);
  if(enhancement) {
   std::uint32_t other=0;
   if(*method<get_key>(enhancement,0xc)(enhancement,nullptr,&other)==key)return true;
  }
  const auto* next=static_cast<const link*>(method<get0>(element,0x18)(element,nullptr));
  handle=next->handle;chain=next->owner;
 }
 return false;
}
int __fastcall prefix_picker(void* m,void*,int level,std::uint32_t h,void* chain){return pick(m,level,h,chain,true,item_capacity_rules::records);}
int __fastcall suffix_picker(void* m,void*,int level,std::uint32_t h,void* chain){return pick(m,level,h,chain,false,item_capacity_rules::records);}
bool __fastcall slot_write(void* record,void*,void* source,unsigned size) {
 std::uint8_t unused=0;
 const bool result=native<bool(__fastcall*)(void*,void*,void*,unsigned)>(0x465580)(record,nullptr,&unused,size);
 put(record,0x12,read<std::uint16_t>(source));return result;
}
bool __fastcall slot_read(void* record,void*,void* destination,unsigned size) {
 const bool result=native<bool(__fastcall*)(void*,void*,void*,unsigned)>(0x4655d0)(record,nullptr,destination,size);
 put(destination,0,read<std::uint16_t>(record,0x12));return result;
}
void __cdecl attached_full() {
 logger::write("W2 attached-enhancement allocation refused: capacity 4096 exhausted");
 save_format::latch_reconstruction_failure("attached-enhancement capacity 4096 exhausted");
}
__declspec(naked) void attached_refusal() {__asm {
 test ecx,ecx
 jnz okay
 pushfd
 pushad
 call attached_full
 popad
 popfd
 push 481a02h
 ret
okay:
 push 481940h
 ret
}}
void __cdecl definitions_full(){logger::write("W2 item-definition registration refused: capacity 1024 exhausted");save_format::latch_reconstruction_failure("item-definition capacity 1024 exhausted");}
void __cdecl records_full(){logger::write("W2 enhancement-record registration refused: capacity 4096 exhausted");save_format::latch_reconstruction_failure("enhancement-record capacity 4096 exhausted");}
void __cdecl world_full(){logger::write("W2 world-item allocation refused: capacity 1024 exhausted");}
std::uint8_t* instance_backing=nullptr;std::uint8_t* attached_backing=nullptr;unsigned published_capacity=0;
void* definition_picker_copy=nullptr;
// 0x483410 is the shared ring<256>::pop, also called by the file, attribute
// and pointer pools at 0x58a209 / 0x5421f9 / 0x4c7089. Its retail body stays
// intact; only the item allocator's edge 0x4833f9 calls this 1024-entry pop.
// Same effect at the W2 offsets: advance the read index (wrap at 1024),
// decrement the live count and return it. The caller reads no register after
// the call (it reloads EAX), so a plain thiscall-compatible function fits.
unsigned __fastcall instance_ring_pop(std::uint8_t* ring,void*){
 const auto next=read<unsigned>(ring,0x1008)+1;
 put(ring,0x1008,static_cast<int>(next)<1024?next:0u);
 const auto count=read<unsigned>(ring,0x100c)-1;
 put(ring,0x100c,count);
 return count;
}
void* __cdecl instance_get(){auto& cell=*native<void**>(0x72a524);if(!cell){put(instance_backing,0,0x6884a8u);native<void*(__fastcall*)(void*,void*)>(0x483170)(instance_backing+4,nullptr);cell=instance_backing;}return cell;}
void* __cdecl attached_get(){auto& cell=*native<void**>(0x717b30);if(!cell){put(attached_backing,0,0x686598u);native<void*(__fastcall*)(void*,void*)>(0x45f6c0)(attached_backing+4,nullptr);cell=attached_backing;}return cell;}
struct storage {
 struct allocation{void* base;bool code;};std::vector<allocation> allocations;
 ~storage(){for(const auto& a:allocations)VirtualFree(a.base,0,MEM_RELEASE);}
 std::uint8_t* allocate(std::size_t size,bool executable=true){
  const auto pages=(size+4095)&~std::size_t(4095);auto* p=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,pages+(executable?0:4096),MEM_RESERVE|MEM_COMMIT,executable?PAGE_EXECUTE_READWRITE:PAGE_READWRITE));
  if(!p)throw std::runtime_error("W2 item storage allocation failed");allocations.push_back({p,executable});
  if(!executable){DWORD old=0;if(!VirtualProtect(p+pages,4096,PAGE_NOACCESS,&old))throw std::runtime_error("pool guard page failed");return p+pages-size;}return p;
 }
};
std::vector<std::shared_ptr<storage>> retained;
std::uint32_t first_type2=0xffffffff;
void resolve_data(std::vector<std::uint8_t>& bytes){for(unsigned i=0;i+4<=bytes.size();++i){const auto value=read<std::uint32_t>(bytes.data(),i);if(value==0xfeed0005)put(bytes.data(),i,address_of(&first_type2));else if(value==0xfeed0006)put(bytes.data(),i,address_of(&world_full));}}
struct built {std::uint32_t lo,hi,base;std::vector<item_capacity_sites::change> edits;};
std::uint32_t mapped(const built& b,std::uint32_t a) {
 std::int64_t delta=0;
 for(const auto& e:b.edits) {
  if(a<b.lo+e.offset)break;
  if(a==b.lo+e.offset)return b.base+e.offset+static_cast<std::uint32_t>(delta);
  if(a<b.lo+e.offset+e.size)throw std::runtime_error("branch into transformed instruction");
  delta+=static_cast<std::int64_t>(bytes_of(e.value).size())-e.size;
 }
 return b.base+(a-b.lo)+static_cast<std::uint32_t>(delta);
}
void append(contribution& dst,contribution part) {
 dst.guards.insert(dst.guards.end(),part.guards.begin(),part.guards.end());dst.regions.insert(dst.regions.end(),part.regions.begin(),part.regions.end());dst.writes.insert(dst.writes.end(),part.writes.begin(),part.writes.end());
 auto chain=[](auto a,auto b){return [a=std::move(a),b=std::move(b)]{if(a)a();if(b)b();};};
 dst.publish=chain(std::move(dst.publish),std::move(part.publish));dst.abandon=chain(std::move(dst.abandon),std::move(part.abandon));dst.retain=chain(std::move(dst.retain),std::move(part.retain));
}
}
int pick(void* manager,int level,std::uint32_t handle,void* chain,bool prefix,std::uint32_t offset) {
 // Shared 16 KiB workspace per thread, exactly one slot per accepted record.
 // The engine is single-threaded here; each invocation resets its count.
 static thread_local std::array<int,4096> candidates;
 const int first=prefix?read<int>(manager,0x605c):0;
 const int last=read<int>(manager,0x605c)+(prefix?read<int>(manager,0x6060):0);
 if(first<0 || last<first || last>4096)return -1;
 unsigned count=0;
 for(int i=first;i<last;++i) {
  auto* r=static_cast<std::uint8_t*>(manager)+offset+i*0x28;
  if(method<get_int>(r,0x1c)(r,nullptr)>level+5)continue;
  if(method<get_int>(r,0x1c)(r,nullptr)<=level-15 && !method<get_bool>(r,0x2c)(r,nullptr))continue;
  if(duplicate(r,handle,chain))continue;
  candidates[count++]=i;
 }
 if(!count)return -1;
 const auto choice=native<int(__cdecl*)(int,int,bool)>(0x5601e0)(0,static_cast<int>(count)-1,true);
 return candidates[static_cast<unsigned>(choice)];
}
unsigned installed_capacity(){return published_capacity;}
void* prepared_definition_picker(){return definition_picker_copy;}
bool preflight(unsigned instance_demand,unsigned attached_demand,unsigned world_demand,const lcsave::NativeLayout& layout,std::string& error) {
 const bool w2=std::string_view(layout.id)=="W2";
 unsigned ip=0,ap=0,live_instances=0,live_attached=0;
 auto read32=[](unsigned a,unsigned& out){return patch_transaction::read_memory(a,&out,4);};
 if(!read32(0x72a524,ip)||!ip||!read32(ip+item_capacity_rules::instance_count_offset(w2),live_instances)||
    !read32(0x717b30,ap)||!ap||!read32(ap+item_capacity_rules::attached_count_offset(w2),live_attached))
 {error="native item/attached pool occupancy cannot be read";return false;}
 auto fits=[](unsigned demand,unsigned live,unsigned cap){return live<=cap&&demand<=cap-live;};
 if(!fits(instance_demand,live_instances,layout.instances))
 {error="native item-instance free capacity ("+std::to_string(layout.instances)+") is insufficient";return false;}
 if(!fits(attached_demand,live_attached,layout.attached_enhancements))
 {error="native attached-enhancement free capacity ("+std::to_string(layout.attached_enhancements)+") is insufficient";return false;}
 if(w2) {
  unsigned manager=0,live_world=0;
  if(!read32(0x72a514,manager)||!manager){error="native world-item pool occupancy cannot be read";return false;}
  for(unsigned i=0;i<layout.world_slots/32;++i){unsigned word=0;if(!read32(manager+item_capacity_rules::world_bitmap+i*4,word)){error="native world-item bitmap cannot be read";return false;}live_world+=std::popcount(word);}
  if(!fits(world_demand,live_world,layout.world_slots)){error="native world-item free capacity ("+std::to_string(layout.world_slots)+") is insufficient";return false;}
 }
 error.clear();return true;
}
bool contribute_w2(contribution& owner,std::string& why) try {
 definition_picker_copy=nullptr;void* prepared_picker=nullptr;
 contribution c;c.owner="W2 item capacities";
 auto storage=std::make_shared<struct storage>();
 for(const auto& g:item_capacity_sites::ledger)c.guards.push_back({g.va,bytes_of(g.bytes),"reviewed SPEC-old item ledger"});
 for(const auto& g:item_capacity_sites::omissions)c.guards.push_back({g.va,bytes_of(g.bytes),"52 instance-pool omissions"});
 std::array<std::uint32_t,6> targets{};
 auto* instances=storage->allocate(item_capacity_rules::instances.size,false);auto* attached=storage->allocate(item_capacity_rules::attached.size,false);
 for(const auto cell:{0x72a524u,0x717b30u,0x72a514u})c.guards.push_back({cell,{0,0,0,0},"W2 item owner must not already exist"});
 // Clone both shared bitset scans, guard complete owners, preserve originals.
 for(unsigned id=1;id<=2;++id) {
  const std::uint32_t lo=id==1?0x44d0d0:0x45e5f0,hi=lo+0x99;
  auto* code=storage->allocate(0x1000);std::memcpy(code,native<void*>(lo),hi-lo);
  const auto& fields=limits_rules::item_find_next.fields;
  for(const auto& f:fields)put(code,f.offset,id==1?1024u:4096u);
  // Hash constants are separately generated and verified in the execution gate.
  const char* hash=id==1?"07dd9f38edf450ed3f57f88a358c254f7acbcb40cdff9e3d1d526ea2862ce931":"97ecf540d7cff63107dbbc839f9eb8fcc861aa255c2ab9c001c23cd3e678e09b";
  c.regions.push_back({"private bitset clone",lo,hi,sha256_of(hash)});targets[id]=address_of(code);
 }
 targets[3]=address_of(&slot_write);targets[4]=address_of(&slot_read);
 targets[5]=address_of(&instance_ring_pop);
 c.regions.push_back({"shared instance ring pop",0x483410,0x48343c,sha256_of("8ccbe1f044600d538d1cf993f6b3ca9802496b85b8589f2b629c691e8d601cbd")});
 for(const auto& r:item_capacity_sites::direct) {
  c.guards.push_back({r.va,bytes_of(r.old),r.kind});auto value=bytes_of(r.value);
  resolve_data(value);
  if(r.target){const auto rel=targets[r.target]-(r.va+5);std::memcpy(value.data()+1,&rel,4);}
  c.writes.push_back({r.va,std::move(value),r.kind});
 }
 for(const auto& block:item_capacity_sites::blocks) {
  auto* memory=storage->allocate(block.hi-block.lo+0x2000);
  if(block.lo==0x47b590)prepared_picker=memory;
  built b{block.lo,block.hi,address_of(memory),block.edits};
  c.regions.push_back({"W2 item copied instruction owner",block.lo,block.hi,sha256_of(block.hash)});
  c.guards.push_back({block.lo,std::vector<std::uint8_t>(native<const std::uint8_t*>(block.lo),native<const std::uint8_t*>(block.hi)),"hashed displaced owner bytes"});
  unsigned old=0;std::vector<std::uint8_t> code;
  for(const auto& e:block.edits) {
   const auto* source=native<const std::uint8_t*>(block.lo+old);code.insert(code.end(),source,source+(e.offset-old));
   auto value=bytes_of(e.value);resolve_data(value);
   if(e.target){auto target=e.target<targets.size()?targets[e.target]:e.target;if(block.lo<=target&&target<block.hi)target=mapped(b,target);const auto rel=target-(b.base+static_cast<unsigned>(code.size())+static_cast<unsigned>(value.size()));std::memcpy(value.data()+value.size()-4,&rel,4);}
   code.insert(code.end(),value.begin(),value.end());old=e.offset+e.size;
  }
  const auto* source=native<const std::uint8_t*>(block.lo+old);code.insert(code.end(),source,source+(block.hi-block.lo-old));
  const auto end=relocation(b.base+static_cast<unsigned>(code.size()),"e9",true,block.hi,0,"return from item width cave");code.insert(code.end(),end.bytes.begin(),end.bytes.end());std::memcpy(memory,code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),memory,code.size());
  c.writes.push_back(relocation(block.lo,"e9",true,b.base,0,"W2 item width cave"));
 }
 // Enhancement record rows use the existing reviewed layout generator, then
 // the aggregate manager allocation includes all four appended structures.
 auto layout=limits_rules::item_layout_for(4096);layout.manager_size=item_capacity_rules::manager_size;
 auto* record_scan=storage->allocate(0x1000);const auto oldscan=bytes_of(limits_rules::item_find_next.hex);std::memcpy(record_scan,oldscan.data(),oldscan.size());limits_rules::finish_item_clone(record_scan,layout);
 for(const auto& s:limits_rules::item_sites) {
  if(s.va==0x47e6d4||s.va==0x47eec6)continue; // whole picker replacements own these operands
  c.guards.push_back({s.va,bytes_of(s.hex),"enhancement record layout"});
 }
 for(const auto& w:limits_rules::item_writes(layout,address_of(record_scan))) {
  if(w.va>=0x47e690&&w.va<0x47e818 || w.va>=0x47ee90&&w.va<0x47effb)continue;
  std::vector<std::uint8_t> value(w.size);std::memcpy(value.data(),&w.value,w.size);c.writes.push_back({w.va,std::move(value),"enhancement records 4096"});
 }
 c.guards.push_back({limits_rules::item_clone_call.va,bytes_of(limits_rules::item_clone_call.hex),"record scan clone call"});
 struct replacement{unsigned lo,hi;const char* hash;const void* target;};
 for(const auto& r:std::initializer_list<replacement>{{0x47e690,0x47e818,"162a0701a627bc3ea7b8446f4111d4419e8e9895923c7ec9c9fa5893ee3bc26b",&prefix_picker},{0x47ee90,0x47effb,"cb6f5f1ef419bb770cb9ccdfb99b3d88b6810fd5c8c9c4f8bbdc95a537594769",&suffix_picker},{0x481938,0x481940,"c7aab90e4d3d7a242beb747d3c3e404b70d74a3c223628cf31424385f81e2708",&attached_refusal}}) {
  c.regions.push_back({"W2 picker / attached refusal owner",r.lo,r.hi,sha256_of(r.hash)});c.writes.push_back(relocation(r.lo,"e9",true,address_of(r.target),r.hi-r.lo>8?0:r.hi-r.lo-5,"W2 picker / attached refusal"));
 }
 // Full registration follows the same native cleanup path, after a named
 // refusal. Preserve flags and every GP register around the observer call.
 struct registration {unsigned branch,target;const char* old;const void* observer;};
 for(const auto& r:std::initializer_list<registration>{{0x480438,0x4806cc,"0f8d8e020000",&definitions_full},{0x47ca4b,0x47cc01,"0f8db0010000",&records_full},{0x47cb44,0x47cc01,"0f8db7000000",&records_full},{0x47cd4b,0x47ce5b,"0f8d0a010000",&records_full},{0x4804ab,0x4806cc,"0f8d1b020000",&records_full}}) {
  auto* memory=storage->allocate(4096);auto bytes=bytes_of("9c60b8");const auto observer=address_of(r.observer);
  const auto* value=reinterpret_cast<const std::uint8_t*>(&observer);bytes.insert(bytes.end(),value,value+4);
  const auto restore=bytes_of("ffd0619d");bytes.insert(bytes.end(),restore.begin(),restore.end());
  const auto tail=relocation(address_of(memory)+static_cast<unsigned>(bytes.size()),"e9",true,r.target,0,"native registration cleanup");bytes.insert(bytes.end(),tail.bytes.begin(),tail.bytes.end());std::memcpy(memory,bytes.data(),bytes.size());
  c.guards.push_back({r.branch,bytes_of(r.old),"registration refusal branch"});c.writes.push_back(relocation(r.branch,"0f8d",true,address_of(memory),0,"named registration refusal"));
 }
 c.regions.push_back({"W2 inventory slot serializer",0x479a70,0x479abf,sha256_of("5f8f1c5c251e258e3096188b7e9781e2f44010f511ebce2a0985e1c1cfcfb269")});
 c.regions.push_back({"W2 inventory slot serializer",0x479fc0,0x47a07a,sha256_of("498594ea920787b198785b69dd7e2105d53c6b707a36b8b822a481731c4daeeb")});
 c.regions.push_back({"W2 item registrar owner",0x47c9e0,0x47cc16,sha256_of("ef8575397f6838c878a6ed11ab09aa7511d159e0549f0f3ffbd29d537d97a6f2")});
 c.regions.push_back({"W2 item registrar owner",0x47cc20,0x47ce72,sha256_of("3ea17da3aac9a05bc969724e1eab5f3ddced2a55e220792b4389dedacb5f6725")});
 c.regions.push_back({"W2 item registrar owner",0x480290,0x4806e1,sha256_of("e937df7c291fad2c7a4565069c63b8d3866f372ba20f0ba3b4b7abd070bd8e10")});
 for(const auto& allocation:storage->allocations)if(allocation.code){MEMORY_BASIC_INFORMATION info{};VirtualQuery(allocation.base,&info,sizeof info);DWORD old=0;if(!VirtualProtect(allocation.base,info.RegionSize,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),allocation.base,info.RegionSize))throw std::runtime_error("executable storage protection/flush failed");}
 c.regions.push_back({"DLL item pool getter",0x483610,0x48365d,sha256_of("2be9f4ee299916f6ac4ba992cc2b08a20d3a1f75d63999a69e4cb7cecde6f155")});
 c.writes.push_back(relocation(0x483610,"e9",true,address_of(&instance_get),0,"DLL item pool getter"));
 c.regions.push_back({"DLL item pool getter",0x45fb00,0x45fb4d,sha256_of("ffafbe2308a18aed7563b79225f60176d6e04ab08d3ce2b5ecf8dc126132a866")});
 c.writes.push_back(relocation(0x45fb00,"e9",true,address_of(&attached_get),0,"DLL item pool getter"));
 if(!verify_expectations(c,why))return false;
 c.publish=[storage,instances,attached]{retained.push_back(storage);instance_backing=instances;attached_backing=attached;published_capacity=4096;};c.abandon=[storage]{definition_picker_copy=nullptr;};c.retain=[storage]{retained.push_back(storage);};
 // W2 supersedes the same existing limit rows in the capacity union.
 owner.writes.erase(std::remove_if(owner.writes.begin(),owner.writes.end(),[](const auto& w){for(const auto& s:limits_rules::item_sites)if(w.va>=s.va&&w.va<s.va+bytes_of(s.hex).size())return true;return w.va==limits_rules::item_clone_call.va+1;}),owner.writes.end());
 append(owner,std::move(c));definition_picker_copy=prepared_picker;why.clear();return true;
} catch(const std::exception& e){why=std::string("W2 items: ")+e.what();return false;}
}
