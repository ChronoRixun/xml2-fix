#include "xml1_grab_carry.hpp"
#include "xml1_air_carry.hpp"
#include "xml1_grab_carry_rules.hpp"
#include "xml1_cling.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <atomic>

namespace xml1_grab_carry
{
namespace
{
using namespace xml1_grab_carry_rules;
using u32 = std::uint32_t;
std::atomic<int> state{-1};
bool trace = false, logged_lookup = false, carry = true;
std::array<void*, 8> attack_slots{};
void* attack_handler = nullptr;
template<class T> T at(const void* p, u32 off) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p) + off, sizeof v); return v; }
template<class T> void put(void* p, u32 off, T v) { std::memcpy(static_cast<unsigned char*>(p) + off, &v, sizeof v); }
template<class T> T fn(u32 va) { return reinterpret_cast<T>(static_cast<std::uintptr_t>(va)); }
template<class T> T slot(const void* p, u32 off) { return at<T>(at<const void*>(p, 0), off); }
using resolve_t = void*(__fastcall*)(const u32*, void*);
using cast_t = void*(__cdecl*)(void*);
using unary_t = void(__fastcall*)(void*, void*);
using pointer_t = void*(__fastcall*)(void*, void*);
using named_t = void*(__fastcall*)(void*, void*, const u32*, void*);
using forced_t = void(__fastcall*)(void*, void*, void*, int, float, u32, u32, u32);
using start_t = void(__fastcall*)(void*, void*, void*, int);
using exclusion_t = void(__fastcall*)(void*, void*, u32, float);
using relative_t = void(__fastcall*)(void*, void*, void*);
using bool_set_t = void(__fastcall*)(void*, void*, bool);

void* resolve(void* actor, u32 offset)
{
    const u32 handle = at<u32>(actor, offset);
    // The native resolver dispatches the installed world's virtual slots. No
    // handle masks, actor array indexes or query layouts are assumed here.
    return fn<resolve_t>(0x4654b0)(&handle, nullptr);
}
void* character(void* entity) { return fn<cast_t>(0x402b60)(entity); }
void* named(void* actor, u32 key)
{
    void* styles = fn<pointer_t>(0x41d0d0)(actor, nullptr);
    return fn<named_t>(0x501a50)(styles, nullptr, reinterpret_cast<const u32*>(key), nullptr);
}
void publish(void* actor) { fn<unary_t>(0x45fe40)(actor, nullptr); }

void acquire_constraints(void* victim)
{
    put<unsigned char>(victim, 0x764, at<unsigned char>(victim, 0x764) | 0x10);
    for (const u32 bit : {2u, 4u, 1u})
    {
        put<u32>(victim, 4, at<u32>(victim, 4) | bit);
        publish(victim);
    }
}
// XML1 CCEAtkGrab 0xd0ce2: the accepted native PC branch has already written
// both owners. Restore the source constraints before its sound/return path.
void __cdecl grab_acquired(void* actor)
{
    if (void* victim = character(resolve(actor, 0x76c))) acquire_constraints(victim);
}
auto grab_acquired_function = &grab_acquired;
u32 grab_acquired_resume = 0x4de89e;
__declspec(naked) void grab_acquired_bridge()
{
    __asm {
        pushad
        push dword ptr [esi]
        call dword ptr [grab_acquired_function]
        add esp, 4
        popad
        mov eax, [esi]
        mov eax, [eax + 444h]
        jmp dword ptr [grab_acquired_resume]
    }
}
// XML1 actor reset 0x3b0cb: cleanup belongs to reset, including default-handler
// ally victim moves. The marker gates this policy; unrelated flags are retained.
void __cdecl reset_constraints(void* actor)
{
    const auto marker = at<unsigned char>(actor, 0x764);
    if (!(marker & 0x10)) return;
    put<unsigned char>(actor, 0x764, marker & ~0x10);
    for (const u32 bit : {2u, 4u, 1u})
    {
        put<u32>(actor, 4, at<u32>(actor, 4) & ~bit);
        publish(actor);
    }
}
auto reset_constraints_function = &reset_constraints;
u32 reset_constraints_resume = 0x42c335;
__declspec(naked) void reset_constraints_bridge()
{
    __asm {
        mov [esi + 3d8h], al
        pushad
        push esi
        call dword ptr [reset_constraints_function]
        add esp, 4
        popad
        jmp dword ptr [reset_constraints_resume]
    }
}

