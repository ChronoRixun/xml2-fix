#pragma once
#include <array>
#include "patch_transaction.hpp"
#include <cstdint>
#include <cstring>
#include <vector>

// All callers install at process startup, before any game thread executes.
// Memory is supplied separately so failure/rollback can be exercised without
// patching a game. Every page is acquired before the first byte changes.
namespace xml1_hook_transaction {
struct patch {
    std::uint32_t address;
    std::vector<unsigned char> before, after;
};
template<class Memory> bool apply(Memory& mem, const std::vector<patch>& writes, bool* safe_to_run=nullptr) {
    if(safe_to_run)*safe_to_run=true;
    struct page { std::uint32_t address, protection; };
    std::vector<page> pages;
    for (const auto& p : writes) {
        if (p.before.empty() || p.before.size()!=p.after.size() ||
            p.address > UINT32_MAX-p.before.size()) return false;
        std::vector<unsigned char> bytes(p.before.size());
        if (!mem.read(p.address,bytes.data(),bytes.size()) || bytes!=p.before) return false;
        const auto last=(p.address+static_cast<std::uint32_t>(p.before.size())-1)&~4095u;
        for (auto a=p.address&~4095u;;a+=4096) {
            bool found=false;
            for (const auto& page:pages) if(page.address==a)found=true;
            if(!found)pages.push_back({a,0});
            if(a==last)break;
        }
    }
    std::vector<patch_transaction::write> diagnostic_before, diagnostic_after;
    if(patch_transaction::record_verified_writes){
        for(const auto& p:writes){diagnostic_before.push_back({p.address,p.before,"legacy transaction"});diagnostic_after.push_back({p.address,p.after,"legacy transaction"});}
        patch_transaction::verified_originals.reserve(patch_transaction::verified_originals.size()+writes.size());
        patch_transaction::verified_writes.reserve(patch_transaction::verified_writes.size()+writes.size());
    }
    std::size_t acquired=0;
    for(;acquired<pages.size();++acquired) {
        if(!mem.acquire(pages[acquired].address,pages[acquired].protection))break;
    }
    bool ok=acquired==pages.size();
    if(ok) {
        for(const auto& p:writes) if(!mem.write(p.address,p.after.data(),p.after.size())){ok=false;break;}
        for(const auto& p:writes) {
            std::vector<unsigned char> bytes(p.after.size());
            if(!mem.read(p.address,bytes.data(),bytes.size()) || bytes!=p.after)ok=false;
        }
        if(ok)ok=mem.flush();
        if(!ok) {
            // All destinations are still writable, including overlapping pages.
            bool restored=true;
            for(const auto& p:writes)if(!mem.write(p.address,p.before.data(),p.before.size()))restored=false;
            for(const auto& p:writes){
                std::vector<unsigned char> bytes(p.before.size());
                if(!mem.read(p.address,bytes.data(),bytes.size())||bytes!=p.before)restored=false;
            }
            if(!mem.flush())restored=false;
            if(!restored&&safe_to_run)*safe_to_run=false;
        }
    }
    while(acquired) {
        --acquired;
        if(!mem.restore(pages[acquired].address,pages[acquired].protection)){
            ok=false;if(safe_to_run)*safe_to_run=false;
        }
    }
    if(ok&&patch_transaction::record_verified_writes){
        for(auto& p:diagnostic_before)patch_transaction::verified_originals.push_back(std::move(p));
        for(auto& p:diagnostic_after)patch_transaction::verified_writes.push_back(std::move(p));
    }
    return ok;
}
inline std::vector<unsigned char> pointer_bytes(const void* p) {
    const auto value=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
    std::vector<unsigned char> bytes(4);
    std::memcpy(bytes.data(),&value,4);
    return bytes;
}
}
