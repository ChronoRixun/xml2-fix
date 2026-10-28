#pragma once
#include "xml1_cling_rules.hpp"
#include "mold_query_rules.hpp"

// Rule 43. Original entry: default.xbe 0xdbd30. Native service contracts and the
// two deliberate XML2 targeting differences are documented in xml1-rogue-decide.md.
namespace xml1_rogue_decide_rules
{
using xml1_cling_rules::setting;
using xml1_cling_rules::parse;
using limits_rules::guard;
constexpr std::uint16_t agreement_revision = 1;
constexpr int special = 24, special2 = 25, neutral_team = 28;
constexpr float reach = 60.f, half_box = 12.f;
struct vec { float x, y, z; };
inline vec endpoint(vec center, vec facing)
{
    return {center.x + facing.x * reach, center.y + facing.y * reach, center.z + facing.z * reach};
}

// Shared by the real entry and the synthetic fixture. The first() service returns
// just the first character hit (or null); rejecting it must never fetch another.
template<class Services> void enter(Services& s, void* actor, void* context)
{
    void* victim = s.first(actor);
    if (victim && !s.ai_attacker(actor))
    {
        const int team = s.team(victim);
        if ((team == s.team(actor) && !s.friendly_hit()) ||
            (team == neutral_team && s.excluded(victim))) victim = nullptr;
    }
    void* chosen = nullptr;
    if (victim)
    {
        chosen = s.chain(actor, context, special);
        // validTarget is evaluated on the victim, and gates only locking.
        if (s.valid_target(victim)) s.lock(actor, victim);
    }
    if (!chosen)
    {
        chosen = s.chain(actor, context, special2);
        if (chosen && !s.may_start(chosen, actor))
        {
            s.fail(context); // context flags |= 3 (one byte)
            s.feedback(actor);
            s.error_sound();
            chosen = nullptr;
        }
    }
    s.tag(context, actor); // source move tag 1, including the failure path
    if (chosen) s.start(actor, chosen, context); // animation enum, then start(...,1)
}

// Filled with short, reviewed native byte signatures. Query methods are invoked
// virtually; their stock bodies are checked only by the file test under capacity.
inline constexpr auto default_vtable_va = xml1_cling_rules::default_vtable_va;
inline constexpr auto lookup_va = xml1_cling_rules::lookup_va;
inline constexpr auto retail_entry = xml1_cling_rules::retail_entry;
inline constexpr std::array<guard, 24> guards{{
    {0x692f4c, "10d74f00d0bd4f0010eb590010eb5900b0cb480000c54f0050ce4f0060c84f00", "default eight-slot handler"},
    {0x41a220, "8b41048bd0c1ea1df6c2017407b81d00", "team decoding"},
    {0x48de40, "8a0d20c57200b80100000084c8751009", "session getter"},
    {0x48dbf0, "8a41102401c3cc", "session friendly flag"},
    {0x5252e0, "8a0d9c887900b80100000084c875398b", "AI manager getter"},
    {0x524a70, "8b5424048b0152ff905802000085c074", "AI neutral exclusion"},
    {0x55d7e0, "8b4104c3cccc", "move owner vt+e0"},
    {0x4f6b20, "64a1000000008a15449e78006aff682e4d670050b801", "move chain vt+118"},
    {0x500b20, "8b4178568d71748b4c24085750518bce", "style node lookup vt+14"},
    {0x4f5fb0, "83ec0c538b5c241485db5556578bf975", "mayStart vt+c"},
    {0x429210, "51568bf18b865c0300008a88ae02000084c90f88e7000000", "validTarget includes stats high-bit test"},
    {0x42d8e0, "538b5c240856578bf98d4c2410895c2410", "lock takes handle by value"},
    {0x42d939, "66c7863c060000030089542410e815250300518d442414d91c24508bcfe875f8feff", "lock history and target handle"},
    {0x421000, "83ec08d905d8b6700056d80594016800", "failure feedback"},
    {0x4fbe10, "e8cb1ef7ff8b108bc8ff9260010000d8", "error sound"},
    {0x4f6450, "8b44240c568bf18b4c240c8b16508b44240c", "source tag dispatch vt+14"},
    {0x500840, "8b81bc000000c3", "animation enum vt+e8"},
    {0x42c340, "56578b7c240c85ff8bf174348b868403", "start move"},
    {0x4712e0, "83ec10568bf18d4c2408ff1560fa6700", "bounds center vt+24"},
    {0x560990, "8b4c2408d90530006800d94104dae9df", "facing direction"},
    {0x402b60, "568b74240885f6742a8b06578bceff108b1540b87000", "character class cast"},
    {0x4f5a30, "53568bf18b0657ff503085c08b5c24188b7c241074158b168bceff52308b4c24148b105351578bc8ff52", "entry context dispatch"},
    {0x4f0486, "8b4b048b118bbec0030000ff92e00000008b178be88b45006a188bcf89442418ff9218010000508b4424188bcdff5014", "entry current move and chain slots"},
    {0x4eaa15, "a0fdb6700084c0db442460d944241cb3", "friendly global gate"},
}};
inline constexpr std::array<mold_query_rules::entry, 5> query_entries{{
    {0x4ab550, "64a1000000008a0d0089", "pool getter"},
    {0x4ab6d0, "568bf1e878feffff33c9", "query acquire"},
    {0x4ab720, "568b31e828feffff85f6", "query release"},
    {0x4ab740, "e80bfeffff8bc88a513333c0", "free query count"},
    {0x4a9590, "568bf1833e007418e8", "contact entity"},
}};
inline constexpr std::array<guard, 8> query_guards{{
    {0x68d6c4, "50a94a00", "first slot"},
    {0x68d6e4, "f0ac4a00", "filter slot"},
    {0x68d718, "80a14a00", "sweep slot"},
    {0x68d724, "10a34a00", "mode slot"},
    {0x4aa950, "568bf18b86e001000085c05774668b463485", "first result"},
    {0x4aacf0, "8b54240856578b7c240c83ff018bf10f85c2", "query filter"},
    {0x4aa180, "51568bf1e8972bfaff33c98a4e326a008b10", "sweep three arguments"},
    {0x4aa310, "8a442404884132c20400cc", "query mode"},
}};
}
