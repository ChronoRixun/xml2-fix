#include "online_menus_anchors.hpp"
#include "log.hpp"
#include "script_functions_rules.hpp"
#include <cstring>

namespace online_menus_anchors {
namespace {
// Five slots, same header, name ordering, reference ownership and allocator.
// +003c: 512 name/value pairs; +103c: 16 mask words; +107c: count.
// +1080: 512 scene-object pointers; +1880: 16 mask words; +18c0: count.
// A power-of-two stride also makes the allocator's pointer division exact.
alignas(16) unsigned char pool[0xa050]{};
bool overflow=false; // retail menu scene loading is synchronous on the game thread
struct fixup { unsigned va,value,relative; };
constexpr fixup fixups[]{
#include "online_menus_anchors_fixups.inc"
};

unsigned __fastcall next_set(const unsigned* mask,void*,int start,bool value) {
    for(int i=start;i<512;++i) if(bool(mask[i/32]&(1u<<(i%32)))==value) return unsigned(i);
    return 512;
}
// ECX is the pointer difference at both retail division sites. Unsigned range
// checks reject negative addresses too; only exact slot starts may reach retail
// occupancy/free logic. ESI receives the index expected by each continuation.
__declspec(naked) void validity_index() {
    __asm {
        cmp ecx,0a000h
        jae invalid
        test ecx,1fffh
        jnz invalid
        mov esi,ecx
        shr esi,13
        push 588b41h
        ret
    invalid:
        push 588b6bh
        ret
    }
}
__declspec(naked) void free_index() {
    __asm {
        cmp ecx,0a000h
        jae invalid
        test ecx,1fffh
        jnz invalid
        mov esi,ecx
        shr esi,13
        push 588c3bh
        ret
    invalid:
        push 588c76h
        ret
    }
}
__declspec(naked) void bounded_push() {
    __asm {
        cmp dword ptr [ecx+840h],512
        jae full
        mov eax,[esp+4]
        push esi
        push 587e45h
        ret
    full:
        mov byte ptr [overflow],1
        ret 4
    }
}
__declspec(naked) void clear_vector_mask() {
    __asm {
        pushfd
        pushad
        xor eax,eax
        mov ecx,16
        rep stosd
        popad
        popfd
        ret
    }
}
__declspec(naked) void clear_name_mask() {
    __asm {
        pushfd
        pushad
        mov edi,esi
        xor eax,eax
        mov ecx,16
        rep stosd
        popad
        popfd
        ret
    }
}
__declspec(naked) void initialize_stack() {
    __asm {
        pushfd
        pushad
        // caller ESP+28 is the temporary vector; return/flags/pushad add 28h.
        lea edi,[esp+850h] // 28h + 28h + 800h = mask
        xor eax,eax
        mov ecx,17
        rep stosd
        popad
        popfd
        ret
    }
}
__declspec(naked) void initialize_tables() {
    __asm {
        pushfd
        pushad
        lea edi,[esi+103ch]
        xor eax,eax
        mov ecx,17
        rep stosd
        lea edi,[esi+1880h]
        mov ecx,17
        rep stosd
        popad
        popfd
        ret
    }
}
void __fastcall scene_walk(void* field,void*,void* node,void* infos,void* transforms) {
    overflow=false;
    reinterpret_cast<void(__thiscall*)(void*,void*,void*,void*)>(0x587f90)(field,node,infos,transforms);
    if(overflow) {
        // Refuse the whole binding set, release the accepted references once,
        // and let retail destroy the temporary transform vector on its usual path.
        reinterpret_cast<void(__thiscall*)(void*)>(0x587990)(infos);
        *reinterpret_cast<unsigned*>(static_cast<unsigned char*>(infos)+0x840)=0;
        logger::write("online menus: scene exceeds %s; anchor bindings refused",capability);
    }
}
}
patch_transaction::contribution contribution() {
    patch_transaction::contribution c;
    c.owner=capability;
    constexpr script_functions_rules::guard guards[]{
#include "online_menus_anchors_guards.inc"
    };
    for(const auto& g:guards) c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),g.what});
    const auto base=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pool));
    for(const auto& f:fixups)
        c.writes.push_back(patch_transaction::relocation(f.va,"",false,f.value+(f.relative ? base : 0),0,"512 anchor layout"));
    const struct { unsigned va,size;void(*target)(); } hooks[]{
        {0x587e40,5,&bounded_push}, {0x587a27,17,&clear_vector_mask},
        {0x587877,17,&clear_name_mask}, {0x58833a,7,&initialize_stack},
        {0x588857,6,&initialize_tables}, {0x58835a,5,reinterpret_cast<void(*)()>(&scene_walk)},
        {0x587869,5,reinterpret_cast<void(*)()>(&next_set)}, {0x587a15,5,reinterpret_cast<void(*)()>(&next_set)},
        {0x588b30,17,&validity_index}, {0x588c2a,17,&free_index}
    };
    for(const auto& h:hooks) {
        c.writes.push_back(patch_transaction::relocation(h.va,"e8",true,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(h.target)),h.size-5,"512 anchor adapter"));
        // These adapters transfer directly to retail continuations.
        if(h.va==0x587e40 || h.va==0x588b30 || h.va==0x588c2a) c.writes.back().bytes[0]=0xe9;
    }
    // Installation precedes engine menu use. Initialize the same empty allocator
    // state also for hosts that don't execute the PE's CRT initializers.
    std::memset(pool,0,sizeof(pool));
    auto put=[](unsigned off,unsigned value) { std::memcpy(pool+off,&value,4); };
    put(0,0x69cba4);
    put(4+0xa024,5); // free ring count (old +2b94)
    for(unsigned i=0;i<5;++i) { put(4+0xa004+i*4,i);put(4+0xa030+i*4,i|8); }
    put(4+0xa044,7);put(4+0xa048,3);
    return c;
}
}
