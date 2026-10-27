// Compare the exact emitted MoveTriggers bytes to the supplied 69f5115 base.
#include "move_triggers_rules.hpp"
#define limits_rules baseline_limits_rules
#define ini_rules baseline_ini_rules
#define move_triggers_rules baseline_move_triggers_rules
#include "baseline/move_triggers_rules.hpp"
#undef move_triggers_rules
#undef ini_rules
#undef limits_rules
#include <cstdio>
#include <cassert>
int main()
{
    unsigned checks=0;
    for(int cap:{20,64,127})for(bool scheduler:{false,true}){
        const move_triggers_rules::scheduler_hooks now{0x10010000,0x10010100,0x10010200,0x10010300,0x10010400,0x10010500,0x10010600,0x10010700};
        const baseline_move_triggers_rules::scheduler_hooks old{0x10010000,0x10010100,0x10010200,0x10010300,0x10010400,0x10010500,0x10010600,0x10010700};
        const auto a=move_triggers_rules::make_plan(cap,0x10010800,0x10020000,scheduler?&now:nullptr);
        const auto b=baseline_move_triggers_rules::make_plan(cap,0x10010800,0x10020000,scheduler?&old:nullptr);
        assert(a.code==b.code&&a.writes.size()==b.writes.size());++checks;
        for(std::size_t i=0;i<a.writes.size();++i){assert(a.writes[i].va==b.writes[i].va&&a.writes[i].size==b.writes[i].size&&a.writes[i].value==b.writes[i].value);++checks;}
    }
    for(std::size_t i=0;i<move_triggers_rules::guards.size();++i){
        const auto a=move_triggers_rules::descriptor_guard(move_triggers_rules::guards[i],false);
        const auto b=baseline_move_triggers_rules::guards[i];assert(a.va==b.va&&a.hex==b.hex);++checks;
    }
    std::printf("PASS OFF vs 69f5115: zero MoveTriggers emitted-byte differences; %u checks\n",checks);
}
