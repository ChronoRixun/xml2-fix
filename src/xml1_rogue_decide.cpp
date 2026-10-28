#include "xml1_rogue_decide.hpp"
#include "xml1_rogue_decide_rules.hpp"
#include "xml1_cling.hpp"
#include "entity_capacity.hpp"
#include "entity_query.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "mold_query_rules.hpp"
#include "xml1_hook_memory.hpp"
#include <atomic>

namespace xml1_rogue_decide
{
namespace
{
using namespace xml1_rogue_decide_rules;
using u32 = std::uint32_t;
std::atomic<int> state{-1};
bool trace = false;
std::array<void*, 8> callbacks{};
void* handler = nullptr;
template<class T> T at(const void* p, u32 off) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p) + off, sizeof v); return v; }
template<class T> void put(void* p, u32 off, T v) { std::memcpy(static_cast<unsigned char*>(p) + off, &v, sizeof v); }
template<class T> T fn(u32 address) { return reinterpret_cast<T>(address); }
template<class T> T slot(const void* p, u32 off) { return at<T>(at<const void*>(p, 0), off); }
using getter_t = void*(__cdecl*)();
using bool_t = bool(__fastcall*)(void*, void*);
using pointer_t = void*(__fastcall*)(void*, void*);
using unary_t = void(__fastcall*)(void*, void*);
using cast_t = void*(__cdecl*)(void*);
using team_t = int(__fastcall*)(void*, void*);
using excluded_t = bool(__fastcall*)(void*, void*, u32);
using acquire_t = void**(__fastcall*)(void**, void*, const void*, const u32*);
using release_t = void(__fastcall*)(void**, void*);
using center_t = vec*(__fastcall*)(void*, void*, vec*);
using facing_t = vec*(__cdecl*)(vec*, const void*);
using filter_t = void(__fastcall*)(void*, void*, int, int);
using mode_t = void(__fastcall*)(void*, void*, int);
using sweep_t = void(__fastcall*)(void*, void*, const vec*, const vec*, const vec*);
using first_t = void**(__fastcall*)(void*, void*, void**);
using chain_t = const u32*(__fastcall*)(void*, void*, int);
using find_t = void*(__fastcall*)(void*, void*, const u32*);
using may_t = bool(__fastcall*)(void*, void*, void*);
using lock_t = void(__fastcall*)(void*, void*, u32);
using tag_t = bool(__fastcall*)(void*, void*, unsigned char, void*, void*);
using anim_t = int(__fastcall*)(void*, void*);
using start_t = void(__fastcall*)(void*, void*, void*, int);

