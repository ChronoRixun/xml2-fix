#include "e4_observe.hpp"
#include "e4_core_state.hpp"
#include "e4_observe_rules.hpp"
#include "e4_observe_plan.hpp"
#include "actor_ai_layout.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <new>

namespace e4_observe {
namespace {
using namespace e4_observe_rules;
using kind=e4_observe_sites::kind;
SRWLOCK mutex=SRWLOCK_INIT;
struct shared_lock {
    shared_lock(){AcquireSRWLockShared(&mutex);}
    ~shared_lock(){ReleaseSRWLockShared(&mutex);}
    shared_lock(const shared_lock&)=delete;
    shared_lock& operator=(const shared_lock&)=delete;
};
monitor m;
bool installed=false;
PVOID veh=nullptr;
u32 logged=0,probe_faults=0,installed_spans=0;
DWORD logged_at=0;bool sampled=false;
std::array<u32,5> accounting_peaks{};
std::array<bool,max_slots> team_destroying{};
constexpr unsigned field_count=15;
constexpr u32 fields[field_count]={0x34,0x38,0x3c,0x2e4,0x2b5,0x18,0x154,0x17c,0x1c,0x2c0,0x2e8,0x2ec,0x340,0x344,0x350};
constexpr unsigned widths[field_count]={4,4,4,4,1,4,4,4,4,4,4,1,4,4,1};
struct provenance {u32 value=0,writer=0,team=0,step=0;bool dissolved=false;};
std::array<std::array<provenance,field_count>,max_slots> provenance_table{};
struct snapshot {u32 point=0,controller=0,team=0,step=0;bool after=false;std::array<u32,field_count> values{};std::array<provenance,field_count> sources{};};
std::array<snapshot,64> snapshots{};u32 snapshot_head=0;
struct damage {u32 caller=0,entity=0,descriptor=0,live=0;damage_reason reason{};};
struct refusal {damage data;u32 result=0,step=0;bool would=false;};
std::array<refusal,64> refusals{};u32 refusal_head=0;
struct caller_count {u32 caller=0;std::array<u32,4> reasons{};u32 would=0,unknown=0;};
std::array<caller_count,64> damage_callers{};
struct trace {u32 point=0,actor=0,team=0,result=0,step=0;};
std::array<trace,256> traces{};u32 trace_head=0,next_team=0;
std::array<u32,max_slots> logical_actors{};
struct identity {u32 token=0,id=0;};std::array<identity,max_slots> teams{};
struct writer_frame {u32 point=0,team=0;std::array<bool,max_slots> members{};std::array<std::array<u32,field_count>,max_slots> before{};};
struct context {
    bool inside=false;
    std::array<borrow,max_depth> borrows{};std::array<u32,max_depth> borrow_sp{};u32 depth=0,borrow_overflow=0;
    std::array<writer_frame,16> writers{};u32 writer_depth=0,writer_overflow=0;
    std::array<damage,16> damage_stack{};u32 damage_depth=0,damage_overflow=0;
    std::array<u32,max_depth> init_stack{};u32 init_depth=0;
    std::array<u32,max_depth> factory_stack{};u32 factory_depth=0;
    e4_unwind::frame* boundary=nullptr;u32 boundary_depth=0;
};
constexpr unsigned context_capacity=32;
struct context_slot {DWORD thread=0;context data;};
context_slot* context_bank=nullptr;
__declspec(thread) context* current_context=nullptr;
u32 contexts_live=0,contexts_peak=0,next_boundary=0,boundary_unwinds=0;
bool attach_context(){
    if(current_context)return true;
    AcquireSRWLockExclusive(&mutex);
    for(unsigned n=0;n<context_capacity;++n)if(!context_bank[n].thread){
        context_bank[n].thread=GetCurrentThreadId();current_context=&context_bank[n].data;
        contexts_peak=(std::max)(contexts_peak,++contexts_live);
        ReleaseSRWLockExclusive(&mutex);return true;
    }
    m.emit(event_kind::overflow,max_slots);
    ReleaseSRWLockExclusive(&mutex);return false;
}
bool recycle_context(context* c,DWORD thread){
    if(c->depth||c->borrow_overflow||c->writer_depth||c->writer_overflow||c->damage_depth||c->damage_overflow||c->init_depth||c->factory_depth||c->boundary_depth)return false;
    for(const auto& r:m.records)if(r.open&&r.thread==thread)return false;
    for(unsigned n=0;n<context_capacity;++n)if(&context_bank[n].data==c){
        current_context=nullptr;c->inside=false;context_bank[n].thread=0;--contexts_live;return true;
    }
    return false;
}
void boundary_begin(e4_unwind::frame& f,DWORD thread){
    if(f.signature!=e4_unwind::magic)fatal("E4 invalid unwind frame");
    f.serial=++next_boundary;if(!f.serial)fatal("E4 unwind identity exhausted");
    f.previous=static_cast<u32>(reinterpret_cast<std::uintptr_t>(current_context->boundary));
    f.borrows=current_context->depth;f.borrow_overflow=current_context->borrow_overflow;
    f.writers=current_context->writer_depth;f.writer_overflow=current_context->writer_overflow;
    f.damage=current_context->damage_depth;f.damage_overflow=current_context->damage_overflow;
    f.init=current_context->init_depth;f.factory=current_context->factory_depth;
    f.boundaries=current_context->boundary_depth;f.resetting=m.resetting;f.thread=thread;f.closed=0;
    current_context->boundary=&f;++current_context->boundary_depth;
}
void boundary_finish(e4_unwind::frame& f,bool abnormal){
    if(f.signature!=e4_unwind::magic)fatal("E4 corrupt unwind frame");
    if(!f.serial||f.closed)return;
    f.closed=1;
    // An open record owned by a returned/unwound boundary can no longer publish.
    // Other live/outer constructions are deliberately not marked by this exit.
    m.unwind_boundary(f.serial,f.thread);if(abnormal)++boundary_unwinds;
    if(!current_context)fatal("E4 unwind context missing");
    current_context->depth=f.borrows;current_context->borrow_overflow=f.borrow_overflow;
    current_context->writer_depth=f.writers;current_context->writer_overflow=f.writer_overflow;
    current_context->damage_depth=f.damage;current_context->damage_overflow=f.damage_overflow;
    current_context->init_depth=f.init;current_context->factory_depth=f.factory;
    current_context->boundary_depth=f.boundaries;
    current_context->boundary=reinterpret_cast<e4_unwind::frame*>(static_cast<std::uintptr_t>(f.previous));
    m.resetting=f.resetting!=0;m.depth=current_context->depth;m.borrows=current_context->borrows;
}
void observe_borrows(event_kind kind,u32 slot,u32 caller){
    if(slot>=max_slots)return;
    event* reported=nullptr;
    // All contexts are stable under mutex. A release/pop must see aliases held
    // by other threads as well as the current thread's nested callbacks.
    for(unsigned n=0;n<context_capacity;++n)if(context_bank[n].thread){const auto& c=context_bank[n].data;
        for(unsigned k=0;k<c.depth;++k)if(c.borrows[k].slot==slot){
            if(!reported){m.emit(kind,slot,caller,c.borrows[k].caller);reported=&m.events[(m.head-1)%64];}
            if(reported->borrow_count<max_depth)reported->borrow_returns[reported->borrow_count]=c.borrows[k].caller;
            ++reported->borrow_count;
        }
    }
}
u32 addr(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
bool read(u32 at,void* out,unsigned n) {
    if(!patch_transaction::read_memory(at,out,n)){++probe_faults;return false;}return true;
}
u32 value(u32 at,unsigned n=4){u32 v=0;read(at,&v,n);return v;}
u32 sp(const u32* r){return r[3]+4;}
u32 arg(const u32* r,unsigned n){return value(sp(r)+4*n);}
u32 controller(u32 address) {
    const auto& d=actor_ai::installed();
    if(address<d.primary_base||address>=d.primary_base+d.controller_stride*d.controllers)return max_slots;
    const auto diff=address-d.primary_base;return diff%d.controller_stride?max_slots:diff/d.controller_stride;
}
u32 control_address(u32 i){return actor_ai::installed().primary_base+i*actor_ai::installed().controller_stride;}
u32 generation(u32 i){const auto& d=actor_ai::installed();return value(d.primary_base+actor_ai::generations(d)+4*i);}
u32 index(u32 token){const auto& d=actor_ai::installed();return token&value(d.cai_object+d.mask_offset_obj);}
u32 team_token(u32 address) {
    const auto& d=actor_ai::installed();
    if(address<d.team_base||(address-d.team_base)%d.team_stride)return 0;
    const auto i=(address-d.team_base)/d.team_stride;if(i>=d.team_storage)return 0;
    return value(d.team_base+actor_ai::team_generations(d)+4*i);
}
u32 logical_team(u32 token) {
    if(!token)return 0;
    const auto& d=actor_ai::installed();const auto i=token&value(d.cai_object+actor_ai::team_mask(d));
    // Only live team lifetimes have an identity. Stale tokens normalize to the
    // same teamless state as the native resolver, without consuming an entry.
    if(i>=d.team_storage||i>=teams.size()||value(d.team_base+actor_ai::team_generations(d)+i*4)!=token||
       !(value(d.team_base+actor_ai::team_active(d)+(i>>5)*4)&(1u<<(i&31))))return 0;
    auto& entry=teams[i];if(entry.token==token)return entry.id;
    if(next_team==0xffffffffu){m.emit(event_kind::overflow,max_slots);return 0;}
    entry={token,++next_team};return entry.id;
}
void trace_actor(u32 point,u32 slot,u32 result=0){
    const auto owner=slot<max_slots?m.slots[slot].owner:0;
    const auto token=slot<max_slots?value(control_address(slot)+0x538):0;
    traces[trace_head++%traces.size()]={point,slot<max_slots?logical_actors[slot]:0,logical_team(token),result,m.step};
    (void)owner;
}
void handler(u32 i){const auto c=control_address(i),p=value(c+0x4c4);m.handler(i,c,p,p?value(p+4):0);}
void take_snapshot(u32 i,u32 point,u32 token,bool after){
    auto& s=snapshots[snapshot_head++%snapshots.size()];s={};s.point=point;s.controller=i;s.team=token;s.step=m.step;s.after=after;
    const auto c=control_address(i);
    for(unsigned f=0;f<field_count;++f){s.values[f]=value(c+fields[f],widths[f]);s.sources[f]=provenance_table[i][f];}
}
void log_construction(u32 esp,u32 thread) noexcept {
    try {
        for(const auto& c:m.records)if(c.open&&c.unwound)
            logger::write("E4: construction escaped (phase %u, actor %08x, controller slot %u, exception %08x at %08x, ESP %08x FS0 %08x)",
                unsigned(c.at),c.actor,c.slot,c.code,c.fault,c.esp,c.seh);
    } catch(...) {}
}
[[noreturn]] void construction_stop(u32 esp,u32 thread){
    __try { log_construction(esp,thread); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    fatal("E4 construction escaped");
}
void checkpoint(u32 esp,u32 thread){
    // Stack depth is diagnostic only. Actual SEH unwind callbacks remove
    // abandoned brackets and latch escaped constructions before recovery.
    m.depth=current_context->depth;m.borrows=current_context->borrows;
    if(m.checkpoint(esp,thread)) {
        ReleaseSRWLockExclusive(&mutex);current_context->inside=false;
        construction_stop(esp,thread);
    }
}
void sample_accounting(){
    const auto faults_before=probe_faults;
    bool open=false;for(const auto& c:m.records)open|=c.open;
    if(open||m.resetting||(value(0x72a798,1)&3))return;
    const auto& d=actor_ai::installed();u32 actors=0,arena_actors=0,bad_tokens=0;
    const auto allocator=value(entity_capacity_layout::allocator_pointer_cell);
    if(allocator) {
        const auto bitmap=d.stage==2?0x31c98u:0x10998u;
        for(unsigned i=0;i<(d.stage==2?96u:32u);++i)if(value(allocator+bitmap+(i/32)*4)&(1u<<(i%32))){
            ++arena_actors;const auto actor=allocator+8+i*0x848,token=value(actor+0x7dc),slot=index(token);
            // Actor allocation does not imply an AI controller (e.g. team heroes).
            // A non-null token, however, must resolve to a unique live controller.
            if(token){
                if(slot<d.controllers&&generation(slot)==token&&m.slots[slot].at==state::published&&m.slots[slot].owner==actor)++actors;
                else ++bad_tokens;
            }
        }
    }
    std::array<bool,max_slots> orphaned{};
    for(unsigned i=0;i<d.controllers;++i)if(m.slots[i].at==state::published){
        const auto owner=m.slots[i].owner;
        orphaned[i]=!owner||value(owner+0x7dc)!=m.slots[i].generation;
    }
    const auto live=value(d.primary_base+actor_ai::primary_live(d));
    const auto arena_live=allocator?value(allocator+(d.stage==2?0x31ca4:0x1099c)):0;
    // Unreadable probes are not evidence of an accounting mismatch.
    if(probe_faults!=faults_before)return;
    for(unsigned i=0;i<d.controllers;++i)if(orphaned[i])m.slots[i].at=state::orphaned;
    m.account(live,d.controllers,actors,true);
    m.account_event(1,!bad_tokens&&arena_actors==arena_live,
        {arena_actors,arena_live,bad_tokens},arena_actors);
    for(unsigned i=0;i<d.controllers;++i)if(m.slots[i].at==state::published)handler(i);
}
void writer_enter(u32 point,u32 team){
    if(current_context->writer_depth==current_context->writers.size()){++current_context->writer_overflow;m.emit(event_kind::overflow,max_slots,point);return;}
    auto& w=current_context->writers[current_context->writer_depth++];w={};w.point=point;w.team=team_token(team);
    const u32 count=(std::min)(value(team+0x28),u32(9));
    for(unsigned n=0;n<count;++n){const auto token=value(team+4+n*4),i=index(token);if(i>=max_slots||generation(i)!=token)continue;
        w.members[i]=true;for(unsigned f=0;f<field_count;++f)w.before[i][f]=value(control_address(i)+fields[f],widths[f]);
        take_snapshot(i,point,w.team,false);
    }
}
void writer_exit(u32 point){
    if(current_context->writer_overflow){--current_context->writer_overflow;return;}
    if(!current_context->writer_depth){m.emit(event_kind::transition,max_slots,point);return;}
    auto& w=current_context->writers[--current_context->writer_depth];if(w.point!=point){m.emit(event_kind::transition,max_slots,point,w.point);return;}
    for(unsigned i=0;i<max_slots;++i)if(w.members[i]){
        for(unsigned f=0;f<field_count;++f){const auto now=value(control_address(i)+fields[f],widths[f]);
            if(now!=w.before[i][f])provenance_table[i][f]={now,point,w.team,m.step,false};
        }take_snapshot(i,point,w.team,true);
    }
}
void __cdecl observe(unsigned encoded,const u32* r) {
    if(!installed)return;
    if(!attach_context())fatal("E4 concurrent diagnostic context capacity exhausted");
    if(current_context->inside)return;
    current_context->inside=true;AcquireSRWLockExclusive(&mutex);
    const auto thread=GetCurrentThreadId();
    if(encoded==e4_unwind::begin||encoded==e4_unwind::end){
        auto& f=*reinterpret_cast<e4_unwind::frame*>(static_cast<std::uintptr_t>(sp(r)));
        if(encoded==e4_unwind::begin)boundary_begin(f,thread);else {boundary_finish(f,false);checkpoint(sp(r),thread);}
        auto* completed=current_context;const bool recycled=recycle_context(completed,thread);
        ReleaseSRWLockExclusive(&mutex);if(!recycled)completed->inside=false;return;
    }
    const unsigned id=encoded/2;const bool post=(encoded&1)!=0;
    if(id>=e4_observe_sites::sites.size()){ReleaseSRWLockExclusive(&mutex);current_context->inside=false;return;}
    const auto& s=e4_observe_sites::sites[id];
    bool core_boundary=false;
    u32 return_address=s.point;
    for(unsigned n=0;n<s.count;++n)if(s.code[n].va==s.point)return_address+=static_cast<u32>(std::strlen(s.code[n].hex)/2);
    m.depth=current_context->depth;m.borrows=current_context->borrows;
    switch(s.type){
    case kind::checkpoint:checkpoint(sp(r),thread);break;
    case kind::step:{
        checkpoint(sp(r),thread);++m.step;sample_accounting();
        bool factory=false;for(unsigned n=0;n<context_capacity;++n)if(context_bank[n].thread&&context_bank[n].data.factory_depth)factory=true;
        // Loading/reset/reentrant construction frames are not quiescent
        // simulation boundaries. Never dissolve teams while this brain owns
        // a borrowed callback frame; the next ordinary step consumes the flag.
        u32 loading=0;const bool readable=read(0x72a798,&loading,1);
        core_boundary=readable&&!m.resetting&&!factory&&!current_context->depth&&!(loading&3);
        break;
    }
    case kind::factory_entry:
        checkpoint(sp(r),thread);
        if(current_context->factory_depth<current_context->factory_stack.size())current_context->factory_stack[current_context->factory_depth++]=sp(r);
        else m.emit(event_kind::overflow,max_slots,s.point);
        break;
    case kind::factory_failed:if(current_context->factory_depth)--current_context->factory_depth;else m.emit(event_kind::transition,max_slots,s.point);break;
    case kind::admitted:{const auto i=r[0];if(e4_core_state::installed&&e4_core_state::on_admitted)e4_core_state::on_admitted(i);m.admit(i,r[1]+4+i*0x848,thread);
        if(i<max_slots){
            if(current_context->factory_depth)m.records[i].esp=current_context->factory_stack[current_context->factory_depth-1];
            if(current_context->boundary)m.records[i].boundary=current_context->boundary->serial;
        }
        break;}
    case kind::actor_init:
        if(!post){
            if(current_context->init_depth==current_context->init_stack.size()){m.emit(event_kind::overflow,max_slots,s.point);break;}
            current_context->init_stack[current_context->init_depth++]=r[6];u32 seh=0;
            __asm mov eax,fs:[0]
            __asm mov seh,eax
            m.enter(r[6],sp(r),seh);
        }else if(current_context->init_depth){const auto actor=current_context->init_stack[--current_context->init_depth];m.advance(actor,phase::init_returned);if(auto* c=m.find(actor))c->esp=sp(r);}
        else m.emit(event_kind::transition,max_slots,s.point);
        break;
    case kind::controller_pop:{checkpoint(sp(r),thread);const auto i=r[7];if(e4_core_state::installed){if(e4_core_state::on_controller_pop)e4_core_state::on_controller_pop();if(actor_ai::reset_controller_slot)actor_ai::reset_controller_slot(i);}observe_borrows(event_kind::reuse_borrowed,i,arg(r,0));const auto depth=m.depth;m.depth=0;m.take(i,generation(i),arg(r,0));m.depth=depth;if(auto* c=m.inner(thread))c->slot=i;break;}
    case kind::controller_token:{const auto i=index(r[7]);m.publish(i,r[1],s.point);m.advance(r[1],phase::controller);
        if(i<max_slots){if(auto* c=m.find(r[1]))logical_actors[i]=c->logical;handler(i);trace_actor(s.point,i);}break;}
    case kind::factory_published:m.advance(r[1],phase::published);if(current_context->factory_depth)--current_context->factory_depth;else m.emit(event_kind::transition,max_slots,s.point);break;
    case kind::actor_free:{checkpoint(sp(r),thread);const auto i=arg(r,1);if(e4_core_state::installed&&e4_core_state::on_actor_free)e4_core_state::on_actor_free(i);if(i<max_slots)m.records[i].open=false;break;}
    case kind::controller_release:{checkpoint(sp(r),thread);const auto i=arg(r,1);observe_borrows(event_kind::release_borrowed,i,arg(r,0));const auto depth=m.depth;m.depth=0;m.release(i,arg(r,0));m.depth=depth;trace_actor(s.point,i);break;}
    case kind::controller_free:m.free(r[0],s.point);if(e4_core_state::installed&&e4_core_state::on_controller_freed)e4_core_state::on_controller_freed(r[7]);break;
    case kind::resolve_hit:case kind::resolve_refused:{const auto token=r[1],i=index(token);
        if(i<actor_ai::installed().controllers){m.slots[i].generation=generation(i);m.resolve(i,token,arg(r,1),s.type==kind::resolve_hit);}break;}
    case kind::team_resolve_hit:case kind::team_resolve_refused:{
        const auto& d=actor_ai::installed();const auto token=r[1],i=token&value(d.cai_object+actor_ai::team_mask(d));
        if(token&&i<d.team_storage){const auto current=value(d.team_base+actor_ai::team_generations(d)+i*4);
            const bool dying=s.type==kind::team_resolve_hit&&team_destroying[i];
            const bool stale=s.type==kind::team_resolve_refused&&token!=current;
            if(dying||stale){m.emit(dying?event_kind::team_resolve_destroy:event_kind::team_stale,0x10000|i,arg(r,1),token);m.events[(m.head-1)%64].generation=current;}
        }break;}
    case kind::reset:checkpoint(sp(r),thread);m.resetting=true;break;
    // 523660 resets CAI session/team metadata; it never clears the controller
    // arena (5238B0). Keep controller lifetimes for the later 523977 releases.
    case kind::reset_done:m.resetting=false;team_destroying.fill(false);teams.fill({});break;
    case kind::borrow_update:case kind::borrow_handler:case kind::borrow_event:
        if(!post){u32 i=max_slots;
            if(s.type==kind::borrow_update)i=controller(r[6]);
            else if(s.type==kind::borrow_event)i=controller(r[1]);
            else i=s.point==0x504f64?controller(r[1]):controller(value(r[6]+4));
            // Non-embedded handlers still have the owner back-pointer at +4.
            if(current_context->depth<max_depth){current_context->borrow_sp[current_context->depth]=sp(r);m.push(i,return_address);current_context->depth=m.depth;current_context->borrows=m.borrows;}
            else {++current_context->borrow_overflow;m.emit(event_kind::overflow,i,s.point);}
            if(s.type==kind::borrow_update)trace_actor(s.point,i);
        } else if(current_context->borrow_overflow){--current_context->borrow_overflow;}
        else if(current_context->depth){const auto i=current_context->borrows[current_context->depth-1].slot;m.pop(return_address);current_context->depth=m.depth;current_context->borrows=m.borrows;
            if(s.type==kind::borrow_update)trace_actor(s.point,i,r[7]&0xff);}
        else m.emit(event_kind::transition,max_slots,s.point);
        break;
    case kind::damage_enter:
        if(current_context->damage_depth<current_context->damage_stack.size()){
            auto& d=current_context->damage_stack[current_context->damage_depth++];d.caller=arg(r,0);d.entity=r[6];d.descriptor=arg(r,1);
            const auto pool=value(0x499b42);d.live=value(pool+(actor_ai::installed().stage?0x3670:0xe18));
            float delay=0,threshold=0;read(d.descriptor+4,&delay,4);read(0x680030,&threshold,4);
            d.reason=classify(delay,threshold,value(d.descriptor+0x14,1),value(d.entity+0x2fc),d.live,actor_ai::installed().stage?124:32);
        }else {++current_context->damage_overflow;m.emit(event_kind::overflow,max_slots,s.point);}
        break;
    case kind::damage_exit:
        if(current_context->damage_overflow){--current_context->damage_overflow;break;}
        if(current_context->damage_depth){const auto d=current_context->damage_stack[--current_context->damage_depth];const auto result=r[7]&0xff;
            if(bool(result)!=(d.reason==damage_reason::accepted))m.emit(event_kind::damage_mismatch,max_slots,d.caller,result);
            const bool would=(d.reason==damage_reason::accepted||d.reason==damage_reason::full)&&would_refuse(d.live);
            refusals[refusal_head++%64]={d,result,m.step,would};
            auto* c=&damage_callers.back();for(auto& row:damage_callers)if(row.caller==d.caller||!row.caller){c=&row;break;}
            c->caller=d.caller;++c->reasons[unsigned(d.reason)];if(would)++c->would;
            if(d.caller!=0x45102e&&d.caller!=0x4efad7&&d.caller!=0x54687a&&d.caller!=0x548e30&&d.caller!=0x54bf4f)++c->unknown;
        }else m.emit(event_kind::transition,max_slots,s.point);
        break;
    case kind::field_store:{const auto f=s.arg&255,i=controller(r[s.arg>>8]);
        if(i<max_slots&&f<field_count&&current_context->writer_depth){const auto& w=current_context->writers[current_context->writer_depth-1];
            provenance_table[i][f]={value(control_address(i)+fields[f],widths[f]),w.point,w.team,m.step,false};}
        break;}
    case kind::writer_enter:writer_enter(s.arg,r[6]);break;
    case kind::writer_exit:writer_exit(s.arg);break;
    case kind::timeout_enter:writer_enter(0x527640,r[6]);break;
    case kind::timeout_exit:writer_exit(0x527640);break;
    case kind::team_free:{const auto i=arg(r,1);if(i<actor_ai::installed().team_storage)team_destroying[i]=true;break;}
    case kind::team_free_done:if(r[0]<actor_ai::installed().team_storage){
        const auto token=team_token(actor_ai::installed().team_base+r[0]*actor_ai::installed().team_stride);
        team_destroying[r[0]]=false;
        if(teams[r[0]].token==token)teams[r[0]]={};
        for(auto& row:provenance_table)for(auto& f:row)if(f.team&&f.team==token)f.dissolved=true;
    }break;
    case kind::formation_read:{const auto i=controller(r[1]);
        const auto valid=value(r[1]+0x2ec,1);
        const bool consumed=s.point==0x5068cb?(valid&&!value(sp(r)+0x13,1)&&!value(sp(r)+0x12,1)):(valid!=(r[4]&255));
        if(consumed&&i<max_slots&&provenance_table[i][10].dissolved)m.emit(event_kind::formation,i,s.point,provenance_table[i][10].team);break;}
    case kind::reregister:{const auto i=controller(r[1]);if(i<max_slots){take_snapshot(i,s.point,post?r[7]:value(r[1]+0x538),post);trace_actor(s.point,i,post?logical_team(r[7]):0);}break;}
    }
    const auto totals=m.tally();
    const u32 samples[]{totals.published,totals.constructing,totals.destroying,totals.orphaned,totals.reserved};
    for(unsigned n=0;n<accounting_peaks.size();++n)accounting_peaks[n]=(std::max)(accounting_peaks[n],samples[n]);
    auto* completed=current_context;
    const bool recycled=recycle_context(completed,thread);
    ReleaseSRWLockExclusive(&mutex);if(!recycled)completed->inside=false;
    if(core_boundary&&e4_core_state::installed&&e4_core_state::on_boundary)e4_core_state::on_boundary();
}
LONG CALLBACK first_chance(EXCEPTION_POINTERS* p) {
    if(installed&&current_context&&!current_context->inside){AcquireSRWLockExclusive(&mutex);
        m.exception(p->ExceptionRecord->ExceptionCode,addr(p->ExceptionRecord->ExceptionAddress),GetCurrentThreadId());
        ReleaseSRWLockExclusive(&mutex);
    }return EXCEPTION_CONTINUE_SEARCH;
}
}
extern "C" EXCEPTION_DISPOSITION __cdecl e4_unwind_callback(EXCEPTION_RECORD* exception,void* registration,CONTEXT*,void*) {
    auto& frame=*static_cast<e4_unwind::frame*>(registration);
    if(installed&&(exception->ExceptionFlags&(EXCEPTION_UNWINDING|EXCEPTION_EXIT_UNWIND))&&!(exception->ExceptionFlags&EXCEPTION_TARGET_UNWIND)&&frame.serial&&!frame.closed){
        if(!current_context)fatal("E4 exception crossed an untracked boundary");
        current_context->inside=true;AcquireSRWLockExclusive(&mutex);
        boundary_finish(*static_cast<e4_unwind::frame*>(registration),true);
        auto* completed=current_context;const bool recycled=recycle_context(completed,GetCurrentThreadId());
        ReleaseSRWLockExclusive(&mutex);if(!recycled)completed->inside=false;
    }
    return ExceptionContinueSearch;
}
#ifdef XML2_E4_TEST
void test_emit(unsigned point,bool post,const unsigned* registers){
    for(unsigned n=0;n<e4_observe_sites::sites.size();++n)if(e4_observe_sites::sites[n].point==point){observe(n*2+(post?1:0),registers);return;}
}
#endif
void core_checkpoint(){
    if(!installed)return;
    if(!attach_context())fatal("E4 core checkpoint context capacity exhausted");
    if(current_context->inside)fatal("E4 recursive core checkpoint");
    current_context->inside=true;AcquireSRWLockExclusive(&mutex);checkpoint(0,GetCurrentThreadId());
    auto* completed=current_context;const bool recycled=recycle_context(completed,GetCurrentThreadId());
    ReleaseSRWLockExclusive(&mutex);if(!recycled)completed->inside=false;
}
void core_team_lifecycle(unsigned i,bool after){
    if(!installed||i>=max_slots)return;AcquireSRWLockExclusive(&mutex);
    if(!after)team_destroying[i]=true;
    else {const auto& d=actor_ai::installed();const auto token=team_token(d.team_base+i*d.team_stride);
        team_destroying[i]=false;if(teams[i].token==token)teams[i]={};
        for(auto& row:provenance_table)for(auto& f:row)if(f.team&&f.team==token)f.dissolved=true;
    }ReleaseSRWLockExclusive(&mutex);
}
void core_fold_snapshot(unsigned i,std::uint32_t token,bool after){
    if(!installed||i>=max_slots)return;AcquireSRWLockExclusive(&mutex);take_snapshot(i,0xe4000001,token,after);ReleaseSRWLockExclusive(&mutex);
}
void core_event_borrow(std::uint32_t brain,bool post,std::uint32_t returned){
    if(!installed)return;if(!attach_context())fatal("E4 null-team callback context exhausted");
    if(current_context->inside)fatal("E4 null-team callback reentry");current_context->inside=true;AcquireSRWLockExclusive(&mutex);
    m.depth=current_context->depth;m.borrows=current_context->borrows;
    if(!post){if(current_context->depth<max_depth){current_context->borrow_sp[current_context->depth]=0;m.push(controller(brain),returned);current_context->depth=m.depth;current_context->borrows=m.borrows;}else{++current_context->borrow_overflow;m.emit(event_kind::overflow,controller(brain),returned);}}
    else if(current_context->borrow_overflow)--current_context->borrow_overflow;
    else if(current_context->depth){m.pop(returned);current_context->depth=m.depth;current_context->borrows=m.borrows;}
    else m.emit(event_kind::transition,controller(brain),returned);
    auto* completed=current_context;const bool recycled=recycle_context(completed,GetCurrentThreadId());ReleaseSRWLockExclusive(&mutex);if(!recycled)completed->inside=false;
}
bool contribute_core(patch_transaction::transaction& tx,HMODULE game,std::string& why){
    if(!enabled()||addr(game)!=0x400000||installed){why="mandatory observer not available for preparation";return false;}
    constexpr unsigned capacity=65536;
    auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    context_bank=static_cast<context_slot*>(VirtualAlloc(nullptr,sizeof(context_slot)*context_capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(context_bank)for(unsigned n=0;n<context_capacity;++n)::new(context_bank+n) context_slot{};
    const auto release=[code]{if(code)VirtualFree(code,0,MEM_RELEASE);if(context_bank){VirtualFree(context_bank,0,MEM_RELEASE);context_bank=nullptr;}};
    std::vector<patch_transaction::write> pending;
    tx.compose([&](auto& contributions){for(const auto& c:contributions)for(const auto& w:c.writes)pending.push_back(w);return true;});
    auto p=code&&context_bank?e4_observe_plan::make(addr(code),addr(reinterpret_cast<const void*>(&observe)),patch_transaction::verified_writes,patch_transaction::verified_originals,&why,addr(reinterpret_cast<const void*>(&e4_unwind_handler)),pending,true,e4_core_state::elide_power):std::nullopt;
    if(!p||p->code.size()>capacity){release();return false;}
    std::memcpy(code,p->code.data(),p->code.size());DWORD old=0;
    if(!VirtualProtect(code,capacity,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code,p->code.size())){why="observer RX protection/flush failed";release();return false;}
    veh=AddVectoredExceptionHandler(1,first_chance);if(!veh){why="first-chance observer unavailable";release();return false;}
    // The exact pending operands have already been emitted into the observed
    // instruction copies. Remove only bytes those observation detours own.
    const auto spans=p->contribution.writes;
    tx.compose([&](auto& contributions){for(auto& c:contributions){std::vector<patch_transaction::write> kept;
        for(const auto& w:c.writes){patch_transaction::write part{0,{},w.what};
            for(unsigned n=0;n<w.bytes.size();++n){const auto va=w.va+n;bool covered=false;for(const auto& s:spans)if(va>=s.va&&va<s.va+s.bytes.size()){covered=true;break;}
                if(covered){if(!part.bytes.empty()){kept.push_back(std::move(part));part={0,{},w.what};}}
                else {if(part.bytes.empty())part.va=va;part.bytes.push_back(w.bytes[n]);}
            }if(!part.bytes.empty())kept.push_back(std::move(part));
        }c.writes=std::move(kept);
    }return true;});
    const auto count=static_cast<unsigned>(p->contribution.guards.size());
    std::vector<actor_ai::observed_span> published_guards;
    for(std::size_t i=0;i<p->contribution.writes.size();++i)published_guards.push_back({p->contribution.writes[i].va,p->contribution.guards[i].bytes,p->contribution.writes[i].bytes});
    p->contribution.publish=[count,published_guards=std::move(published_guards)]()mutable{installed=true;installed_spans=count;actor_ai::observed_spans=std::move(published_guards);e4_core_state::on_event_borrow=&core_event_borrow;logger::write("E4: mandatory 96-controller instrumentation installed (%u spans)",count);};
    p->contribution.abandon=[release]{if(veh){RemoveVectoredExceptionHandler(veh);veh=nullptr;}release();};
    p->contribution.retain=[]{game_version::refuse_startup();};
    return tx.add(std::move(p->contribution));
}
void install(HMODULE game) {
    if(!enabled()||addr(game)!=0x400000)return;
    if(installed)return;
    if(actor_ai::installed().controllers!=32){logger::write("E4: observer metadata requires the pre-core 32-controller layout");game_version::refuse_startup();return;}
    constexpr unsigned capacity=65536;
    auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    context_bank=static_cast<context_slot*>(VirtualAlloc(nullptr,sizeof(context_slot)*context_capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(context_bank)for(unsigned n=0;n<context_capacity;++n)::new (context_bank+n) context_slot{};
    const auto release=[&]{if(code)VirtualFree(code,0,MEM_RELEASE);if(context_bank){VirtualFree(context_bank,0,MEM_RELEASE);context_bank=nullptr;}};
    std::string why;if(!code||!context_bank)why="observer code or per-thread storage allocation failed";
    auto p=code&&context_bank?e4_observe_plan::make(addr(code),addr(reinterpret_cast<const void*>(&observe)),patch_transaction::verified_writes,patch_transaction::verified_originals,&why,addr(reinterpret_cast<const void*>(&e4_unwind_handler))):std::nullopt;
    if(!p||p->code.size()>capacity){release();logger::write("E4: preparation refused: %s",why.c_str());game_version::refuse_startup();return;}
    std::memcpy(code,p->code.data(),p->code.size());DWORD old=0;
    if(!VirtualProtect(code,capacity,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code,p->code.size())){
        release();game_version::refuse_startup();return;}
    veh=AddVectoredExceptionHandler(1,first_chance);
    if(!veh){release();game_version::refuse_startup();return;}
    bool retained=false;
    const auto span_count=static_cast<u32>(p->contribution.guards.size());
    p->contribution.publish=[span_count] {installed=true;installed_spans=span_count;};
    p->contribution.retain=[&] {retained=true;game_version::refuse_startup();};
    patch_transaction::transaction tx;tx.add(std::move(p->contribution));const auto result=tx.commit();
    if(!installed&&!retained){RemoveVectoredExceptionHandler(veh);veh=nullptr;release();game_version::refuse_startup();}
    logger::write("E4Observe: %s (%s); observe-only, actors/controllers=32; %zu spans, %zu function regions",
        patch_transaction::name(result),tx.report().c_str(),std::size_t(installed_spans),e4_observe_sites::regions.size());
}
std::string json() {
    const shared_lock held;
    std::ostringstream out;const auto a=m.tally();
    out<<"{\"enabled\":"<<(enabled()?"true":"false")<<",\"installed\":"<<(installed?"true":"false")<<",\"spans\":"<<installed_spans<<",\"step\":"<<m.step
       <<",\"admitted\":"<<m.admitted<<",\"published_total\":"<<m.published<<",\"boundary_unwinds\":"<<boundary_unwinds<<",\"escaped\":"<<m.escaped
       <<",\"published\":"<<a.published<<",\"constructing\":"<<a.constructing<<",\"destroying\":"<<a.destroying<<",\"orphaned\":"<<a.orphaned
       <<",\"accounting_suppressed\":"<<m.accounting_suppressed<<",\"reserved\":"<<a.reserved<<",\"context_bytes\":"<<sizeof(context_slot)*context_capacity<<",\"context_capacity\":"<<context_capacity<<",\"contexts_live\":"<<contexts_live<<",\"contexts_peak\":"<<contexts_peak<<",\"probe_faults\":"<<probe_faults;
    if(e4_core_state::installed&&e4_core_state::on_snapshot){u32 core[6]{};e4_core_state::on_snapshot(core);out<<",\"native_reserved\":"<<core[0]<<",\"pending_reservations\":["<<core[1]<<','<<core[2]<<','<<core[3]<<"],\"foldbacks\":"<<core[4]<<",\"stale_fold_retirements\":"<<core[5];}
    out<<",\"counts\":[";
    for(unsigned n=0;n<m.counts.size();++n){if(n)out<<',';out<<m.counts[n];}out<<"],\"events\":[";
    const auto count=(std::min)(m.head,u32(64));
    for(unsigned n=0;n<count;++n){if(n)out<<',';const auto& e=m.events[(m.head-count+n)%64];
        out<<'['<<unsigned(e.kind)<<','<<e.slot<<','<<e.caller<<','<<e.generation<<','<<e.step<<','<<e.detail<<','<<e.borrow_count<<",[";for(unsigned k=0;k<(std::min)(e.borrow_count,u32(max_depth));++k){if(k)out<<',';out<<e.borrow_returns[k];}out<<"]]";}
    out<<"],\"event_callers\":[";bool event_comma=false;
    for(const auto& row:m.callers)if(row.caller){if(event_comma)out<<',';event_comma=true;out<<'['<<row.caller;for(auto c:row.counts)out<<','<<c;out<<']';}
    out<<"],\"accounting_peaks\":[";
    for(unsigned n=0;n<accounting_peaks.size();++n){if(n)out<<',';out<<accounting_peaks[n];}
    out<<"],\"phase_counts\":[";
    for(unsigned n=0;n<m.phases.size();++n){if(n)out<<',';out<<m.phases[n];}
    out<<"],\"open_constructions\":[";bool open_comma=false;
    for(const auto& c:m.records)if(c.open){if(open_comma)out<<',';open_comma=true;out<<'['<<unsigned(c.at)<<','<<c.actor<<','<<c.slot<<','<<c.esp<<','<<c.seh<<','<<c.code<<','<<c.fault<<','<<c.thread<<','<<c.boundary<<','<<c.unwound<<']';}
    out<<"],\"slots\":[";
    for(unsigned n=0;n<actor_ai::installed().controllers;++n){if(n)out<<',';const auto& slot=m.slots[n];out<<'['<<n<<','<<unsigned(slot.at)<<','<<slot.generation<<','<<slot.owner<<']';}
    out<<"],\"damage_callers\":[";bool comma=false;
    for(const auto& c:damage_callers)if(c.caller){if(comma)out<<',';comma=true;out<<'['<<c.caller;
        for(auto n:c.reasons)out<<','<<n;out<<','<<c.would<<','<<c.unknown<<']';}
    out<<"],\"damage\":[";const auto dc=(std::min)(refusal_head,u32(64));
    for(unsigned n=0;n<dc;++n){if(n)out<<',';const auto& d=refusals[(refusal_head-dc+n)%64];
        out<<'['<<d.data.caller<<','<<d.data.entity<<','<<d.data.descriptor<<','<<d.data.live<<','<<unsigned(d.data.reason)<<','<<d.result<<','<<d.step<<','<<d.would<<']';}
    out<<"],\"snapshots\":[";const auto sc=(std::min)(snapshot_head,u32(64));
    for(unsigned n=0;n<sc;++n){if(n)out<<',';const auto& s=snapshots[(snapshot_head-sc+n)%64];
        out<<"{\"point\":"<<s.point<<",\"slot\":"<<s.controller<<",\"team\":"<<s.team<<",\"step\":"<<s.step<<",\"after\":"<<s.after<<",\"fields\":[";
        for(unsigned f=0;f<field_count;++f){if(f)out<<',';const auto& p=s.sources[f];out<<'['<<fields[f]<<','<<s.values[f]<<','<<p.value<<','<<p.writer<<','<<p.team<<','<<p.step<<','<<p.dissolved<<']';}out<<"]}";
    }
    out<<"],\"trace\":[";const auto tc=(std::min)(trace_head,u32(256));
    for(unsigned n=0;n<tc;++n){if(n)out<<',';const auto& t=traces[(trace_head-tc+n)%256];out<<'['<<t.point<<','<<t.actor<<','<<t.team<<','<<t.result<<','<<t.step<<']';}
    out<<"]}";return out.str();
}
void sample() {
    if(!installed)return;
    const auto now=GetTickCount();
    AcquireSRWLockExclusive(&mutex);
    const auto next=m.head+refusal_head+snapshot_head+trace_head+m.admitted+m.published;
    const bool changed=next!=logged&&(!sampled||now-logged_at>=1000);
    if(changed){logged=next;logged_at=now;sampled=true;}
    ReleaseSRWLockExclusive(&mutex);
    if(changed){
        try {logger::write("E4 observation: %s",json().c_str());}
        catch(...) {} // logging/formatting only: never unwind telemetry into the game
    }
}
}
