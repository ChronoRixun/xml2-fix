// V-NPC2: execute the generated combo bridges with synthetic brains. Only the brain-table base and external
// continuations are relocated; the timer arithmetic, internal branches and register/stack contracts are real.
#include <Windows.h>
#include "npc_power_rules.hpp"
#include <array>
#include <cmath>
#include <cstdio>
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
    check(cont_bridge(l).size() == cont_size && start_bridge(l).size() == start_size && end_delay_bridge(l).size() == end_delay_size, "emitted bridge lengths");
    check(react_cont_bridge(l).size() == react_cont_size && pre_start_bridge(l).size() == pre_start_size && mark_bridge(l).size() == mark_size && mark_jump(l).size() == reaction_site.patched
          && power_front_bridge(l).size() == power_front_size && power_entry_jump(l).size() == power_entry_site.patched
          && attack_front_bridge(l).size() == attack_front_size && attack_entry_jump(l).size() == attack_entry_site.patched, "revision 5 bridge lengths");
    check(l.base + 786 <= l.react_cont && l.react_cont + react_cont_size <= l.pre_start && l.pre_start + pre_start_size <= l.mark && l.mark + mark_size <= l.power_front
          && l.power_front + power_front_size <= l.attack_front && l.attack_front + attack_front_size <= l.base + code_size
          && l.react == l.next + 4 * ai_slots && l.react_power == l.react + 4 * ai_slots && l.react_attack == l.react_power + 4 * ai_slots
          && l.react_attack + 4 * ai_slots <= l.next + (block_size - code_size), "revision 5 spans do not overlap");
    check(l.cont + cont_size <= l.start && l.start + start_size <= l.delays && l.delays + 120 <= l.end_delay && l.end_delay + end_delay_size <= l.base + code_size, "bridge and data spans do not overlap");
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
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("NPC combo timing: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
