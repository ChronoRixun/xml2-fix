// Item-capacity union fixture: owner-supplied retail instructions execute in-process.
#include "item_capacity.hpp"
#include "item_capacity_rules.hpp"
#include "item_capacity_sites.hpp"
#include "w2_boot_probe.hpp"
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>
extern "C" std::uint8_t fixture_space[0x700000];
namespace save_format {unsigned refusals=0;void latch_reconstruction_failure(const char*){++refusals;}}
namespace logger {std::string last;void write(const char* fmt,...){char buf[512];va_list a;va_start(a,fmt);std::vsnprintf(buf,sizeof buf,fmt,a);va_end(a);last=buf;}}
namespace {
 int checks=0,failures=0;
 void check(bool v,const std::string& s){++checks;failures+=!v;std::printf("%s %s\n",v?"PASS":"FAIL",s.c_str());}
 void* at(std::uint32_t v){return reinterpret_cast<void*>(static_cast<std::uintptr_t>(v));}
 std::uint32_t addr(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
 template<class T>T read(const void* p,unsigned off=0){T v;std::memcpy(&v,static_cast<const std::uint8_t*>(p)+off,sizeof v);return v;}
 template<class T>void put(void* p,unsigned off,T v){std::memcpy(static_cast<std::uint8_t*>(p)+off,&v,sizeof v);}
 template<class T>T native(std::uint32_t v){return reinterpret_cast<T>(at(v));}
 void jump(std::uint32_t va,const void* target){auto w=patch_transaction::relocation(va,"e9",true,addr(target),0,"test external dependency");std::memcpy(at(va),w.bytes.data(),w.bytes.size());FlushInstructionCache(GetCurrentProcess(),at(va),w.bytes.size());}
 std::vector<std::uint8_t> binary;
 bool load_image(const char* path) {
  std::ifstream f(path,std::ios::binary);binary.assign(std::istreambuf_iterator<char>(f),{});
  if(binary.size()<0x1000)return false;
  const auto pe=read<std::uint32_t>(binary.data(),0x3c);
  const auto sections=pe+24+read<std::uint16_t>(binary.data(),pe+20);
  std::memset(fixture_space,0,sizeof fixture_space);
  for(unsigned i=0;i<read<std::uint16_t>(binary.data(),pe+6);++i) {
   const auto row=sections+i*40,va=0x400000+read<std::uint32_t>(binary.data(),row+12),size=read<std::uint32_t>(binary.data(),row+16),off=read<std::uint32_t>(binary.data(),row+20);
   if(off+std::uint64_t(size)>binary.size() || va<addr(fixture_space) || va+std::uint64_t(size)>addr(fixture_space)+sizeof fixture_space)return false;
   std::memcpy(at(va),binary.data()+off,size);
  }
  return true;
 }


 struct protected_buffer {
  void* allocation;std::uint8_t* data;unsigned size;
  protected_buffer(unsigned n):size(n){const auto pages=(n+4095)&~4095u;allocation=VirtualAlloc(nullptr,pages+4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD old;VirtualProtect(static_cast<std::uint8_t*>(allocation)+pages,4096,PAGE_NOACCESS,&old);data=static_cast<std::uint8_t*>(allocation)+pages-n;std::memset(data,0,n);}
  ~protected_buffer(){VirtualFree(allocation,0,MEM_RELEASE);}
 };
 unsigned rng_seed=1,rng_calls=0;int rng_low=0,rng_high=0;bool rng_flag=false;bool cycle=false;
 int __cdecl rng(int lo,int hi,bool flag){++rng_calls;rng_low=lo;rng_high=hi;rng_flag=flag;if(cycle)return int((rng_seed++)%unsigned(hi-lo+1))+lo;rng_seed=rng_seed*1664525u+1013904223u;return lo+int((rng_seed>>1)%unsigned(hi-lo+1));}
 int __fastcall level(void* p,void*){return read<int>(p,4);}
 bool __fastcall old_allowed(void* p,void*){return read<unsigned>(p,12)!=0;}
 unsigned* __fastcall key(void* p,void*,unsigned* out){*out=read<unsigned>(p,8);return out;}
 void* record_vt[24]{};
 void fill_records(void* m,unsigned offset,int n,int requested=30){for(int i=0;i<n;++i){auto* p=static_cast<std::uint8_t*>(m)+offset+i*0x28;put(p,0,addr(record_vt));put(p,4,requested+(i%7)-18);put(p,8,unsigned(i%9));put(p,12,unsigned(i%3!=0));}}
 using picker=int(__fastcall*)(void*,void*,int,unsigned,void*);

 struct chain_link{unsigned handle;void* owner;};
 void* duplicate_record=nullptr;chain_link end_link{};void* chain_element_vt[16]{};void* chain_element=chain_element_vt;
 bool __fastcall chain_valid(void*,void*,unsigned h){return h==1;}
 void* __fastcall chain_get(void*,void*,unsigned){return &chain_element;}
 void* __fastcall enhancement_get(void*,void*){return duplicate_record;}
 void* __fastcall chain_next(void*,void*){return &end_link;}
 void picker_differential(bool retail_rng){
  record_vt[0xc/4]=reinterpret_cast<void*>(&key);record_vt[0x1c/4]=reinterpret_cast<void*>(&level);record_vt[0x2c/4]=reinterpret_cast<void*>(&old_allowed);if(!retail_rng)jump(0x5601e0,&rng);
  bool same=true;unsigned cases=0;
  std::array<std::uint8_t,0x28> duplicate{};put(duplicate.data(),0,addr(record_vt));put(duplicate.data(),8,3u);duplicate_record=duplicate.data();
  void* chain_vt[3]{};chain_vt[0]=reinterpret_cast<void*>(&chain_valid);chain_vt[2]=reinterpret_cast<void*>(&chain_get);void** chain_owner=chain_vt;
  chain_element_vt[0x38/4]=reinterpret_cast<void*>(&enhancement_get);chain_element_vt[0x18/4]=reinterpret_cast<void*>(&chain_next);
  for(bool with_chain:{false,true})for(unsigned n:{0u,1u,10u,375u})for(bool prefix:{false,true}) {
   protected_buffer m(0x7a00);fill_records(m.data,0x2594,n);put(m.data,0x605c,prefix?0u:n);put(m.data,0x6060,prefix?n:0u);
   for(unsigned seed=0;seed<1000;++seed){rng_seed=seed;rng_calls=0;put(at(0x6e0874),0,seed+1);put(at(0x7ad8b4),0,0u);auto a=native<picker>(prefix?0x47e690:0x47ee90)(m.data,nullptr,int(seed%60),with_chain?1u:0u,with_chain?&chain_owner:nullptr);const auto end=retail_rng?read<unsigned>(at(0x6e0874)):rng_seed,calls=retail_rng?read<unsigned>(at(0x7ad8b4)):rng_calls;
    rng_seed=seed;rng_calls=0;put(at(0x6e0874),0,seed+1);put(at(0x7ad8b4),0,0u);auto b=item_capacity::pick(m.data,int(seed%60),with_chain?1u:0u,with_chain?&chain_owner:nullptr,prefix,0x2594);
    same&=a==b&&(retail_rng?read<unsigned>(at(0x6e0874)):rng_seed)==end&&(retail_rng?read<unsigned>(at(0x7ad8b4)):rng_calls)==calls&&(calls==0||(calls==1&&(retail_rng||(rng_low==0&&rng_high>=0&&rng_flag))));++cases;
   }
  }
  check(same,std::string("T6 picker differential: 16000 cases (0/1/10/375, both pickers, duplicate-key chains), identical picks/state/calls; ")+(retail_rng?"actual retail RNG":"recording RNG"));
 }
 void __cdecl instance_init(void* p){std::memset(p,0,0x34);}
 void __cdecl attached_init(void* p){std::memset(p,0,0x14);}
 void pool_tests(){
  using namespace item_capacity_rules;
  jump(0x483480,&instance_init);jump(0x45fa10,&attached_init);
  protected_buffer b(instances.size);auto* object=b.data;auto* pool=object+4;
  native<void*(__fastcall*)(void*,void*)>(0x483170)(pool,nullptr);
  bool okay=read<unsigned>(pool,instances.live)==0&&read<unsigned>(pool,instances.ring_count)==1024;
  std::vector<unsigned> handles;
  for(unsigned i=0;i<1024;++i){auto h=native<unsigned(__fastcall*)(void*,void*)>(0x483390)(object,nullptr);handles.push_back(h);okay&=(h&1023)==i&&h!=0&&native<bool(__fastcall*)(void*,void*,unsigned)>(0x4831f0)(object,nullptr,h);}
  check(okay&&read<unsigned>(pool,instances.live)==1024,"T7 allocate 1024 native instances, indices 0..1023 incl 255/256; all generations validate");
  check(native<unsigned(__fastcall*)(void*,void*)>(0x483390)(object,nullptr)==0,"T7 instance allocation 1025 refused at full count 1024");
  bool recycled=true;
  for(unsigned i:{0u,255u,256u,1023u}){native<void(__fastcall*)(void*,void*,unsigned)>(0x483270)(object,nullptr,handles[i]);recycled&=!native<bool(__fastcall*)(void*,void*,unsigned)>(0x4831f0)(object,nullptr,handles[i]);}
  for(unsigned i:{0u,255u,256u,1023u}){auto h=native<unsigned(__fastcall*)(void*,void*)>(0x483390)(object,nullptr);recycled&=(h&1023)==i&&h!=handles[i];}
  check(recycled&&read<unsigned>(pool,instances.live)==1024,"T7 native ring recycle preserves generation changes at 0/255/256/1023");
  native<void(__fastcall*)(void*,void*)>(0x482a20)(pool,nullptr);
  bool clear=true;for(unsigned i=0;i<32;++i)clear&=read<unsigned>(pool,instances.bitmap+i*4)==0;
  check(clear,"T7 native destructor walk scans 1024 and clears all 32 bitmap words at protected boundary");
  protected_buffer a(attached.size);auto* attached_object=a.data;auto* ap=attached_object+4;
  native<void*(__fastcall*)(void*,void*)>(0x45f6c0)(ap,nullptr);
  bool attached_ok=true;
  for(unsigned i=0;i<4096;++i){auto h=native<unsigned(__fastcall*)(void*,void*)>(0x45f920)(attached_object,nullptr);attached_ok&=(h&4095)==i&&h!=0;}
  check(attached_ok&&read<unsigned>(ap,attached.live)==4096,"T8 attached native allocations 4095/4096 succeed; indices include 255/256/1023/4095");
  check(native<unsigned(__fastcall*)(void*,void*)>(0x45f920)(attached_object,nullptr)==0,"T8 attached allocation 4097 returns full without writing beyond protected storage");
  native<void(__fastcall*)(void*,void*)>(0x45f2a0)(ap,nullptr);
  clear=true;for(unsigned i=0;i<128;++i)clear&=read<unsigned>(ap,attached.bitmap+i*4)==0;
  check(clear,"T8 native attached destructor scans 4096 and clears 128 words");
 }
 void large_pickers(){
  using namespace item_capacity_rules;protected_buffer m(manager_size);
  bool all=true;
  for(unsigned n:{376u,4096u})for(bool prefix:{false,true}){
   fill_records(m.data,item_capacity_rules::records,n); // qualified below to avoid name collision
   for(unsigned i=0;i<n;++i){put(m.data,item_capacity_rules::records+i*0x28+4,30);put(m.data,item_capacity_rules::records+i*0x28+8,0u);}
   put(m.data,0x605c,prefix?0u:n);put(m.data,0x6060,prefix?n:0u);
   std::set<int> seen;cycle=true;rng_seed=0;rng_calls=0;
   for(unsigned i=0;i<n;++i)seen.insert(native<picker>(prefix?0x47e690:0x47ee90)(m.data,nullptr,30,0,nullptr));
   all&=seen.size()==n&&*seen.begin()==0&&*seen.rbegin()==int(n-1)&&rng_calls==n;
  }
  cycle=false;check(all,"T6 installed full pickers cover every index for 376/4096 entries (8944 draws, both pickers), one RNG call per draw");
 }

 std::uint8_t* current_manager=nullptr;
 void* __cdecl get_manager(){return current_manager;}
 int __cdecl compare_names(const char* a,const char* b){return std::strcmp(a,b);}
 std::array<std::uint8_t,0x18000> names;
 void* __cdecl get_names(){return names.data();}
 unsigned* __fastcall definition_name(void* p,void*,unsigned* out){*out=read<unsigned>(p,4);return out;}
 int __fastcall zero0(void*,void*){return 0;}
 int __fastcall zero1(void*,void*,unsigned){return 0;}
 bool __fastcall yes1(void*,void*,unsigned){return true;}
 bool __fastcall no1(void*,void*,unsigned){return false;}
 float __cdecl random_zero(float,float,bool){return 0.0f;}
 void* definition_vt[25]{};void* manager_vt[80]{};
 struct stream {std::vector<std::uint8_t> bytes;unsigned cursor=0;};
 bool __fastcall write_stream(void* p,void*,const void* data,unsigned n){auto& s=*static_cast<stream*>(p);const auto* b=static_cast<const std::uint8_t*>(data);s.bytes.insert(s.bytes.end(),b,b+n);return true;}
 bool __fastcall read_stream(void* p,void*,void* data,unsigned n){auto& s=*static_cast<stream*>(p);if(s.cursor+n>s.bytes.size()){std::memset(data,0,n);return false;}std::memcpy(data,s.bytes.data()+s.cursor,n);s.cursor+=n;return true;}
 void* last_definition=nullptr;
 unsigned __fastcall observe_definition0(void* p,void*){last_definition=p;return 1;}
 unsigned __fastcall observe_definition1(void* p,void*,unsigned){last_definition=p;return 1;}

 __declspec(naked) void saved_header(){__asm {
 pop edi
 pop esi
 pop ebp
 pop ebx
 add esp,30h
 ret 4
 }}
 __declspec(naked) void loaded_header(){__asm {
 pop edi
 pop esi
 pop ebp
 pop ebx
 add esp,90h
 ret 4
 }}
 unsigned __fastcall zero2(void*,void*,unsigned,unsigned){return 0;}
 void shared_ring_consumers(){
  struct consumer {unsigned entry,ring,live,size;const char* name;};
  for(const auto& s:std::initializer_list<consumer>{{0x4c7050,0x404,0x834,0xc40,"4c7050"},{0x5421c0,0x8820,0x8c50,0x905c,"5421c0"},{0x58a1d0,0x820,0xc50,0x105c,"58a1d0"}}){
   // Padding makes the historical out-of-owner writes observable without an
   // AV ending the fixture before it can identify all three foreign callers.
   protected_buffer p(s.size+0x1000);std::memset(p.data+s.size,0xa5,0x1000);
   for(unsigned i=0;i<256;++i)put(p.data,s.ring+4*i,i);
   put(p.data,s.ring+0x408,0u);put(p.data,s.ring+0x40c,256u);
   const auto first=native<unsigned(__fastcall*)(void*,void*)>(s.entry)(p.data,nullptr);
   const auto second=native<unsigned(__fastcall*)(void*,void*)>(s.entry)(p.data,nullptr);
   bool canary=true;for(unsigned i=0;i<0x1000;++i)canary&=p.data[s.size+i]==0xa5;
   check(first==0&&second==1&&read<unsigned>(p.data,s.ring+0x408)==2&&read<unsigned>(p.data,s.ring+0x40c)==254&&canary,
         std::string("shared 256-entry ring: foreign allocator ")+s.name+" advances distinct slots and preserves its allocation boundary");
  }
 }
 // Boot coverage: the older definition tests seeded the relocated array by
 // hand and never executed the manager constructor or XML registration loop.
 struct boot_node {unsigned unused[3]{};int fields=1;unsigned key=1,value=0;};
 std::vector<boot_node> boot_nodes;
 boot_node empty_document{};
 void* selected_document=nullptr;
 unsigned boot_manager_vt[80]{},boot_resource_vt[2]{};
 unsigned* boot_resource=boot_resource_vt;
 void* __cdecl boot_resource_get(){return &boot_resource;}
 void* __fastcall boot_root(void*,void*,const char*){return selected_document;}
 void* __fastcall boot_next_root(void*,void*,const char*){return nullptr;}
 void* __fastcall boot_child(void* document,void*,const char*,void* previous){
  if(document==&empty_document)return nullptr;
  if(!previous)return boot_nodes.data();
  auto* p=static_cast<boot_node*>(previous)+1;
  return p<boot_nodes.data()+boot_nodes.size()?p:nullptr;
 }
 void* __fastcall boot_no_child(void*,void*,const char*){return nullptr;}
 unsigned* __fastcall boot_pair(void*,void*,unsigned* out,unsigned){out[0]=out[1]=0;return out;}
 void __stdcall boot_array(void* p,unsigned stride,unsigned count,unsigned construct,unsigned){
  for(unsigned i=0;i<count;++i){auto* record=static_cast<std::uint8_t*>(p)+i*stride;
   native<void*(__fastcall*)(void*,void*)>(construct)(record,nullptr);
   // Keep the retail definition vtable and name parser.
  }
 }
 void boot_registration(){
  using namespace item_capacity_rules;
  const auto saved=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);
  protected_buffer m(manager_size);current_manager=m.data;
  std::memcpy(boot_manager_vt,at(0x688314),sizeof boot_manager_vt);
  boot_manager_vt[0xfc/4]=addr(&zero2);boot_manager_vt[0x100/4]=addr(&zero1);
  boot_manager_vt[0x12c/4]=addr(&boot_pair);boot_resource_vt[1]=addr(&zero2);
  jump(0x6722d4,&boot_array);jump(0x541080,&zero0);
  jump(0x47dd10,&zero0);jump(0x57d750,&zero0);jump(0x47f410,&zero0);
  jump(0x480290,&zero0);
  native<void*(__fastcall*)(void*,void*)>(0x480700)(m.data,nullptr);
  std::memcpy(at(0x480290),saved.data()+0x480290-addr(fixture_space),5);
  put(m.data,0,addr(boot_manager_vt));
  jump(0x563070,&boot_resource_get);jump(0x47b4e0,&zero0);
  jump(0x564ac0,&boot_root);jump(0x564a40,&boot_next_root);
  jump(0x564cb0,&boot_child);jump(0x564c70,&boot_no_child);
  for(unsigned a:{0x47c9e0u,0x47e820u,0x47f230u,0x47cc20u})jump(a,&yes1);
  jump(0x47f4f0,&zero1);jump(0x47ce80,&zero0);
  jump(0x602120,&std::strlen);jump(0x672562,&compare_names);
  for(unsigned count:{1u,185u,256u,1024u}){
   boot_nodes.assign(count,{});
   for(unsigned i=0;i<count;++i){boot_nodes[i].key=0x681d90;boot_nodes[i].value=addr(names.data()+0x8008+i*12);
    std::snprintf(reinterpret_cast<char*>(names.data())+0x8008+i*12,12,i?"item%04u":"MONEY1",i);}
   w2_boot_probe::cached_input input(boot_nodes.data(),unsigned(boot_nodes.size()*sizeof(boot_node)),&empty_document,sizeof empty_document);
   selected_document=input.first_buffer();
   native<unsigned(__fastcall*)(void*,void*)>(0x480290)(m.data,nullptr);
   const auto found=native<unsigned(__fastcall*)(void*,void*,const char*)>(0x47e320)(m.data,nullptr,"MONEY1");
   std::printf("boot registered %u: count=%u MONEY1=%08x\n",count,read<unsigned short>(m.data,0x2588),found);
   check(read<unsigned short>(m.data,0x2588)==count&&found==0,"boot constructor/registration/name lookup, count "+std::to_string(count));
   check(w2_boot_probe::release_cached_inputs(input,boot_nodes.data(),&empty_document),"menu file-buffer release frees each source once, count "+std::to_string(count));
  }
  std::memcpy(fixture_space,saved.data(),saved.size());FlushInstructionCache(GetCurrentProcess(),fixture_space,sizeof fixture_space);
 }
 __declspec(naked) void reward_done(){__asm {
 pop esi
 ret
 }}
 __declspec(naked) void reward_none(){__asm {
 xor eax,eax
 pop esi
 ret
 }}
 __declspec(naked) void* __cdecl reward_lookup(void*,unsigned){__asm {
 push esi
 mov esi,[esp+8]
 cmp dword ptr [esp+0ch],0
 jne second
 push 4c8fd2h
 ret
second:
 push 4ca129h
 ret
 }}

 void definition_tests(){
  using namespace item_capacity_rules;protected_buffer m(manager_size);current_manager=m.data;
  // Use the actual item-manager vtable; override only environment-owned edges
  // for definition picker filtering and name-table / stream I/O.
  std::memcpy(manager_vt,at(0x688314),sizeof manager_vt);put(m.data,0,addr(manager_vt));
  definition_vt[0x1c/4]=reinterpret_cast<void*>(&definition_name);
  jump(0x602140,&get_names);jump(0x672562,&compare_names);jump(0x480a00,&get_manager);
  jump(0x481170,&zero0); // post-load item stat recalculation is an environment edge
  jump(0x4688f0,&write_stream);jump(0x468940,&read_stream);
  for(unsigned i=0;i<1024;++i){auto* d=m.data+definitions+i*0x3c;native<void*(__fastcall*)(void*,void*)>(0x47b8f0)(d,nullptr);put(d,0,addr(definition_vt));put(d,4,i+1);const unsigned offset=i*12;put(names.data(),4+(i+1)*4,offset);std::snprintf(reinterpret_cast<char*>(names.data())+0x8008+offset,12,"item%04u",i);}
  bool okay=true;
  for(unsigned count:{255u,256u,1023u,1024u}){
   put(m.data,0x2588,std::uint16_t(count));
   okay&=native<unsigned(__fastcall*)(void*,void*)>(0x47b4d0)(m.data,nullptr)==count;
   for(unsigned index:{0u,254u,255u,256u,300u,1022u,1023u}){
    const auto p=native<void*(__fastcall*)(void*,void*,unsigned)>(0x47beb0)(m.data,nullptr,index);
    okay&=p==(index<count?m.data+definitions+index*0x3c:nullptr);
    if(p)okay&=native<unsigned(__fastcall*)(void*,void*,void*)>(0x47c250)(m.data,nullptr,p)==index;
    char name[16];std::snprintf(name,sizeof name,"item%04u",index);
    auto found=native<unsigned(__fastcall*)(void*,void*,const char*)>(0x47e320)(m.data,nullptr,name);
    okay&=index<count?found==index:found==0xffffffff;
   }
  }
  check(okay,"T5 native definition count/accessor/pointer-index/name lookup at counts 255/256/1023/1024 and indices 255/256/300/1023");
  for(auto& entry:definition_vt)entry=reinterpret_cast<void*>(&observe_definition0);
  definition_vt[0x58/4]=reinterpret_cast<void*>(&observe_definition1);definition_vt[0x5c/4]=reinterpret_cast<void*>(&observe_definition1);
  okay=true;
  for(unsigned index:{255u,256u,1023u})for(unsigned thunk:{0x47c220u,0x47c280u,0x47c2b0u,0x47c2e0u,0x47c310u,0x47c340u,0x47c370u,0x47c3a0u,0x47c3d0u,0x47c400u,0x47c430u,0x47c460u,0x47c490u}){
   last_definition=nullptr;
   if(thunk==0x47c310||thunk==0x47c490)native<unsigned(__fastcall*)(void*,void*,unsigned,unsigned)>(thunk)(m.data,nullptr,index,1);
   else native<unsigned(__fastcall*)(void*,void*,unsigned)>(thunk)(m.data,nullptr,index);
   okay&=last_definition==m.data+definitions+index*0x3c;
  }
  check(okay,"T5 all 13 forwarding accessor thunks select the relocated definition at 255/256/1023 (39 native calls)");
  definition_vt[0x1c/4]=reinterpret_cast<void*>(&definition_name);

  manager_vt[0x124/4]=reinterpret_cast<void*>(&zero0);
  manager_vt[0x84/4]=reinterpret_cast<void*>(&zero1);
  manager_vt[0xbc/4]=reinterpret_cast<void*>(&yes1);
  manager_vt[0xc0/4]=reinterpret_cast<void*>(&no1);
  jump(0x5601b0,&random_zero);
  okay=true;
  for(unsigned count:{255u,256u,1023u,1024u}){put(m.data,0x2588,std::uint16_t(count));auto result=native<int(__fastcall*)(void*,void*,int,int,int)>(0x47b590)(m.data,nullptr,0,0,4);std::printf("definition picker count %u -> %d\n",count,result);okay&=result==int(count-1);}
  check(okay,"T5 full copied definition picker reaches indices 254/255/1022/1023 without truncating or looping");
  // Per-instance save/load executes the retail list serializers too (all three
  // lists empty in this fixture); arbitrary name/special/location bytes survive.
  okay=true;
  for(unsigned index:{255u,256u,300u,1023u}){
   std::array<std::uint8_t,0x34> instance{},loaded{};put(instance.data(),4,addr(m.data+definitions+index*0x3c));
   instance[0x2c]=0x12;instance[0x2d]=0xff;instance[0x2e]=3;instance[0x2f]=0x27;
   stream output;native<void(__fastcall*)(void*,void*,void*)>(0x481610)(instance.data(),nullptr,&output);
   std::printf("instance stream index %u size %zu\n",index,output.bytes.size());
   okay&=output.bytes.size()==18&&read<std::uint16_t>(output.bytes.data())==index;
   native<void(__fastcall*)(void*,void*,void*)>(0x482b40)(loaded.data(),nullptr,&output);
   okay&=read<unsigned>(loaded.data(),4)==read<unsigned>(instance.data(),4)&&!std::memcmp(instance.data()+0x2c,loaded.data()+0x2c,4)&&output.cursor==output.bytes.size();
  }
  check(okay,"T5 retail instance save/load round trips u16 definition 255/256/300/1023 with exact 18-byte empty-list stream");
  // Stop immediately after the header: the later carried/world lists have
  // separate executed capacity tests. Both native stream helpers are observed.
  jump(0x47c53f,&saved_header);jump(0x47f82c,&loaded_header);
  jump(0x541080,&zero0);jump(0x47dd10,&zero0);
  manager_vt[0xfc/4]=reinterpret_cast<void*>(&zero2);manager_vt[0x100/4]=reinterpret_cast<void*>(&zero1);
  put(m.data,0x2588,std::uint16_t(1023));std::memset(m.data+states,0,2048);stream header;
  native<void(__fastcall*)(void*,void*,void*)>(0x47c4c0)(m.data,nullptr,&header);
  okay=header.bytes.size()==0x802&&read<std::uint16_t>(header.bytes.data())==1023&&read<std::uint16_t>(header.bytes.data(),2+1023*2)==0;
  const auto expected=std::vector<std::uint8_t>(m.data+states,m.data+states+2048);std::memset(m.data+states,0xa5,2048);
  native<void(__fastcall*)(void*,void*,void*)>(0x47f7a0)(m.data,nullptr,&header);
  okay&=header.cursor==header.bytes.size()&&std::equal(expected.begin(),expected.end(),m.data+states);
  check(okay,"T5 native manager header save/load: count 1023, 1024 state pairs, exact 0x802 bytes, zero state beyond live count");

  // Reward constructor must preserve the pre-existing u16 at reward+0xa.
  std::array<std::uint8_t,0x50> course;course.fill(0xa5);
  native<void*(__fastcall*)(void*,void*)>(0x4c8910)(course.data(),nullptr);
  okay=true;for(unsigned base:{4u,0x18u,0x2cu})okay&=read<std::uint16_t>(course.data(),base+8)==0xffff&&read<std::uint16_t>(course.data(),base+0xa)==0;
  check(okay,"T5 all three Danger Room reward constructors initialize u16 none at +8 and preserve u16 +0xa");
  jump(0x4c8fea,&reward_done);jump(0x4ca141,&reward_done);jump(0x4c904e,&reward_none);jump(0x4ca178,&reward_none);
  // MSVC infers register use from a direct naked call. Its hidden push/ret
  // transfer runs retail code that clobbers ECX/EDX; an indirect volatile call
  // preserves the normal ABI model instead of keeping a loop iterator in ECX.
  void*(__cdecl* volatile call_reward)(void*,unsigned)=&reward_lookup;
  okay=true;std::array<std::uint8_t,0x14> reward{};put(m.data,0x2588,std::uint16_t(1024));put(reward.data(),0xa,std::uint16_t(0x4321));
  for(unsigned index:{255u,256u,300u,1023u,65535u})for(unsigned consumer:{0u,1u}){put(reward.data(),8,std::uint16_t(index));const auto* result=call_reward(reward.data(),consumer);okay&=result==(index==65535?nullptr:m.data+definitions+index*0x3c)&&read<std::uint16_t>(reward.data(),0xa)==0x4321;}
  check(okay,"T5 both retail Danger Room reward sentinel/reader paths resolve 255/256/300/1023 and 0xffff, preserving +0xa");

  // Own-enhancement 255 is now usable; 0xffff alone is none.
  std::array<std::uint8_t,0x3c> d{};put(d.data(),0x24,std::uint16_t(255));put(d.data(),0x26,std::uint16_t(4095));put(d.data(),0x28,std::uint16_t(0xffff));
  okay=(native<unsigned(__fastcall*)(void*,void*,int)>(0x47ad10)(d.data(),nullptr,0)&0xffff)==255;
  okay&=(native<unsigned(__fastcall*)(void*,void*,int)>(0x47ad10)(d.data(),nullptr,1)&0xffff)==4095;
  okay&=(native<unsigned(__fastcall*)(void*,void*,int)>(0x47ad10)(d.data(),nullptr,2)&0xffff)==0xffff;
  check(okay,"T6 native own-enhancement accessor accepts 255 and 4095; only 0xffff is none");
 }
 bool __fastcall parent_save(void*,void*,void*){return true;}
 bool __fastcall slot_lookup(void*,void*,unsigned,void*){return false;}
 void* __fastcall vector_construct(void* p,void*,float,float,float){return p;}
 void __fastcall set_vector(void*,void*,void*){}
 bool __fastcall parent_restore(void*,void*,void*){return true;}
 void entity_slot_tests(){
  protected_buffer m(item_capacity_rules::manager_size);current_manager=m.data;
  std::memset(manager_vt,0,sizeof manager_vt);manager_vt[0x120/4]=reinterpret_cast<void*>(&slot_lookup);put(m.data,0,addr(manager_vt));
  jump(0x496930,&parent_save);jump(0x496db0,&parent_restore);put(at(0x67f9c8),0,addr(&vector_construct));
  void* entity_vt[12]{};entity_vt[0x2c/4]=reinterpret_cast<void*>(&set_vector);
  bool all=true;
  for(unsigned slot:{0u,255u,256u,300u,1023u,65535u}){
   std::array<std::uint8_t,0x330> entity{},restored{};std::array<std::uint8_t,0x20> record{};
   put(entity.data(),0,addr(entity_vt));put(restored.data(),0,addr(entity_vt));put(entity.data(),0x320,std::uint16_t(slot));put(entity.data(),0x322,std::uint16_t(0x1234));
   native<bool(__fastcall*)(void*,void*,void*)>(0x479a70)(entity.data(),nullptr,record.data());
   all&=record[8]==0&&read<std::uint16_t>(record.data(),0x12)==slot&&read<std::uint16_t>(record.data(),9)==0x1234;
   native<bool(__fastcall*)(void*,void*,void*)>(0x479fc0)(restored.data(),nullptr,record.data());
   all&=read<std::uint16_t>(restored.data(),0x320)==slot&&read<std::uint16_t>(restored.data(),0x322)==0x1234;
  }
  check(all,"T7 retail entity save/restore round trips 0/255/256/300/1023/0xffff through record+0x12, payload byte zero, quantity unchanged");
 }

 void helper_differential(){
  const auto clone=0x482a8e+read<unsigned>(at(0x482a8a));
  std::array<unsigned,32> bitmap{};unsigned random=1;bool same=true;unsigned cases=0;
  using scan=int(__fastcall*)(void*,void*,int,bool);
  for(unsigned round=0;round<64;++round){for(unsigned i=0;i<8;++i){random=random*1664525u+1013904223u;bitmap[i]=random;}std::fill(bitmap.begin()+8,bitmap.end(),0u);
   for(int start=0;start<256;++start)for(bool used:{false,true}){const auto a=native<scan>(0x44d0d0)(bitmap.data(),nullptr,start,used);const auto b=native<scan>(clone)(bitmap.data(),nullptr,start,used);same&=a==(std::min)(b,256);++cases;}
  }
  check(same,"T7 private 1024-bit scan vs untouched retail helper: 32768 comparisons at the 256-bit domain");
 }
 void* dummy_item_vt[4]{};void* dummy_item=dummy_item_vt;
 void* __fastcall get_dummy(void*,void*,unsigned){return &dummy_item;}
 void* __fastcall dummy_definition(void*,void*){return &dummy_item;}
 void world_and_preflight(){
  using namespace item_capacity_rules;protected_buffer m(manager_size),ip(instances.size),ap(attached.size);
  current_manager=m.data;
  void* handle_vt[3]{};handle_vt[0]=reinterpret_cast<void*>(&yes1);handle_vt[2]=reinterpret_cast<void*>(&get_dummy);void** handle_owner=handle_vt;dummy_item_vt[2]=reinterpret_cast<void*>(&dummy_definition);
  bool okay=true;unsigned slot=0;
  for(unsigned h=1;h<=1024;++h){const bool result=native<bool(__fastcall*)(void*,void*,unsigned,void*,unsigned*)>(0x47e930)(m.data,nullptr,h,&handle_owner,&slot);okay&=result&&slot==h-1;}
  check(okay&&slot==1023,"T7 native world-slot allocation reaches all 1024 slots including 255/256/1023");
  const auto prior=std::vector<std::uint8_t>(m.data,m.data+manager_size);
  check(!native<bool(__fastcall*)(void*,void*,unsigned,void*,unsigned*)>(0x47e930)(m.data,nullptr,1025,&handle_owner,&slot)&&std::equal(prior.begin(),prior.end(),m.data),"T7 world-slot allocation 1025 refuses without mutating full table");
  check(logger::last.find("world-item allocation refused: capacity 1024")!=std::string::npos,"T7 native world-slot exhaustion reports the named limit");
  // Same production preflight as save_format, reading actual engine globals.
  put(at(0x72a514),0,addr(m.data));put(at(0x72a524),0,addr(ip.data));put(at(0x717b30),0,addr(ap.data));
  put(ip.data,instances.live+4,1023u);put(ap.data,attached.live+4,4095u);std::string error;
  check(item_capacity::preflight(1,1,0,lcsave::w2_layout,error),"T8 LC preflight accepts final free instance/attached entries at actual W2 offsets");
  check(!item_capacity::preflight(2,1,0,lcsave::w2_layout,error)&&error.find("instance free capacity (1024)")!=std::string::npos,"T7 LC preflight refuses insufficient instances by capacity name");
  check(!item_capacity::preflight(1,2,0,lcsave::w2_layout,error)&&error.find("attached-enhancement free capacity (4096)")!=std::string::npos,"T8 LC preflight refuses insufficient attached entries by capacity name");
  check(!item_capacity::preflight(1,1,1,lcsave::w2_layout,error)&&error.find("world-item free capacity (1024)")!=std::string::npos,"T7 LC preflight refuses world allocation while all 1024 slots are occupied");
  native<void(__fastcall*)(void*,void*)>(0x47db70)(m.data+world,nullptr);
  okay=true;for(unsigned i=0;i<32;++i)okay&=read<unsigned>(m.data,world_bitmap+i*4)==0;
  check(okay&&item_capacity::preflight(1,1,1024,lcsave::w2_layout,error),"T7 native world-slot destructor clears all 32 words; preflight now admits count 1024");
  put(ip.data,0x3854,255u);put(ap.data,0x239c,374u);
  check(item_capacity::preflight(1,1,0,lcsave::e10a2_layout,error)&&!item_capacity::preflight(2,1,0,lcsave::e10a2_layout,error),"T8 E10A2 preflight still reads retail count offsets/capacities 256/375");
 }
 void thousand_drops(){
  using namespace item_capacity_rules;protected_buffer m(manager_size);
  fill_records(m.data,item_capacity_rules::records,375);for(unsigned i=0;i<375;++i){put(m.data,item_capacity_rules::records+i*0x28+4,30);put(m.data,item_capacity_rules::records+i*0x28+8,0u);}
  put(m.data,0x605c,187u);put(m.data,0x6060,188u);
  std::set<int> seen;cycle=true;
  for(unsigned drop=0;drop<1000;++drop){rng_seed=drop;seen.insert(native<picker>(0x47e690)(m.data,nullptr,30,0,nullptr));rng_seed=drop;seen.insert(native<picker>(0x47ee90)(m.data,nullptr,30,0,nullptr));}
  cycle=false;check(seen.size()==375&&*seen.begin()==0&&*seen.rbegin()==374,"T6 1000-drop workload (2000 native picker calls) covers all 375 fixture affix IDs, both ranges");
 }

 unsigned __cdecl accepted_registration(){return 1;}
 unsigned __cdecl refused_registration(){return 0;}
 void registration_tests(){
  struct site{unsigned compare,accepted,refused,capacity;const char* name;};
  for(const auto& site:std::initializer_list<site>{{0x480430,0x48043e,0x4806cc,1024,"item-definition"},{0x47ca46,0x47ca51,0x47cc01,4096,"enhancement-record"},{0x47cb3f,0x47cb4a,0x47cc01,4096,"enhancement-record"},{0x47cd46,0x47cd51,0x47ce5b,4096,"enhancement-record"},{0x4804a6,0x4804b1,0x4806cc,4096,"enhancement-record"}}){
   jump(site.accepted,&accepted_registration);jump(site.refused,&refused_registration);
   // Authored ABI caller: next index in EAX and at native ESP+0x1c.
   std::array<std::uint8_t,22> code{{0x83,0xec,0x20,0x8b,0x44,0x24,0x24,0x89,0x44,0x24,0x18,0xba,0,0,0,0,0xff,0xd2,0x83,0xc4,0x20,0xc3}};
   put(code.data(),12,site.compare);auto* memory=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);std::memcpy(memory,code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),memory,code.size());
   const auto probe=reinterpret_cast<unsigned(__cdecl*)(unsigned)>(memory);bool okay=true;
   for(unsigned index:{0u,255u,256u,site.capacity-1})okay&=probe(index)==1;
   logger::last.clear();const auto before=save_format::refusals;okay&=probe(site.capacity)==0;
   okay&=logger::last.find(std::string(site.name)+" registration refused: capacity "+std::to_string(site.capacity))!=std::string::npos&&save_format::refusals==before+1;
   check(okay,std::string("T9b native registration boundary at ")+patch_transaction::hex(site.compare)+": largest index accepted; count+1 refused by name");VirtualFree(memory,0,MEM_RELEASE);
  }
  using namespace item_capacity_rules;protected_buffer m(manager_size);bool okay=true;
  for(unsigned index:{255u,256u,1023u,4095u}){put(m.data,enhancement_bitmap+(index/32)*4,1u<<(index%32));const auto* record=native<void*(__fastcall*)(void*,void*,unsigned)>(0x47bad0)(m.data,nullptr,index);okay&=record==m.data+records+index*0x28;}
  okay&=native<void*(__fastcall*)(void*,void*,unsigned)>(0x47bad0)(m.data,nullptr,4096)==nullptr;
  check(okay,"T9b native enhancement record lookup accepts 255/256/1023/4095 and refuses index4096");
 }

}
LONG WINAPI fault(EXCEPTION_POINTERS* e){std::printf("FAULT %08lx EIP=%08lx ESP=%08lx EAX=%08lx ECX=%08lx EDX=%08lx\n",e->ExceptionRecord->ExceptionCode,e->ContextRecord->Eip,e->ContextRecord->Esp,e->ContextRecord->Eax,e->ContextRecord->Ecx,e->ContextRecord->Edx);std::printf("EDI=%08lx ESI=%08lx EBP=%08lx\n",e->ContextRecord->Edi,e->ContextRecord->Esi,e->ContextRecord->Ebp);return EXCEPTION_CONTINUE_SEARCH;}


