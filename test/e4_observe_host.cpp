#include "e4_observe.hpp"
#include "e4_observe_plan.hpp"
#include "e4_observe_rules.hpp"
#include "actor_pools_plan.hpp"
#include "actor_ai_layout.hpp"
#include "entity_capacity_layout.hpp"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <xmmintrin.h>
extern "C" unsigned char fixture_space[0x700000];
using u32=std::uint32_t;
namespace {bool startup_refused=false;}
namespace ini {bool flag(const wchar_t*,const wchar_t*,bool){return true;}}
namespace logger {void write(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}}
namespace game_version {void refuse_startup(){startup_refused=true;}}
namespace entity_capacity {
    const entity_capacity_layout::descriptor& installed(){return entity_capacity_layout::stock_descriptor;}
    [[noreturn]] void fail_stop_lifecycle(const char*){ExitProcess(77);}
}
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* s){++checks;if(!ok){++failures;std::printf("FAIL %s\n",s);}}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
void put(u32 p,u32 v){std::memcpy(reinterpret_cast<void*>(p),&v,4);}
void jump(u32 p,u32 v){*reinterpret_cast<unsigned char*>(p)=0xe9;put(p+1,v-p-5);}
struct cpu {u32 edi,esi,ebp,esp,ebx,edx,ecx,eax,flags;};
cpu input{},output{},callback_state{};
__declspec(align(16)) std::array<unsigned char,512> fx_output{};
u32 host_sp=0,stack_top=0,entry=0,path=0,callbacks=0,mode=0;
u32 getter_calls=0;
std::array<u32,4096> stack{};
__declspec(naked) void captured(){
    __asm {
        pushfd
        pushad
        fxsave fx_output
        cld
        mov esi,esp
        mov edi,offset output
        mov ecx,9
        rep movsd
        mov esp,host_sp
        popfd
        popad
        ret
    }
}
__declspec(naked) void execute(){
    __asm {
        pushad
        pushfd
        mov host_sp,esp
        mov esp,stack_top
        push input.flags
        popfd
        mov edi,input.edi
        mov esi,input.esi
        mov ebp,input.ebp
        mov ebx,input.ebx
        mov edx,input.edx
        mov ecx,input.ecx
        mov eax,input.eax
        jmp dword ptr [entry]
    }
}

