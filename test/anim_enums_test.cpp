// Own executable image, production contribution, and retail routines copied read-only.
// The game entry point is never called. Engine/XML file services use invented data.
#include <Windows.h>
#include "anim_enums.hpp"
#include "anim_enum_names.hpp"
#include "anim_enum_sites.hpp"
#include "patch_transaction.hpp"
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>
#include <array>
#include <cstring>
#include <xmmintrin.h>
#include <filesystem>
namespace {
using u32=std::uint32_t;
std::optional<std::string> key;
gameplay_capabilities::record agreement;
bool unsafe=false,required=false;
unsigned checks=0;
std::vector<std::string> log_lines;
void check(bool ok,const char* what){++checks;if(!ok){std::printf("FAIL %s\n",what);std::fflush(stdout);std::abort();}}
void* ptr(u32 a){return reinterpret_cast<void*>(a);}
template<class T>void put(void* p,u32 o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof v);}
template<class T>T get(const void* p,u32 o){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof v);return v;}
template<class T>void jump(u32 a,T f){put<unsigned char>(ptr(a),0,0xe9);put<u32>(ptr(a),1,static_cast<u32>(reinterpret_cast<std::uintptr_t>(f))-a-5);}
std::vector<unsigned char> retail;
void load()
{
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail.data()+get<u32>(retail.data(),0x3c));
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(nt)[i];
        if(s.VirtualAddress>=0x1000&&s.VirtualAddress+s.Misc.VirtualSize<=0x401000){
            std::memset(ptr(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);
            std::memcpy(ptr(0x400000+s.VirtualAddress),retail.data()+s.PointerToRawData,s.SizeOfRawData);
        }
    }
}
using unary=void*(__fastcall*)(void*,void*);
using lookup_t=bool(__cdecl*)(const char*,int*);
std::array<unsigned char,0x900> actor{};
std::array<void*,128> actorvt{},dbvt{},wrapvt{};
void* db=dbvt.data();void* wrapper=wrapvt.data();void* wrapperptr=&wrapper;
std::array<unsigned char,0x40000> keys{},animations{};
std::vector<std::string> interned_keys(1),interned_anims(1);
std::string current_lookup;
std::vector<std::string> parser_names,parser_clips;
std::vector<unsigned char> parser_node;
unsigned starts=0;
void* __fastcall xml_ctor(void* p,void*){return p;}
void __fastcall xml_open(void*,void*,const char*,int,int,int){}
void* __fastcall xml_root(void*,void*,const char*){return parser_node.data();}
void* __fastcall xml_anims(void*,void*,const char*){return parser_node.data();}
void __fastcall xml_close(void*,void*){}
void* __fastcall append_path(void* p,void*,const char*){return p;}
void __stdcall vector_construct(void* base,int stride,int count,unary ctor,unary)
{for(int i=0;i<count;++i)ctor(static_cast<unsigned char*>(base)+i*stride,nullptr);}
bool __fastcall start_anim(void*,void*,u32 id,int,int,int,int){++starts;return id==1234;}
u32 inline_target=0x4310ef,release_target=0x421963;
__declspec(naked) void __fastcall release_actor(void*,void*)
{
    __asm {
        push esi
        mov esi,ecx
        call release_target
        pop esi
        ret
    }
}
// Invented caller frame for the inlined native reader, preserving its esi/ebp ABI.
__declspec(naked) u32 __fastcall inline_read(void*,void*,int)
{
    __asm {
        push esi
        push ebp
        mov esi,ecx
        mov eax,[esp+0ch]
        sub esp,28h
        mov [esp+20h],eax
        call inline_target
        mov eax,ebp
        add esp,28h
        pop ebp
        pop esi
        ret 4
    }
}
u32 intern(std::array<unsigned char,0x40000>& pool,std::vector<std::string>& all,const char* text,u32 bias)
{
    auto it=std::find(all.begin(),all.end(),text);
    if(it!=all.end())return static_cast<u32>(it-all.begin());
    const u32 id=static_cast<u32>(all.size());all.emplace_back(text);
    const u32 offset=id*128;
    put(pool.data(),4+id*4,offset);
    std::strcpy(reinterpret_cast<char*>(pool.data()+bias+offset),text);return id;
}
u32 __fastcall key_intern(void*,void*,const char* text,int,int){return intern(keys,interned_keys,text,0x4008);}
u32 __fastcall anim_intern(void*,void*,const char* text,int,int){current_lookup=text;return intern(animations,interned_anims,text,0x8008);}
void* __cdecl key_pool(){return keys.data();}
void* __cdecl anim_pool(){return animations.data();}
int __cdecl length(const char* text){return static_cast<int>(std::strlen(text));}
int __cdecl compare(const char* a,const char* b){return std::strcmp(a,b);}
void __fastcall cookie(u32,void*){}
void* __fastcall make_key(void* who,void*,char* out,int n){strncpy_s(out,n,reinterpret_cast<char*>(who)+0x800,_TRUNCATE);return out;}
void* __fastcall resource(void*,void*){return &wrapperptr;}
void* __fastcall container(void*,void*){return &db;}
bool __fastcall find_clip(void*,void*,const char* name,u32* out)
{
    if(std::strcmp(name,"missing")==0)return false;
    *out=static_cast<u32>(std::strtoul(name+4,nullptr,10))+100;return true;
}
float __fastcall duration(void*,void*,u32 id){return static_cast<float>(id)*0.125f;}
void __cdecl copy_name(char* out,const char* in,int n){strncpy_s(out,n,in,_TRUNCATE);}
void __cdecl lower(char* text){for(;*text;++text)if(*text>='A'&&*text<='Z')*text+=32;}
void services()
{
    jump(0x425c40,&make_key);jump(0x602120,&length);jump(0x602140,&anim_pool);jump(0x602280,&key_pool);
    jump(0x41a500,&key_intern);jump(0x41a320,&anim_intern);jump(0x672562,&compare);jump(0x672161,&cookie);
    jump(0x672170,&copy_name);jump(0x560270,&lower);
    jump(0x425b10,&append_path);jump(0x564a80,&xml_ctor);jump(0x564eb0,&xml_open);
    jump(0x564ac0,&xml_root);jump(0x564c70,&xml_anims);jump(0x564d00,&xml_close);
    jump(0x6722d4,&vector_construct);jump(0x42f0f0,&start_anim);
    actorvt[0x188/4]=reinterpret_cast<void*>(&resource);wrapvt[0x14/4]=reinterpret_cast<void*>(&container);
    dbvt[0x3c/4]=reinterpret_cast<void*>(&find_clip);dbvt[0x54/4]=reinterpret_cast<void*>(&duration);
    put(actor.data(),0,actorvt.data());put(actor.data(),0x43c,-1);
}
std::vector<unsigned char> crt_objects()
{
    // Execute native cache-vector, id-table and map construction in retail CRT order.
    std::memset(ptr(0x70ba58),0,0x710d28-0x70ba58);
    reinterpret_cast<unary>(0x426800)(ptr(0x70ba58),nullptr);
    reinterpret_cast<unary>(0x425ce0)(ptr(0x70f2a0),nullptr);
    reinterpret_cast<unary>(0x427c60)(ptr(0x70f938),nullptr);
    return {static_cast<unsigned char*>(ptr(0x70f938)),static_cast<unsigned char*>(ptr(0x710d28))};
}
u32 table_base();
void parse_table(unsigned n)
{
    parser_names.clear();parser_clips.clear();
    std::vector<std::string> available;
    const unsigned native_count=n==232?209:210;
    for(unsigned i=0;i<native_count;++i)available.emplace_back(anim_enum_registry::native_names[i]);
    if(n>209)for(auto name:anim_enum_registry::builtin_names)available.emplace_back(name);
    if(n>232){
        for(unsigned i=0;i<279;++i){const auto name="ea_mod_"+std::to_string(i);if(anim_enums::extension_id(name.c_str())>=0)available.push_back(name);}
        if(anim_enums::extension_id("ea_mod_first")>=0)available.emplace_back("ea_mod_first");
    }
    // Reverse ID order makes the retail heap sort observable. The full-sidecar
    // run supplies all 512 unique names; the base run repeats valid names once
    // its native+built-in set is exhausted to exercise the same capacity bound.
    for(unsigned i=0;i<n;++i){parser_names.push_back(available[(n-i-1)%available.size()]);parser_clips.emplace_back("clip"+std::to_string(i));}
    parser_node.assign(16+8*n,0);put(parser_node.data(),12,n);
    for(unsigned i=0;i<n;++i){put(parser_node.data(),16+8*i,parser_names[i].c_str());put(parser_node.data(),20+8*i,parser_clips[i].c_str());}
    const auto table=table_base();reinterpret_cast<unary>(0x425b90)(ptr(table),nullptr);
    check(reinterpret_cast<bool(__cdecl*)(void*,const char*)>(0x426680)(ptr(table),"invented.xml"),"retail parser accepts invented ANIMTABLE");
    const auto count=get<u32>(ptr(table),0x1004);check(count==(std::min)(512u,n),"retail parser capacity including full+1");
    reinterpret_cast<unary>(0x426330)(ptr(table),nullptr);
    for(unsigned i=1;i<count;++i)check(get<u32>(ptr(table),8*(i-1))<=get<u32>(ptr(table),8*i),"retail heap sort ascending IDs");
    if(n==232)check(get<u32>(ptr(table),8*(count-1))==232,"parser resolves all 23 built-ins end to end");
    if(n>=512&&available.size()==512)check(get<u32>(ptr(table),8*(count-1))==511,"parser stores final sidecar enum at full capacity");
}
u32 cache_base(){return get<u32>(ptr(0x425fde),2);}
u32 table_base(){return get<u32>(ptr(0x427c47),1);}
void set_table(unsigned n,bool expanded)
{
    const auto d=expanded?anim_enum_layout::raised:anim_enum_layout::retail;
    auto* t=ptr(expanded?table_base():0x70f2a0);
    std::memset(t,0,d.table_size);
    for(unsigned i=0;i<n;++i){put(t,8*i,i);const auto s="clip"+std::to_string(i);put(t,8*i+4,intern(animations,interned_anims,s.c_str(),0x8008));}
    put(t,d.table_count_off,n);
}
void __fastcall release_record(void* rec,void*){reinterpret_cast<void(__fastcall*)(void*,void*)>(0x425ec0)(rec,nullptr);}
}
namespace ini {
std::optional<std::string> text(const wchar_t* s,const wchar_t* k){return std::wcscmp(s,L"Limits")==0&&std::wcscmp(k,L"AnimEnums")==0?key:std::nullopt;}
bool flag(const wchar_t* section,const wchar_t* name,bool f){return std::wcscmp(section,L"Test")==0&&std::wcscmp(name,L"AnimEnumTrace")==0?true:f;}
}
namespace logger {
void write(const char* format,...){char line[2048]{};va_list args;va_start(args,format);std::vsnprintf(line,sizeof line,format,args);va_end(args);log_lines.emplace_back(line);}
const std::filesystem::path& module_dir(){
    static const auto dir=[]{wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);return std::filesystem::path(p).parent_path();}();return dir;
}
}
namespace game_version {
bool require_gameplay_agreement(HMODULE){required=true;return true;}
void refuse_startup(){unsafe=true;}
}
namespace gameplay_agreement {
bool register_rule(std::uint16_t id,std::uint16_t rev,std::array<u32,4> v,bool verified){return agreement.add(id,rev,v,verified);}
}

