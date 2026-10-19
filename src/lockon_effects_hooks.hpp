#pragma once
#include <cstdint>

// Observation-only bridges into the retained native scan. The callback must not
// modify the actor, trace, frame, target or gameplay random stream.
namespace lockon_effects_hooks {
inline void (__cdecl* observe)(void*, const void*, unsigned, void*) = nullptr;
inline std::uint32_t scan_hit = 0x4ef145, scan_exit = 0x4ef274;
inline std::uint32_t hit_resume = 0x4ef206, acquired_resume = 0x4ef248;
inline std::uint32_t locked_resume = 0x4eef2e, query_entity = 0x4a9590;

__declspec(naked) inline void miss_bridge() {
    __asm {
        jnz hit
        pushfd
        pushad
        lea eax,[esp+024h]
        push 0
        push 0
        push eax
        push esi
        call dword ptr [observe]
        add esp,16
        popad
        popfd
        jmp dword ptr [scan_exit]
    hit:
        jmp dword ptr [scan_hit]
    }
}
__declspec(naked) inline void hit_bridge() {
    __asm {
        pushfd
        pushad
        lea eax,[esp+024h]
        push 0
        push 1
        push eax
        push esi
        call dword ptr [observe]
        add esp,16
        popad
        popfd
        lea ecx,[esp+018h]
        call dword ptr [query_entity]
        jmp dword ptr [hit_resume]
    }
}
__declspec(naked) inline void acquired_bridge() {
    __asm {
        pushfd
        pushad
        lea eax,[esp+024h]
        push edi
        push 2
        push eax
        push esi
        call dword ptr [observe]
        add esp,16
        popad
        popfd
        push ecx
        mov ecx,[edi+01ch]
        mov eax,esp
        jmp dword ptr [acquired_resume]
    }
}
__declspec(naked) inline void locked_bridge() {
    __asm {
        add esp,8
        pushfd
        pushad
        lea eax,[esp+024h]
        lea edx,[esi-02ch]
        push edx
        push 3
        push eax
        push edi
        call dword ptr [observe]
        add esp,16
        popad
        popfd
        pop edi
        pop esi
        jmp dword ptr [locked_resume]
    }
}
}
