#include "pool_counters.hpp"
#include "pool_counter_rules.hpp"
#include "pool_counter_observers.hpp"
#include "pool_counters_format.hpp"
#include "limits.hpp"
#include "model_pools.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "game_version.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>

namespace pool_counters {
namespace {
using namespace pool_counter_rules;
using u32=std::uint32_t;
constexpr std::size_t max_style_slots=32; // released FightStyles supports at most 32
constexpr std::size_t particle_operations=11;
static_assert([] {std::size_t n=0;for(const auto& s:pool_counter_sites::observers)if(s.condition==1)++n;return n;}()==particle_operations);
static_assert(pool_counter_sites::readers.size()+15+max_style_slots+particle_operations<=max_pools);
struct memory {
    bool read(u32 address,void* out,std::size_t n) {
        if(address<0x10000 || std::uint64_t(address)+n>0x100000000ull)return false;
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
           std::uint64_t(address)+n>reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize)return false;
        __try {std::memcpy(out,reinterpret_cast<const void*>(address),n);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
SRWLOCK lock=SRWLOCK_INIT;
tracker counters;
std::array<source,max_pools> sources{};
std::array<std::atomic<u32>,max_pools> refusals{};
std::array<u32,max_pools> seen_refusals{};
std::array<u32,max_pools> loading_refusals{};
std::array<snapshot,64> completed{};
unsigned head=0,tail=0;
HANDLE wake=nullptr,log_space=nullptr;
bool sampling=false,observers=false;
std::atomic<DWORD> game_thread{0};
range style_moves{},effect_primitives{};
std::size_t style_max=max_pools,primitive_max=max_pools,moves=max_pools;
std::size_t style_slots_begin=0,style_slots_count=0;
std::array<std::array<char,24>,max_style_slots> style_slot_names{};
struct particle_events {
    std::atomic<u32> calls{0},peak_batch{0},exceeded{0};
    u32 seen_calls=0,seen_exceeded=0,pending_exceeded=0;
    std::int32_t pending_peak=-1;
    std::size_t pool=max_pools;
};
std::array<particle_events,particle_operations> particle_counts{};
source move_free{};
std::array<std::size_t,10> model_ids{};
struct binding {unsigned pool=0,kind=0,owner_register=0;};
// kind 1: FX manager, 2: particle NULL pop, 3: component index,
// 4: curve 0 with native refusal flags, 5: style NULL, 6: native preflight false,
// 7: per-operation particle-array maximum/excess (never a refusal).
std::array<binding,pool_counter_sites::observers.size()> bindings{};

u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
std::size_t find(const char* name) {
    for(std::size_t i=0;i<counters.state.size;++i)if(!std::strcmp(counters.state.pools[i].name,name))return i;
    return max_pools;
}
void end_zone() {
    if(!counters.state.active)return;
    // A burst of zone changes applies backpressure at the zone boundary. The
    // consumer frees a ticket immediately after copying, before formatting/I/O.
    // This keeps every unload line without allocating on the sampling thread.
    if(log_space) {
        auto ready=WaitForSingleObject(log_space,0);
        if(ready==WAIT_TIMEOUT) {
            ReleaseSRWLockExclusive(&lock);
            ready=WaitForSingleObject(log_space,INFINITE);
            AcquireSRWLockExclusive(&lock);
        }
        if(ready==WAIT_OBJECT_0)completed[head++%completed.size()]=counters.state;
        else ++counters.state.dropped;
    } else if(head-tail<completed.size())completed[head++%completed.size()]=counters.state;
    else ++counters.state.dropped;
    counters.end();
    if(wake)SetEvent(wake);
}
DWORD WINAPI log_worker(void*) {
    std::array<u32,max_pools> logged{};
    u32 epoch=0;
    for(;;) {
        WaitForSingleObject(wake,100);
        snapshot s;bool finished=false;
        AcquireSRWLockExclusive(&lock);
        if(tail!=head){s=completed[tail++%completed.size()];finished=true;}
        else s=counters.state;
        ReleaseSRWLockExclusive(&lock);
        if(finished && log_space)ReleaseSemaphore(log_space,1,nullptr);
        if(finished)logger::write("%s",pool_counters_format::line(s).c_str());
        if(s.epoch!=epoch){logged.fill(0);epoch=s.epoch;}
        // Completed snapshots retain any refusal that happened between worker wakes.
        for(std::size_t i=0;i<s.size;++i)if(s.pools[i].refused_zone && !logged[i]) {
            logged[i]=s.pools[i].refused_zone;
            logger::write("pools %s: first refusal %s (zone count %u)",s.zone.data(),s.pools[i].name,s.pools[i].refused_zone);
        }
        if(finished)SetEvent(wake);
    }
}
void bind_observers() {
    using namespace pool_counter_sites;
    unsigned particle=0;
    for(std::size_t i=0;i<bindings.size();++i) {
        const auto& s=pool_counter_sites::observers[i];auto& b=bindings[i];
        b.pool=static_cast<unsigned>(find(s.name));
        const std::string_view name=s.name;
        if(s.condition==1) {
            b.kind=7;b.pool=particle;
            particle_counts[particle].pool=find(s.name);
            if(particle_counts[particle].pool<counters.state.size)
                counters.state.pools[particle_counts[particle].pool].particle_array=true;
            ++particle;continue;
        }
        if(name=="fx_entries" || name=="fx_128" || name=="fx_64" || name=="fx_300") {
            b.kind=1;b.owner_register=std::string_view(s.owner)=="edi"?0:1;
            b.pool=name=="fx_entries"?0:name=="fx_128"?1:name=="fx_64"?2:3;
        } else if(name=="particle_systems") {b.kind=2;b.pool=static_cast<unsigned>(model_ids[9]);}
        else if(name=="model_component")b.kind=3;
        else if(name=="curves")b.kind=4;
        else if(name=="styles")b.kind=5;
        else if(name=="model_preflight"){b.kind=6;b.pool=static_cast<unsigned>(model_ids[8]);}
    }
}
__forceinline void record_particle(particle_events& p,u32 count) {
    if(count>0x7fffffff)return; // native signed count <=0 does not enter its loop
    auto peak=p.peak_batch.load(std::memory_order_relaxed);
    while(peak<count && !p.peak_batch.compare_exchange_weak(peak,count,std::memory_order_relaxed)){}
    if(count>40)p.exceeded.fetch_add(1,std::memory_order_relaxed);
    p.calls.fetch_add(1,std::memory_order_release);
}
void sample_particle_events(bool entering_zone) {
    for(auto& p:particle_counts) {
        if(p.pool>=counters.state.size)continue;
        if(!counters.state.active && !entering_zone) {
            p.pending_peak=-1;p.pending_exceeded=0;
        }
        const auto calls=p.calls.load(std::memory_order_acquire);
        if(calls==p.seen_calls)continue;
        p.seen_calls=calls;
        const auto peak=static_cast<std::int32_t>(p.peak_batch.exchange(0,std::memory_order_relaxed));
        const auto exceeded=p.exceeded.load(std::memory_order_relaxed);
        const auto delta=exceeded-p.seen_exceeded;p.seen_exceeded=exceeded;
        auto& c=counters.state.pools[p.pool];
        counters.observe(p.pool,peak);c.exceeded+=delta;
        if(counters.state.active)c.exceeded_zone+=delta;
        else if(entering_zone) {
            p.pending_peak=(std::max)(p.pending_peak,peak);p.pending_exceeded+=delta;
        }
    }
}
void install_observers(const patch_transaction::hooks* hooks=nullptr) {
    if(!ini::flag(L"Test",L"PoolCounters",false))return;
    constexpr auto size=pool_counter_sites::observers.size()*pool_counter_observers::stride;
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!block){logger::write("pools: refusal observer allocation failed; sampling continues");return;}
    patch_transaction::contribution c;c.owner="PoolCounters";
    for(unsigned i=0;i<pool_counter_sites::observers.size();++i) {
        const auto& s=pool_counter_sites::observers[i];const auto before=patch_transaction::bytes_of(s.hex);
        const u32 at=address(block+i*pool_counter_observers::stride);
        const auto code=pool_counter_observers::stub(s,at,address(reinterpret_cast<void*>(&refused)),i);
        if(code.size()>pool_counter_observers::stride){VirtualFree(block,0,MEM_RELEASE);return;}
        std::memcpy(reinterpret_cast<void*>(at),code.data(),code.size());
        const bool call=std::string_view(s.name)=="model_preflight";
        c.guards.push_back({s.va,before,"pool refusal observer"});
        c.writes.push_back(patch_transaction::relocation(s.va,call?"e8":"e9",true,at,static_cast<unsigned>(before.size()-5),"pool refusal observer"));
    }
    DWORD old=0;
    if(!VirtualProtect(block,size,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),block,size)) {
        VirtualFree(block,0,MEM_RELEASE);logger::write("pools: observer code protection failed; sampling continues");return;
    }
    c.publish=[] {observers=true;};
    c.abandon=[block] {VirtualFree(block,0,MEM_RELEASE);};
    c.retain=[] {game_version::refuse_startup();};
    patch_transaction::transaction tx;tx.add(std::move(c));const auto result=tx.commit(hooks);
    logger::write("pools: refusal observers %s (%zu sites); %s",patch_transaction::name(result),pool_counter_sites::observers.size(),tx.report().c_str());
}
}

// Keep this integer-only and free of CRT, Win32, SIMD and floating-point operations:
// the branch observers preserve EFLAGS/GPRs and the callback changes DLL counters only.
void __cdecl refused(unsigned site,const unsigned* r) {
    if(site>=bindings.size())return;
    const auto& b=bindings[site];unsigned id=b.pool;
    if(b.kind==7) {record_particle(particle_counts[id],r[7]);return;}
    if(b.kind==1) {
        const auto owner=r[b.owner_register];
        if(owner==0x6f8ee8)id+=4;else if(owner!=0x6f3c50)return;
    } else if(b.kind==2 && r[1]!=0)return;
    else if(b.kind==3) {
        const auto kind=r[1];if(kind==0 || kind>8)return;
        id=static_cast<unsigned>(model_ids[kind-1]);
    } else if(b.kind==4 && (r[7]!=0 || !(r[8]&0x40)))return;
    else if(b.kind==5 && r[7]!=0)return;
    else if(b.kind==6 && (r[7]&0xff)!=0)return;
    if(id<max_pools)refusals[id].fetch_add(1,std::memory_order_relaxed);
}

bool enabled(){return sampling;}
void install(HMODULE game) {
    if(address(game)!=0x400000)return;
    memory mem;unsigned char signature[7]{};
    if(!mem.read(0x4d6570,signature,sizeof(signature)) || std::memcmp(signature,"\x8b\x91\x90\xc0\x00\x00\x33",7))return;
    for(const auto& r:pool_counter_sites::readers) {
        const auto i=counters.add(r.name,r.cap);sources[i]={r.root,r.offset,r.cap,0,r.indirect,false};
    }
    sources[find("vectors")].guard=0x70b648;
    const auto model=model_pools::diagnostic_sources();
    constexpr const char* names[]{"transforms","attributes","shader_fx","igb_models","actors","proc_geometry","particle_clouds","movie","models","particle_systems"};
    for(unsigned k=0;k<10;++k){const auto i=counters.add(names[k],model[k].cap);model_ids[k]=i;sources[i]=model[k];}
    const auto limits=limits::diagnostic_sources();
    for(const auto& p:std::array<std::pair<const char*,source>,3>{{{"styles",limits.styles},{"events",limits.events},{"curves",limits.curves}}}) {
        const auto i=counters.add(p.first,p.second.cap);sources[i]=p.second;
    }
    style_moves=limits.style_moves;style_max=counters.add("moves_per_style",150);
    primitive_max=counters.add("prims_per_effect",10);
    // Definition pool bitmap B, not its constructed bitmap A (freed slots retain old record contents).
    effect_primitives={0x6f3c3c,0x4874,0x4874+0x3e0c,0x44,0x30,220};
    moves=find("moves");move_free={0x789e1c,0x1aec8,332,0,true,false};
    style_slots_begin=counters.state.size;
    style_slots_count=(std::min)(static_cast<std::size_t>(style_moves.cap),max_style_slots);
    for(std::size_t i=0;i<style_slots_count;++i) {
        std::snprintf(style_slot_names[i].data(),style_slot_names[i].size(),"style_moves_%zu",i);
        counters.add(style_slot_names[i].data(),150);
    }
    if(ini::flag(L"Test",L"PoolCounters",false))
        for(const auto& s:pool_counter_sites::observers)if(s.condition==1)counters.add(s.name,40);
    bind_observers();
    sampling=ini::flag(L"Test",L"PoolSampling",true);
    install_observers();
    if(sampling)logger::write("pools: read-only sampling enabled; %zu pools; refusal observers %s",counters.state.size,observers?"on":"off");
}
void start_logging() {
    if(!sampling || wake)return;
    // The proxy is process-lived. Pin only after the complete DLL startup was
    // accepted, so a rejected LoadLibrary never leaves a worker in freed code.
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&start_logging),&self))return;
    wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    log_space=CreateSemaphoreW(nullptr,static_cast<LONG>(completed.size()),static_cast<LONG>(completed.size()),nullptr);
    const auto thread=wake && log_space?CreateThread(nullptr,0,&log_worker,nullptr,0,nullptr):nullptr;
    if(thread)CloseHandle(thread);
    else {
        if(wake)CloseHandle(wake);if(log_space)CloseHandle(log_space);wake=log_space=nullptr;
        logger::write("pools: log worker unavailable; pipe sampling continues");
    }
}