// XML1 0xdc3b0. The acquisition already owns the handles. The source entry
// sets constraints explicitly; it does not invent PC's reciprocal owner tokens.
void __fastcall ally_throw_entry(void*, void*, void* actor, void*, void* context)
{
    if (!actor) return;
    void* victim = character(resolve(actor, 0x76c));
    if (!victim)
    {
        fn<start_t>(0x42c340)(actor, nullptr, named(actor, 0x78a8c8), 1);
        return;
    }
    if (!context) return;
    acquire_constraints(victim);
    fn<forced_t>(0x432030)(victim, nullptr, named(victim, 0x78a8d0), -1, at<float>(context, 0x64), 0, 1, 0);
    std::array<float, 3> position{};
    std::memcpy(position.data(), static_cast<unsigned char*>(actor) + 0x20, sizeof position);
    const void* angles = static_cast<unsigned char*>(actor) + 0x2c;
    float direction[3]{};
    fn<float*(__cdecl*)(float*, const void*)>(0x560990)(direction, angles);
    using vector_t = void(__fastcall*)(void*, void*, const void*);
    slot<vector_t>(victim, 0x150)(victim, nullptr, position.data());
    slot<vector_t>(victim, 0x15c)(victim, nullptr, angles);
}

// XML1 0x75180(on=1) writes these two timers without PC's +0x259 refusal.
// Bit stores introduce no floating point operations or FP environment changes.
void __fastcall pickup_timers(void* target, void*, int on, int)
{
    if (!target || !on) return;
    put<u32>(target, 0x23c, 0xbf800000u);
    put<u32>(target, 0x240, 0xbf800000u);
}
// XML1 0xdeaa0: restore the entire acquisition setup, including already-marked
// targets. Do not clear another actor's owner token or rewrite the selection.
void __fastcall pickup_grab_entry(void*, void*, void* actor, void*, void*)
{
    if (!actor) return;
    void* target = fn<cast_t>(0x429110)(resolve(actor, 0x76c));
    if (!target) return;
    pickup_timers(target, nullptr, 1, 0);
    put<unsigned char>(target, 0xc5, at<unsigned char>(target, 0xc5) | 0x40);
    put<unsigned char>(target, 0x30d, at<unsigned char>(target, 0x30d) | 4);
}
// XML1 0xda3b0: normal thrown-land completion always invokes its slot3 cleanup.
void __fastcall thrown_land_complete(void* self, void*, void* actor)
{
    using cleanup_t = void(__fastcall*)(void*, void*, void*);
    slot<cleanup_t>(self, 0xc)(self, nullptr, actor);
}

// XML1 entry 0xdc140. In particular, do not run default entry (which would
// stamp the attacker's deadline). Null move is passed to the native no-op path.
void __fastcall attack_entry(void*, void*, void* actor, void*, void*)
{
    if (!actor) return;
    void* victim = character(resolve(actor, 0x76c));
    if (!victim) return;
    void* move = named(victim, 0x78a87c); // grabattackvictim, NOT the authored victim trigger
    void* before = at<void*>(victim, 0x378);
    fn<forced_t>(0x432030)(victim, nullptr, move, -1, -1.f, 0, 0, 0);
    if (trace || !logged_lookup)
    {
        logger::write("grab carry: grabattackvictim actor %08X victim %08X move_resolves=%d current_matches=%d changed=%d",
                      at<u32>(actor, 0x1c), at<u32>(victim, 0x1c), move != nullptr,
                      move && at<void*>(victim, 0x378) == move, before != at<void*>(victim, 0x378));
        // The authored time-zero victim trigger is independent of the optional
        // grabattackvictim lookup above. Observe it; never force or substitute it.
        u32 key = 0;
        fn<void*(__fastcall*)(u32*, void*, const char*)>(0x41b120)(&key, nullptr, "juggrabthrowforwardvictim");
        void* authored = fn<named_t>(0x501a50)(fn<pointer_t>(0x41d0d0)(victim, nullptr), nullptr, &key, nullptr);
        logger::write("grab carry: authored victim transition juggrabthrowforwardvictim actor %08X victim %08X move_resolves=%d current_matches=%d",
                      at<u32>(actor, 0x1c), at<u32>(victim, 0x1c), authored != nullptr,
                      authored && at<void*>(victim, 0x378) == authored);
        logged_lookup = true;
    }
}
void* __fastcall persistent_destructor(void* self, void*, unsigned) { return self; }
void* __cdecl lookup(const char* name)
{
    return attack_handler && name && _stricmp(name, "ch_grab_attack") == 0 ? &attack_handler : xml1_air_carry::lookup(name);
}

