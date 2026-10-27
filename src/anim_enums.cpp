#include "anim_enums.hpp"
#include "anim_enum_registry.hpp"
#include "anim_enum_names.hpp"
#include "anim_enum_sites.hpp"
#include "patch_transaction.hpp"
#include "ini.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <set>

namespace anim_enums
{
namespace
{
using u32=std::uint32_t;
using namespace anim_enum_layout;
alignas(16) std::array<unsigned char,raised.records*raised.record_stride> cache{};
alignas(16) std::array<unsigned char,raised.table_size> table{};
anim_enum_registry::registry names;
bool installed=false, requested=false, trace=false;
unsigned peak=0;
std::set<std::string> dropped;
// DLL-owned executable storage; every stub is complete and RX before publication.
void* caves=nullptr;
u32 addr(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
void put32(unsigned char* p,u32 value){std::memcpy(p,&value,4);}
u32 get32(const unsigned char* p){u32 v;std::memcpy(&v,p,4);return v;}

void settle(bool verified)
{
    gameplay_agreement::register_rule(gameplay_capabilities::anim_enums_id,1,names.agreement(),verified);
}
void abandon()
{
    installed=false;
    if(caves){VirtualFree(caves,0,MEM_RELEASE);caves=nullptr;}
    settle(false);
    logger::write("AnimEnums: startup transaction refused; native bytes retained, rule 48 unverified");
}
void retain()
{
    installed=false;settle(false);game_version::refuse_startup();
    logger::write("AnimEnums: uncertified rollback; backing and caves retained; startup refused");
}

void __cdecl registration()
{
    reinterpret_cast<void(__cdecl*)()>(0x426a50)();
    if(trace)logger::write("AnimEnums: registration 210 native + %zu extension names; capacity 512, records 128",names.size());
}
int __cdecl lookup_extension(const char* text)
{
    const int id=names.lookup(text);
    if(id<0&&trace&&std::strncmp(text,"ea_",3)==0&&dropped.emplace(text).second)
        logger::write("AnimEnums: dropped unknown animation enum %s",text);
    return id;
}
// Retail 0x426540 has already lowercased/truncated into [esp+4]. Its native map
// result is in eax, *out at [esp+0x50]. Keep hits on the original map path.
__declspec(naked) void lookup_miss()
{
    __asm {
        cmp eax,03fffffffh
        jne native_hit
        push edx
        push ecx
        lea ecx,[esp+0ch]
        push ecx
        call lookup_extension
        add esp,4
        pop ecx
        pop edx
        cmp eax,-1
        je absent
        mov edx,[esp+50h]
        mov [edx],eax
        mov eax,04265e3h
        jmp eax
    native_hit:
        push 04265d6h
        ret
    absent:
        push 04265c7h
        ret
    }
}

bool read_sidecar(std::string& text,std::string& why)
{
    // Same base-folder convention as Data/x1leader.txt and the INI. The mod
    // loader's tier list is deliberately not consulted during DLL startup.
    const auto file=logger::module_dir()/L"Data"/L"anim_enums.txt";
    FILE* f=_wfopen(file.c_str(),L"rb");
    if(!f){if(errno==ENOENT)return true;why="cannot read Data/anim_enums.txt";return false;}
    char buffer[4096];
    while(const auto got=std::fread(buffer,1,sizeof buffer,f))
    {
        text.append(buffer,got);
        if(text.size()>1024*1024){std::fclose(f);why="Data/anim_enums.txt exceeds 1 MiB";return false;}
    }
    const bool ok=!std::ferror(f);std::fclose(f);
    if(!ok)why="Data/anim_enums.txt read failed";
    return ok;
}
}

void contribute(patch_transaction::transaction& transaction,HMODULE game)
{
    // No state, backing, agreement entry or code is changed on the OFF path.
    const auto key=ini::text(L"Limits",L"AnimEnums");
    if(!key||*key=="0")return;
    requested=true;
    game_version::require_gameplay_agreement(game);
    std::string sidecar,why;
    if(*key!="512")why="only [Limits] AnimEnums=512 is supported";
    else if(addr(game)!=0x400000)why="executable is not at retail base 0x400000";
    else if(read_sidecar(sidecar,why))names.load(sidecar,why);
    if(!why.empty())
    {
        logger::write("AnimEnums: refused: %s; rule 48 unverified",why.c_str());settle(false);return;
    }
    trace=ini::flag(L"Test",L"AnimEnumTrace",false);
    cache.fill(0);table.fill(0);
    for(u32 i=0;i<raised.records;++i)put32(cache.data()+i*raised.record_stride+raised.count_off,raised.enums);
    caves=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!caves){settle(false);logger::write("AnimEnums: cannot allocate bounds caves");return;}
    patch_transaction::contribution c;
    c.owner="AnimEnums";
    unsigned bound=0;
    for(const auto& s:anim_enum_sites::sites)
    {
        const auto old=patch_transaction::bytes_of(s.old_hex);
        const std::string label=std::string(s.row)+" AnimEnums "+patch_transaction::hex(s.va);
        c.guards.push_back({s.va,old,label});
        if(s.type==anim_enum_sites::kind::operand)
        {
            auto bytes=old;
            for(unsigned i=0;i<s.edit_count;++i)
                put32(bytes.data()+s.edits[i].offset,anim_enum_sites::resolve(s.edits[i].expression,addr(cache.data()),addr(table.data())));
            c.writes.push_back({s.va,std::move(bytes),label});
        }
        else if(s.type==anim_enum_sites::kind::bound)
        {
            auto* code=static_cast<unsigned char*>(caves)+32*bound++;
            // Replace cmp r32,16 / signed jge short with cmp r32,128;
            // jge original target; jmp fallthrough. 83 /7 imm8 cannot encode +128.
            code[0]=0x81;code[1]=old[1];put32(code+2,raised.records);
            code[6]=0x0f;code[7]=0x8d;
            const u32 target=s.va+5+static_cast<std::int8_t>(old[4]);
            put32(code+8,target-(addr(code)+12));
            code[12]=0xe9;put32(code+13,s.va+5-(addr(code)+17));
            c.writes.push_back(patch_transaction::relocation(s.va,"e9",true,addr(code),0,label));
        }
        else c.writes.push_back(patch_transaction::relocation(s.va,s.row=="M1"?"e8":"e9",true,
            s.row=="M1"?addr(reinterpret_cast<void*>(&registration)):addr(reinterpret_cast<void*>(&lookup_miss)),s.row=="M1"?0:2,label));
    }
    DWORD previous=0;
    if(!VirtualProtect(caves,4096,PAGE_EXECUTE_READ,&previous)||!FlushInstructionCache(GetCurrentProcess(),caves,4096))
    {abandon();return;}
    c.publish=[] {installed=true;settle(true);logger::write("AnimEnums: %s installed 512 enums, %zu extension names, 128 cache records; 81 edits, 5 caves, 2 hooks",builtin_table(),names.size());};
    c.abandon=&abandon;c.retain=&retain;
    if(!transaction.add(std::move(c)))abandon();
}
const descriptor* layout(){return installed?&raised:nullptr;}
const char* builtin_table(){return anim_enum_registry::builtin_block;}
int extension_id(const char* lowercased_native_buffer){return installed&&lowercased_native_buffer?names.lookup(lowercased_native_buffer):-1;}
std::string status()
{
    if(!installed)return requested?"anim enums unverified":"anim enums stock 210; records 16";
    unsigned live=0;
    for(u32 i=0;i<raised.records;++i)live+=(cache[i*raised.record_stride+1]&1)!=0;
    peak=(std::max)(peak,live);
    return "anim enums "+std::to_string(get32(table.data()+raised.table_count_off))+"/512; ext "+std::to_string(names.size())+"; records peak "+std::to_string(peak)+"/128";
}
}
