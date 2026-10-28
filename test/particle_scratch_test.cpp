// Production bridges and all eleven retail operations, in an owned image.
// Invented particle/vector/collision services; no engine or game entry point.
#include "particle_scratch.hpp"
#include "particle_scratch_sites.hpp"
#include "patch_transaction.hpp"
#include "gameplay_capabilities.hpp"
#include "model_pools_rules.hpp"
#include "test_input_rules.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>
#include <xmmintrin.h>
namespace {
using u32=std::uint32_t;
unsigned checks=0, count_calls=0, overflow_logs=0, touches=0;
unsigned scratch_offset=0x470;
LONG WINAPI fault(EXCEPTION_POINTERS* p) { std::printf("FAULT at %08x code %08lx checks %u eax %08x ecx %08x edx %08x esp %08x access %08x\n",p->ContextRecord->Eip,p->ExceptionRecord->ExceptionCode,checks,p->ContextRecord->Eax,p->ContextRecord->Ecx,p->ContextRecord->Edx,p->ContextRecord->Esp,static_cast<unsigned>(p->ExceptionRecord->ExceptionInformation[1]));std::fflush(stdout);return EXCEPTION_EXECUTE_HANDLER; }
std::optional<std::string> key;
gameplay_capabilities::record agreement;
std::array<u32,4> last_values{};
bool required=false, unsafe=false;
std::vector<unsigned char> retail;
std::array<unsigned char,0x470+300*0x90+64> manager;
std::array<unsigned char,0x30000> aggregate;
std::array<unsigned char,4096> helper;
std::array<unsigned char,0x90*300> particles;
std::array<u32,40> vt{},collisionvt{};
std::array<unsigned char,128> array{};
std::array<unsigned char,128> second_array{};
unsigned char* active_array=array.data();
u32 collision=0;
void check(bool ok,const char* s) { ++checks; if(!ok) { std::printf("FAIL %s\n",s); std::fflush(stdout); std::exit(1); } }
void* ptr(u32 a) { return reinterpret_cast<void*>(a); }
u32 addr(const void* p) { return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p)); }
template<class T> void put(void* p,unsigned off,T v) { std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v); }
template<class T> T get(const void* p,unsigned off) { T v; std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v); return v; }
template<class T> void jump(u32 va,T f) { put<unsigned char>(ptr(va),0,0xe9); put<u32>(ptr(va),1,addr(reinterpret_cast<void*>(f))-va-5); }
void load() {
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail.data()+get<u32>(retail.data(),0x3c));
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) { const auto& s=IMAGE_FIRST_SECTION(nt)[i];
        if(s.VirtualAddress>=0x1000 && s.VirtualAddress+s.Misc.VirtualSize<=0x401000) {
            std::memset(ptr(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);
            std::memcpy(ptr(0x400000+s.VirtualAddress),retail.data()+s.PointerToRawData,s.SizeOfRawData);
        }
    }
}
int __fastcall count(void* p,void*) { ++count_calls; return get<int>(p,12); }
void* __cdecl get_manager() { return manager.data(); }
void* __cdecl get_helper() { return helper.data(); }
void* __cdecl get_collision() { return &collision; }
float __cdecl random_value(int) { return 0.25f; }
void* __fastcall vec0(void* p,void*) { std::memset(p,0,12); return p; }
void* __fastcall vec_copy(void* p,void*,const void* q) { std::memcpy(p,q,12); return p; }
void* __fastcall vec_add(void* p,void*,void* out,const void* q) { for(unsigned i=0;i<3;++i) put<float>(out,4*i,get<float>(p,4*i)+get<float>(q,4*i)); return out; }
void __fastcall vec_set(void* p,void*,float x,float y,float z) { put(p,0,x);put(p,4,y);put(p,8,z); }
void* __fastcall vec3(void* p,void*,float x,float y,float z) { vec_set(p,nullptr,x,y,z);return p; }
void* __fastcall vec4(void* p,void*,float x,float y,float z,float w) { vec_set(p,nullptr,x,y,z);put(p,12,w);return p; }
void* __fastcall vec2(void* p,void*,float x,float y) { put(p,0,x);put(p,4,y);return p; }
void* __fastcall vec_index(void* p,void*,int i) { return static_cast<unsigned char*>(p)+4*i; }
float __fastcall vec_value(void* p,void*,int i) { return get<float>(p,4*i); }
void* __fastcall matrix(void*,void*,void* out,const void* q) { std::memcpy(out,q,12);return out; }
void __cdecl basis(void* a,void* b,void* c) { for(auto p:{a,b,c})for(unsigned i=0;i<3;++i)put(p,4*i,1.0f); }
void __fastcall reset(void* p,void*) { ++touches;std::memset(p,0x17,0x90); }
template<unsigned N,unsigned O> void __fastcall setter(void* p,void*,const void* q) { ++touches;std::memcpy(static_cast<unsigned char*>(p)+O,q,N); }
void __fastcall random_scale(void*,void*,const void*) {}
bool __fastcall collide(void*,void*,const void*) { return true; }
void __fastcall trace(void*,void*,void* out,const void* in) { std::memcpy(out,in,12); }
void __fastcall set_time(void*,void*,u32) {}
void* __fastcall object_get(void*,void*) { return helper.data(); }
void* __fastcall get_particle(void*,void*,unsigned i) { check(i<300,"array index");return particles.data()+i*0x90; }
void __fastcall set_particle(void*,void*,const void* p,unsigned i) { check(i<300,"array set index");std::memcpy(particles.data()+i*0x90,p,0x90); }
void __fastcall copy_particle(void* p,void*,const void* q) { ++touches;std::memcpy(p,q,0x90); }
template<class T> void iat(u32 a,T f) { put(ptr(a),0,addr(reinterpret_cast<void*>(f))); }
void services() {
    jump(0x578cc0,&get_manager);jump(0x5788a0,&get_helper);jump(0x495350,&get_collision);jump(0x560190,&random_value);jump(0x4036c0,&matrix);jump(0x560a00,&basis);
    iat(0x67fa60,&vec0);iat(0x67fa2c,&vec_copy);iat(0x67fa10,&vec_add);iat(0x67fa1c,&vec_set);
    iat(0x67f9c4,&vec_index);iat(0x67f9d8,&vec_value);iat(0x67f960,&vec_index);iat(0x67f9c8,&vec3);iat(0x67f9ac,&vec4);iat(0x67f95c,&vec2);iat(0x67f9e0,&vec_copy);
    iat(0x67f820,&reset);iat(0x67f824,&setter<12,0>);iat(0x67f828,&setter<12,24>);iat(0x67f82c,&setter<12,12>);
    iat(0x67f838,&setter<16,36>);iat(0x67f834,&setter<16,52>);iat(0x67f830,&setter<16,68>);
    iat(0x67f848,&setter<8,84>);iat(0x67f844,&setter<8,92>);iat(0x67f840,&setter<8,100>);
    iat(0x67f84c,&setter<12,108>);iat(0x67f850,&setter<8,120>);iat(0x67f83c,&random_scale);iat(0x67f818,&copy_particle);
    iat(0x67fdcc,&object_get);iat(0x67fa94,&set_time);iat(0x67f9dc,&vec3);
    vt[0x74/4]=addr(reinterpret_cast<void*>(&count));vt[0x60/4]=addr(reinterpret_cast<void*>(&get_particle));vt[0x5c/4]=addr(reinterpret_cast<void*>(&set_particle));
    vt[0x80/4]=vt[0x84/4]=addr(reinterpret_cast<void*>(&set_time));
    collisionvt[0x28/4]=addr(reinterpret_cast<void*>(&collide));collisionvt[0x2c/4]=addr(reinterpret_cast<void*>(&trace));collision=addr(collisionvt.data());
    put(array.data(),0,addr(vt.data()));put(second_array.data(),0,addr(vt.data()));put(helper.data(),0,addr(helper.data()+16));
}
// Deliberately overprovision the argument area, restoring ESP explicitly because
// the eleven thiscall methods have different callee-pop argument sizes (0..132).
__declspec(naked) void __cdecl run(u32,void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,100h
        mov edi,esp
        mov esi,[ebp+10h]
        mov ecx,40h
        rep movsd
        mov ecx,[ebp+0ch]
        call dword ptr [ebp+8]
        lea esp,[ebp-0ch]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
void invoke(unsigned op,int n) {
    std::array<u32,16> object{},wrap{};
    static std::array<float,64> args;args.fill(1.0f);
    put(active_array,12,n);wrap[1]=addr(active_array);wrap[2]=addr(helper.data());object[1]=op==0?addr(active_array):addr(wrap.data());
    auto* self=object.data();void* p=args.data();
    const auto va=particle_scratch_sites::sites[op].operation;
    std::array<u32,64> parameters;parameters.fill(addr(p));
    if(op==10) parameters[4]=0;
    if(op==9) { put(parameters.data(),0,2.0f);put(parameters.data(),4,1.0f);parameters[2]=1; }
    run(va,self,parameters.data());
}
void init() { manager.fill(0xa5);particles.fill(0x39);touches=0;count_calls=0; }
void install() { key="40";agreement={};patch_transaction::transaction tx;particle_scratch::contribute(tx,GetModuleHandleW(nullptr));check(tx.contributions()==1,"one contribution");check(tx.commit()==patch_transaction::outcome::committed,"transaction commits");check(agreement.finalize(),"verified agreement"); }
}
namespace ini { std::optional<std::string> text(const wchar_t*,const wchar_t*) { return key; } }
namespace logger { void write(const char* fmt,...) { char s[2048];va_list a;va_start(a,fmt);std::vsnprintf(s,sizeof s,fmt,a);va_end(a);if(std::strstr(s,"exceeds scratch")) { ++overflow_logs;unsigned short changed=0x0b7f;__asm fldcw changed
    _mm_setcsr(0x5f80); } } }
