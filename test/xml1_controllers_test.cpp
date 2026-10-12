#include <WinSock2.h>
#include "game_version.hpp"
#include "ini.hpp"
#include "iat_hook.hpp"
#include "xml1_hook_transaction.hpp"
#include "mold_target_rules.hpp"
#include "mold_combat_rules.hpp"
#include "xml1_capability_rules.hpp"
#include "mold_query_rules.hpp"
#include "turret_rules.hpp"
#include "gameplay_capabilities.hpp"
#include <iostream>
#include <map>

namespace ini {
std::map<std::wstring,std::string> values;
std::optional<std::string> text(const wchar_t* section,const wchar_t* key) {
    const auto it=values.find(std::wstring(section)+L"/"+key);
    return it==values.end()?std::nullopt:std::optional<std::string>(it->second);
}
bool flag(const wchar_t* section,const wchar_t* key,bool fallback) {
    auto v=text(section,key);return v?*v=="1":fallback;
}
int number(const wchar_t* section,const wchar_t* key,int fallback) {
    auto v=text(section,key);return v?std::stoi(*v):fallback;
}
}
namespace logger {
void write(const char*,...){}
void write_once(std::string_view,const char*,...){}
}
namespace iat_hook {
bool fail=false;void* installed=nullptr;int calls=0,forwarded=0;
SOCKET WSAAPI fake_socket(int,int,int){++forwarded;return 71;}
void* hook(HMODULE,const char*,const char*,WORD,void* replacement){
    ++calls;if(fail)return nullptr;installed=replacement;return reinterpret_cast<void*>(&fake_socket);
}
}
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
struct fake_memory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(12288,0x11);
    std::map<std::uint32_t,std::uint32_t> protections{{0,2},{4096,2},{8192,2}};
    int acquire_calls=0,fail_acquire=0,write_calls=0,fail_write=0,flush_calls=0;
    bool bad_readback=false,fail_flush=false,persistent_write_failure=false,persistent_flush_failure=false,fail_restore=false;
    bool read(std::uint32_t a,void* p,std::size_t n){
        if(a+n>bytes.size())return false;
        std::memcpy(p,bytes.data()+a,n);
        if(bad_readback&&write_calls)*static_cast<unsigned char*>(p)^=1;
        return true;
    }
    bool write(std::uint32_t a,const void* p,std::size_t n){
        if(++write_calls==fail_write||(persistent_write_failure&&write_calls>=fail_write))return false;
        if(a+n>bytes.size()||protections[a&~4095u]!=0x40)return false;
        std::memcpy(bytes.data()+a,p,n);return true;
    }
    bool acquire(std::uint32_t a,std::uint32_t& old){
        if(++acquire_calls==fail_acquire)return false;
        old=protections[a];protections[a]=0x40;return true;
    }
    bool restore(std::uint32_t a,std::uint32_t old){if(fail_restore)return false;protections[a]=old;return true;}
    bool flush(){return !(++flush_calls==1&&fail_flush)&&!persistent_flush_failure;}
};
void turret_checks();
int main(){
    turret_checks();
    using namespace mold_combat_rules;
    const auto binding=parse_binding("fictional_stage:2:5");
    CHECK(binding&&binding->flag=="fictional_stage"&&binding->first==2&&binding->width==5);
    CHECK(parse_binding("test:32:1").has_value());
    for(const auto* text:{"","test:0:5","test:30:5","test:1:0","test:1:33","test:1:2:x","test:-1:5","test:1x:5","../test:1:5"})CHECK(!parse_binding(text));
    CHECK(ready_core(false,true,true)==core_action::wait);
    CHECK(ready_core(true,false,true)==core_action::abandon);
    CHECK(ready_core(true,false,false)==core_action::abandon);
    CHECK(ready_core(true,true,false)==core_action::wait);
    CHECK(ready_core(true,true,true)==core_action::fire);
    deadlines schedule(100);
    CHECK(schedule.floor_due(100)==1);
    schedule.floor_result(1,100,false);
    CHECK(schedule.floor_due(100)==2);
    CHECK(schedule.floor_due(100.5f)==2);
    schedule.floor_result(2,100.5f,true);
    CHECK(schedule.floor_due(100.5f)==-1);
    CHECK(schedule.floor_due(100.51f)==1);
    schedule.floor_result(1,101,true);
    CHECK(schedule.floor_due(110)==-1);
    CHECK(schedule.floor_due(110.01f)==0);
    CHECK(ranged_choice(.249f)==0x90);
    CHECK(ranged_choice(.25f)==0xc0);
    CHECK(ranged_choice(.499f)==0xc0);
    CHECK(ranged_choice(.5f)==0x180);
    CHECK(companion(true,false,0,0,200000,0)==companion_action::engage);
    CHECK(companion(false,false,0,0,200000,0)==companion_action::leave);
    CHECK(companion(false,true,-101,-101,89999,0)==companion_action::engage_floor);
    CHECK(companion(false,true,-101,-101,90000,0)==companion_action::follow);
    CHECK(companion(false,true,-100,0,1,1)==companion_action::follow);
    CHECK(companion(false,true,0,-100,1,1)==companion_action::follow);
    CHECK(companion(false,true,0,0,122499,1)==companion_action::engage);
    CHECK(companion(false,true,0,0,122500,1)==companion_action::follow);
    CHECK(companion(false,true,0,0,1,0)==companion_action::follow);
    // Catch the old extra 360-unit center-distance restriction and nearest-core
    // policy. These synthetic query results overlap the native search sphere;
    // their centers need not lie within that radius.
    using mold_target_rules::candidate;
    constexpr std::uint32_t hero_target=(1u<<20)|(1u<<24)|(1u<<29);
    std::array<candidate,3> targets{{{41,hero_target,true,true,300000,25},
                                   {42,hero_target,true,true,700000,25},
                                   {43,hero_target,true,true,800000,-1}}};
    CHECK(mold_target_rules::choose(false,100000,targets)==42);
    CHECK(mold_target_rules::choose(true,999999,targets)==41);
    CHECK(mold_target_rules::choose(false,144401,targets)==0);
    CHECK(mold_target_rules::choose(false,144400,targets)==0);
    CHECK(mold_target_rules::choose(false,144399,targets)==42);
    targets[2].activation_distance=0;
    CHECK(mold_target_rules::choose(false,100000,targets)==43);
    targets[2].activation_distance=-1;
    targets[0].flags|=1u<<28;targets[1].can_activate=false;
    CHECK(mold_target_rules::choose(false,100000,targets)==0);
    CHECK(mold_target_rules::choose(true,999999,targets)==43);
    targets[2].flags|=1u<<30;
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].flags=hero_target|(1u<<31);
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].flags=hero_target&~(1u<<24);
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].flags=hero_target;targets[2].core=false;
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].core=true;targets[2].flags&=~(1u<<20);
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    using namespace xml1_hook_transaction;
    const std::vector<patch> writes{{12,{0x11},{0x22}},{24,{0x11},{0x33}},{8200,{0x11},{0x44}}};
    for(int failure=0;failure<5;++failure){
        fake_memory mem;const auto before=mem.bytes;const auto protection=mem.protections;
        mem.fail_acquire=failure==1?2:0;mem.fail_write=failure==2?2:0;
        mem.bad_readback=failure==3;mem.fail_flush=failure==4;
        CHECK(apply(mem,writes)==(failure==0));
        CHECK(mem.protections==protection);
        CHECK(mem.acquire_calls==2); // Same-page destinations acquire only once.
        if(failure)CHECK(mem.bytes==before);
        else CHECK(mem.bytes[12]==0x22&&mem.bytes[24]==0x33&&mem.bytes[8200]==0x44);
    }
    fake_memory mismatch;mismatch.bytes[24]=0x55;const auto original=mismatch.bytes;
    CHECK(!apply(mismatch,writes));CHECK(mismatch.acquire_calls==0);CHECK(mismatch.bytes==original);
    fake_memory empty;CHECK(!apply(empty,{{12,{},{}}}));CHECK(empty.acquire_calls==0);

    // A failed rollback or page restoration cannot be called native fallback.
    for(int failure=0;failure<4;++failure){
        fake_memory mem;bool safe=true;
        mem.fail_write=failure==0?2:0;mem.persistent_write_failure=failure==0;
        mem.persistent_flush_failure=failure==1;
        mem.fail_restore=failure==2;mem.bad_readback=failure==3;
        CHECK(!apply(mem,writes,&safe));CHECK(!safe);
    }
    fake_memory transient;bool safe=false;transient.fail_write=2;
    CHECK(!apply(transient,writes,&safe));CHECK(safe);

    // Native query entries under both entity layouts (stock retail bytes; raised
    // E9 jumps into the DLL image that owns the installed query manager).
    {
        using namespace mold_query_rules;
        constexpr u32 installed=0x10001230;
        auto retail=[](const entry& e){std::array<unsigned char,16> b{};for(std::size_t i=0;i<e.retail.size()/2;++i)b[i]=limits_rules::hex_byte(e.retail,i);return b;};
        auto jump=[](const entry& e,u32 target){std::array<unsigned char,16> b{};b[0]=0xe9;const auto d=static_cast<std::int32_t>(target-(e.va+5));std::memcpy(b.data()+1,&d,4);return b;};
        for(const auto& e:entries){
            const auto stock=retail(e),exact=jump(e,installed),wrong=jump(e,installed+0x10),outside=jump(e,0x00500000);
            CHECK(entry_ok(false,stock.data(),e,0));               // stock layout: retail entry
            CHECK(!entry_ok(true,stock.data(),e,installed));       // raised but entry still retail
            CHECK(entry_ok(true,exact.data(),e,installed));        // raised: the exact installed replacement
            CHECK(!entry_ok(true,wrong.data(),e,installed));       // raised: another destination in the same DLL
            CHECK(!entry_ok(true,outside.data(),e,installed));     // raised: outside the DLL
            CHECK(!entry_ok(true,exact.data(),e,0));               // raised but no installed target known
            CHECK(!entry_ok(false,exact.data(),e,installed));      // stock but entry patched
            auto altered=stock;altered[e.retail.size()/2-1]^=1;
            CHECK(!entry_ok(false,altered.data(),e,0));            // stock: any other byte refuses
            auto call=exact;call[0]=0xe8;
            CHECK(!entry_ok(true,call.data(),e,installed));        // raised: a call, not the installed jump
        }
        std::vector<unsigned char> manager(0x1dec,0xff);
        CHECK(!stock_pool_free(manager.data())&&!pool_free(false,manager.data(),3,3)); // stock: every query and order busy
        manager[0x33+2*0x1e4]=0;CHECK(!pool_free(false,manager.data(),3,3));         // a query but no order
        manager[0xdc0+1*0x814]=0;CHECK(pool_free(false,manager.data(),0,0));          // stock ignores raised counts
        manager[0x33+2*0x1e4]=1;CHECK(!pool_free(false,manager.data(),3,3));          // an order but no query
        CHECK(!pool_free(false,nullptr,3,3));
        CHECK(!pool_free(true,nullptr,0,3)&&!pool_free(true,nullptr,3,0)&&!pool_free(true,nullptr,0,0)); // raised exhausted
        CHECK(pool_free(true,nullptr,1,1)&&pool_free(true,manager.data(),1,1));
        std::fill(manager.begin(),manager.end(),0);
        CHECK(!pool_free(true,manager.data(),0,0));                                     // raised ignores stock bytes
        CHECK(result_count_ok(0,256)&&result_count_ok(256,256)&&!result_count_ok(257,256)&&!result_count_ok(-1,256));
        CHECK(result_count_ok(257,1024)&&result_count_ok(1024,1024)&&!result_count_ok(1025,1024));
    }
    // Lockstep sessions choose the first player slot holding a living hero.
    using mold_combat_rules::player_hero;
    CHECK(mold_combat_rules::session_hero({})==-1);
    CHECK(mold_combat_rules::session_hero({{{0,true},{7,false},{9,true},{11,true}}})==2);
    CHECK(mold_combat_rules::session_hero({{{5,true},{7,true},{0,false},{0,false}}})==0);
    CHECK(mold_combat_rules::session_hero({{{5,false},{7,false},{0,true},{0,false}}})==-1);

    // Shared gameplay rule 3/4 registration states (effective, never requested).
    using xml1_capability_rules::registration;
    const auto off=xml1_capability_rules::master_mold(false,false,"",0,0);
    CHECK(off.verified&&off.values==(std::array<std::uint32_t,4>{0,0,0,0}));
    const auto ignored=xml1_capability_rules::master_mold(false,true,"stage",1,5); // not requested: never installed
    CHECK(ignored.verified&&ignored.values==off.values);
    const auto failed=xml1_capability_rules::master_mold(true,false,"stage",1,5);
    CHECK(!failed.verified);
    // The flag name is part of the effective state: FNV-1a 32 of the exact name.
    CHECK(xml1_capability_rules::flag_hash("")==2166136261u);
    CHECK(xml1_capability_rules::flag_hash("a")==0xe40c292cu);
    CHECK(xml1_capability_rules::flag_hash("stage")!=xml1_capability_rules::flag_hash("Stage"));
    const auto on=xml1_capability_rules::master_mold(true,true,"stage",1,5);
    CHECK(on.verified&&on.values==(std::array<std::uint32_t,4>{1,1,5,xml1_capability_rules::flag_hash("stage")}));
    const auto other_name=xml1_capability_rules::master_mold(true,true,"stageB",1,5);
    CHECK(other_name.verified&&other_name.values!=on.values&&other_name.values[1]==1&&other_name.values[2]==5);
    CHECK(xml1_capability_rules::master_mold(true,true,"stage",1,5).values==on.values); // same name: identical
    CHECK(!xml1_capability_rules::master_mold(true,true,"",1,5).verified); // missing name: not installed
    CHECK(!xml1_capability_rules::master_mold(true,true,std::string(33,'x'),1,5).verified);
    CHECK(!xml1_capability_rules::master_mold(true,true,"stage",0,5).verified);
    CHECK(!xml1_capability_rules::master_mold(true,true,"stage",1,0).verified);
    CHECK(!xml1_capability_rules::master_mold(true,true,"stage",1,33).verified);
    const auto turret_off=xml1_capability_rules::turrets(false,false,0,0); // turret controller not requested
    CHECK(turret_off.verified&&turret_off.values==off.values);
    using gameplay_capabilities::record;using gameplay_capabilities::result;
    auto peer=[](registration mold,registration turret,std::uint16_t revision=xml1_capability_rules::master_mold_revision){
        record r;r.add(gameplay_capabilities::explosive_first_hit_id,1,{1,0,0,0},true);
        r.add(gameplay_capabilities::master_mold_id,revision,mold.values,mold.verified);
        r.add(gameplay_capabilities::turret_controller_id,xml1_capability_rules::turret_revision,turret.values,turret.verified);
        r.finalize();return r;
    };
    const auto host_on=peer(on,turret_off),client_on=peer(on,turret_off),client_off=peer(off,turret_off);
    CHECK(host_on.ready()&&host_on.compare(client_on.bytes())==result::match);
    CHECK(host_on.compare(client_off.bytes())==result::value&&client_off.compare(host_on.bytes())==result::value);
    CHECK(client_off.compare(peer(off,turret_off).bytes())==result::match);
    CHECK(host_on.compare(peer(xml1_capability_rules::master_mold(true,true,"stage",1,4),turret_off).bytes())==result::value);
    // Same bit range, different saved-stage variable: refused in both directions.
    const auto renamed=peer(other_name,turret_off);
    CHECK(host_on.compare(renamed.bytes())==result::value&&renamed.compare(host_on.bytes())==result::value);
    CHECK(renamed.compare(peer(other_name,turret_off).bytes())==result::match);
    CHECK(!peer(xml1_capability_rules::master_mold(true,true,"",1,5),turret_off).ready());
    CHECK(host_on.compare(peer(on,turret_off,2).bytes())==result::revision);
    const auto broken=peer(failed,turret_off);
    CHECK(!broken.ready()&&broken.bytes().empty()&&broken.compare(host_on.bytes())==result::unavailable);
    CHECK(host_on.compare(broken.bytes())==result::malformed);
    record older;older.add(gameplay_capabilities::explosive_first_hit_id,1,{1,0,0,0},true);older.finalize();
    CHECK(host_on.compare(older.bytes())==result::entries&&older.compare(client_off.bytes())==result::entries);

    using socket_t=SOCKET(WSAAPI*)(int,int,int);
    // Master Mold does not close sockets on its own: it registers rule 3. Turret keys
    // have no effect in this build (no turret code ships).
    for(const auto* key:{L"Game/Xml1MasterMold",L"Game/Xml1Turrets",L"Test/NativeProjectileProbe"}){
        ini::values={{key,"1"}};iat_hook::fail=false;const auto before=iat_hook::calls;game_version::install(nullptr);
        CHECK(game_version::startup_allowed());CHECK(iat_hook::calls==before);CHECK(!game_version::needs_gameplay_agreement());
    }
    ini::values.clear();iat_hook::fail=false;const auto calls=iat_hook::calls;
    game_version::install(nullptr);CHECK(game_version::startup_allowed());
    CHECK(iat_hook::calls==calls);
    CHECK(std::string(game_version::startup_error()).find("network exclusion")!=std::string::npos);
    game_version::refuse_startup();CHECK(!game_version::startup_allowed());
    CHECK(std::string(game_version::startup_error()).find("XML1 controller")!=std::string::npos);
    CHECK(std::string(game_version::startup_error()).find("LC")==std::string::npos);
    ini::values.clear();CHECK(!game_version::startup_allowed());
    ini::values={{L"Save/Format","lc"},{L"Game/Xml1MasterMold","1"},{L"Online/GameVersion","LC24"}};
    game_version::install(nullptr);CHECK(game_version::startup_allowed());CHECK(game_version::needs_gameplay_agreement());
    // Until the shared record (including rule 3) finalizes and verifies, LC host/join stays closed.
    CHECK(reinterpret_cast<socket_t>(iat_hook::installed)(AF_INET,SOCK_DGRAM,IPPROTO_UDP)==INVALID_SOCKET);
    CHECK(!game_version::gameplay_online_allowed());
    iat_hook::fail=true;game_version::install(nullptr);CHECK(!game_version::startup_allowed());
    CHECK(std::string(game_version::startup_error()).find("LC network exclusion")!=std::string::npos);
    std::cout<<"XML1 controller guards: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}

