#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Shared CAI contract. Stage 1 publishes the retail layout before its consumers install.
// Stage 2 must obtain a complete consumer registration set before relocating CAI.
namespace actor_ai
{
    struct descriptor
    {
        std::uint32_t stage, cai_object, primary_base, controller_stride, controllers;
        std::uint32_t team_base, team_stride, team_storage, team_policy, mask_offset_obj, cai_vtable_store_va;
    };
    inline constexpr descriptor stock{0,0x78ba10,0x78ba14,0x61c,32,0x797eb8,0x268,4,0,0xc4a0,0x525302};
    inline descriptor published = stock;
    struct observed_span {std::uint32_t va;std::vector<std::uint8_t> before,after;};
    inline std::vector<observed_span> observed_spans;
    inline const descriptor& installed() { return published; }
    inline std::uint32_t primary_live(const descriptor& d){return d.stage==2?0x24c28:0xc418;}
    inline std::uint32_t generations(const descriptor& d){return d.stage==2?0x24c2c:0xc41c;}
    inline std::uint32_t team_generations(const descriptor& d){return d.stage==2?0xe8ac:0x9cc;}
    inline std::uint32_t team_active(const descriptor& d){return d.stage==2?0xe89c:0x9c4;}
    inline std::uint32_t team_mask(const descriptor& d){return d.stage==2?0x337e4:0xce84;}
    enum class consumer : unsigned { power=1, weapon=2, semi_auto=4, mold=8, turrets=16 };
    inline unsigned consumers = 0;
    inline void register_consumer(consumer c) { consumers |= static_cast<unsigned>(c); }
    inline bool all_consumers_registered() { return consumers == 31; }
    inline void (*reset_controller_slot)(std::uint32_t) = nullptr;
    inline std::vector<std::uint8_t> dword(std::uint32_t value)
    { return {std::uint8_t(value),std::uint8_t(value>>8),std::uint8_t(value>>16),std::uint8_t(value>>24)}; }
    inline std::string hex32(std::uint32_t value)
    {
        constexpr char digits[]="0123456789abcdef";
        std::string result;
        for (const auto b:dword(value)) { result += digits[b>>4]; result += digits[b&15]; }
        return result;
    }
    inline std::string guard(std::uint32_t va, std::string_view original, const descriptor& d)
    {
        std::string result(original);
        const auto replace=[&](std::size_t at,std::uint32_t v) { result.replace(at,8,hex32(v)); };
        if (va==0x5252f7) replace(2,d.primary_base);
        if (va==0x5250bf) { result[4]="0123456789abcdef"[(d.controllers>>4)&15]; result[5]="0123456789abcdef"[d.controllers&15]; }
        if (va==0x525160) replace(22,d.mask_offset_obj);
        if (va==d.cai_vtable_store_va) replace(4,d.cai_object);
        // Some consumer guards include an adjacent handler observation span.
        // Compose only a certified observer's exact retail bytes; its detour
        // remains owned by the observer and is never overwritten by a consumer.
        if(d.stage==2)for(const auto& span:observed_spans){
            bool intersects=false,agrees=true;
            for(std::size_t i=0;i<span.before.size();++i)if(span.va+i>=va&&span.va+i<va+result.size()/2){
                intersects=true;const auto at=static_cast<std::size_t>(span.va+i-va)*2;
                const auto expected=hex32(span.before[i]).substr(0,2);agrees&=result.substr(at,2)==expected;
            }
            if(intersects&&agrees)for(std::size_t i=0;i<span.after.size();++i)if(span.va+i>=va&&span.va+i<va+result.size()/2)result.replace(static_cast<std::size_t>(span.va+i-va)*2,2,hex32(span.after[i]).substr(0,2));
        }
        return result;
    }
}
