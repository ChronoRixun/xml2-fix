// A union host: execute retail pool and guard code at its native addresses inside
// this test executable. Never starts the game, its CRT entry point, or any engine.
// Only the pools' external services (heap, CPath payload construction, scheduling,
// damage descriptor copy) are synthetic. The ring, bitmaps, handle generation,
// capacity decisions and release division are the retail instructions.
#include "actor_pools.hpp"
#include "actor_pools_plan.hpp"
#include "actor_ai_layout.hpp"
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

extern "C" unsigned char union_space[];
using u32=std::uint32_t;
using bytes=std::vector<std::uint8_t>;
static unsigned failures=0,checks=0;
static std::string mode;
static std::string trace_path;
static bool startup_refused=false;
static gameplay_capabilities::record agreement;
static unsigned rule_registrations=0;
static std::vector<void*> heap_blocks;
static unsigned heap_size=0,damage_calls=0,schedules=0;
static void check(bool ok,const char* what) { ++checks;std::printf("%s %s\n",ok?"PASS":"FAIL",what);failures+=!ok; }
static u32 address(const void* p) { return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p)); }
static u32& word(void* p,unsigned offset=0) { return *reinterpret_cast<u32*>(static_cast<std::uint8_t*>(p)+offset); }
static void* ptr(u32 va) { return reinterpret_cast<void*>(va); }

namespace ini
{
    std::optional<std::string> text(const wchar_t*,const wchar_t*)
    { return mode=="off"?std::nullopt:std::optional<std::string>(mode=="invalid"?"128":"124"); }
}
namespace logger
{
    void write(const char* fmt,...) { va_list a;va_start(a,fmt);std::vprintf(fmt,a);std::puts("");va_end(a); }
}
namespace game_version { void refuse_startup() { startup_refused=true; } }
namespace gameplay_agreement
{
    bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<u32,4> values,bool verified)
    { ++rule_registrations;return agreement.add(id,revision,values,verified); }
}
namespace actor_pools_test
{
    void* allocate(void* p,std::size_t size,DWORD type,DWORD protection)
    { return mode=="allocation"?nullptr:VirtualAlloc(p,size,type,protection); }
    const patch_transaction::hooks* transaction_hooks()
    {
        static patch_transaction::hooks hooks;
        if(mode!="protect")return nullptr;
        hooks.protect=[](void* p,std::size_t n,DWORD protection,DWORD* old)
        {
            if(protection==PAGE_EXECUTE_READWRITE) { SetLastError(ERROR_ACCESS_DENIED);return false; }
            return VirtualProtect(p,n,protection,old)!=FALSE;
        };
        return &hooks;
    }
}

