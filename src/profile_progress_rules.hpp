#pragma once

#include "limits_rules.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Personal profile metadata only. The native level calculation, campaign flags,
// character unlock bits and the serialized campaign format are unchanged.
namespace profile_progress_rules
{
    constexpr std::uint32_t notification_site = 0x4b7e17;
    constexpr std::size_t notification_size = 0x4b7e3b - notification_site;
    constexpr std::uint32_t profile_getter = 0x48fed0, profile_vtable = 0x689994;
    constexpr std::uint32_t registry_getter = 0x44b8f0, registry_vtable = 0x68544c;
    constexpr std::uint32_t game_getter = 0x46dce0, game_vtable = 0x686e1c;
    constexpr std::uint32_t session_getter = 0x612be0, session_vtable = 0x6a481c;
    constexpr std::uint32_t seats_getter = 0x48de40, seats_vtable = 0x6898c4;
    constexpr std::size_t profile_size = 0x228, highest_level = 0x228;
    constexpr std::size_t serialized_highest_level = highest_level - 4;
    constexpr std::size_t stats_level = 0x1c, stats_kind = 0x2a0, stats_flags = 0x2ad;
    constexpr std::uint32_t hero_kind = 0x1d;
    constexpr std::uint8_t xpexempt = 0x20;
    constexpr unsigned max_level = 99, max_roster = 0x129;

    // The replacement written over notification_site: push esi; call target (cdecl);
    // add esp, 4; nop to the native level-delta return at 0x4b7e3b. Shared with
    // [Game] XPCurve, whose level-recompute guard spans these bytes.
    inline std::vector<std::uint8_t> notification_bytes_for(std::uint32_t target)
    {
        std::vector<std::uint8_t> bytes(notification_size, 0x90);
        bytes[0] = 0x56;
        bytes[1] = 0xe8;
        const std::uint32_t rel = target - (notification_site + 6);
        std::memcpy(bytes.data() + 2, &rel, 4);
        bytes[6] = 0x83; bytes[7] = 0xc4; bytes[8] = 4;
        return bytes;
    }

    struct context
    {
        bool known = false, campaign = false, online = false, host = false, native_story = false;
        bool empty_setup = false; // both native participant count and first-node pointer are empty
        bool local() const { return known && (!online || host); }
        bool private_setup() const { return known && online && empty_setup; }
        bool local_disk() const { return local() || private_setup(); }
    };

    constexpr bool eligible_hero(std::uint32_t kind, std::uint8_t flags, unsigned level)
    { return kind == hero_kind && !(flags & xpexempt) && level > 0 && level <= max_level; }

    struct lifetime
    {
        bool owned = false, loading = false, failed = false, remote = false;
        bool was_owned = false;
        unsigned pending_level = 0;

        void reset()
        { owned = loading = failed = remote = was_owned = false; }
        void begin_load()
        {
            was_owned = owned;
            owned = false;
            loading = true;
            pending_level = 0; // no deferred write may cross an unconfirmed campaign application
        }
        void finish_load(int mode, bool validated_disk, bool ok, const context& c)
        {
            loading = false;
            failed = !ok || !c.known;
            const bool prior_remote = remote;
            const bool owned_restart = (mode == 0 || mode == 2) && c.known && c.online && c.host && was_owned;
            const bool owned_disk = ok && validated_disk && mode != 1 && mode != 2 && c.local_disk() && c.campaign;
            // Before Post Game the host's local disk selection has no native
            // participants and no host flag yet. Only a validated full disk
            // completion can establish this private proof; emptiness alone is
            // never native-story or incoming-transfer authority.
            owned = ok && ((mode == 1 && was_owned && (c.local() || c.private_setup())) || owned_disk || owned_restart);
            // Partial stats do not replace a borrowed campaign. Neither a
            // disconnect nor another unproven transfer establishes ownership.
            remote = (prior_remote && !owned_disk) ||
                (c.known && c.online && !c.host && !((owned || was_owned) && c.private_setup())) ||
                (mode == 2 && !owned_restart);
            if (failed || remote) pending_level = 0;
        }
        bool permits(const context& c) const
        { return !loading && !failed && !remote && (c.local() || (owned && c.private_setup())) &&
            c.campaign && (owned || c.native_story); }
        void raised(unsigned level) { if (level > pending_level) pending_level = level; }
        void persisted(unsigned level) { if (level >= pending_level) pending_level = 0; }
    };

