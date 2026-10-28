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
#include "dopple_rules.hpp"
#include "avalanche_rules.hpp"
#include "boss_dopple_sites.hpp"
#include "boss_runtime.hpp"
#include "boss_native_sites.hpp"
#include "pyro_rules.hpp"
#include "mystique_rules.hpp"
#include "shadowking_rules.hpp"
#include <fstream>
#include <iterator>
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
void dopple_checks();
void boss_rule_checks();
void avalanche_checks();
void wave2_checks();
void exe_checks(const char* path);
int main(int argc,char** argv){
    turret_checks();
    dopple_checks();
    boss_rule_checks();
    avalanche_checks();
    wave2_checks();
    if(argc>1)exe_checks(argv[1]);else std::cout<<"  skip  retail byte checks (supply XMen2.exe)\n";
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
    CHECK(host_on.compare(peer(on,turret_off,2).bytes())==result::value); // protocol 3: a revision difference is a value (gameplay settings) difference
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
    const char* line="fictional/zone_a|gun_x|10,20,30,0,0,1.5,0,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,10,20,0,0,128,0|dart_fake|fake_ents|,,|,";
    const auto t=parse(line);
    CHECK(t&&t->position[1]==20&&t->rest[2]==1.5f);
    CHECK(t&&t->zone=="fictional/zone_a"&&t->instance=="gun_x"&&t->gun.count==3&&t->gun.recoil==5&&t->gun.projectile=="dart_fake");
    CHECK(t&&std::fabs(t->scan.yawextent-90*radians_per_degree)<1e-6f&&t->scan.track_loss==1&&t->scan.sweep_pause==1&&t->scan.filterteam==29);
    for(const char* bad:{"","a|b","zone|gun|1,2,3,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,10,20,0,0,128,0|p|f|,,|,",
        "zone|gun|1,2,3,0,0,0,0,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,20,10,0,0,128,0|p|f|,,|,",          // damage range reversed
        "zone|gun|1,2,3,0,0,0,0,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,0,20,900,10,20,0,0,128,0|p|f|,,|,",            // projectile without speed
        "zone|gun|1,2,3,0,0,0,0,0,0|600,0,0,1,85,90,0,30,29|2,5,3,20,400,20,900,10,20,0,0,128,0||f|,,|,",           // projectile without entity
        "zone|gun|1,2,3,0,0,0,0,0,0|600,0,0,1,85,90,0,30,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|p|f|,,|,",            // bullet naming a projectile
        "zone|g;x|1,2,3,0,0,0,0,0,0|600,0,0,1,85,90,0,30,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|,",
        "zone|gun|1,2,3,0,0,0,0,0,0|600,0,0,1,85,90,0,30,40|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|,"})CHECK(!parse(bad));
    CHECK(parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|,").has_value());
    // ray weapons: tracetime, radius, PC damage bits and effects (invented paths)
    {const auto b=parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,2,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,524288|||fx/a_muzzle,fx/a_tracer,fx/a_hit|,");
     CHECK(b&&b->gun.kind==weapon_kind::bullet&&b->gun.damage_type==0x80&&b->gun.damage_mods==0x80000&&b->gun.effects[1]=="fx/a_tracer"&&b->gun.range==600);
     const auto f=parse("zone/a|jet1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,0,30,29|1,.03,1,0,0,0,132,5,9,.45,48,65536,0|||,fx/b_stream,|,");
     CHECK(f&&f->gun.kind==weapon_kind::flame&&f->gun.tracetime==.45f&&f->gun.radius==48&&f->gun.damage_type==0x10000&&f->gun.effects[0].empty());
     CHECK(!parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,0,10,20,0,0,128,0|||,,|,"));          // ray without range
     CHECK(!parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,-1,0|||,,|,"));         // bits not unsigned
     CHECK(!parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,x;y|,"));     // effect path
     CHECK(!parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20|||,,|,"));                  // old 9-field weapon
     // aim: no spread keeps the facing; the draws span +-accuracy about the turret's own frame
     const vec o{0,.2f,1.f};const vec straight=aim(o,0,.3f,.9f),fwd=forward(o);
     CHECK(std::fabs(straight[0]-fwd[0])<1e-5f&&std::fabs(straight[1]-fwd[1])<1e-5f&&std::fabs(straight[2]-fwd[2])<1e-5f);
     const float acc=5*radians_per_degree;
     const vec left=aim({0,0,1.f},acc,0,.5f),right=aim({0,0,1.f},acc,1,.5f),down=aim({0,0,1.f},acc,.5f,1);
     CHECK(std::fabs(angles(left)[2]-(1.f-acc))<1e-4f&&std::fabs(angles(right)[2]-(1.f+acc))<1e-4f);
     CHECK(std::fabs(angles(down)[1]-acc)<1e-4f&&std::fabs(angles(down)[2]-1.f)<1e-4f&&std::fabs(dot(down,down)-1)<1e-5f);
     const vec tilted=aim({0,.3f,0},acc,.5f,1);CHECK(std::fabs(angles(tilted)[1]-(.3f+acc))<1e-4f);   // pitches add in the frame
     // segment: 36 behind to range ahead; extents only for the flame
     const auto sb=trace_segment({0,0,10},{1,0,0},600,48,weapon_kind::bullet);
     CHECK(sb.from[0]==-36&&sb.to[0]==600&&sb.from[2]==10&&sb.extent[0]==0);
     const auto sf=trace_segment({0,0,0},{0,1,0},132,48.7f,weapon_kind::flame);
     CHECK(sf.from[1]==-36&&sf.to[1]==132&&sf.extent[2]==48);
     // trace-time gate: at most one trace per tracetime; a negative record never gates
     float last=0;CHECK(!trace_ready(last,.3f,.45f)&&last==0);
     CHECK(trace_ready(last,.5f,.45f)&&last==.5f&&!trace_ready(last,.9f,.45f)&&trace_ready(last,.95f,.45f));
     float never=-1;CHECK(trace_ready(never,0,10)&&never==-1);
     float none=0;CHECK(trace_ready(none,0,0)&&trace_ready(none,0,0));}
    // binding after a clock that goes back (an online session restarts the game clock): a timer set at
    // game time 500 must not delay binding at the restarted time 1.5
    {binding b;CHECK(may_bind(b,0));bind_failed(b,500.f);CHECK(!may_bind(b,500.5f)&&may_bind(b,501.f));
     b.trace_at=900;b.rest_taken=true;bind_failed(b,500.f);
     CHECK(clock_went_back(1.5f,501.f)&&!clock_went_back(501.f,501.f)&&!may_bind(b,1.5f));
     forget(b);CHECK(may_bind(b,1.5f)&&b.trace_at==0&&!b.rest_taken);}
    // resetdelay -1 = not authored: the constructor's track loss 10 and sweep pause 3
    {const auto d=parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,-1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|,");
     CHECK(d&&d->scan.track_loss==10&&d->scan.sweep_pause==3);
     CHECK(!parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,-2,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|,"));}
    // a script-spawned turret: '*name', bound by name with its orientation at binding as the rest basis
    {const auto sp=parse("zone/a|*jet9|0,0,0,0,0,0,0,0,0|600,0,0,1,30,45,0,30,29|1,.03,1,0,0,0,132,5,9,.45,48,65536,0|||,,|,");
     CHECK(sp&&sp->spawned&&sp->instance=="jet9");
     CHECK(!parse("zone/a|gun1|1,2,3,0,0,0,0,0,5000|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|,"));  // muzzle out of range
     const auto m=parse("zone/a|gun1|1,2,3,0,0,0,53.5,8,5.5|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|,");
     CHECK(m&&m->muzzle[0]==53.5f&&m->muzzle[2]==5.5f);
     // the bolt rides the turret's frame: yawed a quarter turn, forward becomes +y; pitched down, forward drops
     const vec q=frame_point({10,20,30},{0,0,1.57079637f},{50,0,5});CHECK(std::fabs(q[0]-10)<1e-3f&&std::fabs(q[1]-70)<1e-3f&&q[2]==35);
     const vec d=frame_point({0,0,0},{0,.5f,0},{10,0,0});CHECK(std::fabs(d[0]-10*std::cos(.5f))<1e-4f&&std::fabs(d[2]+10*std::sin(.5f))<1e-4f);
     const auto ss=parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,|fx/a_bang,fx/a_whirr");
     CHECK(ss&&ss->sound.fire=="fx/a_bang"&&ss->sound.rotate=="fx/a_whirr"&&t&&t->sound.fire.empty());
     CHECK(!parse("zone/a|gun1|1,2,3,0,0,0,0,0,0|600,0,0,1,30,45,45,20,29|0,.25,1,0,0,5,600,10,20,0,0,128,0|||,,"));   // no sound field
     CHECK(t&&!t->spawned&&!parse("zone/a|*|0,0,0,0,0,0,0,0,0|600,0,0,1,30,45,0,30,29|1,.03,1,0,0,0,132,5,9,.45,48,65536,0|||,,|,"));}
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

// The colosseum dopples (dopple_rules.hpp). Every expectation follows the original handlers in default.xbe
// (docs/xml1-controllers.md, "Doppelgangers"): tick 0x110470, Wolverine 0x1136f0, Colossus 0x1138a0, Jean 0x113c50,
// Cyclops 0x113b70.
void dopple_checks(){
    using namespace dopple_rules;
    {   // shared tick: once per distinct time, re-arm after 5 s, both alive and closer than 400
        shared s;
        CHECK(tick(s,1.f,true,399.f));CHECK(s.wolverine==1&&s.colossus==1&&s.rearm_at==6.f);
        CHECK(!tick(s,1.f,true,10.f));                          // same game time: no second tick
        s.wolverine=s.colossus=0;CHECK(!tick(s,5.f,true,10.f)); // re-arm not passed
        CHECK(!tick(s,6.f,true,10.f));                          // equal is not past (fcomp: deadline < now)
        CHECK(tick(s,6.5f,true,10.f));
        shared t;CHECK(!tick(t,1.f,true,400.f));CHECK(!tick(t,2.f,false,10.f));CHECK(t.wolverine==0);
        shared u;u.colossus=2;CHECK(!tick(u,1.f,true,10.f));     // a fastball is running
        shared z;CHECK(!tick(z,0.f,true,10.f));                 // re-arm 0 at the start: 0 < 0 is false
    }
    view v;v.partner_valid=true;v.now=2.f;
    {   // Wolverine: approach, hand-off below 55, then hold (1 s waits) until Colossus throws
        shared s;s.wolverine=1;s.fastball_started=1.f;v.partner_distance=100.f;
        auto a=update(s,who::wolverine,v);CHECK(a.handled&&a.move_to_partner&&!a.stop&&s.wolverine==1);
        v.partner_distance=55.f;a=update(s,who::wolverine,v);CHECK(a.handled&&!a.stop&&s.wolverine==1);
        v.partner_distance=54.9f;a=update(s,who::wolverine,v);
        CHECK(a.handled&&a.move_to_partner&&a.stop&&a.face_partner&&a.face_immediate&&s.wolverine==2&&a.wait==0);
        a=update(s,who::wolverine,v);CHECK(a.handled&&a.wait==1.f&&!a.move_to_partner&&s.wolverine==2);
        s.wolverine=0;a=update(s,who::wolverine,v);CHECK(!a.handled&&s.wolverine==0);
        s.wolverine=3;a=update(s,who::wolverine,v);CHECK(!a.handled&&s.wolverine==3); // other values: native, kept
        s.wolverine=2;view dead=v;dead.partner_valid=false;a=update(s,who::wolverine,dead);CHECK(!a.handled&&s.wolverine==0);
    }
    {   // Colossus: approach, hand-off, then face Wolverine and throw (alternate slot 3, 0x100) at the hero
        shared s;s.wolverine=s.colossus=1;s.fastball_started=1.f;v.partner_distance=30.f;
        auto a=update(s,who::colossus,v);
        CHECK(a.handled&&a.move_to_partner&&a.stop&&a.face_partner&&!a.face_immediate&&s.colossus==2);
        CHECK(slot_for(s,who::colossus).slot==3&&slot_for(s,who::colossus).alternate);
        v.slot_available=false;a=update(s,who::colossus,v);CHECK(a.handled&&a.face_partner&&!a.buttons&&s.colossus==2);
        v.slot_available=true;v.hero=true;s.wolverine=2;a=update(s,who::colossus,v);
        CHECK(a.handled&&a.target_hero&&a.buttons==0x100&&a.wait==1.f&&s.colossus==0&&s.wolverine==0);
        s.colossus=2;s.wolverine=2;v.hero=false;a=update(s,who::colossus,v);CHECK(!a.target_hero&&a.buttons==0x100);
        v.slot_available=false;
    }
    {   // not in the original: an unthrown fastball is abandoned 10 s after it started (live check D2)
        shared s;s.wolverine=2;s.colossus=2;s.fastball_started=1.f;view late=v;late.now=11.f;
        CHECK(update(s,who::wolverine,late).handled);          // exactly 10 s: still held
        late.now=11.5f;CHECK(!update(s,who::colossus,late).handled);CHECK(s.wolverine==0&&s.colossus==0);
    }
    {   // Jean: every 7 s the nearest hazard near the hero; states 1/2 as in the original (nothing enters 2)
        shared s;view j=v;j.now=1.f;
        auto a=update(s,who::jean,j);CHECK(a.handled&&s.jean==3&&s.hazard_at==8.f&&!a.activate_hazard);
        j.hero=false;a=update(s,who::jean,j);CHECK(a.handled&&s.jean==3&&!a.activate_hazard&&a.wait==0);
        j.hero=true;a=update(s,who::jean,j);CHECK(a.handled&&a.brain_target_hero&&a.activate_hazard&&a.wait==1.f&&s.jean==0);
        j.now=8.f;a=update(s,who::jean,j);CHECK(!a.handled&&s.jean==0);   // 8 is not past 8
        j.now=8.5f;a=update(s,who::jean,j);CHECK(a.handled&&s.jean==3&&s.hazard_at==15.5f);
        s.jean=2;CHECK(slot_for(s,who::jean).slot==5&&!slot_for(s,who::jean).alternate);
        j.slot_available=false;a=update(s,who::jean,j);CHECK(a.handled&&!a.buttons&&s.jean==2);
        j.slot_available=true;a=update(s,who::jean,j);CHECK(a.handled&&a.buttons==0x90&&a.wait==1.f&&s.jean==1);
        a=update(s,who::jean,j);CHECK(a.handled&&s.jean==0&&s.cyclops==1);
        s.jean=4;a=update(s,who::jean,j);CHECK(!a.handled&&s.jean==4);
        j.partner_valid=false;a=update(s,who::jean,j);CHECK(!a.handled&&s.jean==0);
    }
    {   // Cyclops: power attack (0x90, 0.4 s) only after Jean's state 1; state 2 holds; otherwise native
        shared s;view c=v;
        CHECK(!update(s,who::cyclops,c).handled);
        s.cyclops=1;c.slot_available=false;auto a=update(s,who::cyclops,c);CHECK(a.handled&&!a.buttons&&s.cyclops==1);
        c.slot_available=true;a=update(s,who::cyclops,c);CHECK(a.handled&&a.buttons==0x90&&a.wait==.4f&&s.cyclops==0);
        s.cyclops=2;CHECK(update(s,who::cyclops,c).handled);
        c.partner_valid=false;CHECK(!update(s,who::cyclops,c).handled);CHECK(s.cyclops==0);
    }
    {   // a fight: Wolverine and Colossus 200 apart close at 3 units per update each; fastballs arm >= 5 s apart
        // and every armed fastball is thrown (slot 3 free every other update)
        shared s;float w=0,c=200,last=-100;int throws=0,armed=0;bool gap_ok=true;
        for(int step=1;step<6000;++step){
            const float t=step*0.0333f;
            if(tick(s,t,true,c-w)){if(t-last<5.f)gap_ok=false;last=t;++armed;}
            view vw;vw.now=t;vw.partner_valid=true;vw.partner_distance=c-w;
            auto aw=update(s,who::wolverine,vw);if(aw.move_to_partner&&!aw.stop)w+=3;
            view vc=vw;vc.slot_available=step%2==0;vc.hero=true;vc.partner_distance=c-w;
            auto ac=update(s,who::colossus,vc);if(ac.move_to_partner&&!ac.stop)c-=3;
            if(ac.buttons==0x100){++throws;w=0;c=200;}
        }
        CHECK(throws>=30&&gap_ok&&armed-throws<=1);
    }
}
// Avalanche (avalanche_rules.hpp), every expectation from default.xbe 0x111ab0 (docs/xml1-controllers.md, "Avalanche").
void avalanche_checks(){
    using namespace avalanche_rules;
    // The band: trunc(6 - trunc(health%) x 0.06f); steps at 83, 66, 50 (6 - 2.99999993 = 3.00000007), 33, 16 and 0.
    CHECK(band(100,100)==0&&band(84,100)==0&&band(83,100)==1&&band(67,100)==1&&band(66,100)==2&&band(51,100)==2);
    CHECK(band(50,100)==3&&band(34,100)==3&&band(33,100)==4&&band(17,100)==4&&band(16,100)==5&&band(1,100)==5&&band(0,100)==6);
    CHECK(band(500,500)==0&&band(10,0)==0);
    CHECK(band(83.99f,100)==1&&band(66.99f,100)==2&&band(50.99f,100)==3); // percent truncation precedes band math
    for(int p=0;p<=100;++p){const double x=6.0-double(0.06f)*p;CHECK(band(float(p),100.f)==int(x));}
    state s;view v;v.health=100;v.health_max=100;v.now=1.f;
    {   // full health, no hero: nothing, no band write
        auto a=update(s,v);CHECK(!a.band_changed&&!a.buttons&&!a.face&&!a.face_last);
    }
    {   // close: power smash when the 4 s deadline passed and slot 6 is free, then smash/attack rolls until it passes again
        v.nearest=99.f;v.slot_available=true;auto a=update(s,v);
        CHECK(a.face&&a.buttons==0xa0&&a.taunt_always&&s.close_at==5.f);
        v.now=5.f;a=update(s,v);CHECK(a.face&&!a.buttons&&a.roll_smash);           // 5 is not past 5
        v.now=5.5f;v.slot_available=false;a=update(s,v);CHECK(a.face&&!a.buttons&&!a.roll_smash&&s.close_at==5.f);
        v.nearest=100.f;v.slot_available=true;a=update(s,v);CHECK(!a.face&&a.face_last&&a.buttons==0x90&&a.taunt_half&&s.mid_at==9.5f);
        v.now=9.f;a=update(s,v);CHECK(!a.face_last&&!a.buttons);                    // mid deadline not passed: nothing
        v.now=10.f;v.nearest=399.f;v.slot_available=false;a=update(s,v);CHECK(a.face_last&&!a.buttons);
        v.nearest=400.f;v.slot_available=true;a=update(s,v);CHECK(!a.face_last&&!a.buttons);
    }
    {   // the band flips the brain's bit 3: odd bands set it, even bands clear it, once per change
        state t;view w;w.health_max=100;w.health=83;auto a=update(t,w);CHECK(a.band_changed&&a.mode_bit&&t.band==1&&t.mode==1);
        a=update(t,w);CHECK(!a.band_changed);
        w.health=60;a=update(t,w);CHECK(a.band_changed&&!a.mode_bit&&t.mode==0);
        w.health=40;a=update(t,w);CHECK(a.band_changed&&a.mode_bit);
    }
}
void boss_rule_checks(){
    using namespace xml1_capability_rules;
    CHECK(gameplay_capabilities::xml1_bosses_id==40);
    CHECK(std::string(gameplay_capabilities::rule_name(40))=="Xml1Bosses");
    CHECK(boss_bit("dopples")==1&&boss_bit("avalanche")==2&&boss_bit("pyro")==4&&boss_bit("")==0);
    CHECK(bosses(true,3,3,1).values[1]==flag_hash("dopples:1;avalanche:2;")&&bosses(true,3,3,1).values[0]==3);
    CHECK(boss_bit("mystique")==8&&boss_bit("shadowking")==16&&boss_bit("marrow")==32&&boss_bit("magneto")==64);
    CHECK(bosses(true,63,63,1).values[1]==flag_hash("dopples:1;avalanche:2;pyro:1;mystique:1;shadowking:1;marrow:1;"));
    CHECK(!bosses(true,63,31,1).verified); // native Marrow requested but its parser/dispatch guard failed
    CHECK(bosses(true,32,32,0).verified&&bosses(true,32,32,0).values[0]==32);
    CHECK(!bosses(true,3,1,1).verified);    // avalanche requested, only the dopples installed
    const auto on=bosses(true,1,1,1);
    CHECK(on.verified&&on.values[0]==1&&on.values[1]==flag_hash("dopples:1;")&&on.values[2]==1&&on.values[3]==0);
    CHECK(bosses(true,1,1,0).values[2]==0&&bosses(true,1,1,0).verified);
    CHECK(!bosses(false,1,1,1).verified);   // an unknown name in the list
    CHECK(!bosses(true,1,0,1).verified);    // requested, not installed
    CHECK(!bosses(true,0,0,0).verified);
    CHECK(bosses_revision==1);
}
void wave2_checks(){
    {
        using namespace boss_target_rules;
        std::array<hero,5> party{{{10,40,0,true,true},{11,80,36,true,false},{12,1,0,false,true},{13,90,-35,true,false},{14,50,0,true,true}}};
        auto a=avalanche(party);CHECK(a.nearest==10&&a.distance==40&&a.controlled==13);
        CHECK(pyro(party).handle==10&&mystique(party)==10);
        party[0].living=false;CHECK(mystique(party)==14&&pyro(party).handle==14);
        party[4].living=false;CHECK(mystique(party)==11&&pyro(party).handle==13);
        party[1].living=false;party[3].living=false;
        CHECK(avalanche(party).nearest==0&&mystique(party)==0&&pyro(party).handle==0);
        std::uint32_t output[8]{1,2,3,4,5,6,7,8};boss_runtime::buttons(output,0x180);
        CHECK(output[0]==0x181&&output[1]==0x182&&output[2]==3&&output[7]==8);
    }
    {   // Pyro: strict height boundary, one shared cooldown across fire states, and no availability fallback.
        using namespace pyro_rules;
        CHECK(height_ok(35.999f)&&height_ok(-35.999f)&&!height_ok(36)&&!height_ok(-36));
        CHECK(none_team==28&&enemy_team==30);
        state s;
        CHECK(update(s,0,10,true,true).buttons==0);
        auto a=update(s,1,299,true,true);CHECK(a.buttons==0xc0&&a.taunt==1&&s.deadline==4);
        s.fire_up=false;CHECK(update(s,4,10,true,true).buttons==0);
        a=update(s,4.01f,199,true,true);CHECK(a.buttons==0x90&&a.taunt==.5f&&s.deadline==9.01f);
        s.deadline=0;a=update(s,1,200,true,true);CHECK(a.buttons==0xc0&&s.deadline==6);
        s.deadline=0;CHECK(update(s,1,199,false,true).buttons==0);CHECK(s.deadline==0);
        CHECK(update(s,1,300,true,true).buttons==0);
        s.fire_up=true;CHECK(update(s,1,299,true,false).buttons==0);
        state other;CHECK(other.deadline==0&&other.fire_up); // independent spawn state
    }
    {   // Mystique: request ordering, grenade scheduling, successful-copy timer and target-loss restoration.
        using namespace mystique_rules;state s;s.active=false;
        CHECK(request(s,"myst1",4)&&!s.active&&s.mode==0);
        CHECK(!request(s,"mag2",4)&&!s.active); // shared asteroid zone never grants another boss ownership
        CHECK(request(s,"myst2",4)&&s.mode==1&&s.active&&s.grenade_at==6&&!s.copied);
        CHECK(!grenade_due(s,6)&&grenade_due(s,6.001f));s.grenade_at=10;
        CHECK(!grenade_due(s,9.999f)&&grenade_due(s,10.01f));
        CHECK(request(s,"myst3",10)&&s.mode==2&&!s.active);
        CHECK(request(s,"gopattern",11)&&s.active&&s.mode==0);
        CHECK(close_buttons(false,0,0)==0x180&&close_buttons(true,.98f,0)==0);
        CHECK(close_buttons(true,.981f,.49f)==0x10&&close_buttons(true,1,.5f)==0x20);
        for(float duration:{10.f,15.f,22.f}){
            copy(s,20,duration,71);CHECK(s.copied&&s.target==71&&s.restore_at==20+duration);
            CHECK(!restore_due(s,20+duration-.01f,true)&&restore_due(s,20+duration,true));
            CHECK(restore_due(s,20,false));
        }
        copy(s,40,10,72);CHECK(s.restore_at==50&&s.target==72); // repeated copy replaces the owner timer
        request(s,"myst2",41);CHECK(restore_due(s,50,true)); // copied-mode reset must not strand a changed skin
        request(s,"mystique",52);CHECK(!s.copied&&s.restore_at==50&&s.target==0&&s.copy_target==72);
        CHECK(restore_due(s,52,true)); // native timed effect would survive the initializer too
    }
    {   // Shadow King: overlapping request/attack branches and fractional clock truncation matter.
        using namespace shadowking_rules;state s;
        s=initial(20.9f,0);CHECK(s.spear_at==26&&s.smash_at==26&&!s.boost&&!s.xtreme&&!s.chasing);
        auto warmup=update(s,26,110,true,true,true,true);CHECK(!warmup.buttons);
        warmup=update(s,26.001f,110,true,true,true,true);CHECK(warmup.buttons==0x90);
        s={};
        auto a=update(s,1.9f,200,true,true,true,true);
        CHECK(a.buttons==0x90&&a.wait==.6f&&a.taunt&&s.spear_at==7);
        a=update(s,7,200,true,true,true,true);CHECK(!a.buttons&&a.close);
        a=update(s,7.01f,200,true,true,true,true);CHECK(a.buttons==0x90&&s.spear_at==13);
        s={};a=update(s,1.9f,100,true,true,true,true);CHECK(a.buttons==0xa0&&s.smash_at==8);
        s={};a=update(s,1,120,false,true,true,true);CHECK(a.buttons==0); // strict smash boundary
        s={};s.boost=true;a=update(s,1,200,false,false,true,true);
        CHECK(a.buttons==0x180&&a.preferred90&&a.wait==.6f&&!s.boost);
        s={};s.boost=true;a=update(s,1,300,false,false,true,true);CHECK(!a.buttons&&s.boost);
        s={};s.xtreme=true;a=update(s,1,110,true,true,true,true);
        CHECK(a.buttons==(0xc0|0xa0)&&a.wait==.6f&&!s.xtreme&&s.spear_at==0); // sk4 continues to close attack
        s={};s.xtreme=true;s.boost=true;a=update(s,1,200,true,true,true,true);
        CHECK(a.buttons==(0xc0|0x180)&&a.preferred90&&!s.boost&&!s.xtreme);
        s={};s.xtreme=true;a=update(s,1,200,true,true,true,false);CHECK(!a.buttons&&s.xtreme&&s.spear_at==0);
        s={};a=update(s,1,301,false,false,false,false);CHECK(a.follow&&s.chasing);
        a=update(s,1,181,false,false,false,false);CHECK(a.follow&&s.chasing);
        a=update(s,1,180,false,false,false,false);CHECK(a.close&&!s.chasing);
        s={};s.mode=1;a=update(s,1.9f,169,true,true,true,true);CHECK(a.buttons==0x90&&s.spear_at==6);
        s={};s.mode=1;a=update(s,1,170,true,true,true,true);CHECK(!a.buttons&&a.follow);
        a=update(s,1,99,true,true,true,true);CHECK(a.close&&!a.follow);
    }
}
// Retail bytes: every dopple guard, the shared brain-dispatch site and Master Mold's script entries, read from the exe.
void exe_checks(const char* path){
    std::ifstream f(path,std::ios::binary);std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    CHECK(d.size()>0x400);if(d.size()<=0x400)return;
    auto u32le=[&](std::size_t o){std::uint32_t v=0;std::memcpy(&v,d.data()+o,4);return v;};
    const auto pe=u32le(0x3c);const unsigned sections=d[pe+6]|(d[pe+7]<<8),optional=d[pe+20]|(d[pe+21]<<8);
    auto file=[&](std::uint32_t va)->const unsigned char*{
        for(unsigned i=0;i<sections;++i){const auto o=pe+24+optional+i*40;const auto v=u32le(o+12)+0x400000,raw=u32le(o+16),at=u32le(o+20);
            if(va>=v&&va-v<raw&&at+(va-v)+16<=d.size())return d.data()+at+(va-v);}
        return nullptr;};
    int checked=0;
    for(const auto& g:boss_dopple_sites::guards){const auto* p=file(g.va);CHECK(p&&limits_rules::matches(p,g.hex));++checked;}
    CHECK(checked==30);
    for(const auto& g:boss_runtime::guards){const auto* p=file(g.va);CHECK(p&&limits_rules::matches(p,g.hex));}
    for(const auto& g:boss_native_sites::marrow){const auto* p=file(g.va);CHECK(p&&limits_rules::matches(p,g.hex));}
    for(const auto& site:std::array<std::pair<std::uint32_t,const char*>,3>{{{0x4dff50,"6aff68204a6700"},{0x4e01a9,"ff9378010000"},{0x4dc05f,"8d4e04e809edffff"}}}){
        const auto* p=file(site.first);CHECK(p&&limits_rules::matches(p,site.second));}
    const std::pair<std::uint32_t,const char*> shared_sites[]{{0x5173f3,"8b460883f840"},{0x49fe31,"08a96800"},{0x68adb8,"30484a00"},{0x68ae58,"a0514a00"},{0x68ae88,"204e4a00"},
        {0x513146,"f686b702000008"},{0x514613,"f683b702000008"}};  // Avalanche: the two readers of brain +0x2b7 bit 3
    for(const auto& site:shared_sites){const auto* p=file(site.first);CHECK(p&&limits_rules::matches(p,site.second));}
    std::cout<<"  retail byte checks: "<<checked<<" dopple + 20 wave-two + 6 native Marrow + 3 copy + 7 shared guards\n";
}
