// V-NPC2: execute the generated combo bridges with synthetic brains. Only the brain-table base and external
// continuations are relocated; the timer arithmetic, internal branches and register/stack contracts are real.
#include <Windows.h>
#include "npc_power_rules.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace npc_power_rules;
int checks = 0, failures = 0;
void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
template<class T> T& field(void* p, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset); }
void* entry = nullptr;
__declspec(naked) int step(void*, void*) {
    __asm { push ebp }
    __asm { push ebx }
    __asm { push esi }
    __asm { push edi }
    __asm { mov ebp,[esp+20] }
    __asm { mov edi,[esp+24] }
    __asm { call dword ptr [entry] }
    __asm { pop edi }
    __asm { pop esi }
    __asm { pop ebx }
    __asm { pop ebp }
    __asm { ret }
}
__declspec(naked) void mark_call(void*) {
    __asm { push esi }
    __asm { mov esi,[esp+8] }
    __asm { call dword ptr [entry] }
    __asm { pop esi }
    __asm { ret }
}
float clock_now = 0;
__declspec(naked) float __fastcall clock_read(void*) {
    __asm { fld dword ptr [clock_now] }
    __asm { ret }
}
void* clock_vtable[0x164 / 4] = {};
void* clock_object[1] = {clock_vtable};
__declspec(naked) void* clock_owner() {
    __asm { mov eax, offset clock_object }
    __asm { ret }
}
float random_now = 0;
__declspec(naked) float random_read(int) {
    __asm { fld dword ptr [random_now] }
    __asm { ret }
}
__declspec(naked) void power_call(void*) {
    __asm { mov ecx,[esp+4] }
    __asm { call dword ptr [entry] }
    __asm { ret }
}
// The to-hit roll's power-attack test (revision 10): esi = the attack, eax/edi as the roll has them; each relocated
// exit is a stub `mov ecx,route; ret`, so ecx names the continuation and eax/edi come back as it left them.
int roll_eax = 0, roll_edi = 0;
__declspec(naked) int roll_call(void*, int) {
    __asm { push esi }
    __asm { push edi }
    __asm { mov esi,[esp+12] }
    __asm { mov eax,[esp+16] }
    __asm { mov edi,0x55 }
    __asm { call dword ptr [entry] }
    __asm { mov [roll_eax],eax }
    __asm { mov [roll_edi],edi }
    __asm { mov eax,ecx }
    __asm { pop edi }
    __asm { pop esi }
    __asm { ret }
}
// Revision 10 item 17: enter the power decision's lock bridge with the flags of `test byte [locked],1` and the
// caller's return address at [esp+0x24] (sub esp,0x14 and four pushes since 0x518770's entry).
__declspec(naked) int lock_call(unsigned, unsigned) {
    __asm { push ebx }
    __asm { mov ebx,esp }
    __asm { mov eax,[ebx+12] }
    __asm { sub esp,0x24 }
    __asm { mov [esp+0x20],eax }
    __asm { test byte ptr [ebx+8],1 }
    __asm { call dword ptr [entry] }
    __asm { mov esp,ebx }
    __asm { pop ebx }
    __asm { ret }
}
// The retail run 0x5187b9..0x5187ce (mov ebp,[esp+0x14]; test byte [ebp+0x764],1; jne; mov ebx,[esp+0x1c]) entered
// with the deciding character at [esp+0x14] and the caller's return address at [esp+0x24].
__declspec(naked) int lock_run(void*, unsigned) {
    __asm {
        push ebx
        push ebp
        push esi
        mov esi, esp
        sub esp, 0x28
        mov eax, [esi+16]
        mov [esp+0x10], eax
        mov eax, [esi+20]
        mov [esp+0x20], eax
        mov dword ptr [esp+0x18], 0x13572468
        call dword ptr [entry]
        cmp ebx, 0x13572468
        jne lock_run_out
        or eax, 2
    lock_run_out:
        mov esp, esi
        pop esi
        pop ebp
        pop ebx
        ret
    }
}
// The retail run 0x512f01..0x512f1a of the attack-state update (esi = the AI): 1 = on at 0x512f1a, 0 = the exit.
__declspec(naked) int exit_run(void*) {
    __asm { push esi }
    __asm { mov esi,[esp+8] }
    __asm { call dword ptr [entry] }
    __asm { pop esi }
    __asm { ret }
}
// Revision 12 item 19: the picker's entry run (thiscall, ecx = the AI).
__declspec(naked) int picker_run(void*) {
    __asm { mov ecx,[esp+4] }
    __asm { call dword ptr [entry] }
    __asm { ret }
}
// Item 21: the category stores 0x511fb4..0x511fc4 with ebx 1, ebp 0, edi 4, ecx 6; out = [esp+0x64..0x70] of the run.
__declspec(naked) void category_run(std::uint32_t*) {
    __asm {
        push ebx
        push ebp
        push edi
        push esi
        mov esi, [esp+20]
        sub esp, 0x80
        mov ebx, 1
        xor ebp, ebp
        mov edi, 4
        mov ecx, 6
        call dword ptr [entry]
        mov eax, [esp+0x60]
        mov [esi], eax
        mov eax, [esp+0x64]
        mov [esi+4], eax
        mov eax, [esp+0x68]
        mov [esi+8], eax
        mov eax, [esp+0x6c]
        mov [esi+12], eax
        add esp, 0x80
        pop esi
        pop edi
        pop ebp
        pop ebx
        ret
    }
}
// Item 22: the mode getter 0x4c87c0 (al = the mode byte is 0xfd) and the gate run (esi = the hit record).
int mode_now = 0;
__declspec(naked) int mode_read() {
    __asm { mov eax, dword ptr [mode_now] }
    __asm { ret }
}
__declspec(naked) void gate_run(void*) {
    __asm { push esi }
    __asm { mov esi,[esp+8] }
    __asm { call dword ptr [entry] }
    __asm { pop esi }
    __asm { ret }
}
// Item 18: the gate (thiscall, ecx = the AI, five arguments, ret 0x14). Its energy read and picker continuation are
// stubs; the continuation records the registers and stack the picker's code after 0x50fe69 starts with.
float energy_now = 0;
__declspec(naked) float __fastcall energy_read(void*) {
    __asm { fld dword ptr [energy_now] }
    __asm { ret }
}
std::uint32_t seen_esi = 0, seen_eax = 0, seen_ecx = 0, seen_esp = 0, seen_pushed = 0, call_esp = 0;
__declspec(naked) void picker_rest() {
    __asm { mov seen_esi,esi }
    __asm { mov seen_eax,eax }
    __asm { mov seen_ecx,ecx }
    __asm { mov seen_esp,esp }
    __asm { mov eax,[esp] }
    __asm { mov seen_pushed,eax }
    __asm { pop esi }
    __asm { add esp,0xac }
    __asm { mov al,1 }
    __asm { ret 0x14 }
}
__declspec(naked) int gate_call(void*) {
    __asm { push esi }
    __asm { mov ecx,[esp+8] }
    __asm { mov esi,0x5a5a5a5a }
    __asm { push 5 }
    __asm { push 4 }
    __asm { push 3 }
    __asm { push 2 }
    __asm { push 1 }
    __asm { mov call_esp,esp }
    __asm { call dword ptr [entry] }
    __asm { cmp esi,0x5a5a5a5a }
    __asm { setne ah }
    __asm { movzx eax,ax }
    __asm { pop esi }
    __asm { ret }
}
__declspec(naked) float finish(void*, float) {
    __asm { push ebp }
    __asm { mov ebp,[esp+8] }
    __asm { fld dword ptr [esp+12] }
    __asm { call dword ptr [entry] }
    __asm { pop ebp }
    __asm { ret }
}
}

