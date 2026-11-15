#include "profile_progress.hpp"
#include "profile_progress_rules.hpp"
#include "skirmish_rules.hpp"
#include "log.hpp"

#include <Windows.h>
#include <array>
#include <cstring>

namespace profile_progress
{
    namespace
    {
        using namespace profile_progress_rules;
        using getter_t = void*(__cdecl*)();
        using count_t = int(__fastcall*)(void*, void*);
        using index_t = short(__fastcall*)(void*, void*, int);
        using predicate_t = bool(__fastcall*)(void*, void*, short);
        using stats_t = void*(__fastcall*)(void*, void*, short);
        using set_t = void(__fastcall*)(void*, void*, int);
        lifetime progress;
        bool active = false;
        unsigned history_before_load = 0;
        unsigned pending_before_load = 0;
        void* history_profile = nullptr;
        bool history_known = false;
        std::array<std::uint8_t,profile_size> snapshot_before_load{}, fallback_profile{};
        bool snapshot_known = false, fallback_active = false;
        void* fallback_owner = nullptr;
        getter_t get_profile = reinterpret_cast<getter_t>(profile_getter);
        getter_t get_registry = reinterpret_cast<getter_t>(registry_getter);
        getter_t get_game = reinterpret_cast<getter_t>(game_getter);
        getter_t get_session = reinterpret_cast<getter_t>(session_getter);

        template<class T> T read(const void* object, std::size_t offset)
        {
            T value{};
            std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset, sizeof value);
            return value;
        }
        template<class F> F method(void* object, unsigned slot)
        { return reinterpret_cast<F>((*static_cast<std::uint32_t**>(object))[slot / 4]); }

