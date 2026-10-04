#include "ladder_paths.hpp"
#include "ladder_paths_rules.hpp"
#include "ini.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include <cstdio>
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace ladder_paths
{
    namespace
    {
        using namespace ladder_paths_rules;
        constexpr address update_slot = 0x682dbc;
        constexpr address goal_slot = 0x682e40;
        constexpr address update_original = 0x41cef0;
        constexpr address goal_original = 0x424240;
        constexpr address scheduler = 0x475d10;
        constexpr address active_test = 0x475330;
        constexpr address evaluator_return = 0x475f60;
        constexpr address timer_return = 0x4762ac;
        constexpr const char* paths[] = {"x1_ladders/sewers/mp_cabinet", "x1_ladders/arbiter/mp_cabinet"};
        tracking tracker;
        std::atomic<unsigned> matched_updates{0}, scheduled{0}, evaluated{0};
        std::atomic<bool> enabled{false};

        using update_t = bool(__fastcall*)(void*, void*, float, bool);
        using goal_t = bool(__fastcall*)(void*, void*);
        using group_t = unsigned(__fastcall*)(void*, void*);
        using getter_t = void*(__cdecl*)();
        using lookup_t = void*(__fastcall*)(void*, void*, const char*);
        template <typename T> T at(address value) { return reinterpret_cast<T>(static_cast<std::uintptr_t>(value)); }
        address word(address value) { return *at<const address*>(value); }

        constexpr limits_rules::guard guards[] = {
            {update_slot, "f0ce4100", "character movement scheduling slot"},
            {goal_slot, "40424200", "character movement-goal slot"},
            {0x682d5c, "80db4100", "character resource-group getter"},
            {update_original, "8a4424088a91d8030000c0e00432c2241032d08891d80300", "original character scheduling ABI"},
            {goal_original, "83ec14568bf1d98680010000d90530006800", "original character goal test"},
            {scheduler, "d944240456d81d300068008b", "generic movement scheduler"},
            {active_test, "51568bf18b869001000085c0", "assigned-path activity test"},
            {0x55af80, "a144c27a0085c075", "resource-name map getter before adjustable allocation operand"},
            {0x55ab00, "8b44240483ec4485c0568bf1743d6a44", "non-loading resource lookup before adjustable entry operand"},
            {0x57af90, "8a0d40ca7d00b80100000084", "motion-path manager getter"},
            {0x475f5a, "ff9074010000", "only evaluator call allowed to override character goals"},
            {0x4762a6, "ff92f0000000", "native callback rescheduling and final disarm site"},
            {0x476220, "83ec1c568bf1e8359cfeffd8", "native movement callback"},
        };

        bool retail()
        {
            __try
            {
                for (const auto& g : guards)
                    if (!limits_rules::matches(at<const std::uint8_t*>(g.va), g.hex)) return false;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Reproduce the successful-lookup part of 0x57aab0, without its fallback
        // precache. The native name lookup follows the installed ResourceNames
        // layout, so no fixed heap-table offsets are copied into this feature.
        bool assigned_ladder(address actor)
        {
            __try
            {
                const auto assigned = word(actor + 0x190);
                if (!assigned) return false;
                auto* object = at<void*>(actor);
                const auto group = at<group_t>(word(word(actor) + 0x90))(object, nullptr);
                const auto manager = reinterpret_cast<address>(at<getter_t>(0x57af90)());
                auto* names = at<getter_t>(0x55af80)();
                if (!manager || !names || word(manager) != 0x69c50c) return false;
                const auto mask = word(manager + 0x1a0);
                if (mask != 15) return false;
                for (const auto* path : paths)
                {
                    for (const auto scope : {group, 2u})
                    {
                        char key[96]{};
                        std::snprintf(key, sizeof key, "%d:motionpaths/%s", static_cast<int>(scope), path);
                        const auto record = reinterpret_cast<address>(at<lookup_t>(0x55ab00)(names, nullptr, key));
                        if (record && manager + 4 + 16 * (word(record + 0xc) & mask) == assigned) return true;
                    }
                }
                return false;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        struct native_engine
        {
            tracking& tracker;
            bool original_update(address actor, float delay, bool force)
            { return at<update_t>(update_original)(at<void*>(actor), nullptr, delay, force); }
            bool is_ladder_path(address actor) { return assigned_ladder(actor); }
            bool path_active(address actor) { return at<goal_t>(active_test)(at<void*>(actor), nullptr); }
            identity key(address actor)
            { return {actor, word(actor + 0x1c), word(actor + 0x190), word(actor + 0x194)}; }
            void schedule_native(address actor, float delay, bool force)
            { ++scheduled; at<update_t>(scheduler)(at<void*>(actor), nullptr, delay, force); }
            bool original_goal_complete(address actor)
            { return at<goal_t>(goal_original)(at<void*>(actor), nullptr); }
        };

        bool __fastcall update_hook(void* object, void*, float delay, bool force)
        {
            native_engine engine{tracker};
            if (assigned_ladder(reinterpret_cast<address>(object))) ++matched_updates;
            return update(engine, reinterpret_cast<address>(object), delay, force,
                          reinterpret_cast<address>(_ReturnAddress()) == timer_return);
        }
        bool __fastcall goal_hook(void* object, void*)
        {
            native_engine engine{tracker};
            if (reinterpret_cast<address>(_ReturnAddress()) == evaluator_return && assigned_ladder(reinterpret_cast<address>(object))) ++evaluated;
            return goal_complete(engine, reinterpret_cast<address>(object),
                                 reinterpret_cast<address>(_ReturnAddress()) == evaluator_return);
        }
    }

    std::string status()
    {
        return std::string("ladder paths ") + (enabled.load() ? "on" : "off") +
               "; ladder matches " + std::to_string(matched_updates.load()) +
               "; ladder schedules " + std::to_string(scheduled.load()) +
               "; ladder evaluations " + std::to_string(evaluated.load());
    }

    void install(HMODULE game)
    {
        if (!ini::flag(L"Game", L"CharacterLadderPaths", false)) return;
        if (reinterpret_cast<std::uintptr_t>(game) != 0x400000 || !retail())
        {
            logger::write("ladder paths: unsupported code; native character behaviour retained");
            return;
        }
        DWORD old = 0, ignored = 0;
        auto* page = at<void*>(update_slot & ~address(0xfff));
        if (!VirtualProtect(page, 0x1000, PAGE_READWRITE, &old))
        {
            logger::write("ladder paths: vtable protection failed; feature disabled");
            return;
        }
        *at<address*>(update_slot) = reinterpret_cast<address>(&update_hook);
        *at<address*>(goal_slot) = reinterpret_cast<address>(&goal_hook);
        VirtualProtect(page, 0x1000, old, &ignored);
        enabled.store(true);
        logger::write("ladder paths: enabled native character scheduling/evaluation for the two generated XML1 ladder paths");
    }
}
