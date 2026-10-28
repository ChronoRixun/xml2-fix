// Retail instructions execute only in this test's fixed-base image. XMen2.exe is a read-only input.
#include "zone_strings.hpp"
#include "save_core_rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <utility>

extern "C" std::uint8_t fixture_space[0x700000];
namespace save_format {std::uint32_t published=0;void publish_zone_strings(std::uint32_t a) {published=a;}}
using namespace zone_strings_rules;
namespace
{
 int checks=0,failures=0;
 void check(bool ok,const std::string& text) {++checks;failures+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",text.c_str());}
 void* at(std::uint32_t va) {return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va));}
 std::uint32_t address(const void* p) {return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
 using add_fn=std::uint32_t(__fastcall*)(std::uint8_t*,void*,const char*,std::uint32_t,int);
 using gc_fn=void(__fastcall*)(std::uint8_t*,void*,int);
 using lookup_fn=bool(__fastcall*)(std::uint8_t*,void*,std::uint32_t*,const char*,std::uint32_t);
 auto native_add=reinterpret_cast<add_fn>(0x41a3c0);
 std::uint32_t call_object,call_text,call_length,call_result,esp_before,esp_after;
 std::uint32_t kept_ebx,kept_ebp,kept_esi,kept_edi,kept_ecx,kept_edx,flags_before,flags_after;
 __declspec(naked) void measured_add()
 {
  __asm {
   push ebp
   push ebx
   push esi
   push edi
   mov esp_before, esp
   mov ebx, 12345678h
   mov ebp, 23456789h
   mov esi, 3456789ah
   mov edi, 456789abh
   push 0
   push call_length
   push call_text
   mov ecx, call_object
   mov edx, 56789abch
   mov eax, 41a3c0h
   call eax
   mov call_result, eax
   mov esp_after, esp
   mov kept_ebx, ebx
   mov kept_ebp, ebp
   mov kept_esi, esi
   mov kept_edi, edi
   mov esp, esp_before
   pop edi
   pop esi
   pop ebx
   pop ebp
   ret
  }
 }
 __declspec(naked) void measured_getter()
 {
  __asm {
   push ebp
   push ebx
   push esi
   push edi
   mov esp_before, esp
   mov ebx, 12345678h
   mov ebp, 23456789h
   mov esi, 3456789ah
   mov edi, 456789abh
   mov ecx, 6789abcdh
   mov edx, 789abcdeh
   xor eax, eax
   stc
   pushfd
   pop flags_before
   mov eax, 6021a0h
   call eax
   mov call_result, eax
   pushfd
   pop flags_after
   mov esp_after, esp
   mov kept_ebx, ebx
   mov kept_ebp, ebp
   mov kept_esi, esi
   mov kept_edi, edi
   mov kept_ecx, ecx
   mov kept_edx, edx
   mov esp, esp_before
   pop edi
   pop esi
   pop ebx
   pop ebp
   ret
  }
 }
 bool preserved() {return esp_before==esp_after&&kept_ebx==0x12345678&&kept_ebp==0x23456789&&kept_esi==0x3456789a&&kept_edi==0x456789ab;}
 void check_add_abi(std::uint8_t* object,const char* text,const char* tag)
 {
  call_object=address(object);call_text=address(text);call_length=static_cast<std::uint32_t>(std::strlen(text)+1);
  measured_add();check(preserved(),std::string(tag)+": callee registers preserved and ret 0xc balances ESP");
 }
 std::uint32_t intern(std::uint8_t* p,const std::string& s) {return native_add(p,nullptr,s.c_str(),static_cast<std::uint32_t>(s.size()+1),0);}
 std::string numbered(unsigned i) {char b[16];std::snprintf(b,sizeof b,"z%04x",i);return b;}
 std::pair<std::string,std::string> high_names()
 {
  std::string high,last;
  for(unsigned i=0;high.empty()||last.empty();++i)
  {const auto s=numbered(i);const auto slot_hash=hash(s.c_str(),static_cast<std::uint32_t>(s.size()+1))&0x1fff;if(slot_hash>2047&&high.empty())high=s;if(slot_hash>=8160&&last.empty())last=s;}
  return {high,last};
 }
 std::string resolved(const std::uint8_t* p,layout l,std::uint32_t h)
 {const auto offset=read32(p+4+(h&0xffffff)*4);return reinterpret_cast<const char*>(p+l.arena_offset()+offset);}
 void jump(std::uint32_t va,const void* dest)
 {const auto w=patch_transaction::relocation(va,"e9",true,address(dest),0,"test stub");std::memcpy(at(va),w.bytes.data(),w.bytes.size());}
 std::vector<std::uint8_t> binary;
 bool load(const char* path)
 {
  std::ifstream f(path,std::ios::binary);if(!f)return false;
  binary.assign(std::istreambuf_iterator<char>(f),{});
  if(binary.size()<0x1000)return false;
  const auto pe=read32(binary.data()+0x3c);
  if(pe+0x100>binary.size())return false;
  std::uint16_t count=0,opt=0;std::memcpy(&count,binary.data()+pe+6,2);std::memcpy(&opt,binary.data()+pe+20,2);
  const auto sections=pe+24+opt;
  for(unsigned i=0;i<count;++i)
  {
   const auto row=sections+i*40;if(row+40>binary.size())return false;
   const auto rva=read32(binary.data()+row+12),size=read32(binary.data()+row+16),off=read32(binary.data()+row+20);
   if(rva==0x1000&&size>=0x27d7b5&&off+size<=binary.size())
   {std::memcpy(at(0x401000),binary.data()+off,0x67e7b5-0x401000);return true;}
  }
  return false;
 }
 void pure_tests()
 {
  std::vector<std::uint8_t> table(w2.size(),0xcc);initialize(table.data(),w2);
  check(read32(table.data())==0&&read32(table.data()+4)==1&&read32(table.data()+w2.used_offset())==2&&table[w2.arena_offset()+1]==0,"W2 layout and retail initialization");
  auto p=table.data();const auto empty=add(p,w2,"",1);
  check(empty.error==refusal::none&&empty.slot==0&&read32(p+w2.used_offset())==2,"empty name resolves reserved slot zero");
  bool ok=true;std::vector<std::uint32_t> slots;
  for(unsigned i=0;i<8191;++i)
  {
   auto s=numbered(i)+std::string(10,'x');if(i==0)s+=std::string(13,'y');
   const auto r=add(p,w2,s.c_str(),static_cast<std::uint32_t>(s.size()+1));ok&=r.error==refusal::none&&r.slot!=0;slots.push_back(r.slot);
  }
  for(unsigned i=0;i<8191;++i)
  {auto s=numbered(i)+std::string(10,'x');if(i==0)s+=std::string(13,'y');const auto r=add(p,w2,s.c_str(),static_cast<std::uint32_t>(s.size()+1));ok&=r.error==refusal::none&&r.slot==slots[i];}
  check(ok&&read32(p+w2.used_offset())==0x1ffff,"pure W2: 8191 names, 0x1ffff used bytes, all found again");
  auto before=table;auto r=add(p,w2,"overflow",9);
  check(r.error==refusal::slot_limit&&r.tries==8192&&table==before,std::string("8192nd refused without writes: ")+name(r.error));
  initialize(p,w2);std::string large(w2.arena-4,'a');r=add(p,w2,large.c_str(),static_cast<std::uint32_t>(large.size()+1));
  before=table;const auto full=add(p,w2,"b",2);
  check(r.error==refusal::none&&full.error==refusal::arena_limit&&before==table,std::string("arena exhaustion without offset publication: ")+name(full.error));
  initialize(p,w2);p[w2.arena_offset()+2]='a';p[w2.arena_offset()+3]=0;
  for(unsigned i=0;i<w2.slots;++i)write32(p+4+i*4,2);
  before=table;r=add(p,w2,"b",2);
  check(r.error==refusal::slot_limit&&r.tries==w2.slots&&before==table,"collision-full table stops at one complete probe cycle");
  const auto& site=save_core_rules::capture_bounds[0];
  check(save_core_rules::capture_compare(site,save_format_rules::retail_profile)==patch_transaction::bytes_of("81f900380000")&&
        save_core_rules::capture_compare(site,save_format_rules::e10a2_profile)==patch_transaction::bytes_of("81f900380000")&&
        save_core_rules::capture_compare(site,save_format_rules::w2_profile)==patch_transaction::bytes_of("81f900000200"),"14d RETAIL/E10A2/W2 compare bytes");
 }
 // Small CSmartEntMgr substitute: the copied GC calls only vt+0x14 and vt+0x18.
 std::vector<std::array<std::uint32_t,2>> smart_records;
 std::uint32_t __fastcall smart_count(void*,void*) {return static_cast<std::uint32_t>(smart_records.size());}
 void* __fastcall smart_at(void*,void*,std::uint32_t i) {return smart_records.at(i).data();}
 void* smart_vt[8]{};struct smart_object {void** vt;} smart{smart_vt};
 void* __cdecl smart_get() {return &smart;}
 char* __cdecl copy_string(char* dest,const char* source,std::size_t n) {return std::strncpy(dest,source,n);}
 unsigned overflows=0;std::uint32_t capture_flags=0;
 void __cdecl overflow(void*) {++overflows;capture_flags|=save_core_rules::capture_incomplete;}
 void native_tests()
 {
  auto retail_get=reinterpret_cast<std::uint8_t*(__cdecl*)()>(0x6021a0);
  auto* stock=retail_get();std::vector<std::uint8_t> expected(retail.size(),0);initialize(expected.data(),retail);
  check(stock==at(0xa26c30)&&!std::memcmp(stock,expected.data(),retail.size()),"retail getter executed and initialization checked");
  bool same=true;std::vector<std::uint32_t> old_offsets;
  for(unsigned i=0;i<2047;++i)
  {
   const auto s=numbered(i);const auto h=intern(stock,s);const auto r=add(expected.data(),retail,s.c_str(),static_cast<std::uint32_t>(s.size()+1));
   same&=h==handle(expected.data(),r.slot);old_offsets.push_back(read32(stock+4+(h&0xffffff)*4));
  }
  check(same&&!std::memcmp(stock,expected.data(),retail.size()),"2047 retail native interns match plain C++ offsets, bytes, used and generation");
  check_add_abi(stock,"z0000","retail intern ABI");
  jump(0x672170,&copy_string);smart_vt[5]=reinterpret_cast<void*>(&smart_count);smart_vt[6]=reinterpret_cast<void*>(&smart_at);jump(0x4b76f0,&smart_get);
  const auto [high,last]=high_names();
  // Establish the unchanged compactor's output for the same names and graph of references.
  initialize(stock,retail);intern(stock,"discarded");
  const auto stock_h1=intern(stock,high),stock_h2=intern(stock,last),stock_h3=intern(stock,"second state record"),stock_h4=intern(stock,"second zone");
  const auto& rp=save_format_rules::retail_profile;
  std::vector<std::uint8_t> stock_manager(rp.manager_size,0);
  write32(stock_manager.data(),stock_h1);write32(stock_manager.data()+4,stock_h2);write32(stock_manager.data()+4+rp.state_record_size,stock_h3);
  write32(stock_manager.data()+rp.state_count,2);write32(stock_manager.data()+rp.record_size,stock_h4);write32(stock_manager.data()+0x1ad64,2);
  smart_records={{stock_h2,stock_h3}};
  reinterpret_cast<gc_fn>(compact_lo)(stock_manager.data(),nullptr,-1);
  const auto stock_gc_used=read32(stock+retail.used_offset());
  const auto stock_gc_arena=std::vector<std::uint8_t>(stock+retail.arena_offset(),stock+retail.arena_offset()+stock_gc_used);
  check(resolved(stock,retail,read32(stock_manager.data()))==high&&resolved(stock,retail,read32(stock_manager.data()+4))==last&&
    resolved(stock,retail,read32(stock_manager.data()+4+rp.state_record_size))=="second state record"&&resolved(stock,retail,read32(stock_manager.data()+rp.record_size))=="second zone", "retail compaction baseline retains the same live-name graph");
  std::printf("RETAIL GC used=%u, densely packed=%u\n",stock_gc_used,static_cast<unsigned>(2+high.size()+1+last.size()+1+20+12));
  smart_records.clear();
  // Every guard and hash is checked before stubbing unrelated imports.
  patch_transaction::contribution c;std::string why;
  const auto original=*static_cast<std::uint8_t*>(at(0x465816));*static_cast<std::uint8_t*>(at(0x465816))^=1;
  check(!zone_strings::contribute_w2(c,why)&&!why.empty()&&!save_format::published,"foreign in-place guard refuses preparation and publication");
  *static_cast<std::uint8_t*>(at(0x465816))=original;
  const auto region_byte=*static_cast<std::uint8_t*>(at(0x466100));*static_cast<std::uint8_t*>(at(0x466100))^=1;
  check(!zone_strings::contribute_w2(c,why)&&why.find("compaction")!=std::string::npos,"foreign compaction region hash refuses preparation");
  *static_cast<std::uint8_t*>(at(0x466100))=region_byte;
  const bool prepared=zone_strings::contribute_w2(c,why);check(prepared,"45 site guards, four entry guards and four retail region hashes: "+why);if(!prepared)return;
  for(const auto& g:c.guards)
  {std::printf("GUARD %08x ",g.va);for(auto v:g.bytes)std::printf("%02x",v);std::printf(" %s\n",g.what.c_str());}
  for(const auto& r:regions)std::printf("REGION %08x..%08x SHA256 %s %s\n",r.lo,r.hi,r.hash,r.name);
  const auto original_gc=std::vector<std::uint8_t>(static_cast<std::uint8_t*>(at(compact_lo)),static_cast<std::uint8_t*>(at(compact_hi)));
  for(const auto& w:c.writes)
  {std::printf("WRITE %08x ",w.va);for(auto v:w.bytes)std::printf("%02x",v);std::printf(" %s\n",w.what.c_str());}
  const auto writes=c.writes;patch_transaction::transaction tx;tx.add(std::move(c));
  const auto status=tx.commit();check(status==patch_transaction::outcome::committed,tx.report());if(status!=patch_transaction::outcome::committed)return;
  bool applied=true;for(const auto& w:writes)applied&=!std::memcmp(at(w.va),w.bytes.data(),w.bytes.size());check(applied,"every transaction write read back");
  const auto copied_gc=compact_lo+5+read32(static_cast<std::uint8_t*>(at(compact_lo))+1);
  bool copy_ok=true;
  for(const auto& s:compact_edits)
  {
   const auto b=patch_transaction::bytes_of(s.w2),old=patch_transaction::bytes_of(s.retail);
   copy_ok&=!std::memcmp(original_gc.data()+s.va-compact_lo,old.data(),old.size())&&!std::memcmp(at(copied_gc+s.va-compact_lo),b.data(),b.size());
   std::printf("COMPACT %08x %s -> %s (copy+%x)\n",s.va,s.retail,s.w2,s.va-compact_lo);
  }
  for(const auto& s:in_place)if(s.va>=compact_lo&&s.va<compact_hi)
  {const auto b=patch_transaction::bytes_of(s.w2);copy_ok&=!std::memcmp(at(copied_gc+s.va-compact_lo),b.data(),b.size());}
  for(const auto va:compact_calls)
  {
   const auto dest=va+5+read32(original_gc.data()+va-compact_lo+1),copy_at=copied_gc+va-compact_lo;
   copy_ok&=*static_cast<std::uint8_t*>(at(copy_at))==0xe8&&copy_at+5+read32(static_cast<std::uint8_t*>(at(copy_at))+1)==dest;
   std::printf("REBASE %08x -> %08x (copy+%x)\n",va,dest,va-compact_lo);
  }
  MEMORY_BASIC_INFORMATION info{};VirtualQuery(at(copied_gc),&info,sizeof info);
  check(copy_ok&&info.Protect==PAGE_EXECUTE_READ,"all compaction operand edits, 32 rebased call targets and RX protection verified");
  measured_getter();auto* table=static_cast<std::uint8_t*>(at(call_result));
  check(preserved()&&kept_ecx==0x6789abcd&&kept_edx==0x789abcde&&flags_before==flags_after,"W2 getter initialization changes only EAX, preserves ESP and flags");
  check(table==at(save_format::published)&&table!=stock&&read32(table+4)==1&&read32(table+w2.used_offset())==2,"W2 getter returns initialized DLL storage, published after commit");
  const auto first=intern(table,"first");measured_getter();
  check(preserved()&&kept_ecx==0x6789abcd&&kept_edx==0x789abcde&&flags_before==flags_after&&call_result==address(table)&&resolved(table,w2,first)=="first","W2 getter fast path preserves registers, flags and existing names");
  initialize(table,w2);check_add_abi(table,"abi","W2 intern success ABI");initialize(table,w2);
  // Catch any surviving reference to the retail static, including its flag.
  DWORD old=0;const bool protected_stock=VirtualProtect(at(0xa26000),0x7000,PAGE_NOACCESS,&old)!=0;
  check(protected_stock,"retail static backing and init flag protected PAGE_NOACCESS");
  std::vector<std::uint32_t> handles;same=true;
  for(unsigned i=0;i<2047;++i)
  {const auto s=numbered(i);const auto h=intern(table,s);handles.push_back(h);same&=read32(table+4+(h&0xffffff)*4)==old_offsets[i]&&resolved(table,w2,h)==s;}
  same&=read32(table)==read32(expected.data())&&read32(table+w2.used_offset())==read32(expected.data()+retail.used_offset())&&
        !std::memcmp(table+w2.arena_offset(),expected.data()+retail.arena_offset(),read32(table+w2.used_offset()));
  check(same,"retail vs W2 differential: per-name offsets, stored bytes, used, generation and resolved names (handles may differ)");
  same=true;for(unsigned i=0;i<2047;++i)same&=intern(table,numbered(i))==handles[i];check(same,"native W2 duplicates preserve handles");
  initialize(table,w2);write32(table+w2.used_offset(),w2.arena-1);
  auto no_seam_snapshot=std::vector<std::uint8_t>(table,table+w2.size());
  check(intern(table,"no room")==0&&zone_strings::last_refusal()==refusal::arena_limit&&overflows==0&&!std::memcmp(table,no_seam_snapshot.data(),no_seam_snapshot.size()),"native refusal also reaches empty-slot tail through the unmodified retail compare");
  // Execute the real LC seam bridge on refusal, not a replacement overflow decision.
  auto* seam=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
  if(!seam) {check(false,"allocate capture seam");return;}
  const auto bridge=save_core_rules::bound_bridge(save_core_rules::capture_bounds[0],address(seam),address(&overflow),save_format_rules::w2_profile);
  std::memcpy(seam,bridge.data(),bridge.size());jump(0x41a427,seam);FlushInstructionCache(GetCurrentProcess(),nullptr,0);
  initialize(table,w2);same=true;handles.clear();
  for(unsigned i=0;i<8191;++i)
  {auto s=numbered(i)+std::string(10,'x');if(i==0)s+=std::string(13,'y');const auto h=intern(table,s);same&=(h&0xffffff)!=0;handles.push_back(h);}
  for(unsigned i=0;i<8191;++i)
  {auto s=numbered(i)+std::string(10,'x');if(i==0)s+=std::string(13,'y');same&=intern(table,s)==handles[i]&&resolved(table,w2,handles[i])==s;}
  check(same&&read32(table+w2.used_offset())==0x1ffff,"native W2 adds 8191 names / 0x1ffff used bytes and finds every name again");
  auto snapshot=std::vector<std::uint8_t>(table,table+w2.size());const auto refused=intern(table,"overflow");
  check(refused==0&&zone_strings::last_refusal()==refusal::slot_limit&&overflows==1&&capture_flags==save_core_rules::capture_incomplete&&!std::memcmp(table,snapshot.data(),snapshot.size()),"native 8192nd refused: named slot limit, empty-slot handle, capture incomplete hook, no writes");
  check_add_abi(table,"overflow","W2 intern slot refusal ABI");
  check(call_result==0&&overflows==2&&!std::memcmp(table,snapshot.data(),snapshot.size()),"measured slot refusal returns the empty handle and leaves storage unchanged");
  initialize(table,w2);write32(table,0x7f);std::string large(w2.arena-4,'q');intern(table,large);snapshot.assign(table,table+w2.size());
  check(intern(table,"no room")==0x7f000000&&zone_strings::last_refusal()==refusal::arena_limit&&overflows==3&&!std::memcmp(table,snapshot.data(),snapshot.size()),"native arena limit preserves generation and all bytes, signals capture incomplete");
  check_add_abi(table,"no room","W2 intern arena refusal ABI");
  initialize(table,w2);table[w2.arena_offset()+2]='a';table[w2.arena_offset()+3]=0;for(unsigned i=0;i<w2.slots;++i)write32(table+4+i*4,2);
  snapshot.assign(table,table+w2.size());std::uint32_t slot=0;
  const auto found=reinterpret_cast<lookup_fn>(0x419d40)(table,nullptr,&slot,"b",2);
  check(!found&&slot==0&&intern(table,"b")==0&&overflows==5&&!std::memcmp(table,snapshot.data(),snapshot.size()),"native bounded lookup and intern both terminate on collision-full table");
  check(read32(static_cast<std::uint8_t*>(at(0x484ac1))+1)==w2.size()&&read32(static_cast<std::uint8_t*>(at(0x484ba6))+1)==w2.size(),"save/load stream size immediates both 0x28008");
  // GC helpers stay retail except for their contributed operand edits.
  auto gc=reinterpret_cast<gc_fn>(compact_lo);
  initialize(table,w2);write32(table,0x7f);std::vector<std::uint8_t> manager(save_format_rules::w2_profile.manager_size,0);
  gc(manager.data(),nullptr,-1);
  check(read32(table)==0&&read32(table+w2.used_offset())==2&&read32(table+4)==1,"copied GC empty bitmap scans 256 dwords, resets and wraps generation 0x7f to zero");
  write32(table,0x7f);write32(table+4+(w2.slots-1)*4,3);write32(table+w2.used_offset(),100);
  reinterpret_cast<void(__fastcall*)(void*,void*)>(0x465670)(table,nullptr);
  check(read32(table)==0&&read32(table+w2.used_offset())==2&&read32(table+4)==1&&read32(table+4+(w2.slots-1)*4)==0&&table[w2.arena_offset()]==0&&table[w2.arena_offset()+1]==0,"native clear widens all 8192 offsets and resets W2 arena fields, generation wraps");
  // Names selected above the retail range, including a bitmap bit in the final dword.
  const auto dead=intern(table,"discarded");const auto h1=intern(table,high),h2=intern(table,last);const auto h3=intern(table,"second state record");const auto h4=intern(table,"second zone");
  const auto& profile=save_format_rules::w2_profile;
  write32(manager.data(),h1);write32(manager.data()+4,h2);write32(manager.data()+4+profile.state_record_size,h3);
  write32(manager.data()+profile.state_count,2);write32(manager.data()+profile.record_size,h4);write32(manager.data()+0x121c44,2);
  smart_records={{h2,h3}};
  gc(manager.data(),nullptr,-1);
  check((h1&0xffffff)>2047&&(h2&0xffffff)>=8160&&resolved(table,w2,read32(manager.data()))==high&&
    resolved(table,w2,read32(manager.data()+4))==last&&resolved(table,w2,read32(manager.data()+4+profile.state_record_size))=="second state record"&&
    resolved(table,w2,read32(manager.data()+profile.record_size))=="second zone"&&resolved(table,w2,smart_records[0][0])==last&&resolved(table,w2,smart_records[0][1])=="second state record"&&
    read32(table+w2.used_offset())==stock_gc_used&&!std::memcmp(table+w2.arena_offset(),stock_gc_arena.data(),stock_gc_arena.size()),"copied GC retains high/final bitmap slots, W2 state stride and second zone, updates smart records; bytes/used match retail GC");
  // Skip argument must be read at the widened stack displacement in all three passes.
  smart_records.clear();gc(manager.data(),nullptr,0);
  check(resolved(table,w2,read32(manager.data()+profile.record_size))=="second zone","copied GC widened skip-record stack argument");
  // Restore protections for clean process shutdown; the fixture has no live retail data afterward.
  if(protected_stock) {DWORD ignored=0;VirtualProtect(at(0xa26000),0x7000,old,&ignored);}
  // No code is called through the seam after release.
  VirtualFree(seam,0,MEM_RELEASE);
  (void)dead;
 }
}
int main(int argc,char** argv)
{
 std::setbuf(stdout,nullptr);pure_tests();
 const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");
 if(!path||!*path)std::puts("SKIP retail executable tests (pass argv[1] or XML2_FIX_TEST_EXE)");
 else
 {
  const auto start=address(fixture_space);DWORD old=0;
  if(GetModuleHandleW(nullptr)!=reinterpret_cast<HMODULE>(0x400000)||start>0x419d40||start+sizeof fixture_space<0xa2d000||
     !VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old)) {check(false,"fixed-base 7 MB fixture");}
  else {const bool loaded=load(path);check(loaded,"read-only retail .text copied to test image");if(loaded)native_tests();}
 }
 std::printf("Zone strings: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
}
