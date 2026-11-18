#include "danger_room_prizes.hpp"
#include "danger_room_prizes_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "item_capacity.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"
#include <utility>

namespace danger_room_prizes
{
    namespace
    {
        bool applied = false, wide_items = false;
        DWORD resume = 0x4ce09d, next_row = 0x4ce19a;

        bool __cdecl has_payload(const std::uint8_t* row)
        {
            return danger_room_prizes_rules::has_payload(row, wide_items);
        }

        const char* __fastcall overview(const std::uint8_t* row, void*, unsigned label,
                                        unsigned first, const char* hero)
        {
            using formatter = const char*(__fastcall*)(const std::uint8_t*, void*, unsigned, unsigned, const char*);
            // Retain the native formatter's scratch-buffer/preview resets even
            // for empty rows. Only its returned display text is filtered.
            const char* text = reinterpret_cast<formatter>(0x4ca3a0)(row, nullptr, label, first, hero);
            return has_payload(row) ? text : "";
        }

        // W2 relocates the two formatter blocks that read the now-16-bit item
        // index. Verify both installed jumps and their exact widened semantics;
        // a published W2 capacity alone does not excuse changed formatter code.
        bool wide_formatter_guards(patch_transaction::contribution& c)
        {
            using namespace patch_transaction;
            for (unsigned site : {0x4ca129u, 0x4ca138u})
            {
                std::array<std::uint8_t, 5> entry{};
                if (!read_memory(site, entry.data(), entry.size()) || entry[0] != 0xe9) return false;
                std::uint32_t delta = 0;
                std::memcpy(&delta, entry.data() + 1, 4);
                const auto target = site + 5 + delta;
                auto expected = bytes_of(site == 0x4ca129 ? "66837e08ff" : "0fb74e08518bc8");
                if (site == 0x4ca129)
                {
                    const auto branch = relocation(target + 5, "0f84", true, 0x4ca178, 0, "W2 empty item branch");
                    expected.insert(expected.end(), branch.bytes.begin(), branch.bytes.end());
                }
                const auto tail = relocation(target + static_cast<unsigned>(expected.size()), "e9", true,
                    site == 0x4ca129 ? 0x4ca12f : 0x4ca13e, 0, "W2 formatter return");
                expected.insert(expected.end(), tail.bytes.begin(), tail.bytes.end());
                std::vector<std::uint8_t> actual(expected.size());
                if (!read_memory(target, actual.data(), actual.size()) || actual != expected) return false;
                c.guards.push_back({site, std::vector<std::uint8_t>(entry.begin(), entry.end()), "published W2 formatter entry"});
                c.guards.push_back({target, std::move(expected), "verified W2 item-index width"});
            }
            return true;
        }

        // Enter only after the native awarded AND newly-awarded tests passed.
        // Empty rows consume the same one-shot presentation bit as the original
        // renderer; the earned bits, inventory and reward dispatcher stay native.
        __declspec(naked) void render_row()
        {
            __asm
            {
                pushfd
                pushad
                push ebp
                call has_payload
                add esp, 4
                test al, al
                jz empty_row
                popad
                popfd
                mov edx, dword ptr [0x6da754]
                jmp dword ptr [resume]
            empty_row:
                popad
                popfd
                and al, 0xf7
                mov byte ptr [ebp + 0x11], al
                jmp dword ptr [next_row]
            }
        }
    }

    void install(HMODULE game)
    {
        if (applied || !ini::flag(L"Game", L"DangerRoomPrizes", true)) return;
        if (reinterpret_cast<std::uintptr_t>(game) != 0x400000) return;
        patch_transaction::contribution c;
        c.owner = "Danger Room empty prize presentation";
        wide_items = item_capacity::installed_capacity() != 0;
        c.guards = {
            {0x4ce084, patch_transaction::bytes_of("8a4511a8010f840b010000a8080f84030100008b1554a76d0024f752b958357800884511"), "newly awarded row, original presentation-bit clear"},
            {0x4ce19a, patch_transaction::bytes_of("4383fb030f8cbcfeffff"), "next reward row"},
            {0x4ca0ef, patch_transaction::bytes_of("8b460485c0"), "XP payload"},
            {0x4ca178, patch_transaction::bytes_of("803e040f84bc000000"), "trait payload"},
            {0x4ca23d, patch_transaction::bytes_of("807e01060f8487000000"), "generated item payload"},
            {0x4ca2ce, patch_transaction::bytes_of("8a4611a802"), "level payload"},
            {0x4ca330, patch_transaction::bytes_of("a8047413"), "talent payload"},
            {0x4ca347, patch_transaction::bytes_of("66837e0a00"), "character payload"},
            {0x4cd154, patch_transaction::bytes_of("e847d2ffff"), "Titanium overview call"},
            {0x4cd184, patch_transaction::bytes_of("e817d2ffff"), "Adamantium overview call"},
            {0x4cd1ea, patch_transaction::bytes_of("e8b1d1ffff"), "Vibranium overview call"},
            {0x4ca3a0, patch_transaction::bytes_of("83ec54a1f8386f00"), "native overview formatter frame"},
            {0x4ca5eb, patch_transaction::bytes_of("c20c00"), "native overview formatter three arguments"},
        };
        if (wide_items)
        {
            if (!wide_formatter_guards(c))
            {
                logger::write("danger room prizes: refused changed W2 formatter; native results retained");
                return;
            }
        }
        else
        {
            c.guards.push_back({0x4ca129, patch_transaction::bytes_of("807e08ff7449"), "byte item payload"});
            c.guards.push_back({0x4ca138, patch_transaction::bytes_of("8a4e08518bc8"), "byte item lookup"});
        }
        c.writes = {patch_transaction::relocation(0x4ce097, "e9", true,
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&render_row)), 1, "skip empty prize rows")};
        for (unsigned site : {0x4cd154u, 0x4cd184u, 0x4cd1eau})
            c.writes.push_back(patch_transaction::relocation(site, "e8", true,
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&overview)), 0, "filter empty overview row"));
        patch_transaction::transaction tx;
        if (!tx.add(std::move(c))) return;
        const auto outcome = tx.commit();
        applied = outcome == patch_transaction::outcome::committed;
        if (outcome == patch_transaction::outcome::rollback_failed) game_version::refuse_startup();
        logger::write("danger room prizes: %s; empty result/overview award rows hidden, native grants unchanged (%s)",
            applied ? "installed" : "refused", tx.report().c_str());
    }
}
