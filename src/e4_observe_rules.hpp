#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace e4_observe_rules {
using u32=std::uint32_t;
constexpr unsigned max_slots=96,max_depth=128;
enum class state:unsigned {free,reserved,constructing,published,destroying,orphaned};
enum class phase:unsigned {admitted,actor_init,controller,init_returned,published};
enum class event_kind:unsigned {release_borrowed,reuse_borrowed,resolve_destroy,transition,handler,stale,escape,accounting,overflow,formation,damage_mismatch,exception,team_resolve_destroy,team_stale};
struct event {event_kind kind{};u32 slot=0,caller=0,generation=0,step=0,detail=0,borrow_count=0;std::array<u32,max_depth> borrow_returns{};};
struct caller_events {u32 caller=0;std::array<u32,14> counts{};};
struct borrow {u32 slot=max_slots,caller=0;};
struct construction {bool open=false;phase at=phase::admitted;u32 actor=0,slot=max_slots,esp=0,seh=0,code=0,fault=0,thread=0,logical=0,boundary=0;bool unwound=false,reported=false;};
struct slot {state at=state::free;u32 generation=0,owner=0;};
struct accounting {u32 published=0,constructing=0,destroying=0,orphaned=0,reserved=0;};
struct monitor {
    std::array<slot,max_slots> slots{};
    std::array<construction,max_slots> records{};
    std::array<borrow,max_depth> borrows{};
    std::array<event,64> events{};
    std::array<u32,14> counts{};
    std::array<caller_events,128> callers{};
    std::array<u32,5> phases{};
    u32 step=0,head=0,depth=0,admitted=0,published=0,escaped=0;
    bool resetting=false;
    struct accounting_repeat {bool active=false;std::array<u32,8> signature{};u32 at=0;};
    std::array<accounting_repeat,2> accounting_reports{};
    u32 accounting_suppressed=0;
    void account_event(unsigned source,bool ok,const std::array<u32,8>& signature,u32 detail){
        auto& last=accounting_reports[source];
        if(ok){last.active=false;return;}
        if(!last.active||last.signature!=signature||step-last.at>=120){
            emit(event_kind::accounting,max_slots,0,detail);last={true,signature,step};
        }else ++accounting_suppressed;
    }
    void emit(event_kind k,u32 i,u32 caller=0,u32 detail=0) {
        ++counts[static_cast<unsigned>(k)];
        auto* bucket=&callers[0];
        if(caller)for(unsigned n=1;n<callers.size();++n)if(callers[n].caller==caller||!callers[n].caller){bucket=&callers[n];bucket->caller=caller;break;}
        ++bucket->counts[static_cast<unsigned>(k)];events[head++%64]={k,i,caller,i<max_slots?slots[i].generation:0,step,detail};
    }
    bool borrowed(u32 i) const {for(unsigned j=0;j<depth;++j)if(borrows[j].slot==i)return true;return false;}
    void push(u32 i,u32 caller) {
        if(depth==max_depth){emit(event_kind::overflow,i,caller);return;}
        borrows[depth++]={i,caller};
    }
    void pop(u32 caller) {
        if(!depth){emit(event_kind::transition,max_slots,caller);return;}
        if(borrows[depth-1].caller!=caller)emit(event_kind::transition,borrows[depth-1].slot,caller);
        --depth;
    }
    u32 borrow_caller(u32 i) const {for(unsigned j=depth;j>0;--j)if(borrows[j-1].slot==i)return borrows[j-1].caller;return 0;}
    void emit_borrow(event_kind kind,u32 i,u32 caller){
        emit(kind,i,caller,borrow_caller(i));auto& e=events[(head-1)%64];
        for(unsigned n=0;n<depth;++n)if(borrows[n].slot==i){if(e.borrow_count<max_depth)e.borrow_returns[e.borrow_count]=borrows[n].caller;++e.borrow_count;}
    }
    void take(u32 i,u32 generation,u32 caller) {
        if(i>=max_slots){emit(event_kind::transition,i,caller);return;}
        if(borrowed(i))emit_borrow(event_kind::reuse_borrowed,i,caller);
        if(slots[i].at!=state::free)emit(event_kind::transition,i,caller);
        slots[i]={state::constructing,generation,0};
    }
    void publish(u32 i,u32 actor,u32 caller) {
        if(i>=max_slots){emit(event_kind::transition,i,caller);return;}
        if(slots[i].at!=state::constructing)emit(event_kind::transition,i,caller);
        slots[i].at=state::published;slots[i].owner=actor;
    }
    void release(u32 i,u32 caller) {
        if(i>=max_slots){emit(event_kind::transition,i,caller);return;}
        if(borrowed(i))emit_borrow(event_kind::release_borrowed,i,caller);
        if(slots[i].at!=state::published)emit(event_kind::transition,i,caller);
        slots[i].at=state::destroying;
    }
    void free(u32 i,u32 caller) {
        if(i>=max_slots){emit(event_kind::transition,i,caller);return;}
        if(slots[i].at!=state::destroying)emit(event_kind::transition,i,caller);
        slots[i].at=state::free;slots[i].owner=0;
    }
    void resolve(u32 i,u32 token,u32 caller,bool hit) {
        if(i>=max_slots)return;
        if(hit && slots[i].at==state::destroying)emit(event_kind::resolve_destroy,i,caller);
        if(!hit && token && token!=slots[i].generation)emit(event_kind::stale,i,caller,token);
    }
    void handler(u32 i,u32 address,u32 pointer,u32 back) {
        if(pointer && (pointer!=address+0x4c8 || back!=address))emit(event_kind::handler,i,0,pointer);
    }
    void admit(u32 i,u32 actor,u32 thread=0) {
        if(i>=max_slots){emit(event_kind::overflow,i);return;}
        if(records[i].open)emit(event_kind::transition,i);
        records[i]={true,phase::admitted,actor};records[i].thread=thread;records[i].logical=++admitted;++phases[0];
    }
    construction* find(u32 actor) {for(auto& c:records)if(c.open&&c.actor==actor)return &c;return nullptr;}
    construction* inner(u32 thread=0) {construction* out=nullptr;for(auto& c:records)if(c.open&&!c.unwound&&c.thread==thread&&c.esp&&(!out||c.esp<out->esp))out=&c;return out;}
    void enter(u32 actor,u32 esp,u32 seh) {
        if(auto* c=find(actor)){c->at=phase::actor_init;c->esp=esp;c->seh=seh;++phases[1];}
        else emit(event_kind::transition,max_slots,0,actor);
    }
    void advance(u32 actor,phase p) {
        if(auto* c=find(actor)){
            c->at=p;++phases[static_cast<unsigned>(p)];
            if(p==phase::published){c->open=false;++published;}
        } else emit(event_kind::transition,max_slots,0,actor);
    }
    void exception(u32 code,u32 address,u32 thread=0) {for(unsigned i=0;i<records.size();++i){auto& c=records[i];if(c.open&&c.esp&&c.thread==thread){if(!c.code){c.code=code;c.fault=address;}emit(event_kind::exception,i,address,code);}}}
    // A first-chance exception is not an escape. Only a proven boundary exit
    // latches abandonment. Checkpoints are independent of current ESP/thread.
    void unwind_boundary(u32 boundary,u32 thread) {
        for(auto& c:records)if(c.open&&c.boundary==boundary&&c.thread==thread)c.unwound=true;
    }
    bool checkpoint(u32 esp,u32 thread=0) {
        (void)esp;(void)thread;bool stop=false;
        for(unsigned i=0;i<records.size();++i){auto& c=records[i];if(c.open&&c.unwound){
            if(!c.reported){emit(event_kind::escape,i,c.fault,static_cast<u32>(c.at));++escaped;c.reported=true;}
            if(c.slot<max_slots)slots[c.slot].at=state::orphaned;
            stop=true;
        }}return stop;
    }
    accounting tally() const {
        accounting c;
        for(const auto& s:slots)switch(s.at){
            case state::published:++c.published;break;case state::constructing:++c.constructing;break;
            case state::destroying:++c.destroying;break;case state::orphaned:++c.orphaned;break;default:break;}
        for(const auto& r:records)if(r.open&&r.slot==max_slots)++c.reserved;
        return c;
    }
    bool account(u32 live,u32 capacity,u32 actors,bool quiescent) {
        const auto c=tally();
        bool ok=live==c.published+c.constructing+c.destroying+c.orphaned&&live+c.reserved<=capacity;
        if(quiescent&&!resetting)ok=ok&&!c.constructing&&!c.destroying&&!c.orphaned&&c.published==actors;
        // Include all relevant quantities so a changed mismatch is visible immediately.
        const std::array<u32,8> signature{live,capacity,actors,c.published,
            c.constructing,c.destroying,c.orphaned,c.reserved};
        account_event(0,ok,signature,live);
        return ok;
    }
};
enum class damage_reason:unsigned {accepted,unsuitable,pending,full};
inline damage_reason classify(float delay,float threshold,u32 flags,u32 pending,u32 live,u32 capacity) {
    if(!(delay>threshold)&&!(flags&1))return damage_reason::unsuitable;
    if(pending)return damage_reason::pending;
    // Retail compares equality, not >=. Do not invent a different refusal.
    if(live==capacity)return damage_reason::full;
    return damage_reason::accepted;
}
constexpr bool would_refuse(u32 occupancy) {return occupancy>=32;}
// Test builds alone can vary token identities. Release code has no setting or
// alternate encoding reader. The complete differential needs the later core.
#ifdef XML2_E4_TEST
struct encoding {u32 mask=127,shift=7,seed=0;bool reverse=false;};
inline encoding test_encoding(unsigned variant) {
    return variant==1?encoding{255,8,0,false}:variant==2?encoding{127,7,5u<<7,false}:variant==3?encoding{127,7,0,true}:encoding{};
}
#endif
}