void __cdecl callback(unsigned,const unsigned*){
    ++callbacks;
    _mm_setcsr(0x3f80);
    __asm {
        fninit
        fldpi
        pxor xmm0,xmm0
        pxor xmm1,xmm1
        mov eax,12345678h
        mov ecx,23456789h
        mov edx,3456789ah
        cmp eax,ecx
    }
}
void reset_fx(){
    _mm_setcsr(0x1f80);
    __asm {fninit}
    __asm {fld1}
    __asm {fldpi}
    __asm {pcmpeqd xmm0,xmm0}
    __asm {pcmpeqd xmm1,xmm1}
    __asm {pxor xmm2,xmm2}
    __asm {pxor xmm3,xmm3}
    __asm {pxor xmm4,xmm4}
    __asm {pxor xmm5,xmm5}
    __asm {pxor xmm6,xmm6}
    __asm {pxor xmm7,xmm7}
}
bool same_fx(const std::array<unsigned char,512>& a,const std::array<unsigned char,512>& b){
    if(std::memcmp(a.data(),b.data(),8)||std::memcmp(a.data()+24,b.data()+24,8)||std::memcmp(a.data()+160,b.data()+160,128))return false;
    const auto top=(a[3]>>3)&7;
    for(unsigned i=0;i<8;++i)if(a[4]&(1u<<((top+i)&7)))if(std::memcmp(a.data()+32+i*16,b.data()+32+i*16,10))return false;
    return true;
}
void stub_tests(unsigned char* code){
    // Exercise all addressing forms used by the 44 B1 calls and a direct call.
    // The callback deliberately destroys arithmetic flags, caller registers,
    // x87 state, XMM0/1 and MXCSR; each must be restored around the native call.
    std::array<u32,64> vt{};for(auto& x:vt)x=address(code+0x3000);
    code[0x3000]=0x40;code[0x3001]=0xc3; // INC EAX; RET
    const char* forms[]={"ff5014","ff5210","ff5354","ff10","ff505c"};
    for(auto h:forms)for(unsigned mode:{0u,1u,2u})for(unsigned flags:{0x202u,0x602u}){
        const auto at=address(code+0x2000),target=address(code+0x3000);auto raw=patch_transaction::bytes_of(h);
        const u32 point=at+1; // prefix PUSH ESI; suffix POP ESI; RET
        e4_observe_sites::instruction ins[]={{at,"56",0,0,0},{point,h,0,0,0},{point+u32(raw.size()),"5e",0,0,0},{point+u32(raw.size())+1,"c3",0,0,0}};
        e4_observe_sites::site s{"test",at,point,point+u32(raw.size())+2,e4_observe_sites::kind::borrow_handler,mode,0,"",ins,4};
        (void)target;
        code[0x2000]=0x56;std::memcpy(code+0x2001,raw.data(),raw.size());code[0x2001+raw.size()]=0x5e;code[0x2002+raw.size()]=0xc3;
        const auto prep=[&]{stack.fill(address(reinterpret_cast<void*>(&captured)));stack_top=address(stack.data()+1024);
            input={0x11111111,0x22222222,0x33333333,0,address(vt.data()),address(vt.data()),0x44444444,address(vt.data()),flags};reset_fx();};
        prep();entry=at;execute();const auto native=output;const auto fx=fx_output;
        auto generated=e4_observe_plan::stub(s,address(code),address(reinterpret_cast<void*>(&callback)),0);
        check(!generated.empty(),"preservation stub emitted");std::memcpy(code,generated.data(),generated.size());
        FlushInstructionCache(GetCurrentProcess(),code,0x4000);prep();callbacks=0;entry=address(code);execute();
        check(!std::memcmp(&native,&output,sizeof(cpu)),"native GPRs, EFLAGS (including DF) and ESP retained");
        check(same_fx(fx,fx_output),"native x87, XMM and MXCSR retained");
        check(callbacks==(mode==2?2u:1u),"before/after observer called exactly once");
    }
}
std::vector<unsigned char> mapped(0x700000);
bool load(const char* file){
    std::ifstream f(file,std::ios::binary);std::vector<unsigned char> raw(std::istreambuf_iterator<char>(f),{});if(raw.size()<1024)return false;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(raw.data());auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(nt)[i];if(s.VirtualAddress<0x1000||s.VirtualAddress+s.SizeOfRawData>0x701000)return false;
        std::memcpy(mapped.data()+s.VirtualAddress-0x1000,raw.data()+s.PointerToRawData,s.SizeOfRawData);}
    return true;
}
void seed(){std::memcpy(fixture_space,mapped.data(),mapped.size());}
void plan_tests(unsigned char* code){
    std::puts("plan seed");seed();std::puts("plan make");auto p=e4_observe_plan::make(address(code),address(reinterpret_cast<void*>(&callback)),{});
    std::puts("plan made");check(bool(p),"complete E4 plan prepared");if(!p)return;
    std::string why;check(patch_transaction::verify_expectations(p->contribution,why),"all spans and whole-function hashes equal retail");
    for(const auto& s:e4_observe_sites::sites){auto* b=reinterpret_cast<unsigned char*>(s.va);*b^=1;
        check(!patch_transaction::verify_expectations(p->contribution,why),"one changed guard byte refuses E4");*b^=1;}
    auto bad=std::vector<patch_transaction::write>{{e4_observe_sites::sites[0].va,{0x90},"foreign overlap"}};
    check(!e4_observe_plan::make(address(code),0,bad),"overlapping prior writer refuses without an overwrite");
    const auto va=e4_observe_sites::regions[0].lo+100;
    const auto before=*reinterpret_cast<unsigned char*>(va);*reinterpret_cast<unsigned char*>(va)^=1;
    check(!patch_transaction::verify_expectations(p->contribution,why),"a changed byte outside the cave is rejected by its region hash");
    std::vector<patch_transaction::write> prior{{va,{static_cast<unsigned char>(before^1)},"verified earlier rule"}};
    auto composed=e4_observe_plan::make(address(code),0,prior,{{va,{before},"original"}});
    check(composed&&patch_transaction::verify_expectations(composed->contribution,why),"certified earlier writes compose into function hashes");
    *reinterpret_cast<unsigned char*>(va)=before;
    for(const auto& s:e4_observe_sites::sites)check(!e4_observe_plan::stub(s,address(code),address(reinterpret_cast<void*>(&callback)),0).empty(),"every generated span emits with exact labels and instruction lengths");
}
unsigned injected_phase=0;bool cpp_fault=false;
unsigned char* actor_arena=nullptr;
void fault(){if(cpp_fault)throw std::runtime_error("native test-vtable construction fault");RaiseException(0xe0420042,0,0,nullptr);}
u32 __fastcall factory_size(void*,void*){return 0;}
void* __cdecl factory_allocate(void*,u32){
    using take=void*(__fastcall*)(void*,void*,u32);
    void* actor=reinterpret_cast<take>(0x4615a0)(actor_arena,nullptr,0x844);
    if(injected_phase==0)fault();return actor;
}
__declspec(naked) void __stdcall publish_fragment(void*,u32){
    __asm {
        push esi
        mov esi,[esp+8]
        mov eax,[esp+0ch]
        mov ecx,42394dh
        call ecx
        pop esi
        ret 8
    }
}
void* __fastcall injected_ctor(void* actor,void*,u32,u32,u32,u32,u32){
    if(injected_phase==1)fault();
    const auto primary=actor_ai::installed().primary_base;
    const auto i=reinterpret_cast<u32(__fastcall*)(void*,void*)>(0x522a10)(reinterpret_cast<void*>(primary),nullptr);
    publish_fragment(actor,*reinterpret_cast<u32*>(primary+0xc41c+i*4));
    if(injected_phase==2)fault();return actor;
}
void __fastcall injected_post(void*,void*){if(injected_phase==3)fault();}
bool recovered_factory(void* receiver){
    using factory=void*(__fastcall*)(void*,void*,u32,u32,u32,u32);
    __try{reinterpret_cast<factory>(0x423a90)(receiver,nullptr,0,0,0,0);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
void recovered_outside(){__try{fault();}__except(EXCEPTION_EXECUTE_HANDLER){}}
[[noreturn]] void __cdecl checkpoint_passed(){ExitProcess(78);}
int escape_child(unsigned phase,unsigned boundary){
    seed();e4_observe::configure();e4_observe::install(GetModuleHandleW(nullptr));if(startup_refused)return 79;
    cpp_fault=phase>=5;injected_phase=cpp_fault?phase-5:phase;
    actor_arena=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x7e6d4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!actor_arena)return 80;
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x4614f0)(actor_arena,nullptr);
    const auto primary=actor_ai::installed().primary_base;
    put(primary+0xc40c,0);put(primary+0xc410,32);put(primary+0xc414,0);put(primary+0xc418,0);
    for(unsigned i=0;i<32;++i){put(primary+0xc384+i*4,i);put(primary+0xc41c+i*4,32+i);}
    put(actor_ai::installed().cai_object+0xc4a0,31);
    // Native services replaced after the guarded install: these are test hooks,
    // not game initialization. Execute only factory/ring/span fragments.
    jump(0x45fed0,address(reinterpret_cast<void*>(&factory_allocate)));
    jump(0x423330,address(reinterpret_cast<void*>(&injected_ctor)));
    *reinterpret_cast<unsigned char*>(0x423953)=0xc3;
    jump(0x4603b0,address(reinterpret_cast<void*>(&injected_post)));
    std::array<u32,4> vt{};vt[1]=address(reinterpret_cast<void*>(&factory_size));u32 receiver=address(vt.data());
    const auto caught=recovered_factory(&receiver);
    if(caught!=(injected_phase<4))return 81;
    if(injected_phase==4)recovered_outside(); // PUBLISHED is closed and must not trip.
    if(boundary){
        // Stop after the isolated frame-entry span if the monitor fails to
        // terminate; no native world update or game entry point can execute.
        jump(0x401d77,address(reinterpret_cast<void*>(&checkpoint_passed)));
        reinterpret_cast<void(__cdecl*)()>(0x401d70)();
    }else{
        reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4615a0)(actor_arena,nullptr,0x844);
    }
    return 78;
}
void construction_tests(const char* exe,const char* retail){
    for(unsigned phase=0;phase<9;++phase)for(unsigned boundary=0;boundary<2;++boundary){
        char command[4096]{};std::snprintf(command,sizeof(command),"\"%s\" \"%s\" escape %u %u",exe,retail,phase,boundary);
        STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        if(!CreateProcessA(nullptr,command,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){check(false,"T20 child created");continue;}
        const auto wait=WaitForSingleObject(pi.hProcess,15000);if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,82);WaitForSingleObject(pi.hProcess,5000);}
        DWORD code=83;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        if(code!=(phase==4?78u:77u))std::printf("T20 phase=%u boundary=%u child=%lu\n",phase,boundary,code);
        check(code==(phase==4?78u:77u),"T20 production native hooks stop recovered construction at both checkpoints; PUBLISHED control continues");
    }
}
__declspec(naked) void __stdcall formation_store(u32,u32,u32){
    __asm {
        pushad
        mov eax,[esp+36]
        mov ebp,[esp+40]
        mov edx,[esp+44]
        call edx
        popad
        ret 12
    }
}
void provenance_tests(){
    const auto primary=actor_ai::installed().primary_base,team=actor_ai::installed().team_base,cai=actor_ai::installed().cai_object;
    put(primary+0xc41c,32);put(cai+0xc4a0,31);put(cai+0xce74,4);put(team+4,32);put(team+0x28,1);
    put(primary+0x2e8,1);*reinterpret_cast<unsigned char*>(primary+0x2ec)=1;put(primary+0x538,4);
    std::array<u32,32> args{};std::array<u32,9> r{};r[3]=address(args.data())-4;r[6]=team;
    const auto enter=[&]{e4_observe::test_emit(0x525fa0,false,r.data());};
    const auto leave=[&]{for(const auto& s:e4_observe_sites::sites)if(s.type==e4_observe_sites::kind::writer_exit&&s.arg==0x525fa0){e4_observe::test_emit(s.point,false,r.data());break;}};
    u32 store=0,next=0;
    for(const auto& s:e4_observe_sites::sites)if(s.type==e4_observe_sites::kind::field_store&&(s.arg&255)==10){store=s.point;next=s.next;break;}
    check(store!=0,"O5 actual formation store hook exists");if(!store)return;
    const auto original=*reinterpret_cast<unsigned char*>(next);*reinterpret_cast<unsigned char*>(next)=0xc3;
    enter();formation_store(primary,1,store);leave();
    e4_observe::test_emit(0x5251f0,false,r.data());
    const auto before_dissolved=e4_observe::json();r[1]=primary;e4_observe::test_emit(0x5068cb,false,r.data());
    check(e4_observe::json()==before_dissolved,"O5 a formation read while team cleanup is still running is not a dissolved-team read");
    r[0]=0;e4_observe::test_emit(0x525262,false,r.data());
    const auto after_free=e4_observe::json();
    check(after_free.find("[744,1,1,5398432,4,")!=std::string::npos,"O5 unchanged formation value retains exact team-writer provenance");
    r[1]=primary;e4_observe::test_emit(0x5068cb,false,r.data());
    auto once=e4_observe::json();
    check(once.find("[9,0,5269707,")!=std::string::npos,"O5 consuming formation from dissolved team reports native caller");
    *reinterpret_cast<unsigned char*>(primary+0x2ec)=0;e4_observe::test_emit(0x5068cb,false,r.data());
    check(e4_observe::json()==once,"O5 invalid formation flag does not count a read that retail skips");
    put(cai+0xce74,8);put(primary+0x538,8);*reinterpret_cast<unsigned char*>(primary+0x2ec)=1;
    enter();formation_store(primary,1,store);leave();
    auto rewritten=e4_observe::json();e4_observe::test_emit(0x5068cb,false,r.data());
    check(e4_observe::json()==rewritten,"O5 same-value write by next team clears stale provenance");
    *reinterpret_cast<unsigned char*>(next)=original;
    std::ofstream out("e4-host-provenance.json");out<<e4_observe::json();
}
void overflow_tests(){
    std::array<u32,32> args{};std::array<u32,9> r{};r[3]=address(args.data())-4;r[6]=actor_ai::installed().primary_base;
    for(unsigned n=0;n<129;++n)e4_observe::test_emit(0x52264f,false,r.data());
    for(unsigned n=0;n<129;++n)e4_observe::test_emit(0x52264f,true,r.data());
    r[6]=actor_ai::installed().team_base;
    for(unsigned n=0;n<17;++n)e4_observe::test_emit(0x525fa0,false,r.data());
    u32 leave=0;for(const auto& s:e4_observe_sites::sites)if(s.type==e4_observe_sites::kind::writer_exit&&s.arg==0x525fa0){leave=s.point;break;}
    for(unsigned n=0;n<17;++n)e4_observe::test_emit(leave,false,r.data());
    std::array<unsigned char,0x400> entity{};std::array<unsigned char,0x6c> descriptor{};
    put(address(descriptor.data()+4),0x7fc00000);args[0]=0x45102e;args[1]=address(descriptor.data());r[6]=address(entity.data());
    for(unsigned n=0;n<17;++n)e4_observe::test_emit(0x499b10,false,r.data());
    for(unsigned n=0;n<17;++n)e4_observe::test_emit(0x499b74,true,r.data());
    check(e4_observe::json().find("\"counts\":[0,0,0,0,0,0,0,0,3,1,0,0,0,0]")!=std::string::npos,
        "bounded borrow/writer/damage overflows each report once and preserve outer stacks without false transitions");
}
void team_resolver_tests(){
    const auto& d=actor_ai::installed();std::array<u32,32> args{};std::array<u32,9> r{};
    r[3]=address(args.data())-4;r[6]=d.team_base;
    put(d.cai_object+0xce74,4);put(d.cai_object+0xce84,3);put(d.team_base+0x9c4,1);
    e4_observe::test_emit(0x5251f0,false,r.data());
    using lookup=void*(__fastcall*)(void*,void*,u32);
    check(reinterpret_cast<lookup>(0x525100)(reinterpret_cast<void*>(d.cai_object),nullptr,4)==reinterpret_cast<void*>(d.team_base),
        "O1 vt+21c team lookup still returns the native live team during its destructor");
    e4_observe::test_emit(0x525262,false,r.data());
    put(d.cai_object+0xce74,8);
    check(!reinterpret_cast<lookup>(0x525100)(reinterpret_cast<void*>(d.cai_object),nullptr,4),"O1 vt+21c stale team token is refused natively");
    const auto before=e4_observe::json();
    check(before.find("\"counts\":[0,0,0,0,0,0,0,0,3,1,0,0,1,1]")!=std::string::npos,
        "O1 team resolve-during-destroy/stale counters use their own pool and caller, not controller generations");
    check(reinterpret_cast<lookup>(0x525100)(reinterpret_cast<void*>(d.cai_object),nullptr,8)==reinterpret_cast<void*>(d.team_base)&&e4_observe::json()==before,
        "O1 current team token after release has no false destroying event");
}
struct borrow_thread_args {HANDLE ready,go;};
DWORD WINAPI borrow_thread(void* pointer){
    const auto* a=static_cast<const borrow_thread_args*>(pointer);
    std::array<u32,32> args{};std::array<u32,9> r{};r[3]=address(args.data())-4;r[6]=actor_ai::installed().primary_base;
    e4_observe::test_emit(0x52264f,false,r.data());SetEvent(a->ready);
    WaitForSingleObject(a->go,5000);e4_observe::test_emit(0x52264f,true,r.data());return 0;
}
void thread_tests(){
    // Publish a synthetic owner using the production phase/state callbacks.
    std::array<unsigned char,0x844> actor{};put(address(actor.data()+0x7dc),32);
    std::array<u32,32> stack_args{};stack_args[0]=0x600001;
    std::array<u32,9> state{};state[3]=address(stack_args.data())-4;
    e4_observe::test_emit(0x423a90,false,state.data());
    state[0]=0;state[1]=address(actor.data())-4;e4_observe::test_emit(0x461674,false,state.data());
    state[6]=address(actor.data());e4_observe::test_emit(0x423ad0,false,state.data());
    state[7]=0;e4_observe::test_emit(0x522a1f,false,state.data());
    state[7]=32;state[1]=address(actor.data());e4_observe::test_emit(0x42394d,true,state.data());
    e4_observe::test_emit(0x423ad0,true,state.data());e4_observe::test_emit(0x423ade,false,state.data());
    HANDLE go=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE ready[]{CreateEventW(nullptr,TRUE,FALSE,nullptr),CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    borrow_thread_args args[]={{ready[0],go},{ready[1],go}};
    HANDLE threads[]{CreateThread(nullptr,0,borrow_thread,&args[0],0,nullptr),CreateThread(nullptr,0,borrow_thread,&args[1],0,nullptr)};
    check(go&&ready[0]&&ready[1]&&threads[0]&&threads[1]&&WaitForMultipleObjects(2,ready,TRUE,5000)==WAIT_OBJECT_0,
        "O1 two threads hold independent borrow frames concurrently");
    state[6]=actor_ai::installed().primary_base;
    e4_observe::test_emit(0x522ed0,false,state.data()); // release on a third thread (this caller)
    SetEvent(go);const auto completed=WaitForMultipleObjects(2,threads,TRUE,5000);
    check(completed==WAIT_OBJECT_0,"O1 both threads unwind their own borrow stack");
    if(completed!=WAIT_OBJECT_0)TerminateProcess(GetCurrentProcess(),84);
    for(auto h:threads)if(h)CloseHandle(h);for(auto h:ready)if(h)CloseHandle(h);if(go)CloseHandle(go);
    state[0]=0;e4_observe::test_emit(0x522f57,false,state.data());
    const auto status=e4_observe::json();
    check(status.find("\"contexts_live\":0")!=std::string::npos&&status.find("\"contexts_peak\":3")!=std::string::npos&&
        status.find("\"counts\":[1,0,0,0,0,0,0,0,3,1,0,0,1,1]")!=std::string::npos,
        "O1 foreign-thread release is detected once; idle contexts recycle with no false transitions");
    const auto returned=std::to_string(0x522652);
    check(status.find(",2,["+returned+","+returned+"]]")!=std::string::npos,"O1 both foreign borrowing return addresses survive in the event record");
}
void __fastcall schedule(void*,void*,void*,u32){}
void* __fastcall copy_damage(void* p,void*,void* src){std::memcpy(p,src,0x6c);return p;}
void* __cdecl payload(void* p){std::memset(p,0,0x6c);return p;}
void __cdecl noop(unsigned,unsigned){}
void damage_tests(unsigned char* code){
    seed();e4_observe::configure();
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto ap=actor_pools_plan::make(address(block),address(code+0x4000),address(reinterpret_cast<void*>(&noop)));
    check(bool(ap),"124-pool plan available for T24");if(!ap)return;
    std::memcpy(code+0x4000,ap->code.data(),ap->code.size());
    patch_transaction::transaction tx;tx.add(ap->contribution);check(tx.commit()==patch_transaction::outcome::committed,"stock-to-124 transaction commits in private host");
    actor_ai::published=actor_ai::stock;actor_ai::published.stage=1;
    jump(0x498890,address(reinterpret_cast<void*>(&payload)));jump(0x453d10,address(reinterpret_cast<void*>(&copy_damage)));jump(0x41a730,address(reinterpret_cast<void*>(&schedule)));
    e4_observe::install(GetModuleHandleW(nullptr));check(!startup_refused,"production E4 installer accepts the union with ActorPools=124");
    const auto threshold=*reinterpret_cast<float*>(0x680030);
    const float values[]={threshold,threshold+1,-1,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    using method=void(__fastcall*)(void*,void*);using take=unsigned char(__fastcall*)(void*,void*,void*,u32);
    std::array<unsigned char,0x6c> desc{};std::array<unsigned char,0x400> entity{};
    for(float delay:values)for(u32 flags:{0u,1u})for(u32 pending:{0u,1u})for(u32 live:{0u,31u,32u,33u,124u}){
        reinterpret_cast<method>(0x499ad0)(block,nullptr);entity.fill(0);desc.fill(0);std::memcpy(desc.data()+4,&delay,4);
        put(address(desc.data()+0x14),flags);put(address(entity.data()+0x2fc),pending);put(address(block+0x3670),live);
        const auto expected=e4_observe_rules::classify(delay,threshold,flags,pending,live,124);
        const auto accepted=reinterpret_cast<take>(0x499b10)(entity.data(),nullptr,desc.data(),0);
        check(bool(accepted)==(expected==e4_observe_rules::damage_reason::accepted),"T24 classification equals actual retail take return");
    }
    const auto observed=e4_observe::json();
    std::vector<unsigned char> image_before(fixture_space,fixture_space+0x700000);
    for(unsigned n=0;n<16;++n){e4_observe::sample();(void)e4_observe::json();}
    check(!std::memcmp(image_before.data(),fixture_space,image_before.size()),"observe-only: repeated production sample/JSON leaves all 7 MiB of private game image identical");
    check(observed.find(",32,40,40,8,24,120]")!=std::string::npos,
        "T24 real production telemetry: all four outcomes and would-refuse at 32/33/full, never 31 or preflight refusals");
    std::ofstream out("e4-host-observation.json");out<<observed;out.close();
}
}
int main(int argc,char** argv){
    std::setbuf(stdout,nullptr);SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* p)->LONG{std::printf("HOST EXCEPTION code=%08lx at=%p ESP=%08lx EBP=%08lx\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress,p->ContextRecord->Esp,p->ContextRecord->Ebp);return EXCEPTION_EXECUTE_HANDLER;});SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if((argc!=2&&argc!=5)||!load(argv[1]))return 2;
    DWORD old=0;VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old);
    if(argc==5)return escape_child(std::strtoul(argv[3],nullptr,10),std::strtoul(argv[4],nullptr,10));
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!code)return 2;
    std::puts("stub tests");stub_tests(code);std::puts("plan tests");plan_tests(code);std::puts("damage tests");damage_tests(code);provenance_tests();overflow_tests();team_resolver_tests();thread_tests();construction_tests(argv[0],argv[1]);
    std::printf("E4 native host: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