    // Exact retail interoperability signatures, read-only verified against the
    // owned PC executable. The replacement starts AFTER the native hero/exempt
    // checks and ends BEFORE the native return-value calculation.
    inline constexpr std::array<limits_rules::guard, 38> guards{{
        {0x4b7e03, "83bea00200001d5f5d752df686ad020000207524", "native normal-hero and xpexempt eligibility"},
        {notification_site, "e8c45efbff8b108bc8ff927c02000084c07411e8a180fdff0fb64e1c8b10518bc8ff522c", "native profile notification only"},
        {0x4b7e3b, "0fb6461c2b4424045e59c3", "level recompute return and stack cleanup"},
        {profile_getter, "64a1000000008a0d88c972006aff68", "personal profile getter"},
        {0x6899c0, "80f24800", "profile monotonic setter slot +0x2c"},
        {0x48f280, "0fb681280200008b5424043bc27e09888128020000c20400889128020000c20400", "profile high-water max setter ABI"},
        {0x48f310, "83c1046828020000518b4c240ce8ce95fdffc20400", "profile serialization is manager+4, 0x228 bytes"},
        {0x6899a0, "d0fd4800", "profile deserialization slot +0x0c"},
        {0x46e458, "e8731a02008b1057538bc8ff520c", "campaign loader forwards original mode to profile deserializer"},
        {0x48fdf7, "83bc246002000002568bf17449", "profile load branches on remote mode 2"},
        {0x48fe1a, "8b8c246402000068280200008d460450e8118bfdff", "non-remote profile import targets manager+4"},
        {0x48fe5f, "8b8c246002000068280200008d54242852c784246002000000000000e8c08afdff", "remote profile import targets stack temporary"},
        {registry_getter, "64a1000000006aff68b132670050a10c777100", "campaign registry getter"},
        {0x685494, "b0b64400e0b64400", "registry hero count and index slots"},
        {0x44b6b0, "8b8130230100c3", "registry bounded roster count"},
        {0x44b6e0, "8b442404668b8441dc200100c20400", "registry roster index ABI"},
        {0x6854c0, "90b84400a0b74400c0b74400", "registry stats, has-stats and herostat slots"},
        {0x44b890, "0fbf4424046bc01c8b8408389b00008d510423821c9b000069c0f804000003c2c20400", "registry stats getter ABI"},
        {0x6854d0, "00b84400", "registry in-campaign slot +0x84"},
        {0x44b800, "0fbf4424046bc01c8a8408409b0000d0e82401c20400", "registry in-campaign predicate"},
        {game_getter, "64a1000000008a0d8ca072006aff68", "game getter"},
        {0x469d50, "8a8116060000c0e8022401c3", "native new-story progress flag read"},
        {session_getter, "64a1000000008a0db834a5006aff68", "native session getter"},
        {0x6123e4, "8a8620040000", "native online session byte"},
        {0x612963, "8a8621040000", "native hosted-here byte"},
        {0x6110b0, "8b8178020000c3", "native session participant count +0x278"},
        {0x611160, "8b817802000085c0750333c0c38b816c020000", "native first participant node +0x26c only when count is nonzero"},
        {seats_getter, "8a0d20c57200b80100000084c87510", "native competitive-seat manager getter"},
        {0x48dd25, "c700c4986800", "native seat manager class"},
        {0x689920, "f0db480000dc4800", "competitive-seat getter and setter slots +0x5c/+0x60"},
        {0x48dbf0, "8a41102401c3", "competitive-seat predicate reads +0x10 bit 0"},
        {0x4d094d, "e8eed4fbff8b108bc8ff525c84c00f8467010000", "competitive-seat predicate dominates fixed and ladder hero levels"},
        {0x4d0ac8, "803d28277800fe7546", "practice fixed-level branch requires native state 0xfe"},
        {0x4d1aac, "c60528277800fd", "Skirmish activation precedes native roster and hero creation"},
        {0x4d186b, "e8d0c5fbff8b106a008bc8ff5260", "teardown clears competitive-seat flag after Danger Room reset"},
        {0x4c8790, "a1541f7800c3", "native drmode getter"},
        {0x4c87d0, "a028277800c3", "native Danger Room state getter"},
        {0x4c8820, "a124487800c3", "native Skirmish kind getter"},
    }};
}
