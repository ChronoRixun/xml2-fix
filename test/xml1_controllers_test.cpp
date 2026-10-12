#include <WinSock2.h>
#include "game_version.hpp"
#include "ini.hpp"
#include "iat_hook.hpp"
#include "xml1_hook_transaction.hpp"
#include "mold_target_rules.hpp"
#include "mold_combat_rules.hpp"
#include "xml1_capability_rules.hpp"
#include "mold_query_rules.hpp"
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
int main(){
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
    const auto turret_off=xml1_capability_rules::turrets(); // no turret controller in this build
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