        bool native_context(context& out)
        {
            __try
            {
                const auto* game = static_cast<const std::uint8_t*>(get_game());
                const auto* session = static_cast<const std::uint8_t*>(get_session());
                if (!game || !session || read<std::uint32_t>(game, 0) != game_vtable ||
                    read<std::uint32_t>(session, 0) != session_vtable) return false;
                const auto danger = *reinterpret_cast<const std::uint8_t*>(skirmish_rules::danger_state);
                const auto kind = *reinterpret_cast<const std::uint32_t*>(skirmish_rules::kind);
                const auto drmode = *reinterpret_cast<const std::uint32_t*>(skirmish_rules::drmode);
                // Exclude preparation and teardown as well as active Skirmish:
                // its temporary levels must never become personal history.
                out.campaign = drmode == 0 && danger == 0xff && kind == 0;
                out.online = session[0x420] != 0;
                out.host = session[0x421] != 0;
                out.native_story = (game[0x616] & 4) != 0;
                out.known = true;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool (*read_context)(context&) = &native_context;

        bool hero_level(void* stats, unsigned& level)
        {
            if (!stats) return false;
            level = read<std::uint8_t>(stats, stats_level);
            return eligible_hero(read<std::uint32_t>(stats, stats_kind), read<std::uint8_t>(stats, stats_flags), level);
        }

        bool raise(unsigned level)
        {
            void* profile = get_profile();
            if (!profile || read<std::uint32_t>(profile, 0) != profile_vtable ||
                (*static_cast<std::uint32_t**>(profile))[0x2c / 4] != 0x48f280) return false;
            const auto before = read<std::uint8_t>(profile, highest_level);
            if (level <= before || level > max_level) return false;
            method<set_t>(profile, 0x2c)(profile, nullptr, static_cast<int>(level));
            if (read<std::uint8_t>(profile, highest_level) != level) return false;
            progress.raised(level);
            logger::write("profile progress: owned campaign high-water %u -> %u; profile persistence queued", unsigned(before), level);
            return true;
        }

        void __cdecl observe_level(void* stats)
        {
            if (!active) return;
            __try
            {
                context c{};
                unsigned level = 0;
                if (read_context(c) && progress.permits(c) && hero_level(stats, level))
                {
                    // The native new-story predicate already authorizes this
                    // local campaign. Retain that proof across a hosted restart
                    // even when its hero has not exceeded personal history yet.
                    if (c.native_story) progress.owned = true;
                    // A new eligible campaign notification establishes live
                    // personal metadata again after an earlier rejected load.
                    fallback_active = false;
                    raise(level);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { /* unknown memory leaves personal progress alone */ }
        }

        void reconcile_load()
        {
            __try
            {
                context c{};
                if (!read_context(c) || !progress.owned || !progress.permits(c)) return;
                void* registry = get_registry();
                if (!registry || read<std::uint32_t>(registry, 0) != registry_vtable) return;
                const int count = method<count_t>(registry, 0x48)(registry, nullptr);
                if (count < 0 || count > static_cast<int>(max_roster)) return;
                unsigned highest = 0;
                for (int i = 0; i < count; ++i)
                {
                    const short index = method<index_t>(registry, 0x4c)(registry, nullptr, i);
                    if (index <= 0 || index >= static_cast<int>(max_roster)) return;
                    if (!method<predicate_t>(registry, 0x78)(registry, nullptr, index) ||
                        !method<predicate_t>(registry, 0x7c)(registry, nullptr, index) ||
                        !method<predicate_t>(registry, 0x84)(registry, nullptr, index)) continue;
                    unsigned level = 0;
                    if (hero_level(method<stats_t>(registry, 0x74)(registry, nullptr, index), level) && level > highest)
                        highest = level;
                }
                // Read the entire roster before publishing anything: a failed
                // index/read cannot partially promote this personal profile.
                if (highest) raise(highest);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { /* no partial profile promotion */ }
        }

        void reject_imported_history()
        {
            if (!history_known) return;
            __try
            {
                void* profile = get_profile();
                if (profile == history_profile && profile && read<std::uint32_t>(profile, 0) == profile_vtable)
                {
                    // Non-mode-2 native loads copy the campaign's profile block
                    // before later reconstruction steps can fail. This byte is
                    // uncommitted import, not newly earned personal progress.
                    static_cast<std::uint8_t*>(profile)[highest_level] = static_cast<std::uint8_t>(history_before_load);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { /* never write a stale or unreadable profile */ }
        }

        bool history_target(unsigned& level)
        {
            if (!history_known) return false;
            __try
            {
                void* profile = get_profile();
                if (!profile || profile != history_profile || read<std::uint32_t>(profile, 0) != profile_vtable) return false;
                level = read<std::uint8_t>(profile, highest_level);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    std::vector<std::uint8_t> notification_bytes()
    {
        // At this point native EDI/EBP have already been restored. ESI is CStats;
        // cdecl preserves it, and the original level-delta return follows intact.
        std::vector<std::uint8_t> bytes(notification_size, 0x90);
        bytes[0] = 0x56; // push esi
        bytes[1] = 0xe8;
        const auto target = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&observe_level));
        const std::uint32_t rel = target - (notification_site + 6);
        std::memcpy(bytes.data() + 2, &rel, 4);
        bytes[6] = 0x83; bytes[7] = 0xc4; bytes[8] = 4;
        return bytes;
    }

    void activate() { active = true; }
    void reset_campaign()
    {
        if (active)
        {
            progress.reset(); history_before_load = pending_before_load = 0;
            history_profile = nullptr; history_known = false;
            snapshot_known = false;
        }
    }
    void begin_load()
    {
        if (!active) return;
        context prior{};
        if (read_context(prior) && progress.permits(prior) && prior.native_story)
            progress.owned = true; // trusted pre-load Begin Story state, never an incoming transfer's flag
        pending_before_load = progress.pending_level;
        progress.begin_load();
        history_before_load = 0;
        history_profile = nullptr;
        history_known = false;
        snapshot_known = false;
        __try
        {
            void* profile = get_profile();
            if (profile && read<std::uint32_t>(profile, 0) == profile_vtable)
            {
                history_before_load = read<std::uint8_t>(profile, highest_level);
                history_profile = profile;
                history_known = true;
                if (fallback_active && fallback_owner != profile)
                {
                    // The suspended personal snapshot belongs to another
                    // object; it cannot establish pending progress here.
                    pending_before_load = 0;
                    fallback_active = false;
                }
                if (pending_before_load && history_before_load >= pending_before_load)
                {
                    if (fallback_active && fallback_owner == profile) snapshot_before_load = fallback_profile;
                    else std::memcpy(snapshot_before_load.data(),static_cast<const std::uint8_t*>(profile)+4,profile_size);
                    snapshot_known = true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { /* no inferred personal history */ }
    }
    void finish_load(int mode, bool validated_owned_disk, bool native_ok)
    {
        if (!active) return;
        context c{};
        read_context(c);
        progress.finish_load(mode, validated_owned_disk, native_ok, c);
        unsigned imported = 0;
        const bool same_profile = history_target(imported);
        if (!same_profile) { progress.owned = false; progress.failed = true; }
        const bool accepted_owned = native_ok && mode != 1 && validated_owned_disk && mode != 2 &&
            progress.owned && progress.permits(c) && same_profile;
        if (mode != 1 && mode != 2 && !accepted_owned) reject_imported_history();
        if (accepted_owned)
        {
            fallback_active = false; // successfully applied owned metadata is now authoritative
            // The campaign's embedded PRFP must not replace greater personal
            // history that was already present before the loader ran.
            __try { if (history_before_load) raise(history_before_load); }
            __except (EXCEPTION_EXECUTE_HANDLER) { /* leave unreadable profile alone */ }
            reconcile_load();
            // Native import may already have supplied the desired value. It
            // still has to reach the personal profile file when no setter ran.
            unsigned final_level = 0;
            if (history_target(final_level) && final_level > history_before_load && final_level <= max_level)
                progress.raised(final_level);
        }
        // A load may invalidate new campaign notifications while already earned
        // personal metadata remains unsaved. Restore only that captured queue,
        // after verifying its original profile and retained/rolled-back byte.
        // A remote guest never carries a deferred write across the transition.
        unsigned retained_level = 0;
        const bool retained_path = accepted_owned || (mode == 1 && native_ok) ||
            (mode == 2 && native_ok && progress.owned && c.online && c.host) ||
            (mode != 1 && !native_ok) || (mode != 1 && mode != 2 && !accepted_owned);
        if (pending_before_load && same_profile && c.local() && !progress.remote && retained_path &&
            history_target(retained_level) && retained_level >= pending_before_load &&
            (accepted_owned || retained_level == history_before_load) &&
            (accepted_owned || mode == 1 || mode == 2 || snapshot_known))
        {
            progress.raised(pending_before_load);
            if (mode != 1 && mode != 2 && !accepted_owned && snapshot_known)
            {
                // Persist previously earned personal metadata without adopting
                // unrelated fields from the rejected native profile import.
                fallback_profile = snapshot_before_load;
                fallback_owner = history_profile;
                fallback_active = true;
            }
        }
        if (!progress.pending_level) fallback_active = false;
        history_before_load = 0;
        pending_before_load = 0;
        history_profile = nullptr;
        history_known = false;
        snapshot_known = false;
    }

    bool pending_profile(std::uint8_t* bytes, std::size_t size)
    {
        if (!active || !progress.pending_level || progress.loading || size != profile_size || !bytes) return false;
        __try
        {
            void* profile = get_profile();
            if (!profile || read<std::uint32_t>(profile, 0) != profile_vtable ||
                read<std::uint8_t>(profile, highest_level) < progress.pending_level) return false;
            if (fallback_active)
            {
                if (profile != fallback_owner || fallback_profile[serialized_highest_level] < progress.pending_level) return false;
                std::memcpy(bytes,fallback_profile.data(),profile_size);
            }
            else std::memcpy(bytes, static_cast<const std::uint8_t*>(profile) + 4, profile_size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void profile_written(const std::uint8_t* bytes, std::size_t size)
    {
        if (active && bytes && size == profile_size)
        {
            progress.persisted(bytes[serialized_highest_level]);
            if (!progress.pending_level) fallback_active = false;
        }
    }
}