int main(int argc,char** argv) {

 AddVectoredExceptionHandler(1,&fault);
 std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");if(!path){std::puts("SKIP: retail exe required");return 2;}
 DWORD old=0;VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old);
 check(addr(fixture_space)==0x401000&&load_image(path),"mapped retail image at its own addresses; no game launched");
 picker_differential(true);picker_differential(false);
 patch_transaction::contribution c;std::string why;
 const bool prepared=item_capacity::contribute_w2(c,why);check(prepared,"all item ledger bytes and region hashes: "+why);
 if(c.writes.empty())return 1;
 const auto stock=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);
 check(patch_transaction::verify_expectations(c,why),"complete generated guards verified: "+why);
 {const auto saved=*static_cast<std::uint8_t*>(at(0x47e37e));*static_cast<std::uint8_t*>(at(0x47e37e))^=1;
  const auto mutated=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction bad;bad.add(c);
  check((bad.commit()==patch_transaction::outcome::refused_guard||bad.result()==patch_transaction::outcome::refused_region)&&std::equal(mutated.begin(),mutated.end(),fixture_space),"T3 copied lookup owner mutated outside entry: entire item union refuses with zero writes");*static_cast<std::uint8_t*>(at(0x47e37e))=saved;
 }
 {const auto saved=*static_cast<std::uint8_t*>(at(0x4830be));*static_cast<std::uint8_t*>(at(0x4830be))^=1;
  const auto mutated=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction bad;bad.add(c);
  check(bad.commit()==patch_transaction::outcome::refused_guard&&std::equal(mutated.begin(),mutated.end(),fixture_space),"T3 omitted instance-ring guard mutated: entire union refuses with zero writes");*static_cast<std::uint8_t*>(at(0x4830be))=saved;
 }
 {const auto saved=*static_cast<std::uint8_t*>(at(0x45e611));*static_cast<std::uint8_t*>(at(0x45e611))^=1;
  const auto mutated=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction bad;bad.add(c);
  check(bad.commit()==patch_transaction::outcome::refused_region&&std::equal(mutated.begin(),mutated.end(),fixture_space),"T3 shared-helper body mutated outside row guards: region hash refuses entire union, zero writes");*static_cast<std::uint8_t*>(at(0x45e611))=saved;
 }

 {const auto saved=*static_cast<std::uint8_t*>(at(0x479a89));*static_cast<std::uint8_t*>(at(0x479a89))^=1;
  const auto mutated=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction bad;bad.add(c);
  check(bad.commit()==patch_transaction::outcome::refused_region&&std::equal(mutated.begin(),mutated.end(),fixture_space),"T3 inventory serializer argument outside hook mutated: whole union refused by owner hash");*static_cast<std::uint8_t*>(at(0x479a89))=saved;
 }

 {const auto saved=*static_cast<std::uint8_t*>(at(0x4806e0));*static_cast<std::uint8_t*>(at(0x4806e0))^=1;
  const auto mutated=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction bad;bad.add(c);
  check(bad.commit()==patch_transaction::outcome::refused_region&&std::equal(mutated.begin(),mutated.end(),fixture_space),"T3 native registrar cleanup ret mutated: entire union refused by complete owner hash");*static_cast<std::uint8_t*>(at(0x4806e0))=saved;
 }
 {const auto saved=*static_cast<std::uint8_t*>(at(0x483434));*static_cast<std::uint8_t*>(at(0x483434))^=1;
  const auto mutated=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction bad;bad.add(c);
  check(bad.commit()==patch_transaction::outcome::refused_region&&std::equal(mutated.begin(),mutated.end(),fixture_space),"shared ring body mutation outside operand guards refuses the whole union with zero writes");*static_cast<std::uint8_t*>(at(0x483434))=saved;
 }
 patch_transaction::transaction tx;tx.add(std::move(c));const auto result=tx.commit();
 check(result==patch_transaction::outcome::committed,"W2 item union commits: "+tx.report());
 if(result!=patch_transaction::outcome::committed)return 1;
 check(!std::memcmp(at(0x483410),stock.data()+0x483410-addr(fixture_space),0x2c),"shared 256-entry ring body remains byte-identical to retail");
 const bool legacy_ring=argc>2&&std::string_view(argv[2])=="--legacy-shared-ring";
 if(legacy_ring){
  // Replay exactly the six in-place writes shipped by W2 a27495f/b603cc8.
  for(unsigned a:{0x483412u,0x483419u,0x48342cu})put(at(a),0,0x1008u);
  for(unsigned a:{0x483424u,0x483437u})put(at(a),0,0x100cu);
  put(at(0x48341e),0,1024u);
  const auto old_call=patch_transaction::bytes_of("e812000000");std::memcpy(at(0x4833f9),old_call.data(),old_call.size());
  FlushInstructionCache(GetCurrentProcess(),at(0x4833f9),0x43);
 }
 if(argc>2&&!legacy_ring){
  const std::string prefix=argv[2];std::ofstream text(prefix+"-text.bin",std::ios::binary);text.write(static_cast<const char*>(at(0x401000)),0x27d7b5);
  std::ofstream manifest(prefix+"-caves.tsv");
  for(const auto& b:item_capacity_sites::blocks){const auto base=b.lo+5+read<unsigned>(at(b.lo+1));int size=int(b.hi-b.lo)+5;for(const auto& e:b.edits)size+=int(patch_transaction::bytes_of(e.value).size())-int(e.size);
   const auto dump_path=prefix+"-code-"+patch_transaction::hex(b.lo)+".bin";std::ofstream dump(dump_path,std::ios::binary);dump.write(static_cast<const char*>(at(base)),size);manifest<<std::hex<<b.lo<<'\t'<<b.hi<<'\t'<<base<<'\t'<<size<<'\t'<<dump_path<<'\n';
  }
 }

 const auto instance=native<void*(__cdecl*)()>(0x483610)(),attached=native<void*(__cdecl*)()>(0x45fb00)();
 check(instance&&attached&&instance==native<void*(__cdecl*)()>(0x483610)()&&attached==native<void*(__cdecl*)()>(0x45fb00)()&&item_capacity::installed_capacity()==4096,"T7/T8 DLL-owned protected pools initialized once through retail getters only after publication");
 shared_ring_consumers();boot_registration();helper_differential();pool_tests();large_pickers();thousand_drops();definition_tests();entity_slot_tests();world_and_preflight();registration_tests();
 *static_cast<std::uint8_t*>(at(0x481a02))=0xc3;const auto refusals=save_format::refusals;
 native<void(__fastcall*)(void*,void*)>(0x481938)(nullptr,nullptr);
 check(save_format::refusals==refusals+1&&logger::last.find("attached-enhancement allocation refused: capacity 4096")!=std::string::npos,"T8 actual attachment null-handle branch logs 4096 and latches failed reconstruction");
 std::printf("Item capacity union: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
}