int main(int argc,char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    check(argc==2||argc==3,"retail input and optional sidecar mode required");
    const std::string mode=argc==3?argv[2]:"";
    std::ifstream f(argv[1],std::ios::binary);retail.assign(std::istreambuf_iterator<char>(f),{});check(!retail.empty(),"retail read");
    DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"fixture writable");load();
    // OFF proof: all four MiB and the serialized agreement record stay identical.
    const std::vector<unsigned char> original(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
    gameplay_capabilities::record stock;stock.finalize();
    for(const auto option:{std::optional<std::string>{},std::optional<std::string>{"0"}}){key=option;patch_transaction::transaction tx;anim_enums::contribute(tx,GetModuleHandleW(nullptr));check(tx.contributions()==0,"OFF contributes nothing");}
    check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"OFF full image identical");
    check(agreement.finalize()&&std::equal(stock.bytes().begin(),stock.bytes().end(),agreement.bytes().begin()),"OFF agreement byte-identical");
    check(!required&&!anim_enums::layout(),"OFF no socket agreement or descriptor");
    key="256";agreement={};{patch_transaction::transaction tx;anim_enums::contribute(tx,GetModuleHandleW(nullptr));check(tx.contributions()==0&&!agreement.finalize(),"unsupported key unverified, no writes");}
    if(mode=="bad-sidecar"){
        key="512";agreement={};patch_transaction::transaction tx;anim_enums::contribute(tx,GetModuleHandleW(nullptr));
        check(tx.contributions()==0&&!agreement.finalize(),"invalid base sidecar refused with unverified rule");
        check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"invalid sidecar changes no image byte");
        std::puts("PASS AnimEnums invalid base sidecar and OFF proof");return 0;
    }
    if(mode=="retain"){
        key="512";agreement={};patch_transaction::transaction tx;anim_enums::contribute(tx,GetModuleHandleW(nullptr));
        u32 saved_cave=0;patch_transaction::hooks hooks;
        hooks.after_apply=[&]{saved_cave=0x42196d+5+get<u32>(ptr(0x42196d),1);*static_cast<unsigned char*>(ptr(0x425b96))^=1;};
        hooks.after_rollback=[]{*static_cast<unsigned char*>(ptr(0x425b96))^=1;};
        check(tx.commit(&hooks)==patch_transaction::outcome::rollback_failed,"uncertified union rollback reported");
        check(unsafe&&!anim_enums::layout()&&!agreement.finalize(),"uncertified rollback refuses startup and agreement");
        MEMORY_BASIC_INFORMATION info{};
        check(saved_cave&&VirtualQuery(ptr(saved_cave),&info,sizeof info)==sizeof info&&info.State==MEM_COMMIT&&info.Protect==PAGE_EXECUTE_READ,"uncertified rollback retains executable caves");
        std::puts("PASS AnimEnums uncertified rollback retains backing/caves and refuses startup");return 0;
    }
    // Every guarded byte is independently corrupted; the shared transaction refuses before any write.
    key="512";
    for(const auto& s:anim_enum_sites::sites)for(unsigned i=0;i<s.old_hex.size()/2;++i){
        agreement={};auto* p=static_cast<unsigned char*>(ptr(s.va+i));const auto byte=*p;*p^=1;
        const std::vector<unsigned char> before(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
        patch_transaction::transaction tx;anim_enums::contribute(tx,GetModuleHandleW(nullptr));
        check(tx.commit()==patch_transaction::outcome::refused_guard,"mutated guard refuses");
        check(std::memcmp(before.data(),ptr(0x401000),before.size())==0&&!agreement.finalize(),"mutated guard writes nothing and unverified");*p=byte;
    }
    // Force post-write failure with an independent contributor; certify union rollback.
    agreement={};{patch_transaction::transaction tx;anim_enums::contribute(tx,GetModuleHandleW(nullptr));
        patch_transaction::contribution other;other.owner="other";other.guards.push_back({0x440000,{*static_cast<unsigned char*>(ptr(0x440000))},"other"});other.writes.push_back({0x440000,{0x90},"other"});tx.add(std::move(other));
        const auto byte=*static_cast<unsigned char*>(ptr(0x440000));patch_transaction::hooks hooks;hooks.after_apply=[]{*static_cast<unsigned char*>(ptr(0x425b96))^=1;};
        check(tx.commit(&hooks)==patch_transaction::outcome::rolled_back,"union rollback certified");check(*static_cast<unsigned char*>(ptr(0x440000))==byte&&!agreement.finalize(),"other contribution restored and agreement poisoned");
        check(std::memcmp(original.data(),ptr(0x401000),original.size())==0,"rollback full image identical");}
    // Differential native build before installation.
    services();const auto original_map=crt_objects();
    reinterpret_cast<void(__cdecl*)()>(0x426a50)(); // native registry, pool and red-black tree
    set_table(210,false);std::strcpy(reinterpret_cast<char*>(actor.data()+0x800),"type0");
    unsigned short cw_before=0,cw_after=0;__asm fnstcw cw_before
    const auto mxcsr=_mm_getcsr();
    check(reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x425ed0)(actor.data(),nullptr),"retail cache builds");
    const std::vector<unsigned char> stock_record(static_cast<unsigned char*>(ptr(0x70ba58)),static_cast<unsigned char*>(ptr(0x70ba58+0x384)));
    std::array<float,11> stock_durations{};
    for(int i=0;i<11;++i)stock_durations[i]=reinterpret_cast<float(__fastcall*)(void*,void*,int)>(0x425b70)(actor.data(),nullptr,i);
    // Reload instructions before guard check; services are installed only after commit.
    load();agreement={};{patch_transaction::transaction tx;anim_enums::contribute(tx,GetModuleHandleW(nullptr));check(tx.contributions()==1,"single startup contribution");check(tx.commit()==patch_transaction::outcome::committed,"production contribution commits");}
    check(anim_enums::layout()&&agreement.finalize(),"descriptor and rule publish on commit");
    __asm fnstcw cw_after
    check(cw_before==cw_after&&mxcsr==_mm_getcsr(),"install preserves x87 and MXCSR");
    services();check(crt_objects()==original_map,"retired static constructors leave native map byte-identical after CRT sequence");
    // Execute the production M1 target: native registration runs against the
    // original tree/pool; our extension never consumes a native map slot.
    reinterpret_cast<void(__cdecl*)()>(0x427c35+get<u32>(ptr(0x427c30),1))();
    for(unsigned n:{209u,232u,512u,513u})parse_table(n);
    set_table(512,true);put(actor.data(),0x43c,-1);
    check(reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x425ed0)(actor.data(),nullptr),"expanded native cache builds");
    const auto* record=static_cast<unsigned char*>(ptr(cache_base()));
    check(std::memcmp(stock_record.data(),record,0x350)==0,"native IDs and record prefix identical to retail");
    check(std::memcmp(stock_record.data()+0x358,record+0x810,44)==0,"eleven native x87 tail values bit-identical");
    for(int i=0;i<11;++i){const float v=reinterpret_cast<float(__fastcall*)(void*,void*,int)>(0x425b70)(actor.data(),nullptr,i);check(std::memcmp(&v,&stock_durations[i],4)==0,"native movement getter bit-identical");}
    for(unsigned i=210;i<512;++i)check(get<u32>(record,8+i*4)==100+i,"extension ID resolved by retail build loop");
    using read_t=bool(__fastcall*)(void*,void*,int,u32*);
    auto read=reinterpret_cast<read_t>(0x42f0a0);auto direct=reinterpret_cast<read_t>(0x42eac0);
    for(int index:{-1,0,15,16,127,128})for(int id:{-1,0,209,210,232,511,512}){
        put(actor.data(),0x43c,index);
        if(index>=0&&index<128){auto* rec=static_cast<unsigned char*>(ptr(cache_base()+index*0x83c));put<unsigned char>(rec,1,1);put(rec,0x80c,512u);if(id>=0&&id<512)put(rec,8+id*4,1234u);}
        u32 out=0xdeadbeef;const bool expected=index>=0&&index<128&&id>=0&&id<512;
        check(read(actor.data(),nullptr,id,&out)==expected,"callable reader boundary");if(expected)check(out==1234,"callable reader value");
        out=0xdeadbeef;check(direct(const_cast<unsigned char*>(record),nullptr,id,&out)==(id>=0&&id<512),"record reader boundary");
        starts=0;const auto played=reinterpret_cast<bool(__fastcall*)(void*,void*,int,int,int,int,int)>(0x430cd0)(actor.data(),nullptr,id,0,0,0,0);
        check(played==expected&&starts==(expected?1u:0u),"native animation-start reader boundary");
        if(expected)check(get<short>(actor.data(),0x44c)==id,"native actor i16 stores extended enum without truncation");
    }
    // Stop the inline block at its native continuation, returning to our owned caller.
    put<unsigned char>(ptr(0x43112c),0,0xc3);
    // The call into the block pushes a return address: reserve its native locals
    // relative to that stack by a tiny adapter at its boundary (see probe below).
    for(int index:{-1,0,15,16,127,128})for(int id:{-1,0,209,210,232,511,512}){
        put(actor.data(),0x43c,index);
        const auto value=inline_read(actor.data(),nullptr,id);
        check(value==((index>=0&&index<128&&id>=0&&id<512)?1234u:0u),"inlined move-start boundary");
    }
    // Start with every record released, then allocate all 128 distinct native keys.
    for(unsigned i=0;i<128;++i)reinterpret_cast<void(__fastcall*)(void*,void*)>(0x425e50)(ptr(cache_base()+i*0x83c),nullptr);
    for(unsigned i=0;i<129;++i){put(actor.data(),0x43c,-1);const auto name="character_type_"+std::to_string(i);std::strcpy(reinterpret_cast<char*>(actor.data()+0x800),name.c_str());
        const bool ok=reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x425ed0)(actor.data(),nullptr);
        check(ok==(i<128),"128 distinct character types allocate, 129th refuses");check(get<int>(actor.data(),0x43c)==(i<128?static_cast<int>(i):-1),"native allocation index");}
    release_record(ptr(cache_base()+37*0x83c),nullptr);put(actor.data(),0x43c,-1);
    check(reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x425ed0)(actor.data(),nullptr)&&get<int>(actor.data(),0x43c)==37,"native release and reuse");
    check(anim_enums::status().find("peak 128/128")!=std::string::npos,"peak occupancy published");
    // Same key shares, rebuilding releases the previous record, and teardown
    // accepts record 127 through C1's ecx compare cave (128 remains excluded).
    const auto reused=get<int>(actor.data(),0x43c);
    check(reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x425ed0)(actor.data(),nullptr)&&get<int>(actor.data(),0x43c)==reused,"native rebuild releases old then reclaims same key");
    auto second_actor=actor;put(second_actor.data(),0x43c,-1);
    check(reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x425ed0)(second_actor.data(),nullptr)&&get<int>(second_actor.data(),0x43c)==reused,"distinct actors with equal key share cache record");
    check(get<unsigned char>(ptr(cache_base()+reused*0x83c),0)==2,"native shared refcount increments");
    put<unsigned char>(ptr(0x421983),0,0xc3);
    for(int index:{-1,127,128}){put(actor.data(),0x43c,index);release_actor(actor.data(),nullptr);}
    check(get<unsigned char>(ptr(cache_base()+127*0x83c),0)==0,"teardown releases last expanded record");
    // Exercise the retail lowercase/truncation wrapper and production miss cave.
    auto lookup=reinterpret_cast<lookup_t>(0x426540);int id=-1;
    for(int i=0;i<210;++i){const bool ok=lookup(anim_enum_registry::native_names[i].data(),&id);if(!ok||id!=i)std::printf("native lookup %d %s => %d %d; pool count %u\n",i,anim_enum_registry::native_names[i].data(),ok,id,get<u32>(ptr(0x70f938),0x1098));check(ok&&id==i,"native names retain IDs");}
    for(int i=0;i<23;++i)check(lookup(anim_enum_registry::builtin_names[i].data(),&id)&&id==210+i,"XML1 names resolve fixed IDs");
    check(lookup("EA_FLYINGCARRY_IDLE",&id)&&id==0xd4,"mixed case uses retail normalization");
    check(!lookup("ea_unknown",&id),"unknown native miss remains miss");
    check(!lookup("EA_UNKNOWN",&id),"same normalized unknown stays a miss");
    check(std::count(log_lines.begin(),log_lines.end(),"AnimEnums: dropped unknown animation enum ea_unknown")==1,"trace logs each normalized unknown once");
    const auto registration="AnimEnums: registration 210 native + "+std::to_string(agreement.rule(1).values[1])+" extension names; capacity 512, records 128";
    check(std::count(log_lines.begin(),log_lines.end(),registration)==1,"M1 trace reports native and extension registration exactly once");
    if(mode=="sidecar"){
        check(lookup("EA_MOD_FIRST",&id)&&id==233,"base sidecar mixed-case native lookup and first ID");
        const std::string longest="ea_"+std::string(60,'a');
        check(lookup(longest.c_str(),&id)&&id==234,"63 character sidecar name resolves through retail wrapper");
        check(lookup((longest+"x").c_str(),&id)&&id==234,"64 character query truncates exactly like retail");
    }
    if(mode=="sidecar-full")check(lookup("ea_mod_278",&id)&&id==511,"final full-sidecar name resolves through native wrapper");
    // Native enum-0 fallback when a mapped clip is unavailable and when no pair exists.
    set_table(2,true);auto* table=ptr(table_base());put(table,12,intern(animations,interned_anims,"missing",0x8008));
    for(unsigned i=0;i<128;++i)reinterpret_cast<void(__fastcall*)(void*,void*)>(0x425e50)(ptr(cache_base()+i*0x83c),nullptr);
    put(actor.data(),0x43c,-1);check(reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x425ed0)(actor.data(),nullptr),"native fallback cache builds");
    check(get<u32>(record,8)==100&&get<u32>(record,12)==100&&get<u32>(record,8+511*4)==100,"unresolved mapped and unmapped IDs use enum zero");
    __asm fnstcw cw_after
    check(cw_before==cw_after&&mxcsr==_mm_getcsr(),"native cache and lookup preserve x87 and MXCSR");
    check(!unsafe,"startup remains certified");
    std::printf("PASS AnimEnums production transaction and retail fixtures: %u checks\n",checks);
    return 0;
}
