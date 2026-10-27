#pragma once
#include "pool_counter_sites.hpp"
#include "patch_transaction.hpp"
#include <cstring>
#include <vector>

namespace pool_counter_observers {
using u32=std::uint32_t;
constexpr std::size_t stride=256;
inline std::vector<std::uint8_t> stub(const pool_counter_sites::observer& s,u32 at,u32 callback,unsigned id) {
    std::vector<std::uint8_t> out;
    const auto bytes=[&](std::string_view hex){auto b=patch_transaction::bytes_of(hex);out.insert(out.end(),b.begin(),b.end());};
    const auto dword=[&](u32 v){for(unsigned b=0;b<4;++b)out.push_back(static_cast<std::uint8_t>(v>>(b*8)));};
    const auto branch=[&](std::string_view op,u32 target){bytes(op);dword(target-(at+static_cast<u32>(out.size())+4));};
    // Callback consumes only integer registers, uses no CRT/Win32/FPU/SIMD code.
    // Preserve DF as well as every register and the exact native arithmetic flags.
    const auto observe=[&]{bytes("9c60fc54");out.push_back(0x68);dword(id);branch("e8",callback);bytes("83c408619d");};
    if(s.condition>=100) {
        const unsigned args=s.condition==100?32:12;
        // Reproduce the original thiscall stack for the unchanged native body.
        // The curve allocator's scratch ECX points at this copied argument block;
        // the generator proves every caller kills ECX before consuming it.
        for(unsigned i=0;i<args/4;++i) {bytes("ff7424");out.push_back(static_cast<std::uint8_t>(args));}
        branch("e8",at+128);
        observe();bytes("c2");out.push_back(static_cast<std::uint8_t>(args));out.push_back(0);
        out.resize(128,0xcc);bytes(s.hex);branch("e9",s.next);
    } else if(s.condition==1) {
        // Original virtual count call, unchanged result, then displaced native
        // instructions. Instrument before scratch selection; never clamp EAX.
        bytes(s.prefix);observe();bytes(s.suffix);branch("e9",s.next);
    } else if(std::string_view(s.name)=="model_preflight") {
        bytes("ff742404");branch("e8",s.target);bytes("8d642404");observe();bytes("c3");
    } else if(s.condition==0) {
        observe();bytes(s.suffix);branch("e9",s.next);
    } else {
        bytes(s.prefix);
        bytes("0f");out.push_back(static_cast<std::uint8_t>(0x80|s.condition));
        const auto fix=out.size();dword(0);
        // JE takes the refusal edge; JNE takes the successful edge.
        if(s.condition!=4)observe();
        bytes(s.suffix);branch("e9",s.next);
        const auto taken=at+static_cast<u32>(out.size());
        const u32 relative=taken-(at+static_cast<u32>(fix)+4);
        std::memcpy(out.data()+fix,&relative,4);
        if(s.condition==4)observe();
        branch("e9",s.target);
    }
    return out;
}
}