static bytes read(const char* path)
{
    std::ifstream f(path,std::ios::binary);return bytes(std::istreambuf_iterator<char>(f),{});
}
static bool load(const bytes& raw)
{
    if(raw.size()<0x400)return false;
    auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.ImageBase!=0x400000||nt->OptionalHeader.SizeOfImage>0x701000)return false;
    DWORD old=0;
    if(!VirtualProtect(union_space,0x700000,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memset(union_space,0,0x700000);
    auto* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
    {
        const auto& s=section[i];
        if(s.VirtualAddress<0x1000||s.VirtualAddress+s.SizeOfRawData>0x701000||s.PointerToRawData+s.SizeOfRawData>raw.size())return false;
        std::memcpy(ptr(0x400000+s.VirtualAddress),raw.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    FlushInstructionCache(GetCurrentProcess(),union_space,0x700000);return true;
}
static bytes snapshot() { return bytes(union_space,union_space+0x700000); }
static void jump(u32 va,const void* target)
{
    auto b=patch_transaction::relocation(va,"e9",true,address(target),0,"test dependency").bytes;
    std::memcpy(ptr(va),b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),ptr(va),b.size());
}
static void* __cdecl heap(unsigned size,unsigned,unsigned)
{
    heap_size=size;
    auto* p=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,size+64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    std::memset(p+size,0xa5,64);heap_blocks.push_back(p);return p;
}
static int __cdecl at_exit(void*) { return 0; }
static void* __fastcall path_payload(void* p,void*) { word(p)=0x689b78;return p; }
static void* __cdecl damage_payload(void* p) { std::memset(p,0,0x6c);return p; }
static void* __fastcall damage_copy(void* p,void*,void* from) { std::memcpy(p,from,0x6c);return p; }
static void __fastcall schedule(void*,void*,void*,u32) { ++schedules; }
static void __fastcall hit(void*,void*,void*,u32,u32,u32) { ++damage_calls; }
static void dependencies()
{
    jump(0x5605e0,reinterpret_cast<const void*>(&heap));
    jump(0x401d50,reinterpret_cast<const void*>(&at_exit));
    jump(0x4924f0,reinterpret_cast<const void*>(&path_payload));
    jump(0x498890,reinterpret_cast<const void*>(&damage_payload));
    jump(0x453d10,reinterpret_cast<const void*>(&damage_copy));
    jump(0x41a730,reinterpret_cast<const void*>(&schedule));
}
using method=void(__fastcall*)(void*,void*);
using get=void*(__cdecl*)();
using path_take=void*(__stdcall*)(u32);
using path_free=void(__stdcall*)(void*);
using take_shadow=u32(__fastcall*)(void*,void*,u32);
using release_shadow=void(__fastcall*)(void*,void*,u32);
using take_damage=unsigned char(__fastcall*)(void*,void*,void*,u32);
using fire_damage=void(__fastcall*)(void*,void*,u32);

static bool canary(void* p,unsigned size)
{ for(unsigned i=0;i<64;++i)if(static_cast<std::uint8_t*>(p)[size+i]!=0xa5)return false;return true; }

static void paths(unsigned capacity)
{
    auto* arena=reinterpret_cast<get>(0x496790)();
    check(arena&&heap_size==(capacity==124?0x33b84:0xd59c),"T6 CPath retail getter requests exact backing size");
    const unsigned live=capacity==124?0x33b80:0xd598;
    const unsigned constructed=capacity*0x6a8;
    std::vector<void*> slots;
    bool good=true;
    for(unsigned i=0;i<capacity;++i)
    {
        auto* p=reinterpret_cast<path_take>(0x496830)(0x12345678);
        good=good&&p==static_cast<std::uint8_t*>(arena)+i*0x6a8;
        good=good&&word(arena,live)==i+1;
        slots.push_back(p);
    }
    check(good,"T6 CPath allocates every slot in FIFO order with exact live count");
    check(reinterpret_cast<path_take>(0x496830)(0)==nullptr&&word(arena,live)==capacity,"T6 CPath refuses capacity+1 without changing live count");
    for(auto* p:slots)reinterpret_cast<path_free>(0x496900)(p);
    check(word(arena,live)==0,"T6 CPath retail pointer division releases every slot");
    slots.clear();
    for(unsigned i=0;i<capacity;++i)slots.push_back(reinterpret_cast<path_take>(0x496830)(0));
    check(std::all_of(slots.begin(),slots.end(),[](void* p){return p!=nullptr;}),"T6 CPath recycled FIFO fills again");
    reinterpret_cast<method>(0x494f90)(arena,nullptr);
    bool clear=true;for(unsigned i=0;i<(capacity+31)/32;++i)clear=clear&&word(arena,constructed+i*4)==0;
    check(clear,"T6 CPath destructor clears every constructed bitmap word");
    reinterpret_cast<void(__cdecl*)()>(0x496800)();
    const unsigned active=capacity==124?0x33b70:0xd594;
    clear=true;for(unsigned i=0;i<(capacity+31)/32;++i)clear=clear&&word(arena,active+i*4)==0;
    check(clear&&word(arena,live)==0&&canary(arena,heap_size),"T6 CPath reset clears every active word and preserves backing canary");
}

static void shadows(unsigned capacity)
{
    const auto size=capacity==124?0x274c:0xa80;
    auto* manager=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,size+64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    std::memset(manager+size,0xa5,64);
    auto* arena=manager+8;
    reinterpret_cast<method>(0x4b6a40)(arena,nullptr);
    const unsigned mask=capacity==124?0x26e8:0xa1c,shift=mask+4,live=capacity==124?0x24f4:0x998;
    check(word(arena,mask)==(capacity==124?127u:31u)&&word(arena,shift)==(capacity==124?7u:5u),"T6 shadow retail constructor derives index mask and shift");
    std::vector<u32> tokens;
    bool good=true;
    for(unsigned i=0;i<capacity;++i)
    {
        auto token=reinterpret_cast<take_shadow>(0x4b6ab0)(manager,nullptr,0x3f800000);
        good=good&&token!=0xffffffff&&(token&word(arena,mask))==i&&word(arena,live)==i+1;
        tokens.push_back(token);
    }
    check(good,"T6 shadow retail take covers every slot and active bitmap word");
    check(reinterpret_cast<take_shadow>(0x4b6ab0)(manager,nullptr,0)==0xffffffff,"T6 shadow refuses capacity+1");
    for(auto token:tokens)reinterpret_cast<release_shadow>(0x4b6d40)(manager,nullptr,token);
    check(word(arena,live)==0,"T6 shadow validates and releases all handles");
    reinterpret_cast<release_shadow>(0x4b6d40)(manager,nullptr,tokens[0]);
    check(word(arena,live)==0,"T6 shadow rejects stale handle after release");
    auto first=reinterpret_cast<take_shadow>(0x4b6ab0)(manager,nullptr,0);
    check(first!=tokens[0]&&(first&word(arena,mask))==0,"T6 recycled shadow slot has a new generation");
    tokens.clear();tokens.push_back(first);
    for(unsigned i=1;i<capacity;++i)tokens.push_back(reinterpret_cast<take_shadow>(0x4b6ab0)(manager,nullptr,0));
    // Bytes immediately after N generations are the mask/shift. The reset must stop
    // at exactly N, retaining both these values and the trailing manager canary.
    const auto m=word(arena,mask),s=word(arena,shift);
    reinterpret_cast<method>(0x4b6be0)(arena,nullptr);
    const unsigned active=capacity==124?0x24e4:0x994,generations=capacity==124?0x24f8:0x99c;
    bool clear=true,advanced=true;
    for(unsigned w=0;w<(capacity+31)/32;++w)clear=clear&&word(arena,active+4*w)==0;
    for(unsigned i=0;i<capacity;++i)advanced=advanced&&word(arena,generations+4*i)==tokens[i]+(1u<<s);
    check(clear&&advanced&&word(arena,mask)==m&&word(arena,shift)==s&&word(arena,live)==0&&canary(manager,size),"T6 shadow reset clears every active word and advances exactly N generations, preserving mask/shift/canary");
    for(const auto token:tokens)reinterpret_cast<release_shadow>(0x4b6d40)(manager,nullptr,token);
    check(word(arena,live)==0,"T6 shadow rejects every pre-reset stale handle, including slots above 31");
    VirtualFree(manager,0,MEM_RELEASE);
}

static void delayed(unsigned capacity,void* block)
{
    reinterpret_cast<method>(0x499ad0)(block,nullptr);
    const unsigned live=capacity==124?0x3670:0xe18;
    std::array<u32,64> vtable{};vtable[0xb0/4]=address(reinterpret_cast<const void*>(&hit));
    std::vector<std::array<std::uint8_t,0x300>> entities(capacity+1);
    std::array<std::uint8_t,0x6c> descriptor{};
    word(descriptor.data(),4)=0x3f800000;
    word(descriptor.data(),0x14)=1;
    bool good=true;
    for(unsigned i=0;i<capacity;++i)
    {
        auto* e=entities[i].data();word(e)=address(vtable.data());
        auto accepted=reinterpret_cast<take_damage>(0x499b10)(e,nullptr,descriptor.data(),0x3f800000);
        good=good&&accepted&&word(e,0x2fc)==address(block)+i*0x6c&&word(block,live)==i+1;
        good=good&&!reinterpret_cast<take_damage>(0x499b10)(e,nullptr,descriptor.data(),0x3f800000)&&word(block,live)==i+1;
    }
    check(good,"T6 delayed damage take accepts all slots and refuses each busy +2fc");
    auto* extra=entities.back().data();word(extra)=address(vtable.data());
    check(!reinterpret_cast<take_damage>(0x499b10)(extra,nullptr,descriptor.data(),0x3f800000)&&word(extra,0x2fc)==0&&word(block,live)==capacity,"T6 delayed damage refuses capacity+1 without arming an event");
    for(unsigned i=0;i<capacity;++i)reinterpret_cast<fire_damage>(0x499990)(entities[i].data(),nullptr,0);
    check(word(block,live)==0&&damage_calls==capacity&&schedules==capacity,"T6 delayed damage fire invokes damage and retail signed division releases every slot");
    good=true;
    for(unsigned i=0;i<capacity;++i)
        good=good&&reinterpret_cast<take_damage>(0x499b10)(entities[i].data(),nullptr,descriptor.data(),0x3f800000);
    check(good&&word(block,live)==capacity,"T6 delayed damage recycles every slot");
    reinterpret_cast<method>(0x497a30)(block,nullptr);
    good=true;for(unsigned w=0;w<(capacity+31)/32;++w)good=good&&word(block,capacity*0x6c+w*4)==0;
    check(good,"T6 delayed damage destructor clears all constructed words");
}

static void plan_mutations()
{
    using namespace entity_capacity_b_sites;
    std::vector<site> changed(sites.begin(),sites.end());
    changed.pop_back();
    check(!actor_pools_plan::make(0x20000000,0x21000000,0x22000000,changed),"T3 dropped ledger row refuses complete plan");
    changed.assign(sites.begin(),sites.end());changed[0].before="00";
    check(!actor_pools_plan::make(0x20000000,0x21000000,0x22000000,changed),"T3 altered ledger guard refuses complete plan");
}

// Three native paths from 0x50dee0, with its own lookup/activation/cleanup
// services replaced by deterministic test receivers. Only the function's final
// epilogue is redirected to capture this synthetic frame's machine state.
static u32 frame_entry=0,frame_controller=0,frame_move=0,frame_target=0,frame_sp=0;
static u32 cap_eax=0,cap_ebx=0,cap_ecx=0,cap_edx=0,cap_esi=0,cap_edi=0,cap_ebp=0,cap_flags=0;
static std::array<std::uint8_t,0x540> controller;
static std::array<std::uint8_t,0x300> actor,team;
static std::array<u32,0x224/4> cai_vtable;
static std::array<u32,0xa0/4> target_vtable;
static u32 cai=0,target=0;
static unsigned lookup_index=0,null_mask=0,activations=0,cleanups=0,actions=0;
static void* __cdecl ai_getter() { return &cai; }
static void* __fastcall lookup(void*,void*,u32)
{
    const auto bit=1u<<lookup_index++;
    return (null_mask&bit)?nullptr:team.data();
}
static void __fastcall activation(void*,void*,void*,u32) { ++activations; }
static void __fastcall cleanup(void*,void*) { ++cleanups; }
static void __fastcall action(void*,void*,void*) { ++actions; }
__declspec(naked) static void frame_exit()
{
    __asm
    {
        mov cap_eax,eax
        mov cap_ebx,ebx
        mov cap_ecx,ecx
        mov cap_edx,edx
        mov cap_esi,esi
        mov cap_edi,edi
        mov cap_ebp,ebp
        pushfd
        pop cap_flags
        mov esp,frame_sp
        popad
        popfd
        ret
    }
}
__declspec(naked) static void frame_enter()
{
    __asm
    {
        pushfd
        pushad
        mov frame_sp,esp
        mov esi,frame_controller
        mov edi,frame_move
        mov ebx,frame_target
        mov ebp,0x31313131
        xor eax,eax
        mov ecx,0x12121212
        mov edx,0x23232323
        jmp dword ptr [frame_entry]
    }
}
static std::vector<u32> guard_run(unsigned path,unsigned nulls)
{
    controller.fill(0);actor.fill(0);team.fill(0);
    word(controller.data(),0x10)=address(actor.data());word(controller.data(),0x538)=0x99;
    word(controller.data(),0x344)=0x55;word(actor.data(),0x1c)=0x123;
    frame_controller=address(controller.data());frame_move=0x14141414;frame_target=address(&target);
    frame_entry=path==0?0x50e1ac:path==1?0x50e3ac:0x50e492;
    lookup_index=activations=cleanups=actions=0;null_mask=nulls;
    frame_enter();
    // Compare all registers, the arithmetic flags native continuations can use,
    // the team's writes, and every synthetic cross-owner side effect.
    return {cap_eax,cap_ebx,cap_ecx,cap_edx,cap_esi,cap_edi,cap_ebp,cap_flags&0x8d5,
            word(team.data(),0x260),word(team.data(),0x264),word(controller.data(),0x340),
            lookup_index,activations,cleanups,actions};
}
static void guard_tests()
{
    cai_vtable.fill(0);cai_vtable[0x21c/4]=address(reinterpret_cast<const void*>(&lookup));cai=address(cai_vtable.data());
    target_vtable.fill(0);target_vtable[0x9c/4]=address(reinterpret_cast<const void*>(&action));target=address(target_vtable.data());
    jump(0x5252e0,reinterpret_cast<const void*>(&ai_getter));
    jump(0x41b5b0,reinterpret_cast<const void*>(&activation));
    jump(0x525460,reinterpret_cast<const void*>(&cleanup));
    jump(0x50e4af,reinterpret_cast<const void*>(&frame_exit));
    std::map<u32,bytes> patched;
    for(const auto& s:entity_capacity_b_sites::sites)if(s.role=="guard")
    {
        patched[s.va]=bytes(static_cast<std::uint8_t*>(ptr(s.va)),static_cast<std::uint8_t*>(ptr(s.va))+s.before.size()/2);
        const auto original=patch_transaction::bytes_of(s.before);std::memcpy(ptr(s.va),original.data(),original.size());
    }
    FlushInstructionCache(GetCurrentProcess(),union_space,0x700000);
    std::array<std::vector<u32>,3> baseline;
    for(unsigned path=0;path<3;++path)baseline[path]=guard_run(path,0);
    for(const auto& [va,b]:patched)std::memcpy(ptr(va),b.data(),b.size());
    FlushInstructionCache(GetCurrentProcess(),union_space,0x700000);
    bool equal=true;
    for(unsigned path=0;path<3;++path)equal=equal&&guard_run(path,0)==baseline[path];
    check(equal,"T9 all three non-null retail paths preserve registers, live flags, writes and calls");
    bool nulls=true;
    for(unsigned mask=1;mask<4;++mask)
    {
        const auto a=guard_run(0,mask);
        nulls=nulls&&a[8]==0&&a[10]==1&&a[12]==1; // no claim, state1, native activation
        if(mask&1)nulls=nulls&&a[11]==1; // first null skips the second lookup
        const auto b=guard_run(1,mask);
        nulls=nulls&&b[14]==1; // native no-team action path
    }
    const auto cleanup_null=guard_run(2,1);
    nulls=nulls&&cleanup_null[13]==0;
    check(nulls,"T8 lookup null at all five sites, separately and combined: correct native continuations, no fault");
}

static void differential(unsigned capacity,void* damage_block)
{
    // Same deterministic churn, bounded to 32 live records, on the unpatched and
    // patched retail code in separate host processes. The runner compares this
    // trace exactly; addresses/indices are intentionally outside its domain.
    auto* path_arena=reinterpret_cast<get>(0x496790)();
    reinterpret_cast<void(__cdecl*)()>(0x496800)();
    std::vector<void*> path_slots;
    std::array<std::uint8_t,0x2800> manager{};
    reinterpret_cast<method>(0x4b6a40)(manager.data()+8,nullptr);
    std::vector<u32> shadow_slots;
    reinterpret_cast<method>(0x499ad0)(damage_block,nullptr);
    std::array<std::array<std::uint8_t,0x300>,32> entities{};
    std::array<u32,64> vtable{};vtable[0xb0/4]=address(reinterpret_cast<const void*>(&hit));
    for(auto& e:entities)word(e.data())=address(vtable.data());
    std::vector<unsigned> damage_slots;
    std::array<std::uint8_t,0x6c> descriptor{};word(descriptor.data(),4)=0x3f800000;word(descriptor.data(),0x14)=1;
    u32 rng=0x12345678,digest=2166136261;
    std::vector<u32> trace;
    const auto mix=[&](u32 value) { trace.push_back(value);for(unsigned i=0;i<4;++i)digest=(digest^std::uint8_t(value>>(8*i)))*16777619u; };
    bool good=true;
    for(unsigned step=0;step<4000;++step)
    {
        rng=rng*1664525+1013904223;
        const bool take=(rng&0xff)<160;
        if(path_slots.empty()||(take&&path_slots.size()<32))
        {
            auto* p=reinterpret_cast<path_take>(0x496830)(0);mix(p!=nullptr);good=good&&p;path_slots.push_back(p);
        }
        else
        {
            const auto i=(rng>>8)%path_slots.size();reinterpret_cast<path_free>(0x496900)(path_slots[i]);path_slots.erase(path_slots.begin()+i);mix(2);
        }
        mix(word(path_arena,capacity==124?0x33b80:0xd598));
        if(shadow_slots.empty()||(take&&shadow_slots.size()<32))
        {
            auto t=reinterpret_cast<take_shadow>(0x4b6ab0)(manager.data(),nullptr,0);mix(t!=0xffffffff);good=good&&t!=0xffffffff;shadow_slots.push_back(t);
        }
        else
        {
            const auto i=(rng>>8)%shadow_slots.size();reinterpret_cast<release_shadow>(0x4b6d40)(manager.data(),nullptr,shadow_slots[i]);shadow_slots.erase(shadow_slots.begin()+i);mix(2);
        }
        mix(word(manager.data()+8,capacity==124?0x24f4:0x998));
        if(damage_slots.empty()||(take&&damage_slots.size()<32))
        {
            unsigned i=0;while(word(entities[i].data(),0x2fc))++i;
            const auto ok=reinterpret_cast<take_damage>(0x499b10)(entities[i].data(),nullptr,descriptor.data(),0);
            mix(ok);good=good&&ok;damage_slots.push_back(i);
            mix(reinterpret_cast<take_damage>(0x499b10)(entities[i].data(),nullptr,descriptor.data(),0));
        }
        else
        {
            const auto i=(rng>>8)%damage_slots.size();reinterpret_cast<fire_damage>(0x499990)(entities[damage_slots[i]].data(),nullptr,0);damage_slots.erase(damage_slots.begin()+i);mix(2);
        }
        mix(word(damage_block,capacity==124?0x3670:0xe18));
    }
    for(auto* p:path_slots)reinterpret_cast<path_free>(0x496900)(p);
    for(auto t:shadow_slots)reinterpret_cast<release_shadow>(0x4b6d40)(manager.data(),nullptr,t);
    for(auto i:damage_slots)reinterpret_cast<fire_damage>(0x499990)(entities[i].data(),nullptr,0);
    check(good,"T7 retail churn completes 4000 steps per pool at live<=32");
    std::printf("TRACE %08lX\n",static_cast<unsigned long>(digest));
    if(!trace_path.empty())
    {
        std::ofstream f(trace_path,std::ios::binary);f.write(reinterpret_cast<const char*>(trace.data()),static_cast<std::streamsize>(trace.size()*sizeof(u32)));
        check(f.good(),"T7 wrote complete per-operation result/live-count trace");
    }
    reinterpret_cast<void(__cdecl*)()>(0x496800)();
    // Re-create the singleton so T6 can assert the original FIFO order exactly.
    word(ptr(0x72c9c0))=0;
    damage_calls=schedules=0;
}

static std::array<std::array<u32,32>,4> reset_tables;
static unsigned reset_hits=0,reset_index=~0u;
static void reset_slot(u32 slot)
{
    ++reset_hits;reset_index=slot;
    if(slot<32)for(auto& table:reset_tables)table[slot]=0;
}
static void controller_reset_test()
{
    std::array<std::uint8_t,0xc4a4> arena{};
    for(unsigned i=0;i<32;++i)word(arena.data(),0xc384+4*i)=i;
    word(arena.data(),0xc410)=32;
    actor_ai::reset_controller_slot=&reset_slot;
    for(auto& t:reset_tables)t.fill(0xdeadbeef);
    bool good=true;
    using pop=u32(__fastcall*)(void*,void*);
    for(unsigned i=0;i<32;++i)
    {
        const auto result=reinterpret_cast<pop>(0x522a10)(arena.data(),nullptr);
        good=good&&result==i&&reset_index==i&&reset_hits==i+1;
        for(const auto& t:reset_tables)good=good&&t[i]==0;
    }
    actor_ai::reset_controller_slot=nullptr;
    check(good&&word(arena.data(),0xc410)==0&&word(arena.data(),0xc418)==32,"W11 retail controller FIFO pop executes reset callback for all32 slots before return");
}

int main(int argc,char** argv)
{
    if(argc!=3&&argc!=4) { std::puts("usage: actor_pools_host <retail XMen2.exe> <on|stock|off|invalid|guard|allocation|protect> [trace.bin]");return 2; }
    mode=argv[2];SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc==4)trace_path=argv[3];
    if(address(union_space)!=0x401000||!load(read(argv[1])))return 3;
    if(mode=="on")plan_mutations();
    const auto before=snapshot();
    if(mode=="guard") { *reinterpret_cast<std::uint8_t*>(0x49684d)^=1; }
    if(mode=="protect") { DWORD old=0;VirtualProtect(ptr(0x491000),4096,PAGE_EXECUTE_READ,&old); }
    const auto input=snapshot();
    if(mode!="stock")actor_pools::install(reinterpret_cast<HMODULE>(0x400000));
    if(mode!="on"&&mode!="stock")
    {
        check(snapshot()==input,"T4/T11 every potentially touched image page remains byte-identical");
        if(mode=="off")
        {
            gameplay_capabilities::record baseline;baseline.finalize();agreement.finalize();
            check(rule_registrations==0&&std::equal(baseline.bytes().begin(),baseline.bytes().end(),agreement.bytes().begin()),"T4 key absent preserves released agreement bytes/digest");
        }
        else check(rule_registrations==1&&!agreement.finalize()&&!agreement.ready(),"T11 requested failure is unverified; admission stays closed");
        check(!startup_refused,"T11 certified failure leaves offline stock startup available");
    }
    else
    {
        const unsigned capacity=mode=="on"?124:32;
        if(mode=="on")check(snapshot()!=before&&agreement.finalize()&&agreement.ready(),"T10 complete production install publishes verified ActorAI");
        dependencies();
        auto* block=mode=="on"?ptr(word(ptr(0x499b42))):ptr(0x753148);
        differential(capacity,block);
        paths(capacity);shadows(capacity);
        delayed(capacity,block);
        if(mode=="on") { controller_reset_test();guard_tests(); }
        std::printf("telemetry: %s\n",actor_pools::telemetry_json().c_str());
    }
    std::printf("actor pools host: %u checks, %u failures\n",checks,failures);std::fflush(stdout);
    for(auto* p:heap_blocks)VirtualFree(p,0,MEM_RELEASE);
    return failures?1:0;
}
