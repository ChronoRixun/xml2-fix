#include "zone_strings.hpp"
#include "save_manager_rules.hpp"
#include "save_format.hpp"
#include <memory>

namespace zone_strings
{
 namespace
 {
  using namespace zone_strings_rules;
  static_assert(w2.slots==save_format_rules::w2_profile.zone_string_slots&&w2.arena==save_format_rules::w2_profile.zone_arena);
  alignas(4) std::uint8_t backing[w2.size()];
  bool initialized=false;
  refusal refused=refusal::none;
  std::uint32_t address_of(const void* p) {return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
  void ensure_initialized() {if(!initialized) {initialize(backing,w2);initialized=true;}}

  // Preserve every register except EAX, including the retail getter's fast-path ECX/EDX.
  __declspec(naked) void* getter()
  {
   __asm {
    pushfd
    pushad
    call ensure_initialized
    popad
    popfd
    mov eax, offset backing
    ret
   }
  }
  bool __fastcall lookup(std::uint8_t* object,void*,std::uint32_t* slot,const char* text,std::uint32_t length)
  {
   const auto q=probe(object,w2,text,length,*slot);*slot=q.slot;return q.found;
  }
  std::uint32_t __cdecl intern(std::uint8_t* object,const char* text,std::uint32_t length)
  {
   const auto result=add(object,w2,text,length);refused=result.error;
   return result.error==refusal::none?result.slot:0xffffffffu;
  }
  // Recreate the exact retail saved-register frame. The native tail constructs the generation/slot handle
  // and returns with ret 0xc. Refusals cross the LC seam so its overflow hook marks capture incomplete.
  __declspec(naked) void add_name()
  {
   __asm {
    push ebx
    push ebp
    mov ebp, [esp+10h]
    push esi
    mov esi, [esp+10h]
    mov ebx, ecx
    push edi
    push ebp
    push esi
    push ebx
    call intern
    add esp, 0ch
    cmp eax, 0ffffffffh
    je failed
    mov edx, eax
    mov eax, 41a44ch
    jmp eax
   failed:
    xor edx, edx
    mov ecx, 7fffffffh
    mov eax, 41a427h
    jmp eax
   }
  }
  struct executable_block
  {
   void* memory=nullptr;bool retained=false;
   void release() {if(memory&&!retained) {VirtualFree(memory,0,MEM_RELEASE);memory=nullptr;}}
   ~executable_block() {release();}
  };
 }
 zone_strings_rules::refusal last_refusal() {return refused;}

 bool contribute_w2(patch_transaction::contribution& c,std::string& why)
 {
  using namespace zone_strings_rules;
  c={};why.clear();c.owner="zone_strings W2";
  for(const auto& s:in_place)
  {
   c.guards.push_back({s.va,patch_transaction::bytes_of(s.retail),s.row});
   c.writes.push_back({s.va,patch_transaction::bytes_of(s.w2),s.row});
  }
  for(const auto& r:regions)
  {
   c.regions.push_back({r.name,r.lo,r.hi,patch_transaction::sha256_of(r.hash)});
   c.guards.push_back({r.lo,patch_transaction::bytes_of(r.entry),r.name});
  }
  // Verify before copying instructions or preparing a callable code block. The transaction checks again.
  if(!patch_transaction::verify_expectations(c,why)) {c={};return false;}
  auto block=std::make_shared<executable_block>();
  const auto size=compact_hi-compact_lo;
  std::vector<std::uint8_t> code(size);
  if(!patch_transaction::read_memory(compact_lo,code.data(),size)) {why="cannot read compaction";c={};return false;}
  auto edit=[&](const site& s)
  {
   const auto bytes=patch_transaction::bytes_of(s.w2);
   std::memcpy(code.data()+s.va-compact_lo,bytes.data(),bytes.size());
  };
  for(const auto& s:in_place)if(s.va>=compact_lo&&s.va<compact_hi)edit(s);
  for(const auto& s:compact_edits)edit(s);
  // The copied owner executes on W2 zone records and managers. Patching only
  // the retired retail body leaves its offsets at 0x4804 / 0x1ad64. These
  // twelve operands used to be duplicated as literals in compact_edits.
  for(const auto& s:entity_capacity_sites::literal_sites)
   if(s.owner=="save" && s.va>=compact_lo && s.va<compact_hi)
   {
    const auto b=save_manager_rules::literal(s,save_format_rules::w2_profile);
    std::memcpy(code.data()+s.va-compact_lo,b.data(),b.size());
   }
  block->memory=VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  if(!block->memory) {why="cannot allocate compaction copy";c={};return false;}
  const auto base=address_of(block->memory);
  for(const auto va:compact_calls)
  {
   const auto off=va-compact_lo;
   const auto target=va+5+read32(code.data()+off+1);
   write32(code.data()+off+1,target-(base+off+5));
  }
  std::memcpy(block->memory,code.data(),size);
  DWORD old=0;
  if(!VirtualProtect(block->memory,size,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),block->memory,size))
  {why="cannot protect/flush compaction copy";c={};return false;}
  c.writes.push_back(patch_transaction::relocation(0x419d40,"e9",true,address_of(&lookup),0,"bounded lookup"));
  c.writes.push_back(patch_transaction::relocation(0x41a3c0,"e9",true,address_of(&add_name),0,"bounded intern"));
  c.writes.push_back(patch_transaction::relocation(0x6021a0,"e9",true,address_of(&getter),0,"DLL zone-string getter"));
  c.writes.push_back(patch_transaction::relocation(compact_lo,"e9",true,base,0,"W2 compaction frame"));
  c.publish=[block] {block->retained=true;save_format::publish_zone_strings(address_of(backing));};
  c.abandon=[block] {block->release();};
  c.retain=[block] {block->retained=true;}; // An uncertified rollback may still jump into it.
  return true;
 }
}
