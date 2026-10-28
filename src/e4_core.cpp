#include "e4_core.hpp"
#include "e4_core_state.hpp"
#include "e4_core_rules.hpp"
#include "e4_core_plan.hpp"
#ifdef XML2_E4_TEST
#include "e4_observe_rules.hpp"
#endif
#include "e4_observe.hpp"
#include "actor_ai_layout.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "save_format.hpp"
#include "anim_enum_layout.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace e4_core {
static_assert(anim_enum_layout::raised.records>=96);
static_assert(127u*176u<=22528u);
namespace {
using u32=std::uint32_t;using namespace e4_core_state;
e4_core_rules::tiers teams;
e4_core_rules::reservations reservations;
void* backing=nullptr;void* caves=nullptr;bool retained=false,fold_pending=false;
std::atomic<unsigned> folds{0},stale_retired{0};std::string failure;HMODULE image=nullptr;
__declspec(thread) u32 entering_actor=0;
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
u32& field(u32 p,u32 off){return *reinterpret_cast<u32*>(static_cast<std::uintptr_t>(p+off));}
u32 get_cai(){if(!(field(0x79889c,0)&1))reinterpret_cast<void*(__cdecl*)()>(0x5252e0)();return cai;}
unsigned slot(u32 actor){
    const u32 allocator=field(0x718fbc,0);
    if(!allocator||actor<allocator+8)return 96;
    const u32 diff=actor-allocator-8;return diff%0x848?96:diff/0x848;
}
[[noreturn]] void broken(const char* site){
    logger::write("E4: controller invariant broken (%s, actor %08x, live %u, reserved %u)",site,entering_actor,cai?field(cai+4,primary_live):0,reservations.reserved);
    e4_observe::fatal(site);
}
bool __cdecl available(u32 allocator){
    e4_observe::core_checkpoint();const auto object=get_cai();
    return reservations.available(field(allocator,0x31ca4),field(object+4,primary_free));
}
void __cdecl begin_controller(u32 actor){
    e4_observe::core_checkpoint();get_cai();
    if(entering_actor||!reservations.begin(slot(actor),field(cai+4,primary_free)))broken("controller allocation backstop");
    entering_actor=actor;
}
u32 __fastcall actor_ring_pop(void* ring,void*){
    const auto p=address(ring);auto& deq=field(p,0x188);deq=deq+1==96?0:deq+1;return --field(p,0x18c);
}
void __fastcall team_init(void* arena,void*){
    const auto p=address(arena);teams.reset();fold_pending=false;
    field(p,team_enqueue)=field(p,team_dequeue)=0;field(p,team_free)=96;
    for(unsigned i=0;i<96;++i)field(p,team_ring+i*4)=i;
}
u32 __fastcall team_pop(void* arena,void*){
    const auto object=address(arena),count=field(cai+4,primary_live);
    const auto i=teams.pop(count);if(!i)broken("team pop without admission");
    field(object,team_active+(*i/32)*4)|=1u<<(*i%32);
    --field(object,team_free);++field(object,team_live);
    field(object,team_enqueue)=teams.enqueue;field(object,team_dequeue)=teams.dequeue;
    for(unsigned j=0;j<4;++j)field(object,team_ring+j*4)=teams.fifo[j];
    return *i;
}
void __fastcall team_release(void* arena,void*,u32 i){
    const auto p=address(arena);if(i>=96||!teams.active(i))broken("invalid team release");
    e4_observe::core_team_lifecycle(i,false);
    // Native destruction precedes clearing active state, exactly as retail.
    reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x525270)(arena,nullptr,i);
    if(!teams.release(i))broken("team FIFO release");
    field(p,team_active+(i/32)*4)&=~(1u<<(i%32));++field(p,team_free);--field(p,team_live);
    field(p,team_enqueue)=teams.enqueue;field(p,team_dequeue)=teams.dequeue;
    for(unsigned j=0;j<4;++j)field(p,team_ring+j*4)=teams.fifo[j];
    e4_observe::core_team_lifecycle(i,true);
}
void __fastcall team_reset(void* arena,void*){
    const auto p=address(arena);std::array<bool,96> retiring{};
    for(unsigned i=0;i<96;++i)if((retiring[i]=teams.active(i)))e4_observe::core_team_lifecycle(i,false);
    field(p,team_live)=0;
    for(unsigned w=0;w<3;++w)field(p,team_active+w*4)=0;
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5228b0)(arena,nullptr);
    for(unsigned i=0;i<96;++i)if(retiring[i])e4_observe::core_team_lifecycle(i,true);
    team_init(arena,nullptr);
    const auto shift=field(p,team_shift);if(shift>=31)broken("team generation shift");
    for(unsigned i=0;i<96;++i){auto& generation=field(p,team_generations+4*i);generation+=1u<<shift;if(generation&0x80000000u)generation=(1u<<shift)|i;}
}
u32 __fastcall registration(void* ai,void*,u32 token){
    const auto p=address(ai)+team_offset,n=field(address(ai)+4,primary_live),limit=n<=32?4u:96u;
    using accept_t=unsigned char(__fastcall*)(void*,void*,u32);
    for(unsigned i=0;i<limit;++i)if(teams.active(i)&&reinterpret_cast<accept_t>(0x525df0)(reinterpret_cast<void*>(p+i*0x268),nullptr,token))return field(p,team_generations+4*i);
    if(teams.admission_count(n)>=e4_core_rules::team_cap(n))return 0;
    const auto i=team_pop(reinterpret_cast<void*>(p),nullptr);
    field(p,team_constructed+(i/32)*4)|=1u<<(i%32);
    auto* record=reinterpret_cast<void*>(p+i*0x268);
    record=reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x522fd0)(record,nullptr);
    reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x525d40)(record,nullptr,token);
    return field(p,team_generations+4*i);
}
void release_prepared(){if(retained)return;if(backing)VirtualFree(backing,0,MEM_RELEASE);if(caves)VirtualFree(caves,0,MEM_RELEASE);backing=caves=nullptr;cai=0;prepared=false;}
// Key-level refusals (configure/prepare: Stage 2 never contributes) write
// nothing for Stage 2, and features without a Stage 2 prerequisite still commit
// in the startup transaction.
void fail(const std::string& why){failure=why;eligible=false;logger::write("Actors: %s; stage 2 writes nothing (other features keep their own transactions); ActorAI unverified",why.c_str());}
// Refusals inside contribute() (an unprepared or unverified prerequisite
// contribution, ledger preparation, cave protection, mandatory instrumentation)
// add a veto to the shared startup transaction: nothing in it installs.
void refuse_transaction(const std::string& why){failure=why;eligible=false;logger::write("Actors: %s; the whole startup transaction is refused (no feature in it installs this start); ActorAI unverified",why.c_str());}
void veto(patch_transaction::transaction& tx,const char* owner,const std::string& why){refuse_transaction(why);patch_transaction::contribution c;c.owner=owner;c.requires_written.push_back({0xffffffff,failure});tx.add(std::move(c));}
void snapshot(u32* out){out[0]=reservations.reserved;for(unsigned i=0;i<3;++i)out[i+1]=reservations.pending[i];out[4]=folds.load(std::memory_order_relaxed);out[5]=stale_retired.load(std::memory_order_relaxed);}
}
void configure(){
    const auto key=ini::text(L"Limits",L"Actors");e4_core_state::requested=key.has_value();eligible=false;
    if(!key)return;
    if(*key!="96"){fail("[Limits] Actors requires 96");return;}
    const std::array<std::pair<const wchar_t*,const char*>,6> keys{{{L"EntityCapacity","1"},{L"ActorPools","124"},{L"AnimEnums","512"},{L"CharSounds","127"},{L"ResidentStats","320"},{L"MotionPool","22528"}}};
    for(const auto& [name,value]:keys){const auto v=ini::text(L"Limits",name);if(!v||*v!=value){std::string ascii;for(auto p=name;*p;++p)ascii+=char(*p);
        fail(!v?"missing prerequisite [Limits] "+ascii+"="+value:"wrong prerequisite [Limits] "+ascii+"="+*v+" (needs "+value+")");return;}}
    eligible=true;const auto power=ini::text(L"Game",L"NpcPowerRule");elide_power=power&&*power=="xml1";
}
void require_agreement(HMODULE game){if(e4_core_state::requested)game_version::require_gameplay_agreement(game);}
void prepare(HMODULE game){
    image=game;if(!eligible)return;
    if(address(game)!=0x400000){fail("retail image base required");return;}
    if(!actor_ai::all_consumers_registered()){fail("CAI consumer registration incomplete");return;}
    backing=VirtualAlloc(nullptr,cai_size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    caves=VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!backing||!caves){fail("core backing allocation failed");release_prepared();return;}
    cai=address(backing);prepared=true;
    on_admitted=&actor_admitted;on_actor_free=&actor_free;on_controller_pop=&controller_pop;on_controller_freed=&controller_freed;on_boundary=&boundary;on_snapshot=&snapshot;
}
void contribute(patch_transaction::transaction& tx,HMODULE game){
    if(!prepared||game!=image)return;
    const e4_core_plan::targets t{address(reinterpret_cast<void*>(&available)),address(reinterpret_cast<void*>(&begin_controller)),address(reinterpret_cast<void*>(&actor_ring_pop)),address(reinterpret_cast<void*>(&team_pop)),address(reinterpret_cast<void*>(&team_init)),address(reinterpret_cast<void*>(&team_release)),address(reinterpret_cast<void*>(&team_reset)),address(reinterpret_cast<void*>(&registration))};
    auto p=e4_core_plan::make(cai,address(caves),t);std::string why;
    bool prerequisites=true;
    tx.compose([&](auto& pending){for(const char* name:{"entity_capacity","ActorPools","AnimEnums","CharSounds","ResidentStats","MotionPool"}){
        const auto it=std::find_if(pending.begin(),pending.end(),[&](const auto& c){return c.owner==name;});
        if(it==pending.end()||!patch_transaction::verify_expectations(*it,why)){why="prerequisite "+std::string(name)+(it==pending.end()?" not prepared":" not verified: "+why);prerequisites=false;break;}
    }return true;});
    if(!p||!prerequisites){veto(tx,"Actors prerequisite refusal",why.empty()?"core ledger preparation failed":why);return;}
    std::memcpy(caves,p->code.data(),p->code.size());DWORD old=0;
    if(!VirtualProtect(caves,8192,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),caves,p->code.size())){veto(tx,"Actors protection refusal","core cave protection failed");return;}
    p->contribution.publish=[] {
        retained=true;e4_core_state::installed=true;reservations={};teams.reset();fold_pending=false;folds=0;stale_retired=0;
        actor_ai::published={2,cai,cai+4,0x61c,96,cai+team_offset,0x268,96,2,0x24db0,0x525302};
        save_format::enable_actor_capacity96();
        logger::write("Actors: 96 installed; actor/controller arena 96, team storage96 P2; rule 1 revision4; rule44 revision2 {96,131168,8126588,15}; actor save feature0x4");
    };
    p->contribution.abandon=[] {if(failure.empty())refuse_transaction("core startup transaction refused");release_prepared();};
    p->contribution.retain=[] {retained=true;failure="uncertified core rollback";eligible=false;logger::write("Actors: uncertified core rollback; backing retained and startup refused; ActorAI unverified");game_version::refuse_startup();};
    tx.add(std::move(p->contribution));
    if(!e4_observe::contribute_core(tx,game,why)){
        veto(tx,"Actors instrumentation refusal","mandatory instrumentation: "+why);
    }
}
void after_commit(bool committed){
    if(!e4_core_state::requested||address(image)!=0x400000)return;
    gameplay_agreement::register_rule(gameplay_capabilities::actor_ai_id,2,e4_core_rules::agreement,committed&&e4_core_state::installed);
    if(!e4_core_state::installed){save_format::actor_capacity96_refused(failure.empty()?std::string("startup transaction not committed"):failure);release_prepared();}
}
bool installed(){return e4_core_state::installed;}
void verify_consumers(){
    if(!e4_core_state::installed)return;
    const unsigned char expected[]{0x83,0xc4,0x04};unsigned char actual[3]{};
    if(!actor_ai::all_consumers_registered()||(elide_power&&(!patch_transaction::read_memory(0x505011,actual,sizeof actual)||std::memcmp(actual,expected,sizeof actual)))){
        logger::write("Actors: required CAI consumer/handler composition failed; startup refused");game_version::refuse_startup();
    }
}
unsigned reserved(){return reservations.reserved;}
void actor_admitted(unsigned i){if(!e4_core_state::installed)return;if(!reservations.admit(i)){logger::write("E4: invalid admitted actor slot %u",i);broken("actor reservation admission");}}
void actor_free(unsigned i){if(e4_core_state::installed)reservations.cancel(i);}
void controller_pop(){
    if(!e4_core_state::installed)return;
    const auto i=slot(entering_actor);if(i<96)reservations.cancel(i);entering_actor=0;
}
void controller_freed(unsigned n){if(e4_core_state::installed&&n==32&&teams.extended())fold_pending=true;}
void boundary(){
    if(!e4_core_state::installed)return;
    const auto n=field(cai+4,primary_live);if(!reservations.valid(n))broken("quiescent reservation accounting");
    if(!fold_pending||n>32)return;fold_pending=false;
    const auto arena=cai+team_offset;
    // Snapshot before any leave mutates a record. Native leave may compact its
    // member array or destroy the record on its last member.
    struct dissolved {u32 token,count;std::array<u32,9> members;};std::array<dissolved,92> snapshots{};unsigned total=0;
    for(unsigned i=4;i<96;++i)if(teams.active(i)){
        auto& s=snapshots[total++];s.token=field(arena,team_generations+4*i);s.count=field(arena+i*0x268,0x28);
        if(s.count>9)broken("team member count");for(unsigned j=0;j<s.count;++j)s.members[j]=field(arena+i*0x268,4+4*j);
    }
    using leave_t=void(__fastcall*)(void*,void*,u32,u32);const auto leave=reinterpret_cast<leave_t>(0x522b30);
    for(unsigned j=0;j<total;++j)for(unsigned k=0;k<snapshots[j].count;++k){const auto token=snapshots[j].members[k];
        auto* controller=reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x525160)(reinterpret_cast<void*>(cai),nullptr,token);
        // Retail leaves stale memberships listed: reactivation re-registers and
        // overwrites +0x538 without leaving the old team (0x516845-0x51684e), and
        // destruction leaves only the current +0x538 team (0x511cdb-0x511ce4).
        // Retail timeout skips a member that no longer resolves (0x5276a4). The
        // native leave works on the token alone, so a stale listing is retired
        // without touching any controller: a dead member is just removed, and a
        // living member that moved keeps its current team.
        if(!controller){leave(reinterpret_cast<void*>(cai),nullptr,snapshots[j].token,token);++stale_retired;continue;}
        const auto i=(address(controller)-cai-4)/0x61c;
        e4_observe::core_fold_snapshot(i,snapshots[j].token,false);
        leave(reinterpret_cast<void*>(cai),nullptr,snapshots[j].token,token);
        if(field(address(controller),0x538)==snapshots[j].token)field(address(controller),0x538)=0;else ++stale_retired;
        e4_observe::core_fold_snapshot(i,snapshots[j].token,true);
    }
    if(teams.extended())broken("fold-back extended team survived");++folds;
}
std::string status(){return "Actors "+std::to_string(e4_core_state::installed?96:32)+" reserved "+std::to_string(reservations.reserved)+" foldbacks "+std::to_string(folds.load())+" stale "+std::to_string(stale_retired.load())+(failure.empty()?"":"; "+failure);}
#ifdef XML2_E4_TEST
bool test_set_encoding(unsigned variant){
    if(!e4_core_state::installed||variant>3||field(cai+4,primary_live)||field(cai+team_offset,team_live)||reservations.reserved)return false;
    const auto e=e4_observe_rules::test_encoding(variant);
    for(const auto base:{cai+4,cai+team_offset}){
        const bool primary=base==cai+4;field(base,primary?primary_mask:team_mask)=e.mask;field(base,primary?primary_shift:team_shift)=e.shift;
        for(unsigned i=0;i<96;++i)field(base,(primary?primary_generations:team_generations)+4*i)=((1u<<e.shift)+e.seed)|i;
    }
    for(unsigned i=0;i<96;++i)field(cai+4,primary_ring+4*i)=e.reverse?95-i:i;
    return true;
}
#endif
}
