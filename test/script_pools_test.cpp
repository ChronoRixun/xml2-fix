// Owned retail-image fixture: calls only isolated pool/compiler routines, never
// the game entry point. Record construction uses invented inert records.
#include "script_pools.hpp"
#include "script_pools_plan.hpp"
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include "script_functions_rules.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <vector>

namespace
{
unsigned checks=0;
std::optional<std::string> key;
bool unsafe=false,required=false;
std::vector<gameplay_capabilities::entry> entries;
std::vector<bool> verified;
std::vector<unsigned char> retail;
std::array<unsigned,5> observed{};
void check(bool ok,const char* what){++checks;if(!ok){std::printf("FAIL %s (%u)\n",what,checks);std::fflush(stdout);std::abort();}}
using script_pools::u32;
LONG WINAPI fault(EXCEPTION_POINTERS* p)
{
    std::printf("FAULT code=%08lX address=%p\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress);
    std::fflush(stdout);return EXCEPTION_EXECUTE_HANDLER;
}
void* at(u32 va){return reinterpret_cast<void*>(va);}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
u32& field(void* p,u32 off){return *reinterpret_cast<u32*>(static_cast<unsigned char*>(p)+off);}
void load()
{
    DWORD fixture_protection=0;
    check(VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&fixture_protection)!=0,"fixture data writable");
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail.data()+*reinterpret_cast<const u32*>(retail.data()+0x3c));
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
    {
        const auto& s=IMAGE_FIRST_SECTION(nt)[i];
        if(s.VirtualAddress>=0x1000&&s.VirtualAddress+s.Misc.VirtualSize<=0x401000)
        {
            DWORD protection=0;check(VirtualProtect(at(0x400000+s.VirtualAddress),s.Misc.VirtualSize,PAGE_EXECUTE_READWRITE,&protection)!=0,"fixture protection");
            std::memset(at(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);
            std::memcpy(at(0x400000+s.VirtualAddress),retail.data()+s.PointerToRawData,s.SizeOfRawData);
        }
    }
    entries.clear();verified.clear();required=false;unsafe=false;
}
template<class T>void jump(u32 va,T f)
{
    auto* p=static_cast<unsigned char*>(at(va));p[0]=0xe9;
    const u32 rel=address(reinterpret_cast<const void*>(f))-va-5;std::memcpy(p+1,&rel,4);
}
void* __fastcall empty12(void* p,void*){std::memset(p,0,0x38);return p;}
void* __fastcall empty560(void* p,void*){std::memset(p,0,560);return p;}
void* __fastcall empty1476(void* p,void*){std::memset(p,0,1476);return p;}
void __cdecl copy56(void* dst,const void* src){std::memcpy(dst,src,56);}
void __cdecl copy560(void* dst,const void* src){std::memcpy(dst,src,560);}
void __cdecl copy1476(void* dst,const void* src){std::memcpy(dst,src,1476);}
void __fastcall destroy(void*,void*,int){}
char* __cdecl copy_text(char* out,const char* in,std::size_t n){return std::strncpy(out,in,n);}
int __cdecl compare_text(const char* a,const char* b){return _stricmp(a,b);}
void* __fastcall cache_miss(void*,void*,const char*){return nullptr;}
void __fastcall cache_add(void*,void*,const char*,void*){}
void __fastcall cache_remove(void*,void*,const char*){}
void __fastcall cache_clear_stub(void*,void*){}
unsigned global_clears=0;
void __fastcall clear_globals_stub(void*,void*){++global_clears;}
script_pools::cache_hooks inert_cache_hooks()
{
    return {address(reinterpret_cast<const void*>(&cache_miss)),address(reinterpret_cast<const void*>(&cache_add)),
        address(reinterpret_cast<const void*>(&cache_remove)),address(reinterpret_cast<const void*>(&cache_clear_stub))};
}
void services()
{
    jump(0x4d65d0,&empty12);jump(0x4d7ab0,&empty560);jump(0x4d7840,&empty1476);
    jump(0x4d7a10,&copy56);jump(0x4d7f20,&copy560);jump(0x4d7f80,&copy1476);
    field(at(0x6905d0),0)=address(reinterpret_cast<const void*>(&destroy));
    field(at(0x6905fc),0)=address(reinterpret_cast<const void*>(&destroy));
    jump(0x672170,&copy_text);jump(0x672562,&compare_text);
}
void __cdecl notify(unsigned p){if(p<5)++observed[p];}
void __cdecl begin(u32){}
void apply_plan()
{
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,16384,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));check(code!=nullptr,"code allocation");
    auto p=script_pools::make_plan(address(code),address(reinterpret_cast<const void*>(&notify)),address(reinterpret_cast<const void*>(&begin)),inert_cache_hooks());
    check(p.code.size()<16384,"code fits backing");std::memcpy(code,p.code.data(),p.code.size());
    patch_transaction::transaction tx;check(tx.add(std::move(p.contribution)),"plan accepted");check(tx.commit()==patch_transaction::outcome::committed,"plan commits");
    // Released on process exit: native image remains redirected for the run.
}
using unary=void*(__fastcall*)(void*,void*);
u32 segment_start=0;
__declspec(naked) void __fastcall segment(void*,void*)
{
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov esi,ecx
        xor ebx,ebx
        call segment_start
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
void constructor(void* owner,unsigned k)
{
    constexpr u32 starts[]={0x4d84a0,0x4d83d2,0x4d84e1,0x4d85df};
    constexpr u32 stops[]={0x4d84e1,0x4d8416,0x4d8538,0x4d8618};
    auto* p=static_cast<unsigned char*>(at(stops[k]));const auto saved=*p;*p=0xc3;
    // The values segment starts after the constructor's three string arguments.
    unsigned char stack_adjust[3]{};if(k==1){std::memcpy(stack_adjust,at(0x4d8406),3);std::memset(at(0x4d8406),0x90,3);}
    segment_start=starts[k];segment(owner,nullptr);
    *p=saved;if(k==1)std::memcpy(at(0x4d8406),stack_adjust,3);
}
void reset_pool(void* owner,unsigned k)
{
    constexpr u32 starts[]={0x4d806a,0x4d8093,0x4d803d,0x4d7ff7};
    constexpr u32 stops[]={0x4d8093,0x4d80bc,0x4d806a,0x4d8017};
    auto* p=static_cast<unsigned char*>(at(stops[k]));const auto saved=*p;*p=0xc3;
    segment_start=starts[k];segment(owner,nullptr);*p=saved;
}
void native_pool(unsigned k,bool raised)
{
    const auto& d=raised?script_pools::raised[k]:script_pools::stock[k];
    const u32 size=raised?script_pools::raised_size:script_pools::retail_size;
    std::vector<unsigned char> bytes(size+128,0xa5);auto* owner=bytes.data()+64;
    std::memset(owner+d.base,0xcc,d.size);
    constructor(owner,k);
    auto* pool=owner+d.base;
    check(field(pool,d.live)==0&&field(pool,d.free)==d.capacity,"constructor initializes counts");
    for(u32 j=0;j<d.words;++j)check(field(pool,d.built+4*j)==0&&field(pool,d.used+4*j)==0,"constructor clears every bitmap word");
    constexpr u32 allocators[]={0x4d7e60,0x4d6570,0x4d86c0,0x4d8700};
    constexpr u32 frees[]={0x4d7790,0x4d5ff0,0x4d7c90,0x4d7c10};
    constexpr u32 clear[]={0x4d7470,0x4d52f0,0x4d7d10,0x4d7b10};
    auto allocate=[&]()->void*{
        if(k==1)return reinterpret_cast<void*(__fastcall*)(void*,void*,int)>(allocators[k])(owner,nullptr,71);
        return reinterpret_cast<unary>(allocators[k])(owner,nullptr);
    };
    auto free_slot=[&](u32 index){reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(frees[k])(pool,nullptr,index);};
    std::set<u32> seen;
    for(u32 i=0;i<d.capacity;++i)
    {
        auto* value=allocate();check(value==pool+i*d.stride,"native allocator sequential unique slot");seen.insert(address(value));
    }
    check(seen.size()==d.capacity,"no aliases up to capacity");
    const auto before=std::vector<unsigned char>(pool,pool+d.size);
    check(!allocate(),"allocation N+1 refused");check(std::equal(before.begin(),before.end(),pool),"refusal changes no pool byte");
    check(field(pool,d.live)==d.capacity&&field(pool,d.free)==0,"full counts exact");
    if(k==0)
    {
        // The real line parser must skip control directives as well as ordinary
        // statements while full, before touching the compiled script.
        std::array<unsigned char,560> script;script.fill(0x5a);const auto untouched=script;
        field(at(script_pools::system_pointer),0)=address(owner);
        auto parse=reinterpret_cast<bool(__fastcall*)(void*,void*,const char*)>(0x4d9740);
        for(const char* line:{"invented()","else","endif"})check(!parse(script.data(),nullptr,line),"native full-pool parser skips every line kind");
        check(script==untouched,"refused control lines leave compiler bookkeeping untouched");
        check(std::equal(before.begin(),before.end(),pool),"refused parser lines leave pool untouched");
        field(at(script_pools::system_pointer),0)=0;
    }
    // Free out of order, recycle across 31/32 and retail/raised boundaries.
    for(u32 i=0;i<d.capacity;++i)free_slot((i* (k==1?3u:1u))%d.capacity);
    // 8192 and 1556 are coprime with 3, so every value slot was freed once.
    check(field(pool,d.live)==0&&field(pool,d.free)==d.capacity,"all frees restore counts");
    seen.clear();for(u32 i=0;i<d.capacity;++i)check(seen.insert(address(allocate())).second,"ring wrap reuses each slot once");
    check(!allocate(),"second full ring refuses");
    for(u32 i=0;i<d.capacity;++i)free_slot(i);
    // Exercise the native findNext/teardown loop with every word populated.
    if(k==1)for(u32 i=0;i<d.capacity;++i)allocate();
    else for(u32 j=0;j<d.words;++j)field(pool,d.built+4*j)=0xffffffff;
    reinterpret_cast<unary>(clear[k])(pool,nullptr);
    for(u32 j=0;j<d.words;++j)check(field(pool,d.built+4*j)==0,"teardown clears every bitmap word");
    for(u32 j=0;j<d.words;++j)field(pool,d.used+4*j)=0xffffffff;
    field(pool,d.live)=d.capacity;
    reset_pool(owner,k);
    check(field(pool,d.live)==0&&field(pool,d.free)==d.capacity,"native bulk reset restores counts");
    for(u32 j=0;j<d.words;++j)check(field(pool,d.built+4*j)==0&&field(pool,d.used+4*j)==0,"native bulk reset clears both full bitmaps");
    check(std::all_of(bytes.begin(),bytes.begin()+64,[](auto c){return c==0xa5;})&&std::all_of(bytes.end()-64,bytes.end(),[](auto c){return c==0xa5;}),"outer canaries intact");
    // The four constructor segments own only the pool and one native vtable
    // assignment at +2f108; all other preserved owner bytes remain unchanged.
    for(u32 j=0;j<size;++j)
        if(!(j>=d.base&&j<d.base+d.size)&&!(k==3&&j>=0x2f108&&j<0x2f10c))check(owner[j]==0xa5,"unmoved owner bytes preserved");
}
void guard_test(bool mutate)
{
    // Enter the actual compiler call checker. A synthetic function takes one
    // integer; the statement's literal pointer is NULL (pool exhaustion).
    std::array<u32,4> entry{0,0,address("n"),address("i")};
    auto __fastcall lookup(void*,void*,const char*) -> void*;
    // The fixture puts a tiny return-immediate lookup in owned image memory.
    auto* code=static_cast<unsigned char*>(at(0x4d6910));code[0]=0xb8;const u32 e=address(entry.data());std::memcpy(code+1,&e,4);code[5]=0xc2;code[6]=4;code[7]=0;
    code=static_cast<unsigned char*>(at(0x4d8770));code[0]=0x31;code[1]=0xc0;code[2]=0xc3;
    if(mutate){auto b=patch_transaction::bytes_of("8b11ff5208");std::memcpy(at(0x4d89f1),b.data(),b.size());}
    std::array<u32,16> stmt{};stmt[9]=1;
    std::array<unsigned char,0x300> script{};
    const auto result=reinterpret_cast<u32(__fastcall*)(void*,void*,const char*,void*)>(0x4d8970)(script.data(),nullptr,"invented",stmt.data());
    check(result==0,"compiler null literal drops line");check(observed[4]>0,"null guard records refusal");
}
bool guard_survives(bool mutate)
{
    __try {guard_test(mutate);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void guard_mutations()
{
    auto p=script_pools::make_plan(0x20000000,0x21000000,0x22000000,inert_cache_hooks());
    for(const auto& g:p.contribution.guards)
        for(unsigned i=0;i<g.bytes.size();++i)
        {
            auto* byte=static_cast<unsigned char*>(at(g.va))+i;*byte^=1;
            patch_transaction::transaction tx;check(tx.add(p.contribution),"mutation contribution accepted");
            check(tx.commit()==patch_transaction::outcome::refused_guard,"each mutated guard byte refuses");
            *byte^=1;
            for(const auto& s:script_pool_sites::sites)
            {
                const auto expected=patch_transaction::bytes_of(s.before);
                check(std::memcmp(at(s.va),expected.data(),expected.size())==0,"guard refusal writes zero bytes");
            }
        }
}
void native_cache_oracle()
{
    std::vector<unsigned char> owner(script_pools::retail_size);
    auto* map=owner.data()+0x26f70;auto* nodes=map+12;
    field(map,0)=0x690748;field(map,4)=0x3fffffff;field(map,8)=0xffffffff;
    reinterpret_cast<unary>(0x4d6440)(nodes,nullptr);
    auto insert=reinterpret_cast<void(__fastcall*)(void*,void*,const char*,void*)>(0x4d71f0);
    auto lookup=reinterpret_cast<void*(__fastcall*)(void*,void*,const char*)>(0x4d6980);
    insert(owner.data(),nullptr,"Name/Unit.py",at(0x1234));
    check(lookup(owner.data(),nullptr,"NAME/unit.PY")==at(0x1234),"retail cache comparison is case insensitive");
    insert(owner.data(),nullptr,"name/unit.py",at(0x5678));
    check(lookup(owner.data(),nullptr,"Name/Unit.py")==at(0x1234),"retail cache preserves first duplicate");
    std::string long_key(255,'x');
    insert(owner.data(),nullptr,(long_key+"a").c_str(),at(0x5678));
    check(lookup(owner.data(),nullptr,(long_key+"b").c_str())==at(0x5678),"retail cache truncates keys at 255 bytes");
    check(field(nodes,0x7fa4)==2,"retail filename-cache live offset confirmed");
    // Negative control: its node ring, independent of the compiled-object
    // pool, aliases the first live name at 121 with a negative free count.
    reinterpret_cast<unary>(0x4d5e50)(nodes,nullptr);field(nodes,0x7fa4)=0;
    auto pop=reinterpret_cast<u32(__fastcall*)(void*,void*)>(0x4d57b0);
    for(u32 i=0;i<120;++i)check(pop(nodes,nullptr)==i,"retail cache ring has 120 unique slots");
    check(pop(nodes,nullptr)==0&&field(nodes,0x7f90)==0xffffffff&&field(nodes,0x7fa4)==121,"retail cache's 121st pop aliases live slot zero");
}
void expanded_cache()
{
    const auto& d=script_pools::raised[2];
    std::vector<unsigned char> bytes(script_pools::raised_size+128,0xb6);auto* owner=bytes.data()+64;
    constructor(owner,0);constructor(owner,1);constructor(owner,2);
    field(at(script_pools::system_pointer),0)=address(owner);
    auto insert=reinterpret_cast<void(__fastcall*)(void*,void*,const char*,void*)>(0x4d71f0);
    auto lookup=reinterpret_cast<void*(__fastcall*)(void*,void*,const char*)>(0x4d6980);
    auto unload=reinterpret_cast<void(__fastcall*)(void*,void*,const char*)>(0x4d94d0);
    auto allocate=reinterpret_cast<unary>(0x4d86c0);
    std::vector<std::string> names;std::vector<void*> scripts;
    for(u32 i=0;i<512;++i)
    {
        names.push_back(i==0?"Name/Unit.py":i==1?std::string(255,'x')+"a":"invented-cache-"+std::to_string(i)+".py");
        auto* script=allocate(owner,nullptr);check(script!=nullptr,"cache fixture allocates compiled object");scripts.push_back(script);
        insert(owner,nullptr,names.back().c_str(),script);
    }
    for(u32 i=0;i<512;++i)check(lookup(owner,nullptr,names[i].c_str())==scripts[i],"all 512 cached names resolve to their original objects");
    check(lookup(owner,nullptr,"NAME/unit.PY")==scripts[0],"raised cache keeps native case comparison");
    check(lookup(owner,nullptr,(std::string(255,'x')+"b").c_str())==scripts[1],"raised cache keeps native truncation");
    field(at(0x787734),0)=123;
    insert(owner,nullptr,"name/unit.py",scripts[1]);
    check(field(at(0x787734),0)==0&&lookup(owner,nullptr,names[0].c_str())==scripts[0],"cache hit clears native compiler state and keeps first pointer");
    insert(owner,nullptr,"refused-cache-513.py",scripts[0]);
    check(lookup(owner,nullptr,"refused-cache-513.py")==nullptr,"cache entry 513 refuses without aliasing");
    auto* statement=reinterpret_cast<unary>(0x4d7e60)(owner,nullptr);
    auto* value=reinterpret_cast<void*(__fastcall*)(void*,void*,int)>(0x4d6570)(owner,nullptr,71);
    check(statement&&value,"cached-script unload fixture owns a native statement and value");
    field(statement,0x24)=1;field(statement,8)=address(value);field(scripts[120],0)=address(statement);
    unload(owner,nullptr,names[120].c_str());
    check(!lookup(owner,nullptr,names[120].c_str())&&field(owner+d.base,d.live)==511,"unload removes name and releases compiled object");
    check(field(owner+script_pools::raised[0].base,script_pools::raised[0].live)==0&&
        field(owner+script_pools::raised[1].base,script_pools::raised[1].live)==0,"native cached-script unload releases its statement tree and owned values");
    auto* recycled=allocate(owner,nullptr);check(recycled==scripts[120],"unloaded compiled slot recycles");
    insert(owner,nullptr,"recycled-cache.py",recycled);
    check(lookup(owner,nullptr,"recycled-cache.py")==recycled&&lookup(owner,nullptr,names[511].c_str())==scripts[511],"recycled name coexists with slot 511");
    check(script_pools::telemetry_json().find("\"cached_scripts\":{\"cap\":512,\"live\":512,\"peak_session\":512,\"refused\":1}")!=std::string::npos,"cache live, peak and refusal telemetry");
    // Run the production reset observer, with its engine service and native
    // continuation isolated. All four pool-reset segments ran above.
    std::array<unsigned char,5> saved{};std::memcpy(saved.data(),at(0x468d40),saved.size());
    const auto resume=*static_cast<unsigned char*>(at(0x4d7fcd));
    *static_cast<unsigned char*>(at(0x4d7fcd))=0xc3;jump(0x468d40,&clear_globals_stub);
    reinterpret_cast<unary>(0x4d7fc8)(owner+0x2f114,nullptr);
    *static_cast<unsigned char*>(at(0x4d7fcd))=resume;std::memcpy(at(0x468d40),saved.data(),saved.size());
    check(global_clears==1,"cache reset observer preserves original globals-clear call");
    check(!lookup(owner,nullptr,names[0].c_str())&&!lookup(owner,nullptr,"recycled-cache.py"),"bulk reset invalidates cached pointers");
    check(script_pools::telemetry_json().find("\"cached_scripts\":{\"cap\":512,\"live\":0")!=std::string::npos,"reset publishes empty cache");
    for(u32 j=0x26f70;j<0x2f108;++j)check(owner[j]==0xb6,"native cache/save-adjacent owner bytes never move or change");
    check(std::all_of(bytes.begin(),bytes.begin()+64,[](auto c){return c==0xb6;})&&std::all_of(bytes.end()-64,bytes.end(),[](auto c){return c==0xb6;}),"cache fixture outer canaries intact");
    field(at(script_pools::system_pointer),0)=0;
}
}
namespace ini {std::optional<std::string> text(const wchar_t*,const wchar_t*){return key;}}
namespace logger {void write(const char*,...){} }
namespace gameplay_agreement
{
bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<u32,4> values,bool ok)
{entries.push_back({id,revision,values});verified.push_back(ok);return true;}
}
namespace game_version
{
bool require_gameplay_agreement(HMODULE){required=true;return true;}
void refuse_startup(){unsafe=true;}
}
int main(int argc,char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(&fault);
    const char* exe=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");check(exe!=nullptr,"retail exe required");
    std::ifstream f(exe,std::ios::binary);retail={std::istreambuf_iterator<char>(f),{}};check(retail.size()>0x200000,"retail image read");
    const std::string mode=argc>2?argv[2]:"all";
    if(mode=="mutate-guard")
    {
        load();apply_plan();
        if(!guard_survives(true)){std::puts("FAIL removed values guard dereferences NULL (negative control)");return 42;}
        return 1;
    }
    load();key.reset();const auto original=std::vector<unsigned char>(static_cast<unsigned char*>(at(0x4d4ea0)),static_cast<unsigned char*>(at(0x4da600)));
    script_pools::require_agreement(GetModuleHandleW(nullptr));
    script_pools::install(GetModuleHandleW(nullptr));check(entries.empty()&&!required,"OFF has no rule or agreement request");check(std::equal(original.begin(),original.end(),static_cast<unsigned char*>(at(0x4d4ea0))),"OFF changes zero script bytes");
    check(script_pools::value_makers_match(),"retail value makers certified");
    for(const char* off:{"620","0","stock"})
    {
        key=off;script_pools::require_agreement(GetModuleHandleW(nullptr));script_pools::install(GetModuleHandleW(nullptr));
        check(entries.empty()&&!required,"explicit stock has no rule or agreement request");
        check(std::equal(original.begin(),original.end(),static_cast<unsigned char*>(at(0x4d4ea0))),"explicit stock changes zero bytes");
    }
    key.reset();
    if(mode=="invalid"||mode=="owner"||mode=="guard")
    {
        key=mode=="invalid"?"x2":"x1";
        script_pools::require_agreement(GetModuleHandleW(nullptr));check(required,"requested profile enlists before startup");
        if(mode=="owner")field(at(script_pools::system_pointer),0)=1;
        if(mode=="guard")*static_cast<unsigned char*>(at(0x4d89f1))^=1;
        script_pools::install(GetModuleHandleW(nullptr));
        check(entries.size()==1&&!verified[0]&&entries[0].values==std::array<u32,4>{0,0,0,0},"failed request is unverified");
        check(!unsafe,"preflight failure is reversible");std::printf("PASS ScriptPools %s: %u checks\n",mode.c_str(),checks);return 0;
    }
    if(mode=="functions")
    {
        const auto reg=script_functions_rules::register_after(0x20000000);
        const auto look=script_functions_rules::lookup_after(0x20000100);
        std::memcpy(at(script_functions_rules::register_site),reg.data(),reg.size());
        std::memcpy(at(script_functions_rules::lookup_site),look.data(),look.size());
        key="x1";script_pools::require_agreement(GetModuleHandleW(nullptr));script_pools::install(GetModuleHandleW(nullptr));
        check(entries.size()==1&&verified[0],"ScriptFunctions redirects compose with production install");
        check(std::memcmp(at(script_functions_rules::register_site),reg.data(),reg.size())==0&&std::memcmp(at(script_functions_rules::lookup_site),look.data(),look.size())==0,"ScriptFunctions bytes preserved");
        std::printf("PASS ScriptPools ScriptFunctions composition: %u checks\n",checks);return 0;
    }
    load();guard_mutations();
    for(unsigned k=0;k<4;++k){load();services();native_pool(k,false);}
    load();services();native_cache_oracle();
    load();apply_plan();services();for(unsigned k=0;k<4;++k)native_pool(k,true);
    check(observed[0]>=2&&observed[1]>=2&&observed[2]>=2&&observed[3]>=2,"all allocation refusals counted");
    guard_test(false);
    load();key="x1";script_pools::require_agreement(GetModuleHandleW(nullptr));check(required,"x1 enlists before startup");
    script_pools::install(GetModuleHandleW(nullptr));check(entries.size()==1&&verified[0]&&entries[0].id==59&&entries[0].values==std::array<u32,4>{4096,0,0,0},"production rule 59 exact payload");
    check(script_pools::descriptor()[1].capacity==8192,"published descriptor contains value capacity");
    check(script_pools::value_makers_match(),"relocated value makers certified through published guards");
    for(u32 a:{0x4d6570u,0x4d6576u,0x4d6578u,0x4d9445u,0x4d944bu,0x4d9450u})
    {
        auto* p=static_cast<unsigned char*>(at(a));*p^=1;check(!script_pools::value_makers_match(),"published value guard rejects mutation");*p^=1;
    }
    gameplay_capabilities::record stock_record,raised_record,failed_record;
    check(stock_record.finalize(),"stock agreement finalizes");
    check(raised_record.add(59,1,{4096,0,0,0},true)&&raised_record.finalize(),"raised agreement finalizes");
    gameplay_capabilities::difference diff;
    check(raised_record.compare(stock_record.bytes(),&diff)==gameplay_capabilities::result::entries,"raised peer refuses stock");
    char description[256]{};raised_record.describe(diff.buckets,diff.local_only,true,description,sizeof description);
    check(std::strstr(description,"ScriptPools")!=nullptr,"mismatch names ScriptPools");
    check(stock_record.compare(raised_record.bytes(),&diff)==gameplay_capabilities::result::entries,"stock peer refuses raised");
    check(!failed_record.add(59,1,{0,0,0,0},false)&&!failed_record.finalize(),"failed request closes agreement");
    std::vector<unsigned char> owner(script_pools::raised_size);
    field(at(script_pools::system_pointer),0)=address(owner.data());
    std::strcpy(static_cast<char*>(at(0x72a758)),"invented-a");
    for(unsigned i=0;i<4;++i)field(owner.data(),script_pools::raised[i].base+script_pools::raised[i].live)=i+3;
    script_pools::sample();
    check(script_pools::telemetry_json().find("\"peak_zone\":3,\"peak_session\":3")!=std::string::npos,"D0 zone and session peaks sampled");
    field(owner.data(),script_pools::raised[0].base+script_pools::raised[0].live)=script_pools::raised[0].capacity;
    check(!reinterpret_cast<unary>(0x4d7e60)(owner.data(),nullptr),"production refusal callback returns native NULL");
    script_pools::sample();
    field(owner.data(),script_pools::raised[0].base+script_pools::raised[0].live)=2;
    std::strcpy(static_cast<char*>(at(0x72a758)),"invented-b");script_pools::sample();
    check(script_pools::telemetry_json().find("\"peak_zone\":2,\"peak_session\":4096,\"refused\":1,\"refused_zone\":0")!=std::string::npos,"D0 zone peak resets, session peak/refusal retained");
    field(at(script_pools::system_pointer),0)=0;script_pools::sample();
    services();expanded_cache();
    check(!unsafe,"startup certified");std::printf("PASS ScriptPools retail pool lifecycles, guard, production install: %u checks\n",checks);return 0;
}
