#include "mold_beam.hpp"
#include "mold_controller.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// Master Mold's core attack plays the native blaster node. After the native muzzle
// calculation, this bridge points the beam at the generation-checked core target;
// ordinary tracing, collision and damage stay native.
namespace mold_beam {
namespace {
using u32 = std::uint32_t;
template<class T> T read(const void* p, u32 off) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p)+off,sizeof v); return v; }
using position_t = const float*(__fastcall*)(void*,void*,float*);
u32 beam_resume=0x452312;
void __cdecl adjust_core_beam(void* actor,const float* start,float* direction) {
    void* target=nullptr;
    if(!mold_controller::beam_target(actor,target)||!target||read<float>(target,0x27c)<=0) return;
    float scratch[3]{};
    const auto* point=read<position_t>(read<const void*>(target,0),0x24)(target,nullptr,scratch);
    float d[3]{},length2=0;
    for(u32 i=0;i<3;++i){d[i]=point[i]-start[i];length2+=d[i]*d[i];}
    if(length2<=0) return;
    const float length=std::sqrt(length2);
    for(u32 i=0;i<3;++i)direction[i]=d[i]/length;
    logger::write("mold beam: native beam from %.3f/%.3f/%.3f to core %.3f/%.3f/%.3f, range %.3f; ordinary trace/damage unchanged",
        start[0],start[1],start[2],point[0],point[1],point[2],length);
}
__declspec(naked) void beam_stub() {
    __asm {
        pushfd
        pushad
        lea eax,[esp+074h]
        lea ecx,[esp+068h]
        push eax
        push ecx
        push edi
        call adjust_core_beam
        add esp,12
        popad
        popfd
        lea ecx,[esp+0214h]
        jmp dword ptr [beam_resume]
    }
}
}
bool supported() {
    constexpr std::array<limits_rules::guard,7> guards{{
        {0x4a1700,"83ec1c568b74242856","entity lookup"},
        {0x4654b0,"568bf1e8682a0600","handle resolver"},
        {0x41b120,"56578b7c240c85ff","string handle constructor"},
        {0x427ca0,"8b442404","combat node lookup"},
        {0x42c340,"56578b7c240c85ff","combat node play"},
        {0x42c7c0,"53578bf9e8f7bf0900","manual combat target"},
        {0x45230b,"8d8c2414020000","beam direction after native muzzle tracing"}}};
    for(const auto& g:guards) if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)) {
        logger::write("mold beam: guard mismatch at %08X",g.va); return false;
    }
    return true;
}
bool prepare(xml1_hook_transaction::patch& patch){
    if(!supported())return false;
    patch.address=0x45230b;
    patch.before={0x8d,0x8c,0x24,0x14,0x02,0x00,0x00};
    patch.after={0xe9,0,0,0,0,0x90,0x90};
    const u32 delta=static_cast<u32>(reinterpret_cast<std::uintptr_t>(&beam_stub))-0x452310;
    std::memcpy(patch.after.data()+1,&delta,4);
    return true;
}
}
