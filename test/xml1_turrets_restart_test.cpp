// The production adapter state/reset/digest, with no game, native services or scheduler dispatch.
#include "../src/xml1_turrets.cpp"
#include <cassert>
#include <iostream>
namespace logger { void write(const char*,...) {} void write_once(std::string_view,const char*,...) {} }
namespace mold_query { bool supported(){assert(false);return false;} bool is_a(void*,std::uint32_t){assert(false);return false;} bool pools_ready(){assert(false);return false;} }
namespace ini { std::optional<std::string> text(const wchar_t*,const wchar_t*){assert(false);return {};} int number(const wchar_t*,const wchar_t*,int){assert(false);return 0;} bool flag(const wchar_t*,const wchar_t*,bool){assert(false);return false;} }
namespace game_version { void refuse_startup(){assert(false);} }

int main() {
    using namespace xml1_turrets;
    // Reserve the retail-address region in the fixture image before Windows creates heaps there.
    DWORD old=0;assert(VirtualProtect(reinterpret_cast<void*>(0x72a758),64,PAGE_READWRITE,&old));
    std::strcpy(reinterpret_cast<char*>(0x72a758),"fixture");
    std::strcpy(zone,"fixture");last_time=5;
    turret t;t.spec.zone="fixture";t.spec.instance="scanner";t.authored_rest={1,2,3};t.spec.rest=t.authored_rest;
    turrets.push_back(t);
    const auto dirty=[] {
        auto& t=turrets[0];t.live=true;t.handle=0x1801;t.s=turret_rules::spawn(2);
        t.s.mode=turret_rules::tracking;t.s.target=0x1802;t.s.burst=7;t.s.emitter=3;t.s.last_trace=4;
        t.s.aim={4,5,6};t.s.seen=3;t.s.last_seen=4;t.s.pause=8;t.s.sweep_back=true;
        t.b={99,88,true};t.spec.rest={7,8,9};t.effects={1,2,3};t.effects_resolved=true;t.fire_sound=4;t.rotate_sound=5;
        flights.push_back({0x1803,0x1802,{1,2,3},2});loaded_files.push_back("warm-definitions");
    };
    dirty();installed=false;const auto warm_s=turrets[0].s;
    cohort_epoch(5);assert(turrets[0].s.burst==warm_s.burst&&flights.size()==1); // uninstalled no-op
    installed=true;const auto warm=probe_digest(10);
    assert(!turret_rules::clock_went_back(5,last_time)&&std::strcmp(zone,"fixture")==0); // old reset would miss this
    cohort_epoch(5);
    const auto& r=turrets[0];const auto fresh=turret_rules::spawn(5);
    assert(!r.live&&r.handle==0&&r.s.target==0&&r.s.burst==0&&r.s.mode==turret_rules::idle);
    assert(r.s.acquire_due==fresh.acquire_due&&r.s.rotate_due==fresh.rotate_due&&r.s.fire_due==fresh.fire_due);
    assert(r.s.acquire_set==5&&r.s.rotate_set==5&&r.s.aim==vec{}&&r.s.seen==0&&r.s.last_seen==0&&r.s.pause==0);
    assert(!r.s.sweep_back&&r.s.emitter==0&&r.s.last_trace==0);
    assert(r.b.next_bind==0&&r.b.trace_at==0&&!r.b.rest_taken&&r.spec.rest==r.authored_rest);
    assert(!r.effects_resolved&&(r.effects==std::array<u32,3>{})&&r.fire_sound==0&&r.rotate_sound==0&&r.loop==0);
    assert(flights.empty()&&loaded_files.empty()&&last_time==5&&std::strcmp(zone,"fixture")==0);
    const auto canonical=probe_digest(10);assert(warm.hash!=canonical.hash&&canonical.placements==1&&canonical.live==0&&canonical.flights==0);
    dirty();cohort_epoch(5);assert(probe_digest(10).hash==canonical.hash); // SAME zone, SAME repeated time, every epoch
    assert(probe_digest(10).hash==canonical.hash&&turrets[0].s.acquire_due==fresh.acquire_due); // read-only digest
    turrets[0].handle=0x1801;turrets[0].s.target=0x1802;flights.push_back({0x1803,0x1802,{1,2,3},2});
    const auto generation_a=probe_digest(10).hash;
    turrets[0].handle=0x3801;turrets[0].s.target=0x3802;flights[0].handle=0x3803;flights[0].target=0x3802;
    assert(probe_digest(10).hash==generation_a); // semantic entity identity, not per-process generations
    turrets[0].s.fire_due+=.1f;assert(probe_digest(10).hash!=generation_a);
    installed=false;turrets.clear();flights.clear();loaded_files.clear();
    DWORD ignored=0;assert(VirtualProtect(reinterpret_cast<void*>(0x72a758),64,old,&ignored));
    std::cout<<"PASS turret cohort restart: uninstalled no-op; same-zone repeated-time epochs reset deadlines, targets, burst, binding, rest, resources and flights; semantic digest is read-only and generation independent\n";
}
