#include "e4_observe_rules.hpp"
#include "test_input_rules.hpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace e4_observe_rules;
namespace {unsigned checks=0,failures=0;void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}}
int main(){
    monitor m;
    m.take(3,131,0x10);m.publish(3,0x1000,0x11);m.push(3,0x1234);m.release(3,0x20);
    m.resolve(3,131,0x22,true);m.free(3,0x23);m.take(3,259,0x24);m.pop(0x1234);
    m.publish(4,0x2000,0x30);m.handler(4,0x3000,0x1234,0x3000);m.resolve(3,131,0x40,false);
    for(auto k:{event_kind::release_borrowed,event_kind::reuse_borrowed,event_kind::resolve_destroy,event_kind::transition,event_kind::handler,event_kind::stale})
        check(m.counts[unsigned(k)]==1,"T19 exactly one of each synthetic lifetime event");
    check(m.events[0].caller==0x20&&m.events[0].detail==0x1234&&m.events[0].borrow_count==1&&m.events[0].borrow_returns[0]==0x1234,"T19 release records source and borrow caller");
    check(m.events[1].caller==0x22&&m.events[2].caller==0x24,"T19 destruction/reuse caller attribution");
    u32 rng=0x42;bool clean=true;
    for(unsigned run=0;run<10000;++run){monitor normal;
        for(unsigned op=0;op<64;++op){rng=rng*1664525+1013904223;const unsigned i=(rng>>16)%32;
            if(normal.slots[i].at==state::free){normal.take(i,(op+1)*128+i,1);normal.publish(i,0x1000+i*0x844,2);
                normal.handler(i,0x1000+i*0x61c,0,0);
            }else{normal.push(i,3);normal.push(i,4);normal.resolve(i,normal.slots[i].generation,5,true);normal.pop(4);normal.pop(3);normal.release(i,6);normal.free(i,7);}
        }for(auto c:normal.counts)clean&=c==0;
    }check(clean,"T19 zero events on 10,000 retail-shaped nested-borrow sequences");
    // A native/test-vtable throw is swallowed by the host, then both continuation
    // checkpoints must detect its still-open construction; ordinary nested
    // creation remains below the saved frame and does not trigger.
    unsigned stops=0;
    for(unsigned p=0;p<4;++p)for(unsigned boundary=0;boundary<2;++boundary){monitor tracker;
        tracker.admit(0,0x1000);tracker.enter(0x1000,0x2000,0x3000);tracker.records[0].at=phase(p);
        try{tracker.exception(0xe06d7363,0x401234);throw std::runtime_error("native test vtable");}catch(const std::runtime_error&){}
        tracker.unwind_boundary(0,0);
        if(tracker.checkpoint(boundary?0x2100:0x1800))++stops;
        check(tracker.escaped==1&&tracker.events[(tracker.head-1)%64].detail==p&&tracker.records[0].fault==0x401234,"T20 phase, exception, address retained after host recovery");
    }check(stops==8,"T20 fail-stop seam invoked for every phase at allocation and boundary");
    monitor closed;closed.admit(0,1);closed.enter(1,0x1000,0);closed.advance(1,phase::published);closed.exception(0xe06d7363,0x401234);check(!closed.checkpoint(0x2000),"T20 exception after PUBLISHED cannot escape an already closed record");
    monitor nested;nested.admit(0,0x1000,1);nested.enter(0x1000,0x2000,0);nested.admit(1,0x1100,1);nested.enter(0x1100,0x1800,0);
    check(!nested.checkpoint(0x1700,1)&&!nested.checkpoint(0x3000,2),"T20 live nested frames and another thread never count as escapes");
    nested.advance(0x1100,phase::published);nested.advance(0x1000,phase::published);
    check(!nested.checkpoint(0x3000,1)&&nested.published==2,"T20 ordinary nested construction never stops");
    bool accounting_ok=true;
    unsigned nested_faults=0;
    for(unsigned run=0;run<10000;++run){monitor a;std::array<unsigned,16> order{};
        for(unsigned n=0;n<16;++n)order[n]=n;
        for(unsigned n=15;n>0;--n){rng=rng*1664525+1013904223;const auto pick=rng%(n+1);auto v=order[n];order[n]=order[pick];order[pick]=v;}
        for(unsigned n=0;n<16;++n){const auto i=order[n];a.admit(i,0x1000+i);a.enter(0x1000+i,0x2000-n*32,0);a.take(i,128+i,0);a.records[i].slot=i;
            a.publish(i,0x1000+i,0);a.advance(0x1000+i,phase::published);accounting_ok&=a.account(n+1,32,n+1,true);}
        for(unsigned n=15;n>0;--n){rng=rng*1664525+1013904223;const auto pick=rng%(n+1);auto v=order[n];order[n]=order[pick];order[pick]=v;}
        for(unsigned n=0;n<16;++n){const auto i=order[n];a.release(i,0);accounting_ok&=a.account(16-n,32,16-n,false);a.free(i,0);accounting_ok&=a.account(15-n,32,15-n,true);}
        monitor nested_churn;nested_churn.admit(0,0x3000);nested_churn.enter(0x3000,0x2000,0);
        nested_churn.admit(1,0x4000);nested_churn.enter(0x4000,0x1800,0);nested_churn.take(1,129,0);nested_churn.records[1].slot=1;
        accounting_ok&=nested_churn.account(1,32,0,false);
        if(run%97==0){nested_churn.exception(0xe06d7363,0x401234);nested_churn.records[0].boundary=1;nested_churn.records[1].boundary=2;nested_churn.unwind_boundary(2,0);const auto stopped=nested_churn.checkpoint(0x1700);
            accounting_ok&=stopped&&nested_churn.tally().orphaned==1&&nested_churn.tally().reserved==1&&nested_churn.account(1,32,0,false);++nested_faults;
        }else{nested_churn.publish(1,0x4000,0);nested_churn.advance(0x4000,phase::published);
            accounting_ok&=nested_churn.account(1,32,1,false);nested_churn.take(0,128,0);nested_churn.records[0].slot=0;
            nested_churn.publish(0,0x3000,0);nested_churn.advance(0x3000,phase::published);accounting_ok&=nested_churn.account(2,32,2,true);}
    }check(accounting_ok&&nested_faults==104,"T23 10,000 randomized allocation/free permutations and nested sequences, including 104 injected escapes");
    monitor escaped;escaped.admit(0,1);escaped.enter(1,0x1000,0);escaped.take(0,128,0);escaped.records[0].slot=0;
    escaped.unwind_boundary(0,0);
    check(escaped.checkpoint(0x1000)&&escaped.tally().orphaned==1,"T23 escaped construction becomes a separate orphan count");
    monitor transient;transient.take(0,128,0);
    check(transient.account(1,32,0,false)&&!transient.account(1,32,0,true),"T23 constructing is legal only within a non-quiescent interval");
    monitor repeats;
    for(unsigned n=0;n<6000;++n){repeats.step=n;repeats.account(1,32,0,true);}
    check(repeats.counts[unsigned(event_kind::accounting)]==50&&repeats.accounting_suppressed==5950,"accounting repeats emit once per 120 steps, not every frame");
    repeats.account(0,32,0,true);repeats.account(1,32,0,true);
    check(repeats.counts[unsigned(event_kind::accounting)]==51,"accounting recovery re-arms an immediate mismatch report");
    repeats.account(2,32,0,true);
    check(repeats.counts[unsigned(event_kind::accounting)]==52,"changed accounting mismatch reports immediately");
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    check(classify(nan,0,0,0,0,124)==damage_reason::unsuitable,"T24 unordered delay is unsuitable");
    check(classify(nan,0,1,0,0,124)==damage_reason::accepted,"T24 flag bit overrides unordered delay");
    check(classify(1,0,0,1,124,124)==damage_reason::pending,"T24 pending takes precedence over full");
    check(classify(1,0,0,0,124,124)==damage_reason::full,"T24 full at 124");
    check(!would_refuse(31)&&would_refuse(32)&&would_refuse(33),"T24 would-refuse threshold is pre-allocation occupancy >=32");
    check(test_encoding(0).shift==7&&test_encoding(1).mask==255&&test_encoding(2).seed==(5<<7)&&test_encoding(3).reverse,"O4 test-only encodings prepared; full T21 deferred to core");
    auto pipe=test_input_rules::parse_command("E4_OBSERVE");check(pipe.what==test_input_rules::command::kind::e4_observe,"E4 pipe command parsed");
    std::printf("E4 rules: %u checks, %u failures; T19 10,000 sequences; T20 8 open-phase/checkpoint faults; T23 10,000 churn sequences\n",checks,failures);
    return failures?1:0;
}