namespace game_version { bool require_gameplay_agreement(HMODULE) { required=true;return true; } void refuse_startup() { unsafe=true; } }
namespace gameplay_agreement { bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<u32,4> v,bool ok) { last_values=v;return agreement.add(id,revision,v,ok); } }
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(&fault);
    const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");check(path!=nullptr,"owned retail exe required");
    std::ifstream f(path,std::ios::binary);retail.assign(std::istreambuf_iterator<char>(f),{});check(!retail.empty(),"retail read");
    DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"fixture writable");load();
    const std::vector<unsigned char> original(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
    for(const auto option:{std::optional<std::string>{},std::optional<std::string>{"0"},std::optional<std::string>{"off"}}) { key=option;agreement={};patch_transaction::transaction tx;particle_scratch::contribute(tx,GetModuleHandleW(nullptr));check(!tx.contributions()&&!required,"OFF has no contribution or agreement request");check(!std::memcmp(original.data(),ptr(0x401000),original.size()),"OFF four MiB identical");check(agreement.finalize()&&agreement.count()==1,"OFF rule absent"); }
    for(const char* invalid:{"41","256","junk"}) { key=invalid;agreement={};patch_transaction::transaction tx;particle_scratch::contribute(tx,GetModuleHandleW(nullptr));check(!tx.contributions()&&!agreement.finalize(),"invalid unverified no writes");check(last_values==std::array<u32,4>{},"refused effective capacity zero"); }
    for(const auto& s:particle_scratch_sites::sites) for(unsigned b=0;b<s.hex.size()/2;++b) {
        load();key="40";agreement={};*static_cast<unsigned char*>(ptr(s.va+b))^=1;
        const std::vector<unsigned char> before(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
        patch_transaction::transaction tx;particle_scratch::contribute(tx,GetModuleHandleW(nullptr));check(tx.commit()==patch_transaction::outcome::refused_guard,"every guard byte refuses");check(!std::memcmp(before.data(),ptr(0x401000),before.size()),"guard refusal zero writes");check(!agreement.finalize(),"guard refusal unverified");
    }
    load();key="40";agreement={};{ patch_transaction::transaction tx;particle_scratch::contribute(tx,reinterpret_cast<HMODULE>(0x500000));check(!tx.contributions()&&!agreement.finalize(),"wrong base refused"); }
    load();{key="40";agreement={};patch_transaction::transaction tx;particle_scratch::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::hooks h;h.after_apply=[] { *static_cast<unsigned char*>(ptr(particle_scratch_sites::sites[0].va))^=1; };check(tx.commit(&h)==patch_transaction::outcome::rolled_back,"readback rollback");check(!std::memcmp(original.data(),ptr(0x401000),original.size()),"rollback four MiB identical");}
    load();{key="40";agreement={};patch_transaction::transaction tx;particle_scratch::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::hooks h;h.protect=[](void*,std::size_t,DWORD,DWORD*) { return false; };check(tx.commit(&h)==patch_transaction::outcome::refused_protect,"protect refusal");check(!std::memcmp(original.data(),ptr(0x401000),original.size()),"protect refusal zero writes");check(!agreement.finalize(),"protect refusal unverified");}
    load();{key="40";agreement={};patch_transaction::transaction tx;particle_scratch::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::contribution foreign;foreign.owner="synthetic conflicting owner";const auto& s=particle_scratch_sites::sites[0];foreign.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"foreign guard"});foreign.writes.push_back({s.va,patch_transaction::bytes_of(s.hex),"foreign conflicting write"});check(tx.add(std::move(foreign)),"conflict contribution added");check(tx.commit()==patch_transaction::outcome::refused_conflict,"conflicting owner refuses");check(!std::memcmp(original.data(),ptr(0x401000),original.size()),"conflict zero writes");check(!agreement.finalize(),"conflict unverified");}
    unsigned controls=0;
    for(int profile:{0,2,4}) {
        load();scratch_offset=0x470;
        if(profile) {
            const auto l=model_pools_rules::aggregate_layout_for(profile==2?model_pools_rules::x2_capacities:model_pools_rules::x4_capacities);
            scratch_offset=l.psys.objects;
            void* cave=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
            const auto plan=model_pools_rules::plan_for(l,{addr(aggregate.data()),addr(manager.data())},addr(cave));check(plan.has_value(),"ModelPools plan valid");
            std::memcpy(cave,plan->cave_code.data(),plan->cave_code.size());
            for(const auto& w:plan->writes) std::memcpy(ptr(w.va),w.bytes.data(),w.bytes.size());
            // No fixture path enters the ModelPools cave after replacing services.
            VirtualFree(cave,0,MEM_RELEASE);
        }
        services();
        std::array<std::array<std::vector<unsigned char>,7>,11> expected;
        std::array<std::array<std::vector<unsigned char>,7>,11> expected_particles;
        const int counts[]={-1,0,1,39,40,41,300};
        for(unsigned op=0;op<11;++op) for(unsigned j=0;j<std::size(counts);++j) { init();invoke(op,(std::min)(40,counts[j]));expected[op][j]={manager.begin(),manager.end()};expected_particles[op][j]={particles.begin(),particles.end()}; }
        install();
        for(const auto& s:particle_scratch_sites::sites) {
            const auto target=s.va+5+get<u32>(ptr(s.va),1);MEMORY_BASIC_INFORMATION info{};
            check(VirtualQuery(ptr(target),&info,sizeof info)!=0&&info.Protect==PAGE_EXECUTE_READ,"published cave RX");
        }
        if(argc>2&&std::strcmp(argv[2],"--remove-guard")==0) {
            const auto& s=particle_scratch_sites::sites[0];const auto raw=patch_transaction::bytes_of(s.hex);
            std::memcpy(ptr(s.va),raw.data(),raw.size());
        }
        check(agreement.rule(1).id==60&&agreement.rule(1).values==std::array<u32,4>{40,0,0,0},"rule 60 installed effective values");
        gameplay_capabilities::record off;check(off.finalize(),"OFF agreement finalized");
        gameplay_capabilities::difference difference;
        char description[256]{};
        check(off.compare(agreement.bytes(),&difference)!=gameplay_capabilities::result::match,"OFF refuses ON");
        off.describe(difference.buckets,difference.local_only,true,description,sizeof description);
        check(std::strstr(description,"ParticleScratch")!=nullptr,"OFF names ParticleScratch mismatch");
        check(agreement.compare(off.bytes(),&difference)!=gameplay_capabilities::result::match,"ON refuses OFF");
        agreement.describe(difference.buckets,difference.local_only,true,description,sizeof description);
        check(std::strstr(description,"ParticleScratch")!=nullptr,"ON names ParticleScratch mismatch");
        for(unsigned op=0;op<11;++op) for(unsigned j=0;j<std::size(counts);++j) {
            unsigned short cw_before=0,cw_after=0;__asm fnstcw cw_before
            const auto mxcsr=_mm_getcsr();
            init();invoke(op,counts[j]);check(count_calls==1,"native virtual count once");
            __asm fnstcw cw_after
            check(cw_before==cw_after&&mxcsr==_mm_getcsr(),"operation preserves x87 control and MXCSR, even when logger changes them");
            if(std::memcmp(manager.data(),expected[op][j].data(),manager.size())) { unsigned at=0;while(manager[at]==expected[op][j][at])++at;std::printf("mismatch profile %d op %u n %d at %x: %02x vs %02x\n",profile,op,counts[j],at,manager[at],expected[op][j][at]); }
            check(!std::memcmp(manager.data(),expected[op][j].data(),manager.size()),"first K identical to retail; canaries untouched");
            check(!std::memcmp(particles.data(),expected_particles[op][j].data(),particles.size()),"array output first K identical; higher particles untouched");
        }
        // Removing one production bridge must expose the actual native overrun.
        for(unsigned op=0;op<11;++op) {
            const auto& s=particle_scratch_sites::sites[op];const auto raw=patch_transaction::bytes_of(s.hex);
            std::vector<unsigned char> saved(static_cast<unsigned char*>(ptr(s.va)),static_cast<unsigned char*>(ptr(s.va))+raw.size());
            std::memcpy(ptr(s.va),raw.data(),raw.size());init();invoke(op,41);
            if(std::any_of(manager.begin()+scratch_offset+40*0x90,manager.end(),[](unsigned char b){return b!=0xa5;}) || std::any_of(particles.begin()+40*0x90,particles.end(),[](unsigned char b){return b!=0x39;}))++controls;
            else {std::printf("control op %u had no scratch write\n",op);check(false,"guard-removal mutation detected");}
            std::memcpy(ptr(s.va),saved.data(),saved.size());
        }
    }
    check(controls==33,"all eleven guard-removal mutations fail at stock/x2/x4");
    check(overflow_logs==1,"same array logs once across operations and frames");
    active_array=second_array.data();init();invoke(0,41);invoke(0,41);invoke(1,41);
    check(overflow_logs==2,"different overflowing array logs once across repeated operations");
    const auto json=particle_scratch::telemetry_json();check(json.find("\"peak\":300")!=std::string::npos&&json.find("\"clamps\":6")!=std::string::npos,"per-operation peak and clamp counters");
    check(test_input_rules::parse_command("PARTICLE_SCRATCH").what==test_input_rules::command::kind::particle_scratch,"pipe parser");
    check(test_input_rules::parse_command("particle_scratch extra").what==test_input_rules::command::kind::unknown,"pipe rejects arguments");
    check(!unsafe,"startup certified");
    std::printf("PASS ParticleScratch: %u checks; 231 retail operation cases; 33 guard-removal mutations; once-per-array log; %s\n",checks,json.c_str());
}