// XML1 0xdc690: publish each flag change, then detach relative to the current
// owner. Neither the ground-victim PC cleanup nor its dead-owner branch does this.
void release_constraints(void* victim)
{
    for (const u32 bit : {2u, 4u, 1u})
    {
        put<u32>(victim, 4, at<u32>(victim, 4) & ~bit);
        publish(victim);
    }
    slot<relative_t>(victim, 0x138)(victim, nullptr, resolve(victim, 0x178));
}
// XML1 0xdc6f0, the shared grab victim's interrupt cleanup (slot 3).
void __fastcall victim_cleanup(void*, void*, void* victim)
{
    if (!victim) return;
    release_constraints(victim);
    if (at<unsigned char>(victim, 0x3db) & 4)
    {
        fn<bool_set_t>(0x41d020)(victim, nullptr, false);
        fn<bool_set_t>(0x41d060)(victim, nullptr, false);
    }
}
const char* move_name(void* move)
{
    using key_t = const u32*(__fastcall*)(void*, void*);
    const u32* name = slot<key_t>(move, 0xd0)(move, nullptr);
    const u32 id = name ? *name : 0;
    if (!id) return "";
    // CName's fixed intern table, independent of the resource-name cache.
    void* table = fn<void*(__cdecl*)()>(0x602280)();
    const u32 offset = at<u32>(table, 4 + (id & 0xffffffu) * 4);
    return static_cast<const char*>(table) + 0x4008 + offset;
}
// XML1 0xdcd40; PC slot 4 receives TWO arguments (ret 8).
void __fastcall victim_frame(void*, void*, void* victim, void*)
{
    if (!victim) return;
    void* owner = character(resolve(victim, 0x178));
    if (owner)
    {
        void* paired = character(resolve(owner, 0x76c));
        void* move = at<void*>(owner, 0x378);
        if (paired == victim && move && retaining_prefix(move_name(move))) return;
        const u32 none = *reinterpret_cast<const u32*>(0x72988c);
        slot<exclusion_t>(owner, 0x104)(owner, nullptr, none, 0.6f);
        slot<exclusion_t>(victim, 0x104)(victim, nullptr, none, 0.6f);
        // XML1 falls through to release on this same callback. Retail PC returns
        // after exclusion here, keeping the constraint until a later callback.
    }
    release_constraints(victim);
    fn<start_t>(0x42c340)(victim, nullptr, named(victim, 0x78a8c8), 1);
}

// XML1 0xddbef: acquire only a living struck character after the tag1 event,
// with a nonnull next move and an empty held slot. Integer IEEE-754 ordering
// expresses health > 0 (including +infinity; excluding NaNs) without new FP ops.
void __cdecl collision_acquire(void* actor, void* struck)
{
    void* victim = character(struck);
    if (!victim) return;
    const u32 health = at<u32>(victim, 0x27c);
    if ((health & 0x80000000u) || health == 0 || health > 0x7f800000u) return;
    if (at<u32>(actor, 0x76c) != *reinterpret_cast<const u32*>(0x72988c)) return;
    // The native lock uses the active World's virtual resolver, including
    // EntityCapacity. Its additional PC targeting-history update is retained.
    fn<void(__fastcall*)(void*, void*, u32)>(0x42d8e0)(actor, nullptr, at<u32>(victim, 0x1c));
    if (trace) logger::write("grab carry: fly-charge actor %08X acquired %08X", at<u32>(actor, 0x1c), at<u32>(victim, 0x1c));
}
auto collision_acquire_function = &collision_acquire;
u32 collision_resume = 0x4ee8bd;
__declspec(naked) void collision_acquire_bridge()
{
    __asm {
        pushad
        push edi
        push ebx
        call dword ptr [collision_acquire_function]
        add esp, 8
        popad
        jmp dword ptr [collision_resume]
    }
}

u32 timeout_continue = 0x4edb88, hold_return = 0x4edbe2;
// Only reached from the native neutral/deadline branch. ESI is the holder.
// No new floating point operations: the original call/comparison and the live
// x87 environment are retained, including unordered/equality behavior.
__declspec(naked) void hold_timeout()
{
    __asm {
        test ah, 41h
        jnz keep
        mov eax, [esi + 35ch]
        test eax, eax
        jz timed
        test byte ptr [eax + 2adh], 8
        jnz keep
    timed:
        jmp dword ptr [timeout_continue]
    keep:
        jmp dword ptr [hold_return]
    }
}

