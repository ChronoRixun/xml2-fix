// Execute our production copy bridges with synthetic continuations. No game code or game process is executed.
#include "../src/boss_mystique.cpp"
#include <iostream>
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t*){return std::nullopt;}
bool flag(const wchar_t*,const wchar_t*,bool fallback){return fallback;}
}
namespace logger {void write(const char*,...){} }
namespace mold_query {bool supported(){return true;} bool is_a(void*,std::uint32_t){return false;} }
namespace {
using namespace boss_mystique;
int failures=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
unsigned char effect_outer[32]{},effect_inner[32]{},event_outer[32]{},event_inner[32]{};
std::uint32_t seh_seen=0,state_seen=0,entry_calls=0;
unsigned char __fastcall entry_spy(void* effect,void*,void* event){
    ++entry_calls;
    CHECK(copying_effect==effect);
    if(event==event_outer){
        CHECK(effect==effect_outer);
        CHECK(copy_hook(effect_inner,nullptr,event_inner)==0xa5);
        CHECK(copying_effect==effect_outer);
    }else CHECK(event==event_inner&&effect==effect_inner);
    return 0xa5;
}
__declspec(naked) void entry_resume(){
    __asm {
        mov eax,[esp]
        mov seh_seen,eax
        mov eax,[esp+4]
        mov state_seen,eax
        add esp,8
        jmp entry_spy
    }
}
std::uint32_t table[100]{},event[1]{0x33333333};
std::uint32_t saved_sp=0,after_sp=0,result_eax=0,result_flags=0,result_esi=0,result_edi=0,result_ebx=0;
std::uint32_t skin_seen=0,variant_seen=0,actor_seen=0;
unsigned char __fastcall skin_spy(void* actor,void*,const void* skin,int variant){
    actor_seen=std::uint32_t(reinterpret_cast<std::uintptr_t>(actor));
    skin_seen=std::uint32_t(reinterpret_cast<std::uintptr_t>(skin));variant_seen=variant;
    return 0x5a;
}
__declspec(naked) void skin_resume_spy(){
    __asm {
        mov result_eax,eax
        mov result_esi,esi
        mov result_edi,edi
        mov result_ebx,ebx
        pushfd
        pop eax
        mov result_flags,eax
        mov after_sp,esp
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) void skin_caller(){
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov saved_sp,esp
        lea ebx,table
        lea edi,event
        mov esi,022222222h
        mov ecx,[edi]
        push 77
        push 044444444h
        jmp skin_stub
    }
}
}
int main(){
    using namespace boss_mystique;
    copy_resume=std::uint32_t(reinterpret_cast<std::uintptr_t>(&entry_resume));
    copying_effect=nullptr;
    CHECK(copy_hook(effect_outer,nullptr,event_outer)==0xa5);
    CHECK(entry_calls==2&&seh_seen==0x674a20&&state_seen==0xffffffff&&copying_effect==nullptr);
    enabled=false; // the observation callback must leave other/native actors alone
    skin_resume=std::uint32_t(reinterpret_cast<std::uintptr_t>(&skin_resume_spy));
    table[0x178/4]=std::uint32_t(reinterpret_cast<std::uintptr_t>(&skin_spy));
    skin_caller();
    CHECK(saved_sp==after_sp);
    CHECK((result_eax&0xff)==0x5a);
    CHECK(result_esi==0x22222222&&result_edi==std::uint32_t(reinterpret_cast<std::uintptr_t>(event)));
    CHECK(result_ebx==std::uint32_t(reinterpret_cast<std::uintptr_t>(table)));
    CHECK(actor_seen==0x33333333&&skin_seen==0x44444444&&variant_seen==77);
    std::cout<<checks<<" Mystique bridge checks, "<<failures<<" failures\n";
    return failures?1:0;
}