// Scan turret rules (turret_rules.hpp) with an invented placement; every expectation below follows
// the original scanner's timing (docs/xml1-controllers.md).
void turret_checks(){
    using namespace turret_rules;
    const char* line="fictional/zone_a|gun_x|10,20,30,0,0,1.5|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,10,20|dart_fake|fake_ents";
    const auto t=parse(line);
    CHECK(t&&t->position[1]==20&&t->rest[2]==1.5f);
    CHECK(t&&t->zone=="fictional/zone_a"&&t->instance=="gun_x"&&t->gun.count==3&&t->gun.recoil==5&&t->gun.projectile=="dart_fake");
    CHECK(t&&std::fabs(t->scan.yawextent-90*radians_per_degree)<1e-6f&&t->scan.track_loss==1&&t->scan.sweep_pause==1&&t->scan.filterteam==29);
    for(const char* bad:{"","a|b","zone|gun|1,2,3,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,10,20|p|f",
        "zone|gun|1,2,3,0,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,20,10|p|f",          // damage range reversed
        "zone|gun|1,2,3,0,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,0,20,900,10,20|p|f",            // projectile without speed
        "zone|gun|1,2,3,0,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,10,20||f",           // projectile without entity
        "zone|gun|1,2,3,0,0,0|600,0,0,1,85,90,0,30,29|0,.25,1,0,0,5,600,10,20|p|f",            // bullet naming a projectile
        "zone|g;x|1,2,3,0,0,0|600,0,0,1,85,90,0,30,29|0,.25,1,0,0,5,600,10,20||",
        "zone|gun|1,2,3,0,0,0|600,0,0,1,85,90,0,30,40|0,.25,1,0,0,5,600,10,20||"})CHECK(!parse(bad));
    CHECK(parse("zone/a|gun1|1,2,3,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20||").has_value());
    // angles
    CHECK(std::fabs(wrap(4.f)-(4.f-two_pi))<1e-5f&&wrap(1.f)==1.f&&std::fabs(wrap(-4.f)-(-4.f+two_pi))<1e-5f);
    const auto a=angles(forward({0,.3f,1.2f}));CHECK(std::fabs(a[1]-.3f)<1e-5f&&std::fabs(a[2]-1.2f)<1e-5f);
    // shortest way and sweep direction
    auto r=step({0,0,3.f},{0,0,-3.f},1.f,.1f,{0,0,0});CHECK(r[2]>3.f-1e-6f||r[2]<-3.f);  // crosses pi, not through zero
    r=step({0,0,0},{0,0,1.f},1.f,.25f,{0,0,-1});CHECK(std::fabs(r[2]+.25f)<1e-6f);         // hint turns the long way
    r=step({0,0,.9f},{0,0,1.f},1.f,.25f,{0,0,1});CHECK(r[2]==1.f);                         // stops on the goal
    // acquisition: scanning polls every second, tracking every 0.1 s; mintime 0 tracks on the first sighting
    auto s=spawn(10.f);CHECK(s.acquire_due==10.3f&&s.fire_due==10.3f&&std::fabs(s.rotate_due-10.01f)<1e-6f);
    sight none,seen{true,0x1234,{100,0,0}};
    CHECK(acquire(s,t->scan,10.3f,false,none)==1.f&&s.mode==idle);
    CHECK(acquire(s,t->scan,11.3f,true,none)==1.f&&s.mode==scanning);
    CHECK(acquire(s,t->scan,12.3f,true,seen)==.1f&&s.mode==tracking&&s.target==0x1234&&s.last_seen==12.3f);
    CHECK(acquire(s,t->scan,12.4f,true,none)==.1f&&s.mode==tracking);                     // within resetdelay
    CHECK(acquire(s,t->scan,13.4f,true,none)==1.f&&s.mode==scanning);                    // lost after 1 s unseen
    scanner slow=t->scan;slow.mintime=.75f;auto u=spawn(0);acquire(u,slow,.3f,true,none);
    CHECK(acquire(u,slow,.8f,true,seen)==1.f&&u.mode==scanning);                          // 0.5 s seen < 0.75
    CHECK(acquire(u,slow,1.8f,true,seen)==.1f&&u.mode==tracking);
    // firing: 1 s polls, burst armed 1 s after a sighting, then one weapon fire per recoil, 20 per burst
    auto f=spawn(0);float next=0;
    CHECK(fire(f,.3f,1,5,next)==fire_result::none&&next==1.f);
    f.mode=tracking;f.last_seen=1.f;
    CHECK(fire(f,1.3f,1,5,next)==fire_result::burst_started&&next==1.f&&f.burst==20);
    int shots=0;float clock=2.3f;
    while(fire(f,clock,1,5,next)==fire_result::shot){++shots;CHECK(next==5.f);clock+=next;f.last_seen=clock;}
    CHECK(shots==20&&f.burst==20);                                                         // the 21st event re-arms (seen)
    auto lost=spawn(0);lost.mode=tracking;lost.last_seen=0;
    CHECK(fire(lost,5.f,1,5,next)==fire_result::none&&next==1.f&&lost.burst==-1);         // not seen within 1 s
    // rotation: sweep to rest + yawextent, pause resetdelay there, then sweep back
    auto g=spawn(0);g.mode=scanning;vec o{0,0,1.5f};bool moved=false,turning=false;float clock2=.01f;
    float d=rotate(g,t->scan,t->rest,clock2,true,{0,0,0},o,moved,turning);
    CHECK(d==.01f&&moved&&turning&&o[2]>1.5f);
    for(int i=0;i<1000&&!g.sweep_back;++i){clock2+=.01f;rotate(g,t->scan,t->rest,clock2,true,{0,0,0},o,moved,turning);}
    CHECK(g.sweep_back&&std::fabs(o[2]-wrap(1.5f+90*radians_per_degree))<.032f&&g.pause==1.f); // reached within 0.001 rad^2
    CHECK(rotate(g,t->scan,t->rest,clock2+.5f,true,{0,0,0},o,moved,turning)==1.f);      // pausing
    // tracking with pitchextent 0 keeps the rest pitch and turns towards the aim
    auto h=spawn(0);h.mode=tracking;h.aim={0,100,50};vec q{0,0,1.5f};
    rotate(h,t->scan,t->rest,.5f,true,{0,0,0},q,moved,turning);CHECK(q[1]==0&&q[2]>1.5f);
    CHECK(draw(17,32,0)==17&&draw(17,32,.99999f)==32&&draw(17,32,.5f)==25);
    // homing: speed kept, turns toward the target, f grows with age and caps at 1.5, level keeps z 0
    {const vec v{400,0,0},to{0,100,-100};const auto h=home(v,to,0,false);
     CHECK(std::fabs(std::sqrt(dot(h,h))-400)<.01f&&h[1]>0&&h[2]<0&&h[0]>h[1]);
     const auto late=home(v,to,40,false);CHECK(late[1]>h[1]);
     CHECK(home(v,to,20,false)==home(v,to,30,false));                                          // capped
     CHECK(home(v,to,0,true)[2]==0&&home(vec{0,0,0},to,0,false)==(vec{0,0,0}));}
    // fan: three projectiles at -1, 0, +1; two at -0.5, +0.5
    CHECK(fan_offset(0,3)==-1&&fan_offset(1,3)==0&&fan_offset(2,3)==1&&fan_offset(0,2)==-.5f&&fan_offset(1,2)==.5f&&fan_offset(0,1)==0);
    // capability: off = revision 1 verified off; requested but not installed: unverified
    CHECK(xml1_capability_rules::turret_revision_for(false)==1&&xml1_capability_rules::turret_revision_for(true)==2);
    CHECK(!xml1_capability_rules::turrets(true,false,3,7).verified);
    const auto on=xml1_capability_rules::turrets(true,true,3,7);CHECK(on.verified&&on.values==(std::array<std::uint32_t,4>{1,3,7,0}));
}
