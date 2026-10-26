// TS1-TS6: production contribution and retail instructions in our own image.
// Never calls the game entry point. Every character and sound is synthetic.
#include <Windows.h>
#include "char_sounds.hpp"
#include "char_sound_sites.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "gameplay_capabilities.hpp"
#define gameplay_capabilities gameplay_capabilities_charsounds_base
#include "gameplay_capabilities_charsounds_base.hpp"
#undef gameplay_capabilities
#include "patch_transaction.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdarg>
#include <xmmintrin.h>
namespace {
using u32=std::uint32_t;
unsigned checks=0;bool required=false,unsafe=false;
std::optional<std::string> key;
gameplay_capabilities::record agreement;
entity_capacity_layout::descriptor layout;
void check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::fflush(stdout);std::abort();}}
void* ptr(u32 a){return reinterpret_cast<void*>(a);}
template<class T>T get(const void* p,u32 o=0){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof v);return v;}
template<class T>void put(void* p,u32 o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof v);}
template<class T>void jump(u32 a,T f){put<unsigned char>(ptr(a),0,0xe9);put<u32>(ptr(a),1,static_cast<u32>(reinterpret_cast<std::uintptr_t>(f))-a-5);}
std::vector<unsigned char> retail;
void load(){const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail.data()+get<u32>(retail.data(),0x3c));
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(nt)[i];
        if(s.VirtualAddress>=0x1000&&s.VirtualAddress+s.Misc.VirtualSize<=0x401000){
            std::memset(ptr(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);
            std::memcpy(ptr(0x400000+s.VirtualAddress),retail.data()+s.PointerToRawData,s.SizeOfRawData);}}}
std::array<unsigned char,0x40000> pool{};
std::array<unsigned char,0x10000> suffix_pool{};
std::vector<std::string> names{std::string{}};
u32 __fastcall intern(void*,void*,const char* text,int,int){
    auto it=std::find(names.begin(),names.end(),text);if(it!=names.end())return static_cast<u32>(it-names.begin());
    const auto id=static_cast<u32>(names.size());names.emplace_back(text);put(pool.data(),4+id*4,id*128);
    std::strcpy(reinterpret_cast<char*>(pool.data()+0x4008+id*128),text);return id;}
void* __cdecl key_pool(){return pool.data();}
void* __cdecl animation_pool(){return suffix_pool.data();}
int __cdecl length(const char* p){return static_cast<int>(std::strlen(p));}
int __cdecl compare(const char* a,const char* b){return std::strcmp(a,b);}
void __cdecl copy(char* a,const char* b,int n){strncpy_s(a,n,b,_TRUNCATE);}
void* __fastcall append(void* a,void*,const char* b){strcat_s(static_cast<char*>(a),256,b);return a;}
char* __cdecl find(char* a,int ch){return std::strchr(a,ch);}
void __fastcall cookie(u32,void*){}
std::array<void*,32> mode_vt{},sound_vt{};
void* mode=mode_vt.data();void* sound=sound_vt.data();
void* __cdecl mode_object(){return &mode;}
void* __cdecl sound_object(){return &sound;}
bool __fastcall is_special(void*,void*){return false;}
bool __fastcall has_sound(void*,void*,const char*){return true;}
u32 __fastcall sound_handle(void*,void*,const char* path,int,int){
    u32 h=2166136261u;for(;*path;++path)h=(h^static_cast<unsigned char>(*path))*16777619u;return h;}
