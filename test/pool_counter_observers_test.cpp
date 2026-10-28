// Execute every generated branch observer against the same retail fragment:
// native destinations, stack, GPRs and flags must match. No game entry point.
#include "pool_counter_observers.hpp"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>
extern "C" unsigned char fixture_space[0x700000];
namespace {
using u32=std::uint32_t;
struct cpu {u32 edi,esi,ebp,esp,ebx,edx,ecx,eax,flags;};
cpu input{},output{},callback_state{};
__declspec(align(16)) std::array<unsigned char,512> fx_output{};
u32 host_sp=0,stack_top=0,entry=0,path=0,callbacks=0,mode=0;
u32 getter_calls=0;
// A refusal edge that returns (w3/power-pools: the observer sits on the unchanged JNE; its fallthrough is XOR EAX,EAX;
// RET): the fixture stack's return address is this island, which records path 2.
u32 return_island=0;
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
void __cdecl callback(unsigned,const unsigned* saved){
    // Scalar-only synthetic callback, as the production callback must be.
    __asm {
        push esi
        push edi
        mov esi,saved
        mov edi,offset callback_state
        mov ecx,9
        cld
        rep movsd
        pop edi
        pop esi
    }
    ++callbacks;
    // Force the caller-saved integer registers/flags away from native values.
    __asm {
        mov eax,12345678h
        mov ecx,23456789h
        mov edx,3456789ah
        cmp eax,ecx
    }
}
void reset_fx(){
    fx_output.fill(0);
    __asm {
        fninit
        pxor xmm0,xmm0
        pxor xmm1,xmm1
        pxor xmm2,xmm2
        pxor xmm3,xmm3
        pxor xmm4,xmm4
        pxor xmm5,xmm5
        pxor xmm6,xmm6
        pxor xmm7,xmm7
    }
}
bool same_fx(const std::array<unsigned char,512>& a,const std::array<unsigned char,512>& b){
    if(std::memcmp(a.data(),b.data(),8) || std::memcmp(a.data()+24,b.data()+24,8) || std::memcmp(a.data()+160,b.data()+160,128))return false;
    const auto top=(a[3]>>3)&7;
    for(unsigned i=0;i<8;++i)if(a[4]&(1u<<((top+i)&7)))
        if(std::memcmp(a.data()+32+i*16,b.data()+32+i*16,10))return false;
    return true; // relocated FPU instruction pointers and empty registers are not semantic state
}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
void put(u32 va,u32 value){std::memcpy(reinterpret_cast<void*>(va),&value,4);}
void jump(u32 va,u32 target){*reinterpret_cast<unsigned char*>(va)=0xe9;put(va+1,target-va-5);}
std::vector<unsigned char> mapped(0x700000);
bool load(const char* exe){
    std::ifstream f(exe,std::ios::binary);std::vector<unsigned char> raw(std::istreambuf_iterator<char>(f),{});
    if(raw.size()<0x400)return false;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(raw.data());auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase!=0x400000)return false;
    for(unsigned k=0;k<nt->FileHeader.NumberOfSections;++k){const auto& s=IMAGE_FIRST_SECTION(nt)[k];
        if(s.VirtualAddress<0x1000 || s.VirtualAddress+s.SizeOfRawData>mapped.size()+0x1000)return false;
        std::memcpy(mapped.data()+s.VirtualAddress-0x1000,raw.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    return true;
}
void reset_image(){std::memcpy(fixture_space,mapped.data(),mapped.size());}
void destination(u32 va,unsigned which,u32 scratch){
    auto* p=reinterpret_cast<unsigned char*>(scratch);p[0]=0xc7;p[1]=0x05;put(scratch+2,address(&path));put(scratch+6,which);jump(scratch+10,address(reinterpret_cast<void*>(&captured)));
    // Some native outcomes are three bytes apart (XOR EAX,EAX; RET).
    // Two-byte jumps fit both destinations without overwriting each other.
    const u32 island=which?va+96:va-96;
    jump(island,scratch);*reinterpret_cast<unsigned char*>(va)=0xeb;
    *reinterpret_cast<unsigned char*>(va+1)=static_cast<unsigned char>(island-va-2);
}
u32& reg(unsigned i){switch(i){case 0:return input.eax;case 1:return input.ecx;case 2:return input.edx;case 3:return input.ebx;case 5:return input.ebp;case 6:return input.esi;default:return input.edi;}}
u32 word(const std::vector<unsigned char>& b,unsigned p,unsigned n){u32 v=0;for(unsigned i=0;i<n;++i)v|=u32(b[p+i])<<(8*i);return v;}
void prepare(const pool_counter_sites::observer& s,bool full,unsigned char* block){
    input={0x11111111,0x22222222,0x33333333,0,0x44444444,0x55555555,0x66666666,0x77777777,0x602u|(full?0x40u:0u)};
    auto b=patch_transaction::bytes_of(s.prefix);unsigned at=0;u32 limit=0;
    if(!b.empty() && b[0]==0xb8){limit=word(b,1,4);input.eax=limit;at=5;}
    if(at<b.size()){
        const auto op=b[at];
        if(op==0x81 || op==0x83){
            const auto mr=b[at+1];const bool memory=(mr>>6)!=3;
            const unsigned disp=(mr>>6)==2?4:(mr>>6)==1?1:0;
            const auto imm=op==0x81?4u:1u;
            limit=word(b,at+2+disp,imm);
            if(memory){reg(mr&7)=address(block);put(address(block)+word(b,at+2,disp),limit-(full?0:1));}
            else reg(mr&7)=limit-(full?0:1);
        }else if(op==0x3d){limit=word(b,at+1,4);input.eax=limit-(full?0:1);}
        else if(op==0x3b){input.ecx=limit-(full?0:1);}
        else if(op==0x39){limit=std::string_view(s.name)=="zone_globals"?32:100;input.eax=limit;input.esi=address(block);put(address(block)+word(b,at+2,4),limit-(full?0:1));}
    }
    if(std::string_view(s.name)=="model_component")input.esi=7;
    if(std::string_view(s.name)=="particle_systems"){input.esi=full?0:0x12345678;input.edi=address(block);}
    stack_top=address(stack.data()+2048);stack.fill(0xa5a5a5a5);
    if(return_island)stack[2048]=return_island;
}
bool same(const cpu& a,const cpu& b){
    // Reserved/undefined arithmetic flag bits are not part of the architectural contract.
    constexpr u32 flags=0xcd5; // OF, DF, SF, ZF, AF, PF, CF
    return a.edi==b.edi && a.esi==b.esi && a.ebp==b.ebp && a.esp==b.esp && a.ebx==b.ebx && a.edx==b.edx && a.ecx==b.ecx && a.eax==b.eax && ((a.flags^b.flags)&flags)==0;
}
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    unsigned checks=0,failures=0;const auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}};
    if(argc!=2 || !load(argv[1])){std::printf("owned retail EXE required\n");return 2;}
    DWORD old=0;check(VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old)!=0,"fixture executable");
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(code && block,"owned code/data storage");
    for(unsigned i=0;i<pool_counter_sites::observers.size();++i){const auto& s=pool_counter_sites::observers[i];
        if(s.condition>=100 || s.condition==1 || std::string_view(s.name)=="model_preflight")continue;
        for(bool full:{false,true}){
            reset_image();prepare(s,full,block);
            destination(s.next,0,address(code+0x1000));if(s.target)destination(s.target,1,address(code+0x1100));
            // Seeded on the branch itself (next == target): the native fallthrough ends in RET, not at s.next.
            const auto suffix=patch_transaction::bytes_of(s.suffix);
            return_island=0;
            if(s.next==s.target && !suffix.empty() && suffix.back()==0xc3) {
                auto* q=code+0x1200;q[0]=0xc7;q[1]=0x05;put(address(q)+2,address(&path));put(address(q)+6,2);
                jump(address(q)+10,address(reinterpret_cast<void*>(&captured)));return_island=address(q);
            }
            stack[2048]=return_island?return_island:0xa5a5a5a5u;
            entry=s.va;callbacks=0;execute();const auto native=output;const auto native_path=path;
            const auto before=patch_transaction::bytes_of(s.hex);
            auto generated=pool_counter_observers::stub(s,address(code),address(reinterpret_cast<void*>(&callback)),i);
            check(generated.size()<=pool_counter_observers::stride,"stub fits its slot");
            std::memcpy(code,generated.data(),generated.size());jump(s.va,address(code));
            for(std::size_t k=5;k<before.size();++k)*reinterpret_cast<unsigned char*>(s.va+k)=0x90;
            prepare(s,full,block);callbacks=0;FlushInstructionCache(GetCurrentProcess(),nullptr,0);execute();
            if(!same(native,output) || path!=native_path){std::printf("site %x full %d native flags %x observed %x\n",s.va,full,native.flags,output.flags);}
            check(same(native,output) && path==native_path,"GPRs, flags, stack and native branch match");
            check(callbacks==(s.condition==0?1u:full?1u:0u),"callback only on refusal edge");
        }
    }
    return_island=0;
    // Eleven real virtual-count call/suffix spans, including the displaced FLD.
    // Observe 41/300 unchanged: diagnostics must not install a scratch clamp.
    unsigned particle_ops=0;
    for(const auto& s:pool_counter_sites::observers)if(s.condition==1) {
        ++particle_ops;
        for(u32 count:{0u,39u,40u,41u,300u,0xffffffffu}) {
            reset_image();
            const unsigned char getter[]{0x9c,0xff,0x05,0,0,0,0,0x9d,0x8b,0x41,0x0c,0xc3};
            std::memcpy(code+0x1200,getter,sizeof getter);
            put(address(code+0x1203),address(&getter_calls));
            put(address(block)+0x1074,address(code+0x1200));put(address(block)+0x0c,count);
            const auto prep=[&]{prepare(s,false,block);input.ecx=address(block);input.eax=input.edx=address(block)+0x1000;entry=s.va;reset_fx();};
            destination(s.next,0,address(code+0x1000));
            getter_calls=0;prep();execute();const auto native=output;const auto native_fx=fx_output;
            check(getter_calls==1,"native particle fragment calls the count accessor once");
            auto generated=pool_counter_observers::stub(s,address(code),address(reinterpret_cast<void*>(&callback)),0);
            std::memcpy(code,generated.data(),generated.size());jump(s.va,address(code));
            for(std::size_t k=5;k<s.hex.size()/2;++k)*reinterpret_cast<unsigned char*>(s.va+k)=0x90;
            callbacks=0;getter_calls=0;prep();FlushInstructionCache(GetCurrentProcess(),nullptr,0);execute();
            check(getter_calls==1,"particle observer does not duplicate the native virtual call");
            check(same(native,output),"particle count call retains GPRs, EFLAGS, stack and original count");
            check(callbacks==1 && callback_state.eax==count,"particle observer sees the unclamped original count once");
            check(same_fx(native_fx,fx_output),"particle observer retains FPU/SIMD state including native FLD");
        }
    }
    check(particle_ops==11,"all eleven particle virtual-count sites execute");
    // Both native value factories must still return NULL on exhaustion. Leave
    // the later compiler dereference at 0x4d89f1 byte-identical (no crash fix).
    for(u32 allocator:{0x4d6510u,0x4d6570u}) {
        reset_image();std::memset(block,0,0x40000);put(address(block)+0xc090,1556);
        const auto& s=*std::find_if(pool_counter_sites::observers.begin(),pool_counter_sites::observers.end(),[&](const auto& x){return std::string_view(x.name)=="values" && (allocator==0x4d6510?x.va==0x4d6518:x.va==0x4d657e);});
        const auto prep=[&]{prepare(s,false,block);input.ecx=address(block);put(stack_top,address(reinterpret_cast<void*>(&captured)));put(stack_top+4,0x3f800000);entry=allocator;};
        prep();execute();const auto native=output;
        check(native.eax==0,"native exhausted value allocator returns NULL");
        auto generated=pool_counter_observers::stub(s,address(code),address(reinterpret_cast<void*>(&callback)),0);
        std::memcpy(code,generated.data(),generated.size());jump(s.va,address(code));
        callbacks=0;prep();FlushInstructionCache(GetCurrentProcess(),nullptr,0);execute();
        check(same(native,output) && callbacks==1,"value NULL return counted once without changing native behavior");
        check(!std::memcmp(reinterpret_cast<void*>(0x4d89f1),mapped.data()+0x4d89f1-0x401000,5),"compiler NULL dereference path is unchanged");
    }
    // Curve zero is a valid preset as well as the exhaustion fallback. Prove the
    // native EFLAGS distinction using the original constructor and allocator.
    for(unsigned scenario=0;scenario<3;++scenario){
        const bool full=scenario==1;
        reset_image();std::memset(block,0,0x40000);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x4162b0)(block,nullptr);
        if(full)put(address(block)+0x7f88,900);
        const auto& s=*std::find_if(pool_counter_sites::observers.begin(),pool_counter_sites::observers.end(),[](const auto& x){return x.condition==100;});
        std::array<u32,8> args{};std::memcpy(args.data(),block,32);if(scenario!=0)args[0]=0x3f800000;
        const std::vector<unsigned char> before_pool(block,block+0x81c0);
        const auto prep=[&]{prepare(s,false,block);input.ecx=address(block);put(stack_top,address(reinterpret_cast<void*>(&captured)));std::memcpy(reinterpret_cast<void*>(stack_top+4),args.data(),32);entry=s.va;};
        prep();execute();const auto native=output;
        check(native.eax==(scenario==2?140u:0u) && bool(native.flags&0x40)==full,"native preset-zero/refusal-zero/nonzero results distinguish");
        const std::vector<unsigned char> after_pool(block,block+0x81c0);
        std::memcpy(block,before_pool.data(),before_pool.size());
        auto generated=pool_counter_observers::stub(s,address(code),address(reinterpret_cast<void*>(&callback)),0);
        std::memcpy(code,generated.data(),generated.size());jump(s.va,address(code));*reinterpret_cast<unsigned char*>(s.va+5)=0x90;
        callbacks=0;prep();FlushInstructionCache(GetCurrentProcess(),nullptr,0);execute();
        auto adjusted=output;
        // ECX is a scratch pointer to the by-value argument block. Copying the
        // native thiscall arguments moves that block by 36 bytes. The generator
        // proves every one of the 15 callers overwrites ECX before using it.
        if(native.eax==0) {
            check(native.ecx==output.ecx+36,"curve scratch argument pointer has exactly the expected stack translation");
            adjusted.ecx=native.ecx;
        }
        check(!std::memcmp(block,after_pool.data(),after_pool.size()),"curve observer retains every native pool byte");
        check(same(native,adjusted),"curve entry observer preserves result, flags, cleanup and non-scratch registers");
        check(callbacks==1 && callback_state.eax==native.eax && bool(callback_state.flags&0x40)==full,"curve callback sees unmodified native result/flags");
    }
    // Entry and call observers: independent native bodies make the original
    // argument and stack-cleanup contracts explicit, without starting a parser.
    for(const auto& s:pool_counter_sites::observers)if(s.condition==101 || std::string_view(s.name)=="model_preflight") {
        for(bool full:{false,true}) {
            reset_image();prepare(s,false,block);
            const bool preflight=s.condition!=101;
            if(preflight) {
                const unsigned char body[]{0x8b,0x54,0x24,0x04,0xb8,static_cast<unsigned char>(full?0:1),0x56,0x34,0x12,0x83,0xf8,0,0xc3};
                std::memcpy(reinterpret_cast<void*>(s.target),body,sizeof body);
                put(stack_top,0x31415926);destination(s.next,0,address(code+0x1000));
            } else {
                const unsigned char body[]{0x8b,0xc3,0x5b,0x83,0xf8,0,0xc2,0x0c,0};
                std::memcpy(reinterpret_cast<void*>(s.next),body,sizeof body);
                put(stack_top,address(reinterpret_cast<void*>(&captured)));put(stack_top+4,0x1111);put(stack_top+8,full?0:0x7777);put(stack_top+12,0x3333);
            }
            const auto args=std::array<u32,4>{*reinterpret_cast<u32*>(stack_top),*reinterpret_cast<u32*>(stack_top+4),*reinterpret_cast<u32*>(stack_top+8),*reinterpret_cast<u32*>(stack_top+12)};
            entry=s.va;execute();const auto native=output;
            auto generated=pool_counter_observers::stub(s,address(code),address(reinterpret_cast<void*>(&callback)),0);
            std::memcpy(code,generated.data(),generated.size());jump(s.va,address(code));if(preflight)*reinterpret_cast<unsigned char*>(s.va)=0xe8;
            prepare(s,false,block);std::memcpy(reinterpret_cast<void*>(stack_top),args.data(),sizeof args);
            callbacks=0;FlushInstructionCache(GetCurrentProcess(),nullptr,0);execute();
            check(same(native,output),"entry/call observer preserves arguments, result, GPRs, EFLAGS and cleanup");
            check(callbacks==1 && callback_state.eax==native.eax,"entry/call callback sees exact native return");
        }
    }
    VirtualFree(code,0,MEM_RELEASE);VirtualFree(block,0,MEM_RELEASE);
    std::printf("pool observers: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
