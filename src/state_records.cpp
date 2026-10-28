#include "state_records.hpp"
#include "state_records_rules.hpp"
#include <cstring>
#include <memory>

namespace state_records
{
    bool contribute_w2(patch_transaction::contribution& c, std::string& why) try
    {
        using namespace state_records_rules;
        patch_transaction::contribution out;
        out.owner = "state_records W2";
        out.regions.push_back({"restore frame owner", restore_begin, restore_end, patch_transaction::sha256_of(restore_sha256)});
        for (const auto& g : invariants)
            out.guards.push_back({g.va, bytes(g.retail), g.what});
        for (const auto& s : sites)
        {
            out.guards.push_back({s.va, bytes(s.retail), s.what});
            out.writes.push_back({s.va, replacement(s), s.what});
        }
        // Shared ownership covers exceptions, pre-commit abandon and failed add.
        // A committed/uncertified contribution keeps the executable block alive.
        struct storage
        {
            void* p = nullptr;
            bool keep = false;
            ~storage() { if (p && !keep) VirtualFree(p, 0, MEM_RELEASE); }
        };
        auto owned = std::make_shared<storage>();
        constexpr std::size_t capacity = 0x1000;
        owned->p = VirtualAlloc(nullptr, capacity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!owned->p) { why = "state_records: cannot allocate executable caves"; return false; }
        const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(owned->p));
        std::size_t used = 0;
        for (const auto& s : caves)
        {
            const auto body = cave(s, base + static_cast<std::uint32_t>(used));
            if (used + body.size() > capacity) { why = "state_records: caves exceed allocation"; return false; }
            std::memcpy(static_cast<std::uint8_t*>(owned->p) + used, body.data(), body.size());
            // Full quoted block guard plus SHA-256 for each replaced function/block.
            out.guards.push_back({s.va, bytes(s.retail), s.what});
            out.regions.push_back({s.what, s.va, s.end, patch_transaction::sha256_of(s.sha256)});
            out.writes.push_back(patch_transaction::relocation(s.va, "e9", true, base + static_cast<std::uint32_t>(used), 0, s.what));
            used = (used + body.size() + 15) & ~std::size_t(15);
        }
        if (!FlushInstructionCache(GetCurrentProcess(), owned->p, used))
        { why = "state_records: cannot flush executable caves"; return false; }
        out.publish = [owned] { owned->keep = true; };
        out.retain = [owned] { owned->keep = true; };
        out.abandon = [owned] { owned->keep = false; };
        c = std::move(out);
        why.clear();
        return true;
    }
    catch (const std::exception& e) { why = std::string("state_records: ") + e.what(); return false; }
}