void services(){
    jump(0x602120,&length);jump(0x602280,&key_pool);jump(0x41a500,&intern);jump(0x672562,&compare);
    jump(0x602140,&animation_pool);
    jump(0x672170,&copy);jump(0x57ac60,&append);jump(0x67220a,&find);jump(0x672161,&cookie);
    jump(0x484990,&mode_object);jump(0x592480,&sound_object);
    mode_vt[0x3c/4]=reinterpret_cast<void*>(&is_special);sound_vt[0x34/4]=reinterpret_cast<void*>(&has_sound);sound_vt[0x38/4]=reinterpret_cast<void*>(&sound_handle);
    // Every native sound slot resolves to an invented nonempty suffix. Empty
    // suffixes deliberately skip loading and cannot test directory ownership.
    put<u32>(suffix_pool.data(),8,128);
    std::strcpy(reinterpret_cast<char*>(suffix_pool.data()+0x8008+128),"v");
    for(unsigned i=0;i<51;++i)put<u32>(ptr(0x711b10),i*4,1);
    std::strcpy(static_cast<char*>(ptr(0x684180)),"s/");
}
u32 release_target=0x421983;
__declspec(naked) void __fastcall teardown(void*,void*){
    __asm {
        push esi
        mov esi,ecx
        call release_target
        pop esi
        ret
    }
}
using alloc_t=bool(__cdecl*)(const void*,int*);
using getter_t=void*(__cdecl*)(int);
void mismatch(const gameplay_capabilities::record& a,const gameplay_capabilities::record& b,const char* expected){
    gameplay_capabilities::difference d;char reason[256]{};
    check(a.compare(b.bytes(),&d)!=gameplay_capabilities::result::match,"TS6 mismatch refused");
    a.describe(d.buckets,d.local_only,true,reason,sizeof reason);
    check(std::strstr(reason,"49 CharSounds")&&std::strstr(reason,expected),"TS6 mismatch named in direction");}
}
namespace ini {std::optional<std::string> text(const wchar_t* s,const wchar_t* k){return std::wcscmp(s,L"Limits")==0&&std::wcscmp(k,L"CharSounds")==0?key:std::nullopt;}}
namespace logger {void write(const char*,...) {}}
namespace game_version {bool require_gameplay_agreement(HMODULE){required=true;return true;}void refuse_startup(){unsafe=true;}}
namespace gameplay_agreement {bool register_rule(std::uint16_t id,std::uint16_t rev,std::array<u32,4> values,bool verified){return agreement.add(id,rev,values,verified);}}
namespace entity_capacity {const entity_capacity_layout::descriptor& installed(){return layout;}}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    check(argc==2,"retail fixture input");std::ifstream f(argv[1],std::ios::binary);retail.assign(std::istreambuf_iterator<char>(f),{});check(!retail.empty(),"fixture read");
    DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"owned image writable");load();
    const std::vector<unsigned char> original(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
    gameplay_capabilities::record stock;check(stock.finalize(),"stock record");
    gameplay_capabilities_charsounds_base::record base_record;
    check(base_record.finalize()&&std::equal(stock.bytes().begin(),stock.bytes().end(),base_record.bytes().begin()),"TS2 baseline 69f5115 agreement digest identical");
    for(const auto option:{std::optional<std::string>{},std::optional<std::string>{"16"}}){
        key=option;patch_transaction::transaction tx;char_sounds::require_agreement(GetModuleHandleW(nullptr));char_sounds::contribute(tx,GetModuleHandleW(nullptr));
        check(tx.contributions()==0,"TS2 OFF no contribution");
        check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"TS2 OFF four MiB identical");}
    check(agreement.finalize()&&std::equal(stock.bytes().begin(),stock.bytes().end(),agreement.bytes().begin()),"TS2 OFF digest identical");check(!required,"TS2 OFF no online hook");
    for(const auto option:{"0","128","126","127 ",""}){
        key=option;agreement={};patch_transaction::transaction tx;char_sounds::contribute(tx,GetModuleHandleW(nullptr));
        check(!tx.contributions()&&!agreement.finalize(),"unsupported values unverified");}
    key="127";
    for(unsigned actors:{127u,128u,256u}){layout.actors=actors;agreement={};patch_transaction::transaction tx;char_sounds::contribute(tx,GetModuleHandleW(nullptr));
        check(!tx.contributions()&&!agreement.finalize(),"TS5 actor capacity >=127 refuses");}
    layout.actors=96;
    for(const auto& s:char_sound_sites::sites)for(unsigned i=0;i<s.hex.size()/2;++i){
        auto* p=static_cast<unsigned char*>(ptr(s.va+i));*p^=1;agreement={};patch_transaction::transaction tx;char_sounds::contribute(tx,GetModuleHandleW(nullptr));
        check(tx.commit()==patch_transaction::outcome::refused_guard&&!agreement.finalize(),"every capacity guard byte refuses");*p^=1;
        check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"guard refusal writes zero image bytes");}
    for(u32 a:{0x4239beu,0x4391c0u,0x438d10u,0x438cf0u}){
        auto* p=static_cast<unsigned char*>(ptr(a));*p^=1;agreement={};patch_transaction::transaction tx;char_sounds::contribute(tx,GetModuleHandleW(nullptr));
        check(tx.commit()==patch_transaction::outcome::refused_guard&&!agreement.finalize(),"observer/constructor guards refuse");*p^=1;}
    // Certified rollback must undo the whole row including its teardown check.
    agreement={};{patch_transaction::transaction tx;char_sounds::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::hooks h;
        h.after_apply=[]{*static_cast<unsigned char*>(ptr(0x42198f))^=1;};
        check(tx.commit(&h)==patch_transaction::outcome::rolled_back&&!agreement.finalize(),"atomic row rollback");
        check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"rollback image identical");}
    agreement={};unsigned short cw=0,after=0;__asm fnstcw cw
    const auto mxcsr=_mm_getcsr();
    {patch_transaction::transaction tx;char_sounds::contribute(tx,GetModuleHandleW(nullptr));check(tx.commit()==patch_transaction::outcome::committed,"production row commits");}
    check(agreement.finalize()&&agreement.rule(1).id==49&&agreement.rule(1).revision==1&&agreement.rule(1).values==std::array<u32,4>{127,0,0,0},"TS6 verified rule 49 values");
    mismatch(stock,agreement,"disabled on this PC");mismatch(agreement,stock,"not on the other PC");
    const auto base=get<u32>(ptr(0x438cc3),1);
    for(unsigned i=0;i<127;++i)check(get<unsigned char>(ptr(base+i*0xdc))==0&&get<u32>(ptr(base+i*0xdc),0xd8)==51,"all 127 records natively constructed");
    const auto old_static=std::vector<unsigned char>(static_cast<unsigned char*>(ptr(0x710d48)),static_cast<unsigned char*>(ptr(0x711b10)));
    services();put<unsigned char>(ptr(0x4219a2),0,0xc3);
    const auto allocate=reinterpret_cast<alloc_t>(0x4239c3+get<u32>(ptr(0x4239be),1));
    const auto getter=reinterpret_cast<getter_t>(0x438cb0);
    std::array<std::array<unsigned char,0x500>,128> stats{};
    std::array<std::array<unsigned char,0x850>,128> actors{};
    for(unsigned i=0;i<128;++i){std::snprintf(reinterpret_cast<char*>(stats[i].data()+0x230),128,"synth_%u",i);
        int index=-1;const bool ok=allocate(stats[i].data(),&index);
        check(ok==(i<127)&&index==(i<127?static_cast<int>(i):-1),"TS3 127 keys allocate, 128th refuses without out write");
        put<int>(actors[i].data(),0x444,index);
        if(i<127){check(getter(index)==ptr(base+i*0xdc),"TS4 getter returns claimed record, including 16..126");
            for(unsigned sound_index=0;sound_index<51;++sound_index)
                check(get<u32>(getter(index),8+sound_index*4)==sound_handle(nullptr,nullptr,(std::string("s/synth_")+std::to_string(i)+"/v").c_str(),0,0),"native sound handles belong to directory");}}
    for(int i:{-1,127,128})check(getter(i)==ptr(base),"native invalid-index record-0 fallback retained");
    int same=-1;check(allocate(stats[126].data(),&same)&&same==126&&get<unsigned char>(getter(126))==2,"TS3 equal key shares/refcounts");
    teardown(actors[126].data(),nullptr);check(get<unsigned char>(getter(126))==1,"TS3 teardown high shared record decrements");
    teardown(actors[126].data(),nullptr);check(get<unsigned char>(getter(126))==0&&(get<unsigned char>(getter(126),1)&1)==0,"TS3 teardown index126 frees");
    int recycled=-1;check(allocate(stats[127].data(),&recycled)&&recycled==126,"TS3 first free high record recycled");
    for(unsigned i=0;i<126;++i)teardown(actors[i].data(),nullptr);
    put<int>(actors[127].data(),0x444,126);teardown(actors[127].data(),nullptr);
    for(unsigned i=0;i<127;++i)check(get<unsigned char>(getter(i))==0&&(get<unsigned char>(getter(i),1)&1)==0,"TS3 all records released including >=16");
    int untouched=-1;check(!allocate(nullptr,&untouched)&&untouched==-1,"null stats native early return");
    std::array<unsigned char,0x500> blank{};check(!allocate(blank.data(),&untouched)&&untouched==-1,"empty directory native early return");
    const auto telemetry=char_sounds::telemetry_json();check(telemetry.find("\"peak\":127")!=std::string::npos&&telemetry.find("\"refusals\":1")!=std::string::npos&&telemetry.find("\"empty_sounddir_actors\":2")!=std::string::npos,"exact counters through production observer");
    check(std::memcmp(old_static.data(),ptr(0x710d48),old_static.size())==0,"retired static untouched");
    __asm fnstcw after
    check(cw==after&&mxcsr==_mm_getcsr()&&!unsafe,"floating state and certified startup");
    std::printf("PASS CharSounds TS2-TS6 retail production fixture: %u checks\n%s\n",checks,telemetry.c_str());
    return 0;
}