bool supported()
{
    xml1_hook_transaction::memory memory;
    for (const auto& g : guards)
    {
        std::array<unsigned char, 64> bytes{};
        if (g.hex.size() > bytes.size() * 2 || !memory.read(g.va, bytes.data(), g.hex.size() / 2) ||
            !limits_rules::matches(bytes.data(), g.hex))
        {
            logger::write("grab carry: unsupported service %08X (%s)", g.va, g.what);
            return false;
        }
    }
    return true;
}
}

void install(HMODULE game)
{
    state = -1;
    carry = true;
    const auto wanted = parse(ini::text(L"Game", L"Xml1GrabCarry").value_or(""));
    if (wanted == setting::native) return;
    state = 0;
    // [Game] Xml1FlyingCarry=0 (beta 8 080h): no flying carry; everything else in the rule as with it. Read before any
    // refusal so a refused request advertises the mask it asked for.
    carry = ini::flag(L"Game", L"Xml1FlyingCarry", true);
    if (wanted != setting::xml1 || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || !supported())
    {
        logger::write("grab carry: requested but unavailable; requires retail services and Xml1GrabCarry=xml1");
        return;
    }
    std::memcpy(attack_slots.data(), reinterpret_cast<const void*>(xml1_cling_rules::default_vtable_va), sizeof attack_slots);
    attack_slots[0] = reinterpret_cast<void*>(&persistent_destructor);
    attack_slots[1] = reinterpret_cast<void*>(&attack_entry);
    attack_handler = attack_slots.data();

    using namespace xml1_hook_transaction;
    std::vector<patch> writes;
    if (!xml1_air_carry::prepare(writes, carry))
    {
        attack_handler = nullptr;
        xml1_air_carry::abandon();
        logger::write("grab carry: AnimEnums, LiftRule=xml1 or flight/carry services unavailable; no patches installed");
        return;
    }
    auto pointer = [&](u32 va, const void* value) {
        const auto* old = reinterpret_cast<const unsigned char*>(va);
        writes.push_back({va, {old, old + 4}, pointer_bytes(value)});
    };
    std::vector<unsigned char> jump(5, 0xe9);
    const u32 relative = static_cast<u32>(reinterpret_cast<std::uintptr_t>(&hold_timeout)) - timeout_va - 5;
    std::memcpy(jump.data() + 1, &relative, 4);
    const auto* old = reinterpret_cast<const unsigned char*>(timeout_va);
    writes.push_back({timeout_va, {old, old + 5}, jump});
    // Both hold cleanup slots are XML1 0xdc360 == native PC 0x4ed890.
    pointer(hold_vtable + 8, fn<void*>(0x4ed890));
    pointer(hold_vtable + 12, fn<void*>(0x4ed890));
    // Shared victim: default entry, no normal-exit cleanup, interrupt cleanup,
    // retention frame. Generic update and other inherited slots remain native.
    pointer(victim_vtable + 4, fn<void*>(0x4fbdd0));
    pointer(victim_vtable + 8, fn<void*>(0x59eb10));
    pointer(victim_vtable + 12, reinterpret_cast<void*>(&victim_cleanup));
    pointer(victim_vtable + 16, reinterpret_cast<void*>(&victim_frame));
    pointer(0x693318 + 4, reinterpret_cast<void*>(&ally_throw_entry));
    pointer(0x693438 + 4, reinterpret_cast<void*>(&pickup_grab_entry));
    pointer(0x693438 + 12, fn<void*>(0x59eb10));
    pointer(0x69345c + 12, fn<void*>(0x59eb10));
    pointer(0x6933a8 + 8, reinterpret_cast<void*>(&thrown_land_complete));
    auto branch = [&](u32 address, u32 target, unsigned size, unsigned char opcode) {
        std::vector<unsigned char> after(size, 0x90);
        after[0] = opcode;
        const u32 displacement = target - address - 5;
        std::memcpy(after.data() + 1, &displacement, 4);
        const auto* before = reinterpret_cast<const unsigned char*>(address);
        writes.push_back({address, {before, before + size}, after});
    };
    // Keep native health, input, strict deadline and cleanup tail. Only PC's
    // owner-token gate and two-second clearing are bypassed (XML1 0xe0fe0).
    branch(0x4f49e9, 0x4f4a3d, 7, 0xe9);
    // Normal pickup uses the same source timer setup as pickup-grab. All its
    // flag publication, completion, sound and per-frame work remains native.
    branch(0x4f0b6e, static_cast<u32>(reinterpret_cast<std::uintptr_t>(&pickup_timers)), 5, 0xe8);
    // XML1 nonactor collisions always take Special; there is no contact Z gate.
    branch(0x4ee87c, 0x4ee8a0, 6, 0xe9);
    // Only reached with a selected next move. Damage/tag dispatch stays native;
    // the independent jump/fly-charge damage producers are not touched.
    branch(0x4ee8b3, static_cast<u32>(reinterpret_cast<std::uintptr_t>(&collision_acquire_bridge)), 10, 0xe9);
    // Restore the source lifecycle services that PC moved out of acquisition
    // and reset. Raw handler-table transplantation alone leaves flags missing
    // on acquisition or stuck when a victim transitions to a default move.
    branch(0x4de896, static_cast<u32>(reinterpret_cast<std::uintptr_t>(&grab_acquired_bridge)), 8, 0xe9);
    branch(0x42c32f, static_cast<u32>(reinterpret_cast<std::uintptr_t>(&reset_constraints_bridge)), 6, 0xe9);
    // XML1 3AF9E forwards collision velocity/thrower to the body's impact
    // receiver even for world geometry. PC 42C093 additionally requires a
    // physical target or its temporary psy flag, which reset clears on throw.
    // The native receiver still owns timing, amount, defenses and attribution.
    writes.push_back({world_hit_gate,{world_hit_before.begin(),world_hit_before.end()},
                     {world_hit_after.begin(),world_hit_after.end()}});
    memory mem;
    bool safe = true;
    if (!apply(mem, writes, &safe))
    {
        attack_handler = nullptr;
        xml1_air_carry::abandon();
        if (!safe) game_version::refuse_startup();
        logger::write("grab carry: grab/carry/flight transaction refused");
        return;
    }
    if (!xml1_cling::register_lookup(game, &lookup))
    {
        std::vector<patch> undo;
        for (const auto& p : writes) undo.push_back({p.address, p.after, p.before});
        if (!apply(mem, undo, &safe) || !safe) game_version::refuse_startup();
        attack_handler = nullptr;
        xml1_air_carry::abandon();
        logger::write("grab carry: shared lookup refused; all rule45 patches rolled back");
        return;
    }
    trace = ini::flag(L"Test", L"GrabCarryTrace", false);
    state = 1;
    if (!carry)
    {
        // The builder's DLL gate reads this line ([Game] Xml1FlyingCarry, beta 8 080h).
        logger::write("grab carry: XML1 grab/pickup/recovery installed (rule 45 revision %u, feature mask %u); fix7 and XML1 upkeep installed",
                      static_cast<unsigned>(agreement_revision), feature_mask_no_flying_carry);
        logger::write("grab carry: flying carry off (Xml1FlyingCarry=0): no carry handlers, PC flight descent");
        logger::write("grab carry: thrown bodies forward world impacts (42C09E)");
        return;
    }
    logger::write("grab carry: XML1 grab/pickup/recovery installed (rule 45 revision %u, feature mask %u); flying carry, fix7 and XML1 upkeep installed",
                  static_cast<unsigned>(agreement_revision), feature_mask);
    // The builder's DLL gate reads this line (beta 8 080e: a carry chain that resolves to itself no longer re-enters).
    logger::write("grab carry: flying carry keeps its established node (no self-chain)");
    // Beta 8 080g (fix3): world impacts reach a thrown body's receiver (0x42C09E), carry pose from animated bones.
    logger::write("grab carry: thrown bodies forward world impacts (42C09E); carry pose on animated bones");
}
std::string status() { const int s = state.load(); return std::string("grab carry ") + (s == 1 ? (carry ? "xml1 grab + flying carry + flight upkeep" : "xml1 grab + flight upkeep (flying carry off)") : s == -1 ? "game's own" : "unavailable"); }
std::uint32_t feature_mask() { return mask_for(carry); }
bool flying_carry() { return carry; }
bool requested() { return state.load() != -1; }
bool installed() { return state.load() == 1; }
bool verified() { return !requested() || installed(); }
}