int main() {
    auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, block_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    const auto l = layout_at(static_cast<address>(reinterpret_cast<std::uintptr_t>(memory)));
    auto bytes = block(l.base);
    std::array<std::uint8_t, ai_stride * ai_slots + 0x80> brains{};
    std::array<std::uint8_t, 0x600> ai{};
    std::array<std::uint8_t, 0x800> actor{};
    const auto base = static_cast<address>(reinterpret_cast<std::uintptr_t>(brains.data()));
    auto edit = [&](address a, address v) { put32(bytes, a - l.base, v); };
    edit(l.start + 3, base); edit(l.cont + 3, base);
    const auto leave = [&](address operand, address target) { edit(operand, target - (operand + 4)); };
    leave(l.start + 0x43, l.base + 720);
    leave(l.cont + 0x65, l.base + 736); // press
    leave(l.cont + 0x6a, l.base + 728); // wait
    leave(l.cont + 0x6f, l.base + 744); // complete
    leave(l.cont + 0x7a, l.base + 752); // original fallback
    // Revision 5: the reaction front of the continue step, the unmarking front of the start, the hit's mark.
    std::array<std::uint8_t, ai_stride * ai_slots> ais{};
    edit(l.react_cont + 3, base); edit(l.pre_start + 3, base);
    leave(l.react_cont + 0x52, l.base + 744); // complete
    edit(l.mark + 0x0f, static_cast<address>(reinterpret_cast<std::uintptr_t>(ais.data())));
    leave(l.mark + 0x51, l.base + 768); // the pain decision's jz: a plain return here
    bytes[768] = 0xc3;
    // The power front: the clock owner is a stub object whose vt+0x160 returns `clock_now`; the resume undoes the
    // displaced prologue (pop ebp; pop ebx; add esp,0x14; ret).
    edit(l.power_front + 4, base);
    leave(l.power_front + 0x3f, static_cast<address>(reinterpret_cast<std::uintptr_t>(&clock_owner)));
    leave(l.power_front + 0xa2, l.base + 772);
    const std::uint8_t undo[] = {0x5d, 0x5b, 0x83, 0xc4, 0x14, 0xc3};
    std::memcpy(bytes.data() + 772, undo, sizeof(undo));
    // The attack front: the same clock stub, a random stub, and a resume that undoes sub esp,0x58; push ebx; push ebp.
    edit(l.attack_front + 4, base);
    leave(l.attack_front + 0x4c, static_cast<address>(reinterpret_cast<std::uintptr_t>(&clock_owner)));
    leave(l.attack_front + 0x7e, static_cast<address>(reinterpret_cast<std::uintptr_t>(&random_read)));
    leave(l.attack_front + 0xb2, l.base + 780);
    const std::uint8_t undo58[] = {0x5d, 0x5b, 0x83, 0xc4, 0x58, 0xc3};
    std::memcpy(bytes.data() + 780, undo58, sizeof(undo58));
    // The unknown-combo fallback's constant lives in our allocation too.
    edit(l.end_delay + 0x1b, l.base + 760);
    const float stock_delay = 0.2f;
    std::memcpy(bytes.data() + 760, &stock_delay, 4);
    for (int value = 0; value < 5; ++value) {
        const std::size_t at = 720 + 8 * value;
        bytes[at] = 0xb8; put32(bytes, at + 1, value); bytes[at + 5] = 0xc3;
    }
    // Revision 10 items 17-18: the lock bridge's exits are the value stubs (0 = the native exit 0x518ac9, 1 = on at
    // 0x5187ca); the gate's energy read and picker continuation are the stubs above.
    leave(l.power_lock + 0x0d, l.base + 720);
    leave(l.power_lock + 0x12, l.base + 728);
    leave(l.gate + 14, static_cast<address>(reinterpret_cast<std::uintptr_t>(&energy_read)));
    leave(l.gate + 50, static_cast<address>(reinterpret_cast<std::uintptr_t>(&picker_rest)));
    check(power_lock_bridge(l).size() == power_lock_size && power_lock_jump(l).size() == 6 && gate_bridge(l).size() == gate_size
          && l.attack_front + attack_front_size <= l.base + 2048 && l.base + 2048 + 35 <= l.power_lock && l.power_lock + power_lock_size <= l.base + code_size
          && l.gate + gate_size <= l.constants, "revision 10 lock bridges: lengths and spans");
    check(cont_bridge(l).size() == cont_size && start_bridge(l).size() == start_size && end_delay_bridge(l).size() == end_delay_size, "emitted bridge lengths");
    check(react_cont_bridge(l).size() == react_cont_size && pre_start_bridge(l).size() == pre_start_size && mark_bridge(l).size() == mark_size && mark_jump(l).size() == reaction_site.patched
          && power_front_bridge(l).size() == power_front_size && power_entry_jump(l).size() == power_entry_site.patched
          && attack_front_bridge(l).size() == attack_front_size && attack_entry_jump(l).size() == attack_entry_site.patched, "revision 5 bridge lengths");
    check(l.base + 786 <= l.react_cont && l.react_cont + react_cont_size <= l.pre_start && l.pre_start + pre_start_size <= l.mark && l.mark + mark_size <= l.power_front
          && l.power_front + power_front_size <= l.attack_front && l.attack_front + attack_front_size <= l.base + code_size
          && l.react == l.next + 4 * ai_slots && l.react_power == l.react + 4 * ai_slots && l.react_attack == l.react_power + 4 * ai_slots
          && l.react_attack + 4 * ai_slots <= l.next + (block_size - code_size), "revision 5 spans do not overlap");
    check(l.cont + cont_size <= l.start && l.start + start_size <= l.delays && l.delays + 120 <= l.end_delay && l.end_delay + end_delay_size <= l.base + code_size, "bridge and data spans do not overlap");
    // Revision 10, XML1's block rule: the roll bridge's three exits go to stubs after it (mov ecx,route; ret).
    check(hit_roll_bridge(l).size() == hit_roll_size && hit_roll_jump(l).size() == hit_roll_site.patched && block_chance_skip().size() == block_chance_site.patched
          && l.attack_front + attack_front_size <= l.hit_roll && l.hit_roll + hit_roll_size + 24 <= l.base + code_size, "block rule bridge length and span");
    {
        const auto rel = [](const std::vector<std::uint8_t>& c, std::size_t at, address from) {
            return from + static_cast<address>(at + 4) + (c[at] | c[at + 1] << 8 | c[at + 2] << 16 | static_cast<address>(c[at + 3]) << 24);
        };
        const auto bridge = hit_roll_bridge(l);
        check(rel(bridge, 0x06, l.hit_roll) == hit_roll_combo_va && rel(bridge, 0x15, l.hit_roll) == hit_roll_rolled_va && rel(bridge, 0x1f, l.hit_roll) == hit_roll_result_va,
              "the roll bridge's exits: combo text 0x44f594, the draw 0x44f5b4, the result 0x44f6bd");
        check(rel(hit_roll_jump(l), 1, hit_roll_site.va) == l.hit_roll && hit_roll_jump(l)[5] == 0x90, "0x44f58e jumps to the roll bridge");
        const auto skip = block_chance_skip();
        check(skip[0] == 0xe9 && rel(skip, 1, block_chance_site.va) == block_chance_skip_va && skip[5] == 0x90
              && retail_bytes(block_chance_site, 6) == std::vector<std::uint8_t>({0x0f, 0x8a, 0x86, 0x00, 0x00, 0x00})
              && block_chance_site.va + 6 + 0x86 == block_chance_skip_va, "0x437b08: the chance test's jp to 0x437b94 becomes a jmp there");
        check(sub_bytes(hit_roll_site, 0, 6) == std::vector<std::uint8_t>({0xf6, 0x46, 0x35, 0x04, 0x74, 0x20}) && hit_roll_site.va + 6 + 0x20 == hit_roll_rolled_va
              && hit_roll_site.va + 6 == hit_roll_combo_va && sub_bytes(hit_roll_site, 0x1c, 10) == std::vector<std::uint8_t>({0xbf, 0x01, 0x00, 0x00, 0x00, 0xe9, 0x09, 0x01, 0x00, 0x00})
              && hit_roll_site.va + 0x26 + 0x109 == hit_roll_result_va, "the displaced test and the game's own always-hit exit");
        check(sub_bytes(miss_counts_site, 0, 7) == std::vector<std::uint8_t>({0x8a, 0x46, 0x35, 0x24, 0x78, 0x3c, 0x08})
              && sub_bytes(blocked_flag_site, 0, 5) == std::vector<std::uint8_t>({0x83, 0x7c, 0x24, 0x20, 0x02}), "the contact's damagescale and blocked-flag encodings the rule relies on");
    }
    // The stubs sit past the item-17 lock bridge (block+2112, 22 bytes), which l.hit_roll + 64 would overwrite.
    const address roll_stub = l.hit_roll + 128;
    check(l.power_lock + power_lock_size <= roll_stub && roll_stub + 24 <= l.base + code_size, "block rule test stubs clear of the lock bridge");
    for (int route = 0; route < 3; ++route) {
        const std::size_t at = roll_stub - l.base + 8 * route;
        bytes[at] = 0xb9; put32(bytes, at + 1, route + 1); bytes[at + 5] = 0xc3;
    }
    leave(l.hit_roll + 0x06, roll_stub);      // 1 = the combo-text path
    leave(l.hit_roll + 0x15, roll_stub + 8);  // 2 = the draw
    leave(l.hit_roll + 0x1f, roll_stub + 16); // 3 = the result (a hit without a draw)
    std::memcpy(memory, bytes.data(), bytes.size());
    DWORD old = 0;
    if (!VirtualProtect(memory, code_size, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), memory, code_size)) return 2;
    // Synth, Echo and the last slot share no deadlines; every AI slot uses its own cell.
    for (unsigned slot = 0; slot < ai_slots; ++slot) for (unsigned combo = 0; combo < 6; ++combo) {
        auto* brain = brains.data() + slot * ai_stride;
        field<void*>(brain, 4) = ai.data();
        field<unsigned>(brain, 0x3c) = combo;
        field<unsigned>(brain, 0x30) = 0;
        field<unsigned>(brain, 0x34) = xml1_combo_lengths[combo];
        field<float>(ai.data(), ai_now) = 40.0f + slot;
        entry = reinterpret_cast<void*>(l.start);
        check(step(brain, actor.data()) == 0, "start reaches tail");
        auto& next = field<float>(reinterpret_cast<void*>(l.next), slot * 4);
        check(next == 40.0f + slot + xml1_combo_delays[combo][0], "start arms slot deadline");
        entry = reinterpret_cast<void*>(l.cont);
        field<float>(ai.data(), ai_now) = next;
        check(step(brain, actor.data()) == 1 && field<unsigned>(brain, 0x30) == 0, "equal timestamp waits");
        field<float>(ai.data(), ai_now) = std::numeric_limits<float>::quiet_NaN();
        check(step(brain, actor.data()) == 1 && field<unsigned>(brain, 0x30) == 0, "unordered timestamp waits");
        for (unsigned press = 1; press <= xml1_combo_lengths[combo]; ++press) {
            actor[0x764] = 1; // XML1 advances the combo timer even with the actor's hold bit set.
            field<float>(ai.data(), ai_now) = next + 0.03125f;
            const bool last = press == xml1_combo_lengths[combo];
            check(step(brain, actor.data()) == (last ? 3 : 2) && field<unsigned>(brain, 0x30) == press, "due timer advances once and selects press or completion");
            if (!last) check(next == field<float>(ai.data(), ai_now) + xml1_combo_delays[combo][press - 1], "subsequent deadline uses previous button delay");
        }
        entry = reinterpret_cast<void*>(l.end_delay);
        check(finish(brain, 17.0f) == 17.0f + xml1_combo_delays[combo][xml1_combo_lengths[combo] - 1], "completion adds final button delay to existing x87 accumulator");
    }
    entry = reinterpret_cast<void*>(l.cont);
    // V-NPC2: clock rewind/slot reuse must not wait for an old absolute deadline.
    auto* brain = brains.data();
    field<void*>(brain, 4) = ai.data();
    field<unsigned>(brain, 0x3c) = 2;
    field<unsigned>(brain, 0x34) = 3;
    auto& next = field<float>(reinterpret_cast<void*>(l.next), 0);
    auto rewind = [&] {
        field<unsigned>(brain, 0x30) = 0;
        field<float>(ai.data(), ai_now) = 0;
        next = 40;
    };
    // Restore the previous wait branch in this owned allocation as the negative control.
    auto wait_branch = [&](std::uint8_t displacement) {
        DWORD protection = 0;
        if (!VirtualProtect(memory, code_size, PAGE_EXECUTE_READWRITE, &protection)) ExitProcess(2);
        memory[l.cont - l.base + 0x33] = displacement;
        if (!VirtualProtect(memory, code_size, protection, &protection) || !FlushInstructionCache(GetCurrentProcess(), memory, code_size)) ExitProcess(2);
    };
    wait_branch(0x35);rewind();
    check(step(brain, actor.data()) == 1 && field<unsigned>(brain, 0x30) == 0, "negative control stalls on pre-load deadline");
    wait_branch(0x4a);rewind();
    check(step(brain, actor.data()) == 2 && field<unsigned>(brain, 0x30) == 1 && next == .2f, "rewound clock expires and rearms from current time");
    for (float remaining : {0.f, .2f, .3f, max_combo_delay, max_combo_delay + .0001f, 40.f, std::numeric_limits<float>::infinity()}) {
        field<unsigned>(brain, 0x30) = 0;field<float>(ai.data(), ai_now) = 0;next = remaining;
        const bool expired = remaining > max_combo_delay;
        check(step(brain, actor.data()) == (expired ? 2 : 1) && field<unsigned>(brain, 0x30) == (expired ? 1u : 0u), "only remaining times above maximum expire");
    }
    field<unsigned>(brain, 0x30) = 2;field<float>(ai.data(), ai_now) = 1;next = 100;
    check(step(brain, actor.data()) == 3 && field<unsigned>(brain, 0x30) == 3, "stale final deadline reaches native completion");
    field<unsigned>(brain, 0x30) = 0;field<float>(ai.data(), ai_now) = 50;next = 0;
    check(step(brain, actor.data()) == 2 && next == 50.2f, "unarmed slot and forward clock jump advance normally");
    // Repeated clamped, due and waiting paths must keep the x87 stack balanced.
    unsigned short before = 0, after = 0;
    __asm { fnclex }
    __asm { fnstsw before }
    for (unsigned n = 0; n < 1000; ++n) {
        field<unsigned>(brain, 0x30) = 0;field<float>(ai.data(), ai_now) = 1;next = n % 3 == 0 ? 100.f : (n % 3 == 1 ? 1.f : 0.f);
        step(brain, actor.data());
    }
    __asm { fnstsw after }
    check((before & 0x3800) == (after & 0x3800) && !(after & 0x41), "deadline clamp leaves x87 depth balanced without invalid operations");
    field<unsigned>(brains.data() + 1, 0x30) = 0;
    field<unsigned>(brains.data() + 1, 0x38) = 0;
    check(step(brains.data() + 1, actor.data()) == 4, "unaligned brain uses native fallback");
    check(step(brains.data() + ai_stride * ai_slots, actor.data()) == 4, "brain beyond last slot uses native fallback");
    field<unsigned>(brains.data(), 0x3c) = 6;
    entry = reinterpret_cast<void*>(l.end_delay);
    check(finish(brains.data(), 17.0f) == 17.2f, "unmapped combo keeps native completion addition");
    // V-NPC3 (revision 5): a hit reaction spends the combo steps that come due before the brain runs again.
    {
        auto* react = reinterpret_cast<std::uint32_t*>(l.react);
        auto* nexts = reinterpret_cast<float*>(l.next);
        const unsigned s = 5;
        auto* b = brains.data() + s * ai_stride;
        field<void*>(b, 4) = ai.data();
        // The hit's mark: only a reacting hit marks, and only the victim's slots.
        std::array<std::uint8_t, 0xc0> hit{};
        field<void*>(hit.data(), 0) = actor.data();
        field<void*>(ais.data() + s * ai_stride, 0x10) = actor.data();
        for (unsigned i = 0; i < ai_slots; ++i) react[i] = 0;
        entry = reinterpret_cast<void*>(l.mark);
        hit[0xac] = 0x03;
        mark_call(hit.data());
        check(react[s] == 0, "a hit without a reaction leaves the slot unmarked");
        hit[0xac] = 0x0b;
        mark_call(hit.data());
        bool others = false;
        for (unsigned i = 0; i < ai_slots; ++i) if (i != s && react[i]) others = true;
        auto* react_power = reinterpret_cast<std::uint32_t*>(l.react_power);
        auto* react_attack = reinterpret_cast<std::uint32_t*>(l.react_attack);
        check(react[s] == 1 && react_power[s] == 1 && react_attack[s] == 1 && !others, "a reacting hit marks the victim's slot only (combo, power, attack)");
        // A combo start clears a stale mark.
        entry = reinterpret_cast<void*>(l.pre_start);
        field<unsigned>(b, 0x3c) = 5; field<unsigned>(b, 0x34) = 5; field<unsigned>(b, 0x30) = 0;
        field<float>(ai.data(), ai_now) = 9.7f;
        check(step(b, actor.data()) == 0 && react[s] == 0 && nexts[s] == 9.7f + 0.3f, "combo start unmarks and arms");
        // Marked: the steps due during the reaction are spent, the continue step then waits for the next one.
        entry = reinterpret_cast<void*>(l.react_cont);
        react[s] = 1; nexts[s] = 10.0f; field<unsigned>(b, 0x30) = 0; field<float>(ai.data(), ai_now) = 10.65f;
        const int r1 = step(b, actor.data());
        check(r1 == 1 && field<unsigned>(b, 0x30) == 3 && react[s] == 0 && std::fabs(nexts[s] - 10.9f) < 1e-5f, "spent steps advance the index and the clock, then wait");
        check(step(b, actor.data()) == 1 && field<unsigned>(b, 0x30) == 3, "unmarked call afterwards still waits");
        field<float>(ai.data(), ai_now) = 10.95f;
        check(step(b, actor.data()) == 2 && field<unsigned>(b, 0x30) == 4, "the first step after the reaction presses on time");
        // Marked with every step overdue: the combo completes natively.
        field<unsigned>(b, 0x3c) = 1; field<unsigned>(b, 0x34) = 2; field<unsigned>(b, 0x30) = 0;
        react[s] = 1; nexts[s] = 10.0f; field<float>(ai.data(), ai_now) = 11.0f;
        check(step(b, actor.data()) == 3 && field<unsigned>(b, 0x30) == 2 && react[s] == 0, "a reaction past the last step completes the combo");
        // Unmarked: identical to the revision 4 continue step.
        field<unsigned>(b, 0x30) = 0; nexts[s] = 10.0f; field<float>(ai.data(), ai_now) = 10.05f;
        check(step(b, actor.data()) == 2 && field<unsigned>(b, 0x30) == 1, "no mark: the revision 4 step");
        // Marked with an unordered clock: no spending loop, the continue step's own wait.
        field<unsigned>(b, 0x30) = 0; react[s] = 1; nexts[s] = 10.0f; field<float>(ai.data(), ai_now) = std::numeric_limits<float>::quiet_NaN();
        check(step(b, actor.data()) == 1 && field<unsigned>(b, 0x30) == 0, "unordered clock spends nothing");
        // Brains outside the table take the continue step's fallback.
        check(step(brains.data() + 1, actor.data()) == 4, "unaligned brain: native fallback through the front");
        // The power front: a re-check that fell due during the reaction is spent and re-armed (2.0 s for an NPC,
        // 0.5 s with a controller); unmarked or future timers are untouched; the x87 stack stays balanced.
        clock_vtable[0x160 / 4] = reinterpret_cast<void*>(&clock_read);
        field<void*>(ai.data(), 0x10) = actor.data();
        auto& timer = field<float>(ai.data(), 0x1cc);
        unsigned short fb = 0, fa = 0;
        __asm { fnclex }
        __asm { fnstsw fb }
        entry = reinterpret_cast<void*>(l.power_front);
        field<std::int32_t>(actor.data(), 0x3d4) = -1;
        react_power[s] = 1; timer = 20.0f; clock_now = 20.3f;
        power_call(b);
        check(react_power[s] == 0 && timer == 22.0f, "an overdue NPC re-check is spent and re-armed by 2.0 s");
        react_power[s] = 1; timer = 20.0f; clock_now = 24.5f;
        power_call(b);
        check(timer == 26.0f, "every re-check that fell due is spent");
        field<std::int32_t>(actor.data(), 0x3d4) = 0;
        react_power[s] = 1; timer = 20.0f; clock_now = 20.3f;
        power_call(b);
        check(timer == 20.5f, "a character with a controller re-arms by 0.5 s");
        react_power[s] = 1; timer = 30.0f; clock_now = 20.3f;
        power_call(b);
        check(react_power[s] == 0 && timer == 30.0f, "a future re-check is left alone");
        react_power[s] = 0; timer = 20.0f; clock_now = 20.3f;
        power_call(b);
        check(timer == 20.0f, "no mark: the decision as before");
        react_power[s] = 1; timer = std::numeric_limits<float>::quiet_NaN(); clock_now = 20.3f;
        power_call(b);
        check(std::isnan(timer), "an unordered timer is left alone");
        field<std::int32_t>(actor.data(), 0x3d4) = -1;
        react_power[s] = 1; timer = -1000.0f; clock_now = 20.3f;
        power_call(b);
        check(timer == -984.0f, "at most eight re-checks are spent per call");
        power_call(brains.data() + 1);
        // The attack front: with no combo running, a decision that fell due during the reaction is spent and the
        // next attack time re-armed the tail's way (offset + random x random-add) from it; a running combo leaves
        // the mark for later; unmarked, future and unordered times are untouched; at most four are spent.
        entry = reinterpret_cast<void*>(l.attack_front);
        auto& next_attack = field<float>(ai.data(), 0x1c);
        field<float>(ai.data(), 0x288) = 0.6f; field<float>(ai.data(), 0x28c) = 1.2f; random_now = 0.5f;
        b[0x2c] = 0; react_attack[s] = 1; next_attack = 20.0f; clock_now = 20.3f;
        power_call(b);
        check(react_attack[s] == 0 && std::fabs(next_attack - 21.2f) < 1e-5f, "a decision due during the reaction is spent and re-armed");
        b[0x2c] = 1; react_attack[s] = 1; next_attack = 20.0f;
        power_call(b);
        check(react_attack[s] == 1 && next_attack == 20.0f, "a running combo keeps the mark for later");
        b[0x2c] = 0; react_attack[s] = 1; next_attack = 30.0f;
        power_call(b);
        check(react_attack[s] == 0 && next_attack == 30.0f, "a future attack time is left alone");
        react_attack[s] = 0; next_attack = 20.0f;
        power_call(b);
        check(next_attack == 20.0f, "no mark: the decision as before");
        react_attack[s] = 1; next_attack = -100.0f;
        power_call(b);
        check(std::fabs(next_attack - (-95.2f)) < 1e-4f, "at most four decisions are spent per call");
        react_attack[s] = 1; next_attack = std::numeric_limits<float>::quiet_NaN();
        power_call(b);
        check(std::isnan(next_attack), "an unordered attack time is left alone");
        power_call(brains.data() + 1);
        __asm { fnstsw fa }
        check((fb & 0x3800) == (fa & 0x3800) && !(fa & 0x40), "the power front leaves the x87 stack balanced (no stack fault; the NaN case may set IE as the native fcomp does)");
    }
    {
        // Revision 10, XML1's block rule. The plain rules: XMen2.exe rolls everything but combo text; XML1 never rolls a
        // PowerAttack (damagescale other than normal here); both flag the hit blocked exactly on result 2.
        for (int b = 0; b < 256; ++b) {
            const auto v = static_cast<std::uint8_t>(b);
            const bool text = (v & 4) != 0, normal = (v & 0x78) == 8;
            check(roll_route_xml2(v) == (text ? roll_route::combo_text : roll_route::rolled), "XMen2.exe: combo text or the draw");
            check(roll_route_xml1(v) == (text ? roll_route::combo_text : normal ? roll_route::rolled : roll_route::hit), "XML1: a power attack is a hit without a draw");
            entry = reinterpret_cast<void*>(l.hit_roll);
            std::uint8_t attack[0x40] = {};
            attack[0x35] = v;
            const int route = roll_call(attack, 0x12345678);
            const int want = text ? 1 : normal ? 2 : 3;
            check(route == want && roll_eax == 0x12345678 && roll_edi == (want == 3 ? 1 : 0x55) && attack[0x35] == v,
                  "the bridge takes XML1's route, keeps eax, sets edi = 1 only for the hit, writes nothing");
        }
        check(flagged_blocked(2) && !flagged_blocked(1) && !flagged_blocked(3), "blocked only on a miss");
        // The receive's block_chance roll: XMen2.exe zeroes a positive hit when the draw is at most the chance (so with
        // no affecter, chance 0, only a draw of exactly 0); an unordered compare skips. XML1: never.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        check(block_chance_zeroes_xml2(5.0f, 0.0f, 0.0f) && !block_chance_zeroes_xml2(5.0f, 0.001f, 0.0f) && block_chance_zeroes_xml2(5.0f, 0.2f, 0.25f)
              && !block_chance_zeroes_xml2(0.0f, 0.0f, 0.5f) && !block_chance_zeroes_xml2(-1.0f, 0.0f, 0.5f) && !block_chance_zeroes_xml2(nan, 0.0f, 0.5f)
              && !block_chance_zeroes_xml2(5.0f, nan, 0.5f) && !block_chance_zeroes_xml2(5.0f, 0.1f, nan), "XMen2.exe's block_chance test");
        check(!block_chance_zeroes_xml1(5.0f, 0.0f, 0.0f) && !block_chance_zeroes_xml1(5.0f, 0.2f, 1.0f), "XML1 has no block_chance roll");
    }
    // Revision 12 items 19-20: the lock tests' jne (0x50fe63 in the picker, 0x5187c4 in the power decision) become a
    // short jmp to the instruction after them (eb 04 + four nops); revision 11's caller-scoped bridge is no longer used.
    {
        const auto picker = picker_lock_bytes(), power = power_lock_bytes();
        check(picker == std::vector<std::uint8_t>({0xeb, 0x04, 0x90, 0x90, 0x90, 0x90}) && power == picker
              && sub_bytes(picker_entry_site, 19, 6) == std::vector<std::uint8_t>({0x0f, 0x85, 0x82, 0x01, 0x00, 0x00})
              && picker_lock_jne_va == picker_entry_site.va + 19 && picker_lock_jne_va + 2 + 4 == picker_unlocked_va
              && sub_bytes(power_lock_site, 11, 6) == std::vector<std::uint8_t>({0x0f, 0x85, 0xff, 0x02, 0x00, 0x00})
              && power_lock_va + 2 + 4 == power_lock_resume_va,
              "items 19-20: both lock tests' jne become jmp to the next instruction (0x50fe69, 0x5187ca)");
    }
    // Item 18: the gate enters the picker after its lock test with the picker's own first instructions done.
    {
        std::array<std::uint8_t, 0x600> gai{};
        std::array<std::uint8_t, 0x800> gactor{};
        field<void*>(gai.data(), 0x10) = gactor.data();
        field<float>(gactor.data(), 0x314) = 147.0f;
        field<std::uint8_t>(gactor.data(), 0x764) = 1; // locked: the native picker would return 0 here
        entry = reinterpret_cast<void*>(l.gate);
        const auto gai_va = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(gai.data()));
        const auto gactor_va = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(gactor.data()));
        std::uint16_t fb = 0, fa = 0;
        __asm { fnstsw fb }
        energy_now = 100.0f; seen_esi = seen_eax = seen_ecx = seen_esp = 0;
        int r = gate_call(gai.data());
        check(r == 1 && seen_esi == gai_va && seen_ecx == gai_va && seen_eax == gactor_va && call_esp - seen_esp == 4 + 0xac + 4 && seen_pushed == 0x5a5a5a5a,
              "item 18: enough energy enters the picker past its lock test (esp - 0xb4, esi = ecx = the AI, eax = the actor, the caller's esi pushed)");
        energy_now = 70.0f; seen_esi = 0;
        r = gate_call(gai.data());
        check(r == 0 && seen_esi == 0, "item 18: under half the energy still refuses without the picker");
        field<std::uint8_t>(gai.data(), 0x2b6) = 8; seen_esi = 0;
        r = gate_call(gai.data());
        check(r == 1 && seen_esi == gai_va, "item 18: the forcing flag still enters the picker");
        __asm { fnstsw fa }
        check((fb & 0x3800) == (fa & 0x3800), "item 18: the gate leaves the x87 stack balanced");
        const auto gate = gate_bridge(l);
        check(std::vector<std::uint8_t>(gate.begin() + 37, gate.begin() + 49) == sub_bytes(picker_entry_site, 0, 12) && picker_unlocked_va == picker_entry_site.va + 25,
              "item 18: the gate's copy is the picker's first 12 bytes, and it resumes after the lock test's jne");
    }
    // XMen2.exe's own code (the guarded retail bytes, which xml2_test checks against the executable), run natively:
    // item 16's run 0x512f01..0x512f1a and item 17's run 0x5187b9..0x5187ce, retail and with the rule's writes.
    {
        auto* run = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        if (!run) return 2;
        check(sub_bytes(lock_exit_site, 10, 2) == std::vector<std::uint8_t>({0x74, 0x0d}) && lock_exit_va == lock_exit_site.va + 10
              && lock_exit_bytes() == std::vector<std::uint8_t>({0xeb, 0x0d}) && lock_exit_va + 2 + 0x0d == 0x512f1a,
              "item 16: the exit's jz becomes a jmp to the same continuation (0x512f1a)");
        std::array<std::uint8_t, 0x800> who{};
        std::array<std::uint8_t, 0x40> owner{};
        field<void*>(owner.data(), 0x10) = who.data();
        for (const bool rule : {false, true}) {
            const auto native = retail_bytes(lock_exit_site, limits_rules::hex_size(lock_exit_site.hex)); // 25 bytes
            std::memcpy(run, native.data(), native.size());
            const std::uint8_t tail[] = {0xb8, 1, 0, 0, 0, 0xc3, 0x31, 0xc0, 0xc3}; // +25 (0x512f1a): eax = 1; +31 (the exit): eax = 0
            std::memcpy(run + 25, tail, sizeof(tail));
            const std::int32_t to_exit = 31 - 25; // the jnz rel32 at +21 (0x512f16) relocated to +31
            std::memcpy(run + 21, &to_exit, 4);
            if (rule) { const auto w = lock_exit_bytes(); std::memcpy(run + 10, w.data(), w.size()); }
            FlushInstructionCache(GetCurrentProcess(), run, 64);
            entry = run;
            bool ok = true;
            for (const int lock : {0, 1}) for (const int flag : {0, 0x10, 0xef, 0xff}) {
                field<std::uint8_t>(who.data(), 0x764) = static_cast<std::uint8_t>(lock);
                field<std::uint8_t>(who.data(), 0x3d9) = static_cast<std::uint8_t>(flag);
                const int expected = (!rule && lock && (flag & 0x10)) ? 0 : 1;
                ok &= exit_run(owner.data()) == expected;
            }
            check(ok, rule ? "item 16: with the rule the attack-state update never takes the input-lock exit"
                           : "item 16: retail exits exactly for a locked move with +0x3d9 bit 0x10 (the enemy after a power)");
        }
        // Item 17: the retail run with the jne at +11 (0x5187c4) relocated to a stub, or replaced by the rule's jump to a
        // copy of the bridge whose exits are relocated into this buffer.
        const auto lb = layout_at(static_cast<address>(reinterpret_cast<std::uintptr_t>(run)));
        const auto at = [&](std::uint32_t offset) { return static_cast<address>(reinterpret_cast<std::uintptr_t>(run)) + offset; };
        for (const bool rule : {false, true}) {
            std::memset(run, 0xcc, 4096);
            const auto native = retail_bytes(power_lock_site, limits_rules::hex_size(power_lock_site.hex)); // 21 bytes
            std::memcpy(run, native.data(), native.size());
            const std::uint8_t tail[] = {0xb8, 1, 0, 0, 0, 0xc3, 0x31, 0xc0, 0xc3}; // +21 after mov ebx: eax = 1; +27 (0x518ac9): eax = 0
            std::memcpy(run + 21, tail, sizeof(tail));
            if (!rule) {
                const std::int32_t to_exit = 27 - 17; // the jne rel32 at +13, next instruction +17
                std::memcpy(run + 13, &to_exit, 4);
            } else {
                const auto w = power_lock_bytes();
                std::memcpy(run + 11, w.data(), w.size());
            }
            FlushInstructionCache(GetCurrentProcess(), run, 4096);
            entry = run;
            bool ok = true;
            for (const int lock : {0, 1}) for (const unsigned caller : {melee_power_call_return_va, 0x519fe7u, 0u}) {
                field<std::uint8_t>(who.data(), 0x764) = static_cast<std::uint8_t>(lock);
                const int expected = (lock && !rule) ? 0 : 3; // 3: on, ebx = [esp+0x1c]
                ok &= lock_run(who.data(), caller) == expected;
            }
            check(ok, rule ? "item 20: with the rule a locked body reaches the re-check for every caller (melee brain, officer weapon brain, any)"
                           : "item 17: retail refuses every locked call before the re-check");
        }
        (void)lb; (void)at;
        // Item 19: the picker's entry run 0x50fe50..0x50fe69 (sub esp,0xac; push esi; mov esi,ecx; mov eax,[esi+0x10];
        // test byte [eax+0x764],1; jne 0x50ffeb), retail and with the rule's bytes: +25 (0x50fe69) and the relocated
        // exit undo the frame and return 1 (on into the slot walk) or 0 (the native refusal 0x50ffeb).
        for (const bool rule : {false, true}) {
            std::memset(run, 0xcc, 4096);
            const auto native = retail_bytes(picker_entry_site, 25);
            std::memcpy(run, native.data(), native.size());
            const std::uint8_t on[] = {0xb8, 1, 0, 0, 0, 0x5e, 0x81, 0xc4, 0xac, 0, 0, 0, 0xc3}; // +25: eax 1; pop esi; add esp,0xac; ret
            const std::uint8_t off[] = {0x31, 0xc0, 0x5e, 0x81, 0xc4, 0xac, 0, 0, 0, 0xc3};    // +40: eax 0; pop esi; add esp,0xac; ret
            std::memcpy(run + 25, on, sizeof(on));
            std::memcpy(run + 40, off, sizeof(off));
            const std::int32_t to_exit = 40 - 25; // the jne rel32 at +21, next instruction +25
            std::memcpy(run + 21, &to_exit, 4);
            if (rule) { const auto w = picker_lock_bytes(); std::memcpy(run + 19, w.data(), w.size()); }
            FlushInstructionCache(GetCurrentProcess(), run, 4096);
            entry = run;
            std::array<std::uint8_t, 0x40> pai{};
            field<void*>(pai.data(), 0x10) = who.data();
            bool ok = true;
            for (const int lock : {0, 1, 2, 3}) {
                field<std::uint8_t>(who.data(), 0x764) = static_cast<std::uint8_t>(lock);
                const int expected = (!rule && (lock & 1)) ? 0 : 1;
                ok &= picker_run(pai.data()) == expected;
            }
            check(ok, rule ? "item 19: with the rule the picker never refuses a locked body at its entry (every caller; the slot chain test stays)"
                           : "item 19: the retail picker refuses a locked body (bit 0 of +0x764) at its entry");
        }
        // Item 21: the class name walk (0x511fce..0x512011: strstr of 26 thirty-byte fragments on the stats name, the
        // first match wins), on a table whose fragment 17 is the guarded retail one or the rule's, the other 25 the
        // retail fragments; and the category stores 0x511fb4..0x511fc4 (ebx 1, ebp 0, edi 4, ecx 6) run natively,
        // retail and with the rule's store.
        {
            std::array<char, 30 * 26> table{};
            const char* fragments[26] = {"rchangel", "east", "ishop", "olossus", "yclops", "rost", "ambit", "avok", "ceman", "rrow", "agma", "ightcrawler",
                                         "hoenix", "sylocke", "ogue", "torm", "olverine", nullptr, "entinel", "ingless", "hades", "alanche", "uggernau",
                                         "lob", "yro", "adowking"};
            for (int rule = 0; rule < 2; ++rule) {
                table.fill(0);
                for (int i = 0; i < 26; ++i) if (fragments[i]) std::memcpy(table.data() + 30 * i, fragments[i], std::strlen(fragments[i]));
                const auto seventeen = rule ? class_name_bytes() : retail_bytes(class_name_site, 11);
                std::memcpy(table.data() + 30 * 17, seventeen.data(), seventeen.size());
                const auto match = [&](const char* name) {
                    for (int i = 0; i < 26; ++i) if (std::strstr(name, table.data() + 30 * i)) return i;
                    return 26;
                };
                const bool walk = rule ? match("SentinelAdvanced") == 17 && match("SentinelAdvanced_b") == 17 && match("Sentinel") == 18
                                       : match("SentinelAdvanced") == 18 && match("SentinelAdvanced_b") == 18 && match("Sentinel") == 18;
                check(walk && match("JuggernautAct3") == 22 && match("JuggernautFlashback") == 22 && match("JuggernautScripted") == 22,
                      rule ? "item 21: with the fragment Advanced Sentinel matches 17 (category 0), as XML1's \"dvanced\""
                           : "item 21: retail \"xxxdvanced\" never matches: Advanced Sentinel falls to Sentinel (18, category 4)");
                std::memset(run, 0xcc, 4096);
                const auto stores = retail_bytes(class_category_site, 16);
                std::memcpy(run, stores.data(), stores.size());
                if (rule) { const auto w = class_juggernaut_bytes(); std::memcpy(run + 4, w.data(), w.size()); }
                run[16] = 0xc3;
                FlushInstructionCache(GetCurrentProcess(), run, 64);
                entry = run;
                std::uint32_t out[4] = {};
                category_run(out);
                check(out[0] == 1 && out[1] == (rule ? 4u : 0u) && out[2] == 4 && out[3] == 6,
                      rule ? "item 21: with the rule Juggernaut (22) is category 4, as XML1 0x109e08; 21, 23, 24 unchanged"
                           : "item 21: retail Juggernaut (22) is category 0 (ebp)");
            }
            check(retail_bytes(class_loop_site, 7) == std::vector<std::uint8_t>({0x33, 0xdb, 0xbd, 0x20, 0x4d, 0x69, 0x00})
                  && retail_bytes(class_edi_site, 5) == std::vector<std::uint8_t>({0xbf, 4, 0, 0, 0}) && class_name_va == 0x694d20 + 30 * 17
                  && class_juggernaut_va == class_category_site.va + 4 && class_name_bytes().size() == limits_rules::hex_size(class_name_site.hex),
                  "item 21: the walk starts at 0x694d20 (fragment 17 at +510), edi is the Sentinel category 4, the writes fit their guards");
        }
        // Item 22: the Danger Room gate 0x435b09..0x435b52, natively: the mode getter (0x4c87c0) and the clock owner
        // (0x46dce0, vt+0x160) are stubs, the constants 0.0 / 2.0 the test's; esi = the hit record, [esi] = the victim.
        {
            static float zero = 0.0f, two = 2.0f;
            clock_vtable[0x160 / 4] = reinterpret_cast<void*>(&clock_read);
            for (const bool rule : {false, true}) {
                std::memset(run, 0xcc, 4096);
                const auto native = retail_bytes(danger_room_gate_site, 0x49);
                std::memcpy(run, native.data(), native.size());
                run[0x49] = 0xc3; // 0x435b52: the check after the gate
                const auto rel_at = [&](std::size_t operand, const void* to) {
                    const auto v = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (reinterpret_cast<std::uintptr_t>(run) + operand + 4));
                    std::memcpy(run + operand, &v, 4);
                };
                const auto abs_at = [&](std::size_t operand, const void* to) {
                    const auto v = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(to));
                    std::memcpy(run + operand, &v, 4);
                };
                rel_at(1, reinterpret_cast<const void*>(&mode_read));      // call 0x4c87c0
                rel_at(0x1f, reinterpret_cast<const void*>(&clock_owner)); // call 0x46dce0
                abs_at(0x13, &zero);                                       // fcomp [0x680030]
                abs_at(0x37, &two);                                        // fcomp [0x684118]
                if (rule) { const auto w = danger_room_gate_bytes(); std::memcpy(run, w.data(), w.size()); } // jmp +0x44: position independent
                FlushInstructionCache(GetCurrentProcess(), run, 4096);
                entry = run;
                std::array<std::uint8_t, 0x800> victim{};
                std::array<std::uint8_t, 0x100> hit{};
                field<void*>(hit.data(), 0) = victim.data();
                bool ok = true;
                for (const int mode : {0, 1}) for (const float previous : {0.0f, -1.0f, 7.0f, 8.0f, 9.0f, 9.5f}) {
                    clock_now = 10.0f;
                    mode_now = mode;
                    field<float>(victim.data(), 0x5f4) = previous;
                    field<std::uint8_t>(hit.data(), 0xac) = 0x0f;
                    gate_run(hit.data());
                    const bool suppressed = !rule && mode && previous > 0.0f && 10.0f - previous < 2.0f;
                    ok &= field<std::uint8_t>(hit.data(), 0xac) == (suppressed ? 0x07 : 0x0f);
                }
                check(ok, rule ? "item 22: with the rule a Danger Room hit within 2 s of the last pain still makes the victim react (XML1 has no such gate)"
                               : "item 22: retail clears the reaction bit only in Danger Room mode (0xfd) within 2 s of a positive last pain");
            }
            const auto jump = danger_room_gate_bytes();
            std::int32_t rel = 0;
            std::memcpy(&rel, jump.data() + 1, 4);
            check(jump.size() == 5 && jump[0] == 0xe9 && danger_room_gate_va + 5 + rel == danger_room_gate_resume_va && danger_room_gate_resume_va == danger_room_gate_site.va + 0x49
                  && retail_bytes(danger_room_gate_site, 9) == std::vector<std::uint8_t>({0xe8, 0xb2, 0x2c, 0x09, 0x00, 0x84, 0xc0, 0x74, 0x40}),
                  "item 22: 0x435b09 (call the mode getter; test al,al; je 0x435b52) jumps straight to 0x435b52");
        }
        VirtualFree(run, 0, MEM_RELEASE);
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("NPC combo timing: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