struct services
{
    void* query = nullptr;
    ~services() { if (query) fn<release_t>(0x4ab720)(&query, nullptr); }
    void* first(void* actor)
    {
        // Both native and capacity acquisition assume a free pooled query. No
        // order is acquired by this path, so only the query count is needed.
        if (fn<int(__cdecl*)()>(0x4ab740)() <= 0) return nullptr;
        const u32 excluded_handle = at<u32>(actor, 0x1c);
        fn<acquire_t>(0x4ab6d0)(&query, nullptr, reinterpret_cast<const void*>(0x70b83c), &excluded_handle);
        if (!query) return nullptr;
        // Never call retail query implementations directly: EntityCapacity
        // supplies its own vtable, preserving these slots and conventions.
        vec center{}, facing{};
        slot<center_t>(actor, 0x24)(actor, nullptr, &center);
        fn<facing_t>(0x560990)(&facing, static_cast<unsigned char*>(actor) + 0x2c);
        const vec end = endpoint(center, facing), box{half_box, half_box, half_box};
        slot<filter_t>(query, 0x40)(query, nullptr, 1, 1);
        slot<mode_t>(query, 0x80)(query, nullptr, 0);
        slot<sweep_t>(query, 0x74)(query, nullptr, &center, &end, &box); // XML2 ret 12, not XML1 ret 16
        void* contact = nullptr;
        slot<first_t>(query, 0x20)(query, nullptr, &contact);
        void* entity = fn<pointer_t>(0x4a9590)(&contact, nullptr); // capacity-supported bridge
        return entity ? fn<cast_t>(0x402b60)(entity) : nullptr;
    }
    bool ai_attacker(void* actor) { return (at<unsigned char>(actor, 0x3d8) & 1) != 0; }
    int team(void* entity) { return fn<team_t>(0x41a220)(entity, nullptr); }
    bool friendly_hit()
    {
        if (*reinterpret_cast<const unsigned char*>(0x70b6fd)) return true;
        void* session = fn<getter_t>(0x48de40)();
        return session && slot<bool_t>(session, 0x5c)(session, nullptr);
    }
    bool excluded(void* victim)
    {
        void* ai = fn<getter_t>(0x5252e0)();
        return ai && slot<excluded_t>(ai, 0x78)(ai, nullptr, at<u32>(victim, 0x7dc));
    }
    void* chain(void* actor, void* context, int action)
    {
        void* source = at<void*>(context, 4);
        if (!source) return nullptr;
        void* style = slot<pointer_t>(source, 0xe0)(source, nullptr);
        void* current = at<void*>(actor, 0x3c0); // entry-time source; not +0x378's update helper
        if (!style || !current) return nullptr;
        const u32* key = slot<chain_t>(current, 0x118)(current, nullptr, action);
        return key ? slot<find_t>(style, 0x14)(style, nullptr, key) : nullptr;
    }
    bool valid_target(void* victim) { return fn<bool_t>(0x429210)(victim, nullptr); }
    void lock(void* actor, void* victim) { fn<lock_t>(0x42d8e0)(actor, nullptr, at<u32>(victim, 0x1c)); }
    bool may_start(void* move, void* actor) { return slot<may_t>(move, 0xc)(move, nullptr, actor); }
    void fail(void* context) { put<unsigned char>(context, 0x6c, at<unsigned char>(context, 0x6c) | 3); }
    void feedback(void* actor) { fn<unary_t>(0x421000)(actor, nullptr); }
    void error_sound() { fn<void(__cdecl*)()>(0x4fbe10)(); }
    void tag(void* context, void* actor)
    {
        if (void* source = at<void*>(context, 4)) slot<tag_t>(source, 0x14)(source, nullptr, 1, actor, actor);
    }
    void start(void* actor, void* move, void* context)
    {
        put<int>(context, 0, slot<anim_t>(move, 0xe8)(move, nullptr));
        fn<start_t>(0x42c340)(actor, nullptr, move, 1);
    }
};
void __fastcall entry(void*, void*, void* actor, void*, void* context)
{
    if (!actor || !context) return;
    services s;
    enter(s, actor, context);
    if (trace) logger::write("rogue decide: actor %08X entry flags %02X animation %d", at<u32>(actor, 0x1c), at<unsigned char>(context, 0x6c), at<int>(context, 0));
}
void* __fastcall destructor(void* self, void*, unsigned) { return self; }
void* __cdecl lookup(const char* name)
{
    return name && _stricmp(name, "ch_roguedecide") == 0 ? &handler : nullptr;
}
bool supported()
{
    xml1_hook_transaction::memory memory;
    for (const auto& g : guards)
    {
        std::array<unsigned char, 64> bytes{};
        if (g.hex.size() > bytes.size() * 2 || !memory.read(g.va, bytes.data(), g.hex.size() / 2) || !limits_rules::matches(bytes.data(), g.hex))
        {
            logger::write("rogue decide: unsupported service %08X (%s)", g.va, g.what);
            return false;
        }
    }
    const bool raised = entity_capacity::installed().raised;
    for (const auto& e : query_entries)
    {
        std::array<unsigned char, 24> bytes{};
        if (!memory.read(e.va, bytes.data(), e.retail.size() / 2) ||
            !mold_query_rules::entry_ok(raised, bytes.data(), e, raised ? entity_capacity::installed_target(e.va) : 0)) return false;
    }
    if (raised && (!entity_query::installed_manager() || fn<getter_t>(0x4ab550)() != entity_query::installed_manager())) return false;
    // The acquired raised query uses the capacity owner's own vtable. Stock
    // query implementations are never entered with the raised object layout.
    if (!raised)
        for (const auto& g : query_guards)
        {
            std::array<unsigned char, 64> bytes{};
            if (!memory.read(g.va, bytes.data(), g.hex.size() / 2) || !limits_rules::matches(bytes.data(), g.hex)) return false;
        }
    return true;
}
}
void install(HMODULE game)
{
    state = -1;
    const auto value = ini::text(L"Game", L"Xml1RogueDecide").value_or("");
    const auto wanted = parse(value);
    if (wanted == setting::native) return; // no reads/writes to the image; no agreement entry
    state = 0;
    if (wanted != setting::xml1 || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || !supported())
    {
        logger::write("rogue decide: requested but unavailable; requires retail services and Xml1RogueDecide=xml1");
        return;
    }
    std::memcpy(callbacks.data(), reinterpret_cast<const void*>(default_vtable_va), sizeof callbacks);
    callbacks[0] = reinterpret_cast<void*>(&destructor);
    callbacks[1] = reinterpret_cast<void*>(&entry);
    handler = callbacks.data(); // slot 6 stays generic 0x4fce50
    if (!xml1_cling::register_lookup(game, &lookup))
    {
        handler = nullptr;
        logger::write("rogue decide: shared handler lookup refused");
        return;
    }
    trace = ini::flag(L"Test", L"RogueDecideTrace", false);
    state = 1;
    logger::write("rogue decide: xml1 (rule 43): first hit, 60 units / box 12; native target validity and history retained");
}
std::string status() { const int s = state.load(); return std::string("rogue decide ") + (s == 1 ? "xml1" : s == -1 ? "game's own" : "unavailable"); }
bool requested() { return state.load() != -1; }
bool installed() { return state.load() == 1; }
bool verified() { return !requested() || installed(); }
}
