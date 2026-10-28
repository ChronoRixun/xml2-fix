#include "actor_pools.hpp"
#include "actor_pools_plan.hpp"
#include "actor_ai_layout.hpp"
#include "e4_core_state.hpp"
#include "game_version.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>

#ifdef XML2_ACTOR_POOLS_TEST
namespace actor_pools_test
{
    void* allocate(void*,std::size_t,DWORD,DWORD);
    const patch_transaction::hooks* transaction_hooks();
}
#define VirtualAlloc actor_pools_test::allocate
#endif

namespace actor_pools
{
    namespace
    {
        struct counters { std::atomic<unsigned> peak{0},would_refuse{0},refused{0},takes{0}; };
        std::array<counters,3> counts;
        bool attempted=false,applied=false;
        std::string last_sample;
        void __cdecl observe(unsigned pool,unsigned live)
        {
            if(pool==3) { if(actor_ai::reset_controller_slot)actor_ai::reset_controller_slot(live);return; }
            if(pool>=counts.size())return;
            auto& c=counts[pool];
            c.takes.fetch_add(1,std::memory_order_relaxed);
            if(live>=32)c.would_refuse.fetch_add(1,std::memory_order_relaxed);
            if(live>=124)c.refused.fetch_add(1,std::memory_order_relaxed);
            unsigned peak=c.peak.load(std::memory_order_relaxed),next=live<124?live+1:live;
            while(peak<next&&!c.peak.compare_exchange_weak(peak,next,std::memory_order_relaxed)){}
        }
        std::uint32_t address(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
        void __cdecl observe_event(unsigned post,unsigned brain,unsigned return_address){if(e4_core_state::installed&&e4_core_state::on_event_borrow)e4_core_state::on_event_borrow(brain,post!=0,return_address);}
    }
    void install(HMODULE game)
    {
        if(attempted)return;
        attempted=true;
        const auto setting=ini::text(L"Limits",L"ActorPools");
        if(!setting)return;
        const auto register_result=[] { if(!e4_core_state::requested)gameplay_agreement::register_rule(gameplay_capabilities::actor_ai_id,1,{32,4,124u|(124u<<16),1},applied); };
        // Not the game (XMen2.exe isn't at 0x400000): nothing can be patched, so no entry, as the [Limits] capacities
        // (409f714) - an unverified entry there would demand the network exclusion hook and refuse that process.
        if(reinterpret_cast<std::uintptr_t>(game)!=0x400000)
        {
            logger::write("ActorPools: [Limits] ActorPools=%s ignored (not the game image)",setting->c_str());
            return;
        }
        if(*setting!="124")
        {
            logger::write("ActorPools: [Limits] ActorPools=%s refused (only 124 is supported on the retail image); stock offline, online unverified",setting->c_str());
            register_result();return;
        }
        // Pool getters/CRT construction must not have run before the atomic install.
        std::uint32_t path=0,shadow=0,damage=0;
        if(!patch_transaction::read_memory(0x72c9c0,&path,4)||!patch_transaction::read_memory(0x767388,&shadow,4)||
           !patch_transaction::read_memory(0x753f60,&damage,4)||path||shadow||damage)
        {
            logger::write("ActorPools: pools already constructed or unreadable; nothing written; online unverified");register_result();return;
        }
        auto* block=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x3674,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        auto* caves=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        const auto release=[&] { if(block)VirtualFree(block,0,MEM_RELEASE);if(caves)VirtualFree(caves,0,MEM_RELEASE); };
        const auto p=actor_pools_plan::make(address(block),address(caves),address(reinterpret_cast<const void*>(&observe)));
        if(!block||!caves||!p)
        { release();logger::write("ActorPools: allocation or complete ledger preparation failed; nothing written; online unverified");register_result();return; }
        std::memcpy(caves,p->code.data(),p->code.size());
        DWORD old=0;
        if(!VirtualProtect(caves,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),caves,p->code.size()))
        { release();logger::write("ActorPools: cave protection/flush failed; nothing written; online unverified");register_result();return; }
        auto c=p->contribution;
        bool retained=false;
        c.retain=[&] { retained=true;game_version::refuse_startup(); };
        c.publish=[&] { applied=true;actor_ai::published=actor_ai::stock;actor_ai::published.stage=1; };
        patch_transaction::transaction transaction;transaction.add(std::move(c));
        const auto result=transaction.commit(
#ifdef XML2_ACTOR_POOLS_TEST
            actor_pools_test::transaction_hooks()
#endif
        );
        if(!applied&&!retained)release();
        logger::write("ActorPools: %s (%s); paths/shadows/delayed damage %u; actors/controllers 32, teams 4; block 0x%08lX",
                      patch_transaction::name(result),transaction.report().c_str(),applied?124:32,applied?address(block):0);
        register_result();
    }
    bool contribute(patch_transaction::transaction& tx,HMODULE game)
    {
        if(attempted||!e4_core_state::prepared||address(game)!=0x400000)return false;
        attempted=true;
        struct storage {
            std::uint8_t* block=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x3674,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            std::uint8_t* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            bool retained=false;
            ~storage(){if(!retained){if(block)VirtualFree(block,0,MEM_RELEASE);if(code)VirtualFree(code,0,MEM_RELEASE);}}
        };
        auto s=std::make_shared<storage>();
        const auto p=actor_pools_plan::make(address(s->block),address(s->code),address(reinterpret_cast<const void*>(&observe)),entity_capacity_b_sites::sites,true,address(reinterpret_cast<const void*>(&observe_event)));
        if(!s->block||!s->code||!p)return false;
        // The lazy pools must not exist before the shared startup transaction.
        auto c=p->contribution;
        c.guards.push_back({0x72c9c0,{0,0,0,0},"ActorPools path not created"});
        c.guards.push_back({0x767388,{0,0,0,0},"ActorPools shadow not created"});
        c.guards.push_back({0x753f60,{0,0,0,0},"ActorPools damage not live"});
        std::memcpy(s->code,p->code.data(),p->code.size());DWORD prior=0;
        if(!VirtualProtect(s->code,8192,PAGE_EXECUTE_READ,&prior)||!FlushInstructionCache(GetCurrentProcess(),s->code,p->code.size()))return false;
        c.publish=[s]{s->retained=true;applied=true;actor_ai::published=actor_ai::stock;actor_ai::published.stage=1;};
        c.retain=[s]{s->retained=true;game_version::refuse_startup();};
        return tx.add(std::move(c));
    }
    std::string telemetry_json()
    {
        std::string out="{\"installed\":"+std::string(applied?"true":"false");
        constexpr std::array<const char*,3> names{"path","shadow","delayed_damage"};
        for(unsigned i=0;i<counts.size();++i)
        {
            const auto& c=counts[i];
            out+=",\""+std::string(names[i])+"\":{\"peak\":"+std::to_string(c.peak.load())+",\"would_refuse\":"+std::to_string(c.would_refuse.load())+
                 ",\"refused\":"+std::to_string(c.refused.load())+",\"takes\":"+std::to_string(c.takes.load())+"}";
        }
        return out+"}";
    }
    void sample()
    {
        if(!applied)return;
        const auto now=telemetry_json();
        if(now!=last_sample) { logger::write("ActorPools telemetry: %s",now.c_str());last_sample=now; }
    }
}
