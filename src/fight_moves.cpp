#include "fight_moves.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "game_version.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include <cstdio>
#include <atomic>

namespace fight_moves
{
namespace
{
using namespace fight_moves_layout;
bool attempted=false,active=false;
DWORD published_count=stock_count;
void* cave=nullptr;
struct atomic_counts { std::atomic<std::uint64_t> refused{0},map_refused{0},aliasing{0}; } totals;
// A style is an address in the fixed style registry. Bound the once-only log book
// without allocating in the admission callback; 2048 exceeds every supported style cap.
struct notice { DWORD style=0;unsigned reasons=0; };
std::array<notice,2048> notices{};
DWORD address(const void* p){return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p));}
bool read(DWORD va,DWORD& out){return patch_transaction::read_memory(va,&out,sizeof out);}
void sample(DWORD pool)
{
    if(!pool)return;
    DWORD live=0,free=0;const auto l=layout_for(published_count);
    if(read(pool+l.live,live)&&read(pool+l.free,free)&&(live>published_count||static_cast<std::int32_t>(free)<0))totals.aliasing.fetch_add(1,std::memory_order_relaxed);
}
void refusal(DWORD style,unsigned reason)
{
    auto entry=std::find_if(notices.begin(),notices.end(),[style](auto& n){return n.style==style||!n.style;});
    if(entry==notices.end()||(entry->reasons&reason))return;
    entry->style=style;entry->reasons|=reason;
    logger::write("limits: FightMoves: style 0x%08lX refused (%s); moves capacity %lu, per-style map 150",style,reason==1?"pool full":"style map full",published_count);
}
int __cdecl admit(void* style)
{
    DWORD pool=0,live=0;
    read(pool_pointer,pool);sample(pool);
    // Pool priority makes each refusal count exactly once if both are saturated.
    if(pool&&read(pool+layout_for(published_count).live,live)&&live>=published_count){totals.refused.fetch_add(1,std::memory_order_relaxed);refusal(address(style),1);return 0;}
    if(read(address(style)+style_live,live)&&live>=style_capacity){totals.map_refused.fetch_add(1,std::memory_order_relaxed);refusal(address(style),2);return 0;}
    return 1;
}
std::vector<std::uint8_t> entry_code(DWORD where)
{
    // Save every integer register and flags across the helper. No native prologue
    // executes on refusal; both stack arguments are consumed by RET 8.
    auto b=patch_transaction::bytes_of("9c6051e80000000083c40485c0740d619d535556578bd9e900000000619d32c0c20800");
    const DWORD call=limits_rules::rel32(where+3,address(reinterpret_cast<const void*>(&admit)));
    const DWORD resume=limits_rules::rel32(where+23,0x500de6);
    std::memcpy(b.data()+4,&call,4);std::memcpy(b.data()+24,&resume,4);
    return b;
}
}

void install(HMODULE game,const patch_transaction::hooks* test_hooks)
{
    if(attempted)return;attempted=true;
    const auto key=ini::text(L"Limits",L"FightMoves");
    if(!key||limits_rules::parse_count(*key)==static_cast<int>(stock_count))return;
    // An unrelated executable cannot own this pool or an agreement entry.
    if(address(game)!=limits_rules::image_base)return;
    // register_rule enlists the shared exclusion/transport hooks itself. Calling
    // require_gameplay_agreement here would skip that late-enlistment path when
    // FightMoves is the only configured gameplay key.
    const DWORD wanted=capacity(key);
    const auto settle=[wanted](bool ok){gameplay_agreement::register_rule(gameplay_capabilities::fight_moves_id,1,{ok?wanted:stock_count,0,0,0},ok);};
    if(wanted==stock_count){logger::write("limits: FightMoves must be 333..2048 (332 or absent is stock); rule 54 unverified");settle(false);return;}
    patch_transaction::contribution c;c.owner="limits.FightMoves";
    const auto layout=layout_for(wanted);
    for(const auto& s:sites){
        c.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"FightMoves layout operand"});
        std::vector<std::uint8_t> b(s.width);const DWORD v=value(layout,s.value);std::memcpy(b.data(),&v,s.width);
        c.writes.push_back({s.va+s.offset,std::move(b),"FightMoves layout"});
    }
    for(const auto& g:guards)c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),g.what});
    // Checked again by commit, after every allocation/protection preparation and
    // immediately before the first write. A live singleton can never be resized.
    c.guards.push_back({pool_pointer,{0,0,0,0},"FightMove singleton must be null"});
    std::string why;
    if(!patch_transaction::verify_expectations(c,why)){logger::write("limits: FightMoves refused: %s",why.c_str());settle(false);return;}
    cave=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!cave){logger::write("limits: FightMoves cannot allocate entry cave");settle(false);return;}
    const auto code=entry_code(address(cave));std::memcpy(cave,code.data(),code.size());DWORD old=0;
    if(!VirtualProtect(cave,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),cave,code.size())){
        VirtualFree(cave,0,MEM_RELEASE);cave=nullptr;logger::write("limits: FightMoves cannot protect/flush entry cave");settle(false);return;
    }
    c.writes.push_back(patch_transaction::relocation(0x500de0,"e9",true,address(cave),1,"FightMoves entry admission"));
    c.publish=[wanted,settle]{published_count=wanted;active=true;settle(true);logger::write("limits: FightMoves raised from 332 to %lu; descriptor published before MoveTriggers; pool and 150-node style map admission guarded",wanted);};
    c.abandon=[settle]{VirtualFree(cave,0,MEM_RELEASE);cave=nullptr;settle(false);};
    c.retain=[settle]{settle(false);game_version::refuse_startup();logger::write("limits: FightMoves uncertified rollback; cave retained; startup refused");};
    patch_transaction::transaction tx;
    tx.add(std::move(c));
    const auto result=tx.commit(test_hooks);
    if(result!=patch_transaction::outcome::committed)logger::write("limits: FightMoves refused: %s",tx.report().c_str());
}
bool installed(){return active;}
pool_descriptor descriptor(){DWORD base=0;read(pool_pointer,base);return {base,published_count};}
counts counters(){return {totals.refused.load(std::memory_order_relaxed),totals.map_refused.load(std::memory_order_relaxed),totals.aliasing.load(std::memory_order_relaxed)};}
std::string status()
{
    DWORD pool=0,live=0;read(pool_pointer,pool);sample(pool);
    const bool valid=pool&&read(pool+layout_for(published_count).live,live);
    const auto observed=counters();
    return "moves "+(valid?std::to_string(live):"-")+"/"+std::to_string(published_count)+" (refused "+std::to_string(observed.refused)+", map-refused "+std::to_string(observed.map_refused)+", aliasing "+std::to_string(observed.aliasing)+")";
}
}
