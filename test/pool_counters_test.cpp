#include "pool_counter_rules.hpp"
#include "pool_counters_format.hpp"
#include "test_input_rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>

static bool count_allocations=false;
static unsigned allocations=0;
void* operator new(std::size_t n) { if(count_allocations)++allocations; if(auto p=std::malloc(n))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
int main() {
    unsigned checks=0,failures=0;
    const auto check=[&](bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}};
    using namespace pool_counter_rules;
    tracker t;
    const auto i=t.add("invented",4);
    count_allocations=true;
    t.begin("invented/zone");
    t.observe(i,3);
    check(t.state.pools[i].live==3 && t.state.pools[i].peak_zone==3,"observed live and zone peak");
    check(t.state.pools[i].peak_session==3,"session peak");
    t.observe(i,1);
    check(t.state.pools[i].live==1 && t.state.pools[i].peak_zone==3,"releasing does not erase peak");
    t.refusal(i,2);
    check(t.state.pools[i].refused==2 && t.state.pools[i].refused_zone==2,"refusal accounting");
    t.end();t.begin("invented/zone");
    check(t.state.pools[i].peak_zone==-1 && t.state.pools[i].peak_session==3,"same-name reload resets only zone");
    check(t.state.pools[i].refused_zone==0 && t.state.pools[i].refused==2,"zone refusal reset preserves session");
    t.observe(i,-1);
    check(t.state.pools[i].live==-1 && t.state.pools[i].peak_zone==-1,"unreadable remains unknown");
    t.observe(i,4);
    check(t.state.pools[i].peak_zone==4,"saturation retained");
    t.observe(i,5);
    check(t.state.pools[i].corrupt && t.state.pools[i].live==5,"overflow is visible, never clamped");
    check(allocations==0,"no allocations including zone transitions");
    count_allocations=false;
    const auto cmd=test_input_rules::parse_command("pools");
    check(cmd.error.empty() && cmd.what!=test_input_rules::command::kind::unknown,"pipe accepts pools");
    check(test_input_rules::parse_command("pools extra").what==test_input_rules::command::kind::unknown,"pipe rejects pools arguments");
    t.begin("invented\"\\\nzone");
    check(!std::strcmp(t.state.zone.data(),"invented___zone"),"zone labels cannot inject JSON or log controls");
    t.observe(i,4);t.refusal(i,1);
    const auto text=pool_counters_format::line(t.state);
    check(text.find("pools invented___zone: invented 4/4 peak 4 SAT")!=std::string::npos,"zone line includes saturation and peak");
    check(text.find("refused 1")!=std::string::npos,"zone line includes refused demand");
    const auto json=pool_counters_format::json(t.state,false);
    check(json.find("\"peak_zone\":4,\"peak_session\":5")!=std::string::npos,"JSON retains zone and session peaks");
    std::printf("pool counters: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
