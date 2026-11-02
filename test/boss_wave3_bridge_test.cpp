// Production bridges exercised against invented readers/actors and continuations; no game code executes.
#include "../src/boss_magneto.cpp"
#include "../src/boss_blob.cpp"
#include <iostream>
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t*){return {};}
bool flag(const wchar_t*,const wchar_t*,bool fallback){return fallback;}
}
namespace logger {void write(const char*,...){} }
namespace mold_effects {bool supported(){return true;} }
namespace mold_query {
bool supported(){return true;}bool is_a(void*,std::uint32_t){return false;}
bool pools_ready(){return false;}void* active_hero(std::uint32_t&){return nullptr;}
}
namespace entity_capacity {const entity_capacity_layout::descriptor& installed(){return entity_capacity_layout::stock_descriptor;} }
namespace {
using u32=std::uint32_t;
using boss_magneto::hurt_stub;using boss_blob::parsed_stub;
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
unsigned char object[64]{};u32 reader[1]{},vtable[7]{};
short authored=4;unsigned reads=0;bool correct_attribute=false;
void __fastcall read_number(void*,void*,const char* name,short* out){++reads;correct_attribute=std::strcmp(name,"xml1heaviness")==0;if(authored>=0)*out=authored;}
u32 saved_sp=0,after_sp=0,seen_edx=0,seen_imm=0,seen_this=0,seen_hit=0,seen_ebx=0,seen_esi=0,seen_edi=0;
float fpu_value=2.5f,fpu_result=0;
__declspec(naked) void parse_continuation(){
    __asm {
        mov seen_edx,edx
        mov eax,[esp]
        mov seen_imm,eax
        add esp,4
        ret
    }
}
__declspec(naked) void parse_caller(){
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov saved_sp,esp
        mov ebx,011112222h
        lea esi,object
        lea edi,reader
        fld fpu_value
        call parsed_stub
        fstp fpu_result
        mov after_sp,esp
        mov seen_ebx,ebx
        mov seen_esi,esi
        mov seen_edi,edi
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) void hurt_continuation(){
    __asm {
        mov seen_this,ebp
        mov eax,[esp+05ch]
        mov seen_hit,eax
        pop ebp
        add esp,054h
        ret 0ch
    }
}
__declspec(naked) void hurt_caller(){
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov saved_sp,esp
        mov ebx,011112222h
        mov esi,033334444h
        mov edi,055556666h
        push 3
        push 2
        push 077778888h
        lea ecx,object
        fld fpu_value
        call hurt_stub
        fstp fpu_result
        mov after_sp,esp
        mov seen_ebx,ebx
        mov seen_esi,esi
        mov seen_edi,edi
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
u32 address(void* p){return u32(reinterpret_cast<std::uintptr_t>(p));}
}
int main(){
    boss_blob::parsed_resume=address(reinterpret_cast<void*>(&parse_continuation));
    reader[0]=address(vtable);vtable[6]=address(reinterpret_cast<void*>(&read_number));
    boss_runtime::put(object,0x1c,0x141u);parse_caller();
    CHECK(saved_sp==after_sp&&fpu_result==fpu_value);
    CHECK(reads==1&&correct_attribute&&seen_imm==0x68902c&&seen_edx==address(vtable));
    CHECK(seen_ebx==0x11112222&&seen_esi==address(object)&&seen_edi==address(reader));
    CHECK(boss_blob::weights.get(0x41,0x141)==4);
    authored=5;boss_runtime::put(object,0x1c,0x241u);parse_caller();
    CHECK(!boss_blob::weights.get(0x41,0x141)&&boss_blob::weights.get(0x41,0x241)==5);
    authored=-1;parse_caller();CHECK(!boss_blob::weights.get(0x41,0x241));
    boss_magneto::enabled=false;boss_magneto::hurt_resume=address(reinterpret_cast<void*>(&hurt_continuation));
    hurt_caller();CHECK(saved_sp==after_sp&&fpu_result==fpu_value);
    CHECK(seen_this==address(object)&&seen_hit==0x77778888);
    CHECK(seen_ebx==0x11112222&&seen_esi==0x33334444&&seen_edi==0x55556666);
    std::cout<<checks<<" wave-three bridge checks, "<<failures<<" failures\n";return failures?1:0;
}