void sample() {
    if(!sampling)return;
    const auto tid=GetCurrentThreadId();
    DWORD unbound=0;
    game_thread.compare_exchange_strong(unbound,tid,std::memory_order_relaxed);
    if(tid!=game_thread.load(std::memory_order_relaxed))return; // never follow owners from a second renderer thread
    memory mem;unsigned char loading=0;std::array<char,128> zone{};
    if(!mem.read(0x72a798,&loading,1) || !mem.read(0x72a758,zone.data(),zone.size()))return;
    zone.back()=0;
    // Normalize before comparing with the stored name (and before it reaches a log/JSON).
    for(auto& c:zone){if(!c)break;const auto b=static_cast<unsigned char>(c);if(b<32 || b>=127 || c=='"' || c=='\\')c='_';}
    AcquireSRWLockExclusive(&lock);
    const bool busy=(loading&3)!=0;
    sample_particle_events(busy || zone[0]);
    // Drain the last simulation step before retiring its zone. While inactive,
    // preserve loading requests for the destination instead of discarding them
    // or charging them all to whichever zone is sampled next.
    for(std::size_t i=0;i<counters.state.size;++i) {
        const auto current=refusals[i].load(std::memory_order_relaxed);
        const auto delta=current-seen_refusals[i];seen_refusals[i]=current;
        counters.refusal(i,delta);
        if(!counters.state.active && (busy || zone[0]))loading_refusals[i]+=delta;
        else if(!counters.state.active && !busy)loading_refusals[i]=0;
    }
    if(counters.state.active && (busy || !zone[0] || std::strcmp(counters.state.zone.data(),zone.data())))end_zone();
    if(busy){ReleaseSRWLockExclusive(&lock);return;}
    if(zone[0] && !counters.state.active) {
        counters.begin(zone.data());
        for(std::size_t i=0;i<counters.state.size;++i) {
            counters.state.pools[i].refused_zone=loading_refusals[i];loading_refusals[i]=0;
        }
        for(auto& p:particle_counts)if(p.pool<counters.state.size) {
            auto& c=counters.state.pools[p.pool];c.live=-1;
            if(p.pending_peak>=0)counters.observe(p.pool,p.pending_peak);
            c.exceeded_zone=p.pending_exceeded;
            p.pending_peak=-1;p.pending_exceeded=0;
        }
    }
    for(std::size_t i=0;i<style_slots_begin;++i) {
        if(i!=style_max && i!=primitive_max)counters.observe(i,read_source(mem,sources[i]));
    }
    counters.observe(style_max,largest(mem,style_moves));
    for(std::size_t i=0;i<style_slots_count;++i)
        counters.observe(style_slots_begin+i,range_item(mem,style_moves,static_cast<u32>(i)));
    counters.observe(primitive_max,largest(mem,effect_primitives));
    // Native FightMoves has an unchecked pop: negative free/live>cap is aliasing, not a refusal.
    u32 owner=0,raw_free=0;
    if(mem.read(move_free.root,&owner,4) && owner>=0x10000 && mem.read(owner+move_free.offset,&raw_free,4) && static_cast<std::int32_t>(raw_free)<0)
        counters.state.pools[moves].corrupt=true;
    ReleaseSRWLockExclusive(&lock);
}
std::string json() {
    snapshot copy;
    AcquireSRWLockShared(&lock);copy=counters.state;ReleaseSRWLockShared(&lock);
    return pool_counters_format::json(copy,observers);
}
}
