// V-TBD: execute the generated combo bridges with synthetic brains. Only the brain-table base and external
// continuations are relocated; the timer arithmetic, internal branches and register/stack contracts are real.
#include <Windows.h>
#include "npc_power_rules.hpp"
#include <array>
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
    // The unknown-combo fallback's constant lives in our allocation too.
    edit(l.end_delay + 0x1b, l.base + 760);
    const float stock_delay = 0.2f;
    std::memcpy(bytes.data() + 760, &stock_delay, 4);
    for (int value = 0; value < 5; ++value) {
        const std::size_t at = 720 + 8 * value;
        bytes[at] = 0xb8; put32(bytes, at + 1, value); bytes[at + 5] = 0xc3;
    }
    check(cont_bridge(l).size() == cont_size && start_bridge(l).size() == start_size && end_delay_bridge(l).size() == end_delay_size, "emitted bridge lengths");
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
    field<unsigned>(brains.data() + 1, 0x30) = 0;
    field<unsigned>(brains.data() + 1, 0x38) = 0;
    check(step(brains.data() + 1, actor.data()) == 4, "unaligned brain uses native fallback");
    check(step(brains.data() + ai_stride * ai_slots, actor.data()) == 4, "brain beyond last slot uses native fallback");
    field<unsigned>(brains.data(), 0x3c) = 6;
    entry = reinterpret_cast<void*>(l.end_delay);
    check(finish(brains.data(), 17.0f) == 17.2f, "unmapped combo keeps native completion addition");
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("NPC combo timing: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
