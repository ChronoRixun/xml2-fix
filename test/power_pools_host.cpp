// Execute the owned retail pool methods on synthetic owners. The game's entry
// point and engine are never started. Only heap, atexit and payload services
// are replaced; admission, rings, bitmaps, generations and teardown stay native.
#include "power_pools.hpp"
#include "power_pool_plan.hpp"
#include "pool_counter_sites.hpp"
#include "pool_counter_observers.hpp"
#include "gameplay_capabilities.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

extern "C" unsigned char union_space[];
using namespace power_pool_layout;
using bytes=std::vector<std::uint8_t>;
static unsigned checks=0,failures=0,registrations=0,refusals=0;
static bool startup_refused=false;
static std::string mode="suite";
static gameplay_capabilities::record agreement;
static void check(bool ok,const char* what) {++checks;failures+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",what);std::fflush(stdout);}
static u32 address(const void* p) {return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
static void* ptr(u32 va) {return reinterpret_cast<void*>(va);}
static u32& word(void* p,u32 off=0) {return *reinterpret_cast<u32*>(static_cast<std::uint8_t*>(p)+off);}
namespace logger {void write(const char* fmt,...) {va_list a;va_start(a,fmt);std::vprintf(fmt,a);std::puts("");va_end(a);}}
namespace game_version {void refuse_startup() {startup_refused=true;}}
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t* key) {
 if(mode=="off" || mode=="base")return std::nullopt;
 if(mode=="attached" && std::wstring_view(key)==L"VFNodes")return std::nullopt;
 if(mode=="vf" && std::wstring_view(key)==L"AttachedPowerups")return std::nullopt;
 return mode=="invalid"?"128":"256";
}
}
namespace gameplay_agreement {
bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<u32,4> values,bool verified) {
 ++registrations;return agreement.add(id,revision,values,verified);
}
}
static bytes read(const char* path) {std::ifstream f(path,std::ios::binary);return bytes(std::istreambuf_iterator<char>(f),{});}
static bool load(const bytes& raw) {
 if(raw.size()<0x400)return false;
 const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase!=0x400000 || nt->OptionalHeader.SizeOfImage>0x701000)return false;
 DWORD old=0;if(!VirtualProtect(union_space,0x700000,PAGE_EXECUTE_READWRITE,&old))return false;
 std::memset(union_space,0,0x700000);const auto* sec=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)std::memcpy(ptr(0x400000+sec[i].VirtualAddress),raw.data()+sec[i].PointerToRawData,sec[i].SizeOfRawData);
 return true;
}
static bytes snapshot() {return bytes(union_space,union_space+0x700000);}
static void jump(u32 va,const void* target) {const auto b=patch_transaction::relocation(va,"e9",true,address(target),0,"test payload service").bytes;std::memcpy(ptr(va),b.data(),b.size());}
static void* reservation=nullptr;
static void* owner_memory=nullptr;
static u32 allocated=0,freed=0;
static void* __cdecl heap(u32 size,u32 pool_id,u32 alignment) {
 if(pool_id!=14 || alignment!=16)return nullptr;
 allocated=size;const u32 committed=(size+64+4095)&~4095u;
 reservation=VirtualAlloc(nullptr,committed+4096,MEM_RESERVE,PAGE_NOACCESS);
 if(!reservation || !VirtualAlloc(reservation,committed,MEM_COMMIT,PAGE_READWRITE))return nullptr;
 owner_memory=static_cast<std::uint8_t*>(reservation)+committed-size;
 std::memset(static_cast<std::uint8_t*>(owner_memory)-64,0xa5,64);
 std::memset(owner_memory,0xcd,size);return owner_memory;
}
static void __cdecl release_heap(void* p,u32 id) {if(p==owner_memory && id==14)++freed;}
static int __cdecl at_exit(void*) {return 0;}
static void* __fastcall pair_zero(void* p,void*,u32) {word(p)=word(p,4)=0;return p;}
static void* __fastcall power_payload(void* p,void*) {std::memset(p,0,0x68);return p;}
static void dependencies() {
 jump(0x5605e0,reinterpret_cast<void*>(&heap));jump(0x5606a0,reinterpret_cast<void*>(&release_heap));
 jump(0x401d50,reinterpret_cast<void*>(&at_exit));jump(0x435370,reinterpret_cast<void*>(&pair_zero));
 jump(0x537400,reinterpret_cast<void*>(&power_payload));
}
static bool overflow_faults(void* p,u32 size) {
 __try {static_cast<volatile std::uint8_t*>(p)[size]=0x42;return false;}
 __except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION;}
}
static bool clear(void* p,u32 offset,u32 words) {for(u32 i=0;i<words;++i)if(word(p,offset+i*4))return false;return true;}
static bool prefix_intact() {const auto* p=static_cast<std::uint8_t*>(owner_memory)-64;return std::all_of(p,p+64,[](auto b){return b==0xa5;});}
static void __cdecl observed(unsigned,const unsigned*) {++refusals;}
static void observer(pool which,void* storage) {
 const auto va=which==pool::attached?0x5386aau:0x54c39au;
 const auto found=std::find_if(pool_counter_sites::observers.begin(),pool_counter_sites::observers.end(),[&](const auto& s){return s.va==va;});
 check(found!=pool_counter_sites::observers.end(),"D1 uses unchanged branch span");
 if(found==pool_counter_sites::observers.end())return;
 const auto b=pool_counter_observers::stub(*found,address(storage),address(reinterpret_cast<void*>(&observed)),0);
 std::memcpy(storage,b.data(),b.size());jump(va,storage);
}
static bool install_plan(pool which,void* code,const patch_transaction::hooks* hooks=nullptr) {
 auto p=power_pool_plan::make(which,address(code));
 for(unsigned i=0;i<p.code.size();++i)if(!p.code[i].empty())std::memcpy(static_cast<std::uint8_t*>(code)+i*power_pool_plan::stride,p.code[i].data(),p.code[i].size());
 patch_transaction::transaction tx;tx.add(std::move(p.transaction));const auto result=tx.commit(hooks);
 if(result!=patch_transaction::outcome::committed)std::printf("transaction: %s\n",tx.report().c_str());
 return result==patch_transaction::outcome::committed;
}
static void unchanged_helpers(const bytes& before) {
 for(const auto& h:{power_pool_plan::helper_0,power_pool_plan::helper_1,power_pool_plan::helper_2})
  check(std::equal(before.begin()+h.va-0x401000,before.begin()+h.va-0x401000+h.hex.size()/2,static_cast<const std::uint8_t*>(ptr(h.va))),"shared helper body remains byte-identical");
 for(u32 va:{0x405319u,0x463c69u,0x47dce9u,0x539308u,0x53c709u,0x53d24du,0x544209u,0x5bf9c4u,0x5c2df9u,0x464e49u,0x5395c9u,0x544899u,0x5862a0u,0x586339u})
  check(std::equal(before.begin()+va-0x401000,before.begin()+va-0x401000+5,static_cast<const std::uint8_t*>(ptr(va))),"other shared-helper caller remains byte-identical");
}
using method=void*(__fastcall*)(void*,void*);
using allocate_t=u32(__fastcall*)(void*,void*);
using handle_t=u32(__fastcall*)(void*,void*,u32);
using valid_t=bool(__fastcall*)(void*,void*,u32);
using release_t=void(__fastcall*)(void*,void*,u32);
static void lifecycle(const bytes& retail,pool which,u32 n,void* code,bool diagnostic) {
 check(load(retail),"retail image maps in native fixture");const auto before=snapshot();
 if(n==256) {check(install_plan(which,code),"production pool transaction commits");unchanged_helpers(before);}
 const auto l=layout_for(which,n);dependencies();
 allocated=freed=refusals=0;owner_memory=reservation=nullptr;
 void* p=reinterpret_cast<void*(__cdecl*)()>(getter(which))();
 check(p==owner_memory && allocated==l.size && (address(p)&15)==0,"retail getter requests exact aligned allocation size");
 if(!p)return;
 const auto w=(n+31)/32;
 check(clear(p,l.built,w)&&clear(p,l.used,w)&&word(p,l.live)==0&&word(p,l.free)==n,"dirty owner constructor clears every bitmap word and initializes all free slots");
 check(word(p,l.mask)==(n==192?255u:n-1)&&word(p,l.shift)==(n==128?7u:8u),"native constructor derives handle mask and shift");
 check(prefix_intact()&&overflow_faults(p,l.size),"canary prefix intact; write one byte past owner faults at protected page");
 check(reinterpret_cast<void*(__cdecl*)()>(getter(which))()==p,"retail singleton getter reuses constructed owner");
 const u32 alloc_va=which==pool::attached?0x5386a0:0x54c390;
 const u32 valid_va=which==pool::attached?0x538620:0x54c310;
 const u32 lookup_va=which==pool::attached?0x538680:0x54c370;
 const u32 release_va=which==pool::attached?0x538810:0x54c540;
 const auto alloc=reinterpret_cast<allocate_t>(alloc_va);
 const auto valid=reinterpret_cast<valid_t>(valid_va);
 const auto lookup=reinterpret_cast<handle_t>(lookup_va);
 const auto release=reinterpret_cast<release_t>(release_va);
 if(diagnostic)observer(which,static_cast<std::uint8_t*>(code)+power_pool_plan::code_size);
 std::vector<u32> first;bool good=true;
 for(u32 i=0;i<n;++i) {
  auto h=alloc(p,nullptr);first.push_back(h);
  good&=h!=0 && (h&word(p,l.mask))==i && valid(p,nullptr,h) && lookup(p,nullptr,h)==address(p)+4+i*l.stride && word(p,l.live)==i+1;
 }
 check(good&&std::set<u32>(first.begin(),first.end()).size()==n,"retail allocation reaches every slot with unique valid handles and exact live counts");
 check(alloc(p,nullptr)==0&&word(p,l.live)==n&&word(p,l.free)==0,"N+1 attachment/callback refuses without changing live/free counts");
 check(!diagnostic||refusals==1,"D1 counts the full refusal with stock and installed layouts");
 good=true;
 for(u32 i=0;i<n;++i)good&=(word(p,l.used+(i/32)*4)&(1u<<(i%32)))&&(word(p,l.built+(i/32)*4)&(1u<<(i%32)));
 check(good,"all live and constructed bits set across every bitmap boundary");
 // FIFO permutation crosses bit 31/32, the stock limits, bit 223/224 and 255.
 std::vector<u32> order;for(u32 i=0;i<n;++i)order.push_back((i*17)%n);
 good=true;for(auto i:order) {release(p,nullptr,first[i]);good&=!valid(p,nullptr,first[i]);}
 check(good&&word(p,l.live)==0&&word(p,l.free)==n&&clear(p,l.used,w)&&clear(p,l.built,w),"retail release clears every bit and rejects all stale generation handles");
 std::vector<u32> current(n);
 good=true;for(auto i:order) {auto h=alloc(p,nullptr);current[i]=h;good&=(h&word(p,l.mask))==i&&h!=first[i]&&valid(p,nullptr,h)&&!valid(p,nullptr,first[i]);}
 check(good&&word(p,l.live)==n,"free ring wraps and reuses every slot in FIFO order with advanced generations");
 good=true;
 for(unsigned cycle=0;cycle<3;++cycle) {
  for(u32 i=0;i<n;++i)release(p,nullptr,current[i]);
  for(u32 i=0;i<n;++i) {const auto h=alloc(p,nullptr);good&=(h&word(p,l.mask))==i&&h!=current[i]&&!valid(p,nullptr,current[i]);current[i]=h;}
 }
 check(good&&word(p,l.live)==n,"three further full ring wraps preserve generations and occupancy");
 // Exercise signed generation rollover at a high bitmap slot.
 const auto last=n-1;word(p,l.ids+last*4)=(0x7fffffffu&~word(p,l.mask))|last;
 release(p,nullptr,word(p,l.ids+last*4));
 const auto reused=alloc(p,nullptr);
 check(reused==((1u<<word(p,l.shift))|last)&&valid(p,nullptr,reused),"native generation rollover resets to a positive nonzero token");
 // Dirty all constructed words so teardown must scan the full enlarged bitmap.
 reinterpret_cast<void(__cdecl*)()>(which==pool::attached?0x538960:0x54c660)();
 check(freed==1&&word(ptr(owner(which)))==0&&clear(p,l.built,w),"retail shutdown clears all constructed bitmap words, frees correct owner and resets singleton");
 check(prefix_intact(),"all allocations, reuse and destruction preserve owner prefix canary");
 VirtualFree(reservation,0,MEM_RELEASE);reservation=nullptr;
 std::printf("MATRIX pool=%s capacity=%u size=0x%x diagnostics=%u\n",which==pool::attached?"attached":"vf",n,l.size,diagnostic);
}
static void bitmap_helpers(const bytes& retail,void* code) {
 for(const auto which:{pool::attached,pool::vf}) {
  load(retail);check(install_plan(which,code),"bitmap helper test installs private copy");
  auto helper=reinterpret_cast<u32(__fastcall*)(void*,void*,u32,u32)>(code);
  std::array<u32,10> bits{};bits.front()=bits.back()=0xa5a5a5a5;
  bool good=true;
  for(u32 i=0;i<256;++i) {
   std::fill(bits.begin()+1,bits.end()-1,0);bits[1+i/32]=1u<<(i%32);
   good&=helper(bits.data()+1,nullptr,0,1)==i&&helper(bits.data()+1,nullptr,i,1)==i&&helper(bits.data()+1,nullptr,i+1,1)==256;
   std::fill(bits.begin()+1,bits.end()-1,~0u);bits[1+i/32]&=~(1u<<(i%32));
   good&=helper(bits.data()+1,nullptr,0,0)==i&&helper(bits.data()+1,nullptr,i+1,0)==256;
  }
  check(good&&bits.front()==0xa5a5a5a5&&bits.back()==0xa5a5a5a5,"private bitmap helper finds set/clear bits at all 256 boundaries and returns 256 at end");
 }
}
static void clear_negative_controls(const bytes& retail,void* code) {
 for(const auto which:{pool::attached,pool::vf})for(unsigned omit=0;omit<3;++omit) {
  load(retail);const auto l=layout_for(which,256);
  auto p=power_pool_plan::make(which,address(code));
  const auto& clears=which==pool::attached?power_pool_plan::attached_clears:power_pool_plan::vf_clears;
  for(unsigned i=0;i<p.code.size();++i)if(!p.code[i].empty())std::memcpy(static_cast<std::uint8_t*>(code)+i*power_pool_plan::stride,p.code[i].data(),p.code[i].size());
  auto& writes=p.transaction.writes;
  writes.erase(std::remove_if(writes.begin(),writes.end(),[&](const auto& w){return w.va==clears[omit].va;}),writes.end());
  patch_transaction::transaction tx;tx.add(std::move(p.transaction));
  check(tx.commit()==patch_transaction::outcome::committed,"negative clear control installs deliberately incomplete plan");
  dependencies();reservation=owner_memory=nullptr;
  auto* owner=reinterpret_cast<void*(__cdecl*)()>(getter(which))();
  bool caught=false;
  if(omit) caught=!clear(owner,omit==1?l.built:l.used,8);
  else {
   // Constructor was complete; mark all records constructed so the real
   // destructor traverses all 256 bits before its deliberately old clear.
   for(unsigned i=0;i<8;++i)word(owner,l.built+4*i)=~0u;
   reinterpret_cast<void(__cdecl*)()>(which==pool::attached?0x538960:0x54c660)();
   caught=!clear(owner,l.built,8);
  }
  check(caught&&prefix_intact(),"NEGATIVE CONTROL: omitting any one expanded clear is detected on dirty owner");
  VirtualFree(reservation,0,MEM_RELEASE);reservation=nullptr;
 }
}
static void transaction_refusals(const bytes& retail,void* code) {
 for(const auto which:{pool::attached,pool::vf}) {
  load(retail);const auto original=power_pool_plan::make(which,address(code));
  bool good=true;
  for(const auto& g:original.transaction.guards) {
   load(retail);*static_cast<std::uint8_t*>(ptr(g.va))^=1;const auto before=snapshot();
   auto p=power_pool_plan::make(which,address(code));bool published=false;p.transaction.publish=[&]{published=true;};
   patch_transaction::transaction tx;tx.add(std::move(p.transaction));
   good&=tx.commit()==patch_transaction::outcome::refused_guard&&!published&&snapshot()==before;
  }
  check(good,"every generated operand/helper/clear/singleton guard rejects corruption with zero writes/publication");
  for(bool rollback:{false,true}) {
   load(retail);const auto before=snapshot();auto p=power_pool_plan::make(which,address(code));
   bool published=false,abandoned=false;p.transaction.publish=[&]{published=true;};p.transaction.abandon=[&]{abandoned=true;};
   patch_transaction::hooks h;
   unsigned flush_count=0;
   if(rollback) {h.flush=[&flush_count](void* a,std::size_t n){if(flush_count++==0)return false;return FlushInstructionCache(GetCurrentProcess(),a,n)!=FALSE;};}
   else h.protect=[](void*,std::size_t,DWORD,DWORD*){return false;};
   patch_transaction::transaction tx;tx.add(std::move(p.transaction));const auto result=tx.commit(&h);
   check(result==(rollback?patch_transaction::outcome::rolled_back:patch_transaction::outcome::refused_protect)&&!published&&abandoned&&snapshot()==before,"page/flush failure preserves or rolls back all bytes and abandons unpublished storage");
  }
 }
}
static void runtime(const bytes& retail) {
 load(retail);
 if(mode=="guard") {*static_cast<std::uint8_t*>(ptr(0x53899e))^=1;*static_cast<std::uint8_t*>(ptr(0x54c69e))^=1;}
 if(mode=="singleton") {word(ptr(owner(pool::attached)))=1;word(ptr(owner(pool::vf)))=1;}
 const auto before=snapshot();power_pools::install(reinterpret_cast<HMODULE>(mode=="base"?0x500000:0x400000));power_pools::register_agreement();
 const bool active=mode=="on"||mode=="attached"||mode=="vf";
 if(!active) {
  check(snapshot()==before,"runtime OFF/invalid/wrong-base/guard/existing-singleton writes zero bytes");
  check(!power_pools::installed(pool::attached)&&!power_pools::installed(pool::vf),"runtime refusal publishes no raised layout");
  if(mode=="off"||mode=="base") {gameplay_capabilities::record stock;stock.finalize();check(registrations==0&&agreement.finalize()&&std::equal(stock.bytes().begin(),stock.bytes().end(),agreement.bytes().begin()),"OFF agreement record is exactly stock; no rule 61/62");}
  else check(registrations==2&&!agreement.finalize(),"requested failure registers unverified rules and closes admission");
 } else {
  check(agreement.finalize()&&registrations==(mode=="on"?2u:1u),"successful runtime registers exactly the requested verified rules");
  for(const auto which:{pool::attached,pool::vf}) {
   const bool installed=mode=="on"||(mode=="attached"&&which==pool::attached)||(mode=="vf"&&which==pool::vf);
   const auto& l=power_pools::descriptor(which);
   check(power_pools::installed(which)==installed&&l.capacity==(installed?256:stock_capacity(which)),"runtime layout descriptor is published only for committed request");
   const auto r=std::find_if(pool_counter_sites::readers.begin(),pool_counter_sites::readers.end(),[&](const auto& x){return x.root==owner(which);});
   check(r!=pool_counter_sites::readers.end()&&r->cap==l.capacity&&r->offset==l.live,"D0 reader uses committed capacity and relocated count");
  }
  for(std::size_t i=1;i<agreement.count();++i)check(agreement.rule(i).revision==1&&agreement.rule(i).values==std::array<u32,4>{256,0,0,0},"rule 61/62 has revision 1 and effective values {256,0,0,0}");
 }
 check(!startup_refused,"certified installation/refusal keeps offline startup available");
}
int main(int argc,char** argv) {
 if(argc<2)return 2;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(address(union_space)!=0x401000)return 3;
 const auto retail=read(argv[1]);if(!load(retail))return 4;
 if(argc>2)mode=argv[2];
 if(mode=="plan"&&argc==4) {
  std::ofstream out(argv[3]);
  for(const auto which:{pool::attached,pool::vf}) {
   const auto p=power_pool_plan::make(which,which==pool::attached?0x12340000:0x12341000);
   for(const auto& w:p.transaction.writes) {out<<"W\t"<<std::hex<<w.va<<"\t";for(auto b:w.bytes){char hex[3];std::snprintf(hex,sizeof(hex),"%02x",b);out<<hex;}out<<"\n";}
  }
  return 0;
 }
 if(mode=="suite") {
  for(const auto t:std::array<std::optional<std::string_view>,7>{std::nullopt,"128","192","255","257","bad","256junk"}) {
   const auto r=power_pool_plan::choose(t);check(t?!r.valid:!r.requested,"only an absent key or exact supported 256 is accepted");
  }
  check(power_pool_plan::choose("256").valid,"supported capacity 256 accepted");
  void* code=VirtualAlloc(nullptr,power_pool_plan::code_size+256,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(!code)return 5;
  transaction_refusals(retail,code);bitmap_helpers(retail,code);clear_negative_controls(retail,code);
  for(const auto which:{pool::attached,pool::vf})for(u32 n:{stock_capacity(which),256u})for(bool diagnostic:{false,true})lifecycle(retail,which,n,code,diagnostic);
  VirtualFree(code,0,MEM_RELEASE);
 } else runtime(retail);
 std::printf("power pools host: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
