#include "actor_ai_layout.hpp"
#include "xml1_turrets.hpp"
#include "turret_rules.hpp"
#include "mold_query.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "limits_rules.hpp"
#include "game_version.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// XML1 scan turrets on the PC engine. The first game's scanturretent is a scanent subclass with
// three self-rescheduling events (acquisition, rotation, firing). The converted map keeps each
// turret as a physent; this adapter keeps the original events' state per placement and runs them
// from the native event scheduler's dispatch (vtable 0x68de14 +0xc, called by the game update at
// 0x46f0e9 with the game time), so every co-op peer runs them in the same lockstep frame. No DLL
// event enters the native scheduler and no state is saved: a load recreates the scanner from the
// authored placement, as the original's constructor does. Shots use the native projectile service
// with the turret physent as the source.
namespace xml1_turrets {
void register_ai_layout(){actor_ai::register_consumer(actor_ai::consumer::turrets);}
namespace {
using u32=std::uint32_t;
using turret_rules::vec;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T> void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T> T fn(u32 va){return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
using resolve_t=void*(__fastcall*)(const u32*,void*);
using clock_t=float(__cdecl*)();
using center_t=const float*(__fastcall*)(void*,void*,float*);
using orient_t=void(__fastcall*)(void*,void*,const float*);
using process_t=void(__fastcall*)(void*,void*,float);
using intern_t=u32*(__fastcall*)(u32*,void*,const char*);
using attack_t=void*(__fastcall*)(void*,void*,u32,float,short,u32,u32,u32,unsigned char,const float*,const float*,const float*);
using system_t=void*(__cdecl*)();
using spawn_t=void(__fastcall*)(void*,void*,u32*,void*,const void*,const void*,u32,float,u32);
using query_t=void(__fastcall*)(void**,void*,const void*,const u32*);
using filter_t=void(__fastcall*)(void*,void*,int,int);
using sphere_t=void(__fastcall*)(void*,void*,const float*,float,u32,unsigned char);
using segment_t=void(__fastcall*)(void*,void*,const float*,const float*,const float*);
using results_t=void*(__cdecl*)(void*,const void*);
using count_t=int(__fastcall*)(void*,void*);
using item_t=void*(__fastcall*)(void*,void*,int);
using flag_t=unsigned char(__fastcall*)(void*,void*);
using reader_ctor_t=void(__fastcall*)(void*,void*);
using reader_load_t=unsigned char(__fastcall*)(void*,void*,const char*,unsigned char,u32,unsigned char);
using reader_root_t=void*(__fastcall*)(void*,void*);
using definitions_t=void(__fastcall*)(void*,void*,void*,void*,u32,u32,unsigned char,unsigned char);
using definition_t=void*(__fastcall*)(void*,void*,const char*);
using hitinfo_t=void*(__fastcall*)(void*,void*,void**,int);
using take_hit_t=void(__fastcall*)(void*,void*,void*,u32,u32,u32);
using effect_resolve_t=u32(__fastcall*)(void*,void*,const char*,u32);
using effect_play_t=void(__fastcall*)(void*,void*,u32,const float*,const float*,u32,float,u32,float);
using danger_t=void(__fastcall*)(void*,void*,const float*,u32,float,float,u32,u32,u32);
using sound_resolve_t=u32(__fastcall*)(void*,void*,const char*,u32,u32);
using sound_loop_t=u32(__fastcall*)(void*,void*,u32,const float*,float,float,float);
using sound_stop_t=void(__fastcall*)(void*,void*,u32);
using entity_sound_t=void(__fastcall*)(void*,void*,u32);

constexpr u32 scheduler=0x75d340,scheduler_vtable=0x68de14,process_slot=scheduler_vtable+0xc,native_process=0x4b2d60;
constexpr u32 enabled_bit=1u<<20;           // PC 'startenabled' (XBE bit 19, scanner 0xa9000 test)
constexpr u32 actor_class=0x70b83c,obstruction_class=0x778a4c,physent_class=0x753f64;

struct turret {
    turret_rules::spec spec;
    u32 handle=0;
    turret_rules::state s;
    bool live=false;
    turret_rules::binding b;
    std::array<u32,3> effects{};   // resolved muzzle / tracer / impact effect handles (zone group)
    bool effects_resolved=false;
    u32 fire_sound=0,rotate_sound=0,loop=0;   // resolved sounds; the rotate loop playing (0 = none)
};
std::vector<turret> turrets;
struct flight{u32 handle=0,target=0;float last[3]{};float born=0;};
std::vector<flight> flights;   // homing projectiles in flight (zone-local, never saved)
std::vector<std::string> lines;
bool was_requested=false,installed=false,trace=false;
process_t real_process=nullptr;
char zone[64]{};
float last_time=-1;
std::vector<std::string> loaded_files;
std::array<unsigned char,12> reader{};       // definitions stay registered for the process (see load_definitions)

float now(){return fn<clock_t>(0x45fe60)();}
void* resolve(u32 h){return h?fn<resolve_t>(0x4654b0)(&h,nullptr):nullptr;}
vec position(void* e){return {at<float>(e,0x20),at<float>(e,0x24),at<float>(e,0x28)};}
vec orientation(void* e){return {at<float>(e,0x2c),at<float>(e,0x30),at<float>(e,0x34)};}
vec center(void* e){float scratch[3]{};const auto* p=at<center_t>(at<const void*>(e,0),0x24)(e,nullptr,scratch);return {p[0],p[1],p[2]};}
float health(void* e){return at<float>(e,0x27c);}
float random01(){return fn<float(__cdecl*)(unsigned char)>(0x560190)(1);}

struct query_guard {
    void* value=nullptr;void* results=nullptr;
    ~query_guard(){
        if(results)fn<void(__cdecl*)(void*)>(0x4ab7d0)(results);
        if(value)fn<void(__fastcall*)(void**,void*)>(0x4ab720)(&value,nullptr);
    }
};
// The nearest-first ordering metric the native queries use (mold_query; 0x683ed0, three axes).
struct metric{u32 vtable;float position[3];u32 axes;};
// Sentry visibility (0x4b42c0..0x4b4361): an obstruction query between the two centres, swept
// with unit extents, ordered along the direction (metric 0x68202c); clear when it finds nothing.
bool clear_line(u32 self,const vec& from,const vec& to){
    if(!mold_query::pools_ready())return false;
    query_guard q;u32 exclusion=self;
    fn<query_t>(0x4ab6d0)(&q.value,nullptr,reinterpret_cast<const void*>(obstruction_class),&exclusion);
    if(!q.value)return false;
    const float ones[3]{1,1,1};
    at<segment_t>(at<const void*>(q.value,0),0x74)(q.value,nullptr,from.data(),to.data(),ones);
    struct {u32 vtable;float direction[3];} order{0x68202c,{to[0]-from[0],to[1]-from[1],to[2]-from[2]}};
    const float n=std::sqrt(order.direction[0]*order.direction[0]+order.direction[1]*order.direction[1]+order.direction[2]*order.direction[2]);
    if(n>0)for(auto& v:order.direction)v/=n;
    q.results=fn<results_t>(0x4ab770)(q.value,&order);
    if(!q.results)return false;
    if(at<count_t>(at<const void*>(q.results,0),0)(q.results,nullptr)!=0)return false;
    return at<flag_t>(at<const void*>(q.value,0),0x28)(q.value,nullptr)==0;
}
// Scanner candidates (0xa908e..0xa9668): the team-filtered entities of the scan volume, nearest
// first; the first four inside the vision cone; living ones; then the nearest visible one.
turret_rules::sight look(turret& t,void* self){
    turret_rules::sight seen;
    if(!mold_query::pools_ready())return seen;
    const auto& c=t.spec.scan;
    const vec origin=position(self),facing=turret_rules::forward(orientation(self));
    std::array<u32,4> picked{};int n=0;
    {
        query_guard q;u32 exclusion=t.handle;
        fn<query_t>(0x4ab6d0)(&q.value,nullptr,reinterpret_cast<const void*>(actor_class),&exclusion);
        if(!q.value)return seen;
        const auto* vt=at<const void*>(q.value,0);
        at<filter_t>(vt,0x40)(q.value,nullptr,2,c.filterteam);
        metric order{0x683ed0,{origin[0],origin[1],origin[2]},3};
        at<sphere_t>(vt,0x68)(q.value,nullptr,order.position,c.visrange,0,0);
        q.results=fn<results_t>(0x4ab770)(q.value,&order);
        if(!q.results)return seen;
        const auto* rvt=at<const void*>(q.results,0);
        const int count=at<count_t>(rvt,0)(q.results,nullptr);
        const float cone=std::cos(c.visextent);
        for(int i=0;i<count&&n<4;++i){
            auto* e=at<item_t>(rvt,4)(q.results,nullptr,i);
            if(!e||!mold_query::is_a(e,actor_class))continue;
            const vec p=center(e);vec d{p[0]-origin[0],p[1]-origin[1],p[2]-origin[2]};
            const float len=std::sqrt(turret_rules::dot(d,d));
            if(len>c.visrange)continue;
            if(len>0){for(auto& v:d)v/=len;if(turret_rules::dot(d,facing)<cone)continue;}
            if(!(health(e)>0))continue;
            // 0xa91c0..0xa91f8: a candidate slower than minspeed (velocity XBE +0x100, PC +0x120) is skipped,
            // and a hero filter skips heroes whose XBE +0x334 bit 0 is set (PC +0x3d8 bit 0, the same
            // character bit the take-hit 0x438a10 / 0x46270 tests before notifying the attacker's AI).
            if(c.minspeed>0){const vec v{at<float>(e,0x120),at<float>(e,0x124),at<float>(e,0x128)};if(turret_rules::dot(v,v)<c.minspeed*c.minspeed)continue;}
            if(trace)logger::write("xml1 turrets candidate: %s %08X team-filter %d flag3d8 %02X",t.spec.instance.c_str(),at<u32>(e,0x1c),c.filterteam,at<unsigned char>(e,0x3d8));
            if(c.filterteam==29&&(at<unsigned char>(e,0x3d8)&1))continue;
            picked[n++]=at<u32>(e,0x1c);
        }
    }
    // 0xa9590..0xa95f0: the ray starts at the scanner's own position (+0x20), aimed at the
    // candidate's centre (vt+0x24).
    const vec from=origin;
    int blocked=0;
    for(int i=0;i<n;++i){
        auto* e=resolve(picked[i]);if(!e)continue;
        const vec to=center(e);
        if(clear_line(t.handle,from,to)){seen.found=true;seen.target=picked[i];seen.aim=to;break;}
        ++blocked;
    }
    if(trace&&n)logger::write("xml1 turrets look: %s candidates %d blocked %d found %08X",t.spec.instance.c_str(),n,blocked,seen.target);
    return seen;
}
// The placement's physent: names repeat across entity types (haarp_ext02 has a waypoint and the
// turret both named tank_turret), so the turret is the physent of that name at its authored spot.
// A turret a zone script spawns (the Hive tanks' spawn("tank1", "tank_turret_sp01", "turret1", ...))
// has no authored spot: the physent of that name anywhere in the zone.
u32 bind(const turret& t){
    if(!mold_query::pools_ready())return 0;
    query_guard q;u32 exclusion=0;
    fn<query_t>(0x4ab6d0)(&q.value,nullptr,reinterpret_cast<const void*>(physent_class),&exclusion);
    if(!q.value)return 0;
    metric order{0x683ed0,{t.spec.position[0],t.spec.position[1],t.spec.position[2]},3};
    at<sphere_t>(at<const void*>(q.value,0),0x68)(q.value,nullptr,order.position,t.spec.spawned?1.e6f:16.f,0,0);
    q.results=fn<results_t>(0x4ab770)(q.value,&order);
    if(!q.results)return 0;
    const auto* vt=at<const void*>(q.results,0);
    const int count=at<count_t>(vt,0)(q.results,nullptr);
    for(int i=0;i<count&&(t.spec.spawned||i<64);++i){
        auto* e=at<item_t>(vt,4)(q.results,nullptr,i);
        if(!e||!mold_query::is_a(e,physent_class))continue;
        const auto* name=fn<const char*(__fastcall*)(void*,void*)>(0x41fbb0)(e,nullptr);
        if(name&&_stricmp(name,t.spec.instance.c_str())==0)return at<u32>(e,0x1c);
    }
    return 0;
}
bool load_definitions(const std::string& file,const std::string& entity){
    auto* manager=fn<system_t>(0x4625c0)();
    const auto* vt=at<const void*>(manager,0);
    if(at<definition_t>(vt,0x20)(manager,nullptr,entity.c_str()))return true;
    for(const auto& f:loaded_files)if(f==file)return false;
    loaded_files.push_back(file);
    // The diagnostic reader of 0xc9d76d9: read Data/entities/<file> and register its definitions
    // the way a zone's own entity files register (manager vt+4, group 11).
    std::memset(reader.data(),0,reader.size());
    fn<reader_ctor_t>(0x564a80)(reader.data(),nullptr);
    const auto path="data/entities/"+file;
    const auto ok=fn<reader_load_t>(0x564eb0)(reader.data(),nullptr,path.c_str(),0,3,0);
    auto* root=fn<reader_root_t>(0x564aa0)(reader.data(),nullptr);
    u32 key=0;fn<intern_t>(0x41b120)(&key,nullptr,file.c_str());
    if(ok&&root)at<definitions_t>(vt,4)(manager,nullptr,root,root,11,key,1,1);
    const bool found=at<definition_t>(vt,0x20)(manager,nullptr,entity.c_str())!=nullptr;
    logger::write("xml1 turrets: definitions %s %s (%s)",file.c_str(),found?"registered":"unavailable",entity.c_str());
    return found;
}
// One weapon shot (prototype fire 0xbcba0 -> projectile branch 0xbb6f0 -> spawn 0x5a090): count
// projectiles fanned about the muzzle yaw by spread degrees (k = offset from the fan centre: yaw -
// k*spread, start + k along the right vector, speed x (1 + 0.1 k)); direct damage is one draw per
// shot (0xbcc47..0xbcc99), shared by the fan. A homing projectile of a scanner source homes on the
// scanner's target (0x5a090: projectile +0x2c8 = scanent +0x340): watch_flights steers it.
// The muzzle is the model's Gun1 bolt in the turret's frame (0xb9e40, Gun<emitter+1>, emitter 0).
void shoot_ray(turret& t,void* self);
void resolve_effects(turret& t);
void play(u32 effect,const vec& point,const vec& direction);
void shoot(turret& t,void* self){
    const auto& w=t.spec.gun;
    if(w.kind==turret_rules::weapon_kind::bullet||w.kind==turret_rules::weapon_kind::flame){shoot_ray(t,self);return;}
    if(w.kind!=turret_rules::weapon_kind::projectile){logger::write_once("turret-kind","xml1 turrets: weapon kind %d not emitted in this build",int(w.kind));return;}
    if(!load_definitions(w.entfile,w.projectile))return;
    const vec facing=orientation(self),muzzle=turret_rules::frame_point(position(self),facing,t.spec.muzzle);
    if(!t.effects_resolved)resolve_effects(t);
    play(t.effects[0],muzzle,turret_rules::forward(facing));             // muzzlefx (0xbcba0, before the branch)
    const float damage=turret_rules::draw(w.damage_min,w.damage_max,random01());
    u32 key=0;fn<intern_t>(0x41b120)(&key,nullptr,w.projectile.c_str());
    int homing=0;
    for(int i=0;i<w.count;++i){
        const float k=turret_rules::fan_offset(i,w.count);
        const vec e{0,facing[1],facing[2]-k*w.spread*turret_rules::radians_per_degree};
        const vec d=turret_rules::forward(e),right=turret_rules::forward({0,0,e[2]-1.57079637f});  // 0x120a30 basis
        std::array<unsigned char,0x30> description{};
        const float origin[3]{muzzle[0]+right[0]*k,muzzle[1]+right[1]*k,muzzle[2]+right[2]*k};
        std::memcpy(description.data(),origin,12);
        std::memcpy(description.data()+12,d.data(),12);
        put(description.data(),0x1c,key);
        put(description.data(),0x24,static_cast<short>(w.speed*(k*.1f+1.f)));
        put(description.data(),0x2c,static_cast<unsigned char>(1));
        std::array<unsigned char,0x6c> attack{};const float zero[3]{};
        // 0xbcba0 builds one attack per fire with the weapon's damage type (+0x1d0) and damagemods
        // (+0x1cc) for every weapon kind (0xbcc51 / 0xbcc8e): the HAARP salvo keeps its auto knockback.
        fn<attack_t>(0x429860)(attack.data(),nullptr,t.handle,damage,0,5,w.damage_type,w.damage_mods,1,zero,zero,zero);
        auto* system=fn<system_t>(0x450030)();
        u32 result=0;
        at<spawn_t>(at<const void*>(system,0),0x28)(system,nullptr,&result,self,description.data(),attack.data(),0,30.f,0);
        auto* projectile=resolve(result);
        void* data=projectile?at<void*>(projectile,0x320):nullptr;
        if(data&&t.s.target&&(at<unsigned char>(data,0xac)&1)&&flights.size()<64){
            flights.push_back({result,t.s.target,{at<float>(projectile,0x20),at<float>(projectile,0x24),at<float>(projectile,0x28)},now()});++homing;}
    }
    if(trace)logger::write("xml1 turrets: %s fired %d x %s at %.3f yaw %.3f pitch %.3f spread %.0f speed %.0f damage %.0f target %08X homing %d",
        t.spec.instance.c_str(),w.count,w.projectile.c_str(),now(),facing[2],facing[1],w.spread,w.speed,damage,t.s.target,homing);
}
// Effects: the effect manager (getter 0x407750, singleton 0x6f3c50, vtable 0x68190c) resolves a
// path under effects/ in the zone's resource group 11 (vt+0x1c, as 0x4e6cb0 does for an event's
// effect) and plays one at a point along a direction (vt+0x34, as the hit service 0x453160 does).
// The zone unload releases the group, so a (re)bound turret resolves its effects again.
void resolve_effects(turret& t){
    t.effects_resolved=true;t.effects={};
    auto* manager=fn<system_t>(0x407750)();
    for(int i=0;i<3;++i){
        const auto& path=t.spec.gun.effects[i];
        if(path.empty())continue;
        t.effects[i]=at<effect_resolve_t>(at<const void*>(manager,0),0x1c)(manager,nullptr,path.c_str(),11);
        if(!t.effects[i])logger::write("xml1 turrets: %s effect %s unavailable",t.spec.instance.c_str(),path.c_str());
    }
}
void play(u32 effect,const vec& point,const vec& direction){
    if(!effect)return;
    auto* manager=fn<system_t>(0x407750)();
    at<effect_play_t>(at<const void*>(manager,0),0x34)(manager,nullptr,effect,point.data(),direction.data(),0,0.f,0,-1.f);
}
// The first entity a ray weapon's segment crosses (the PC beam's trace, 0x45237d..0x452447, without
// its hostility filter, as the original 0xba571.. has none): physent query 0x4ab6d0 excluding the
// turret, vt+0x40 (1, 1) and (2, 0xe) (XML1's (1, 1) and (2, 0xd); PC flag bits are one higher),
// swept segment vt+0x74, ordered by time of impact (metric 0x68d684 over the query), contact record
// from the results' vt+8 (+0x30 point, +0x20 normal).
struct ray_hit{u32 handle=0;vec point{},normal{};};
ray_hit first_hit(u32 self,const turret_rules::segment& g){
    ray_hit hit;
    if(!mold_query::pools_ready())return hit;
    query_guard q;u32 exclusion=self;
    fn<query_t>(0x4ab6d0)(&q.value,nullptr,reinterpret_cast<const void*>(physent_class),&exclusion);
    if(!q.value)return hit;
    const auto* vt=at<const void*>(q.value,0);
    at<filter_t>(vt,0x40)(q.value,nullptr,1,1);
    at<filter_t>(vt,0x40)(q.value,nullptr,2,0xe);
    at<segment_t>(vt,0x74)(q.value,nullptr,g.from.data(),g.to.data(),g.extent.data());
    struct {u32 vtable;void* query;} order{0x68d684,q.value};
    q.results=fn<results_t>(0x4ab770)(q.value,&order);
    if(!q.results)return hit;
    const auto* rvt=at<const void*>(q.results,0);
    if(at<count_t>(rvt,0)(q.results,nullptr)<1)return hit;
    auto* e=at<item_t>(rvt,4)(q.results,nullptr,0);
    if(!e)return hit;
    void* record=nullptr;
    at<hitinfo_t>(rvt,8)(q.results,nullptr,&record,0);
    hit.handle=at<u32>(e,0x1c);
    if(record){
        hit.point={at<float>(record,0x30),at<float>(record,0x34),at<float>(record,0x38)};
        hit.normal={at<float>(record,0x20),at<float>(record,0x24),at<float>(record,0x28)};
    }else hit.point=center(e);
    return hit;
}
// One bullet or flame fire (0xbcba0 -> 0xba420 / 0xba850). The muzzle is the model's Gun1 bolt in
// the turret's frame (0xb9e40; the entity origin when the model has none) and the aim its orientation
// spread by the weapon's accuracy; the draw is one integer per fire as for missiles. The first entity on the
// segment takes the hit when it is an actor (0xba70a..: vt+0xa8(attack, 1, 1, 1) on the XBE, no
// to-hit roll): on the PC the same direct take-hit call, entity vt+0xb0, that the projectile contact
// 0x49c0d0 uses for an attacker that is not a character, with the attack's point (+0x40), direction
// (+0x4c) and normal (+0x58) written after the constructor (which zeroes them). Walls stop the ray (the sentry's line-of-sight test to the contact).
void shoot_ray(turret& t,void* self){
    const auto& w=t.spec.gun;
    if(!t.effects_resolved)resolve_effects(t);
    const vec origin=turret_rules::frame_point(position(self),orientation(self),t.spec.muzzle);
    const float ry=random01(),rp=random01();
    const vec dir=turret_rules::aim(orientation(self),w.accuracy*turret_rules::radians_per_degree,ry,rp);
    const float damage=turret_rules::draw(w.damage_min,w.damage_max,random01());
    play(t.effects[0],origin,dir);                                   // muzzlefx (0xbcba0, before the branch)
    const float time=now();
    if(!turret_rules::trace_ready(t.s.last_trace,time,w.tracetime))return;
    play(t.effects[1],origin,dir);                                   // muzzleaccfx: tracer / flame stream
    const auto g=turret_rules::trace_segment(origin,dir,w.range,w.radius,w.kind);
    const auto hit=first_hit(t.handle,g);
    void* e=resolve(hit.handle);
    bool applied=false;float before=0,after=0;
    if(e&&clear_line(t.handle,origin,hit.point)){
        if(mold_query::is_a(e,actor_class)&&mold_query::is_a(e,physent_class)){
            std::array<unsigned char,0x6c> attack{};
            fn<attack_t>(0x429860)(attack.data(),nullptr,t.handle,damage,0,5,w.damage_type,w.damage_mods,1,hit.point.data(),dir.data(),hit.normal.data());
            // 0x429860 ends in 0x4539a0, which zeroes +0x40/+0x4c/+0x58 again; native callers write
            // them after construction (0x4b3ebb..), as the XBE bullet branch does (0xba753..0xba78d).
            put(attack.data(),0x40,hit.point);put(attack.data(),0x4c,dir);put(attack.data(),0x58,hit.normal);
            before=health(e);
            at<take_hit_t>(at<const void*>(e,0),0xb0)(e,nullptr,attack.data(),1,1,1);
            after=health(e);applied=true;
        }
        play(t.effects[2],hit.point,hit.normal);                     // impactfx
    }
    if(trace)logger::write("xml1 turrets: %s %s at %.3f dir %.3f/%.3f/%.3f hit %08X%s damage %.0f health %.1f -> %.1f",
        t.spec.instance.c_str(),w.kind==turret_rules::weapon_kind::flame?"flame":"bullet",time,dir[0],dir[1],dir[2],hit.handle,
        applied?" (actor)":"",damage,before,after);
}
// After each weapon fire the original warns the AI (0xa9a80 from the fire event 0xa9a78): AI manager
// 0xf9a00 vt+0x22c(aim point +0x318, team 0x1a = neutral, radius 70, the fire's recoil + 0.5 s, 0), a
// temporary danger notice every AI steers away from. PC: AI manager 0x5252e0 (vtable 0x695cbc)
// vt+0x240 = 0x523c50 -> store 0x5044f0, neutral team 0x1c, two more zero arguments (as the harm
// notice 0x439391.. passes them).
void warn_ai(const turret& t,float recoil){
    auto* ai=fn<system_t>(0x5252e0)();
    at<danger_t>(at<const void*>(ai,0),0x240)(ai,nullptr,t.s.aim.data(),0x1c,70.f,recoil+.5f,0,0,0);
}
// Sounds: the sound system (getter 0x592480, singleton 0x7f07b8, vtable 0x69d1ac) resolves a name
// (vt+0x38 = 0x590bd0(name, 0, 0), as an event's sound at prepare 0x4e8e00; "none" is [0x69d058]).
// A fire plays the weapon's firesound on the turret (entity vt+0x8c, the event sound's plain play
// 0x4e8a70; the original plays it at the muzzle, 0xbcba0). The rotation loops the turret's
// rotatesound while the turret turns (XBE 0xa8a90: loop at the scanner's position with volume 1,
// range 2 x visrange / visrange x 0.5; stopped on arrival): vt+0x4c = 0x5917f0 starts a 3D loop
// (as the constant beam's loop 0x4eec70), vt+0x74 = 0x590780 stops it.
void* sound_system(){return fn<system_t>(0x592480)();}
u32 sound_none(){return at<u32>(reinterpret_cast<const void*>(0x69d058),0);}
void resolve_sounds(turret& t){
    auto* s=sound_system();const u32 none=sound_none();
    auto get=[&](const std::string& name)->u32{
        if(name.empty())return 0;
        const u32 h=at<sound_resolve_t>(at<const void*>(s,0),0x38)(s,nullptr,name.c_str(),0,0);
        if(h==none){logger::write("xml1 turrets: %s sound %s unavailable",t.spec.instance.c_str(),name.c_str());return 0;}
        return h;};
    t.fire_sound=get(t.spec.sound.fire);t.rotate_sound=get(t.spec.sound.rotate);t.loop=0;
}
void stop_loop(turret& t){
    if(!t.loop)return;
    auto* s=sound_system();
    at<sound_stop_t>(at<const void*>(s,0),0x74)(s,nullptr,t.loop);
    t.loop=0;
}
void start_loop(turret& t,void* self){
    if(t.loop||!t.rotate_sound)return;
    auto* s=sound_system();const vec p=position(self);
    const u32 h=at<sound_loop_t>(at<const void*>(s,0),0x4c)(s,nullptr,t.rotate_sound,p.data(),1.f,t.spec.scan.visrange*2,t.spec.scan.visrange*.5f);
    t.loop=h==at<u32>(reinterpret_cast<const void*>(0x69d05c),0)?0:h;
}
// A zone change or a clock that went back: nothing learnt earlier in the process may delay binding
// (turret_rules::binding), and a playing rotate loop is stopped (0x590780 ignores unknown handles).
void forget(){for(auto& t:turrets){stop_loop(t);t.live=false;t.handle=0;t.effects_resolved=false;turret_rules::forget(t.b);}flights.clear();}
void update(float time){
    const char* current=reinterpret_cast<const char*>(0x72a758);
    if(std::strncmp(current,zone,sizeof zone)!=0||turret_rules::clock_went_back(time,last_time)){
        std::strncpy(zone,current,sizeof zone-1);forget();
        logger::write("xml1 turrets: zone %s",zone);
    }
    last_time=time;
    for(auto& t:turrets){
        if(t.spec.zone!=zone)continue;
        void* self=t.live?resolve(t.handle):nullptr;
        if(self&&(!mold_query::is_a(self,physent_class)||!(health(self)>0)))self=nullptr;
        if(!self){
            if(t.live){logger::write("xml1 turrets: %s gone at %.3f",t.spec.instance.c_str(),time);stop_loop(t);t.live=false;t.handle=0;turret_rules::bind_failed(t.b,time);}
            if(!turret_rules::may_bind(t.b,time))continue;
            turret_rules::bind_failed(t.b,time);
            const u32 h=bind(t);self=resolve(h);
            if(!self||!(health(self)>0))continue;
            t.handle=h;t.live=true;t.s=turret_rules::spawn(time);t.effects_resolved=false;
            loaded_files.clear();   // a zone (re)load drops group-11 definitions: register again on demand
            const auto o=orientation(self);
            if(t.spec.spawned&&!t.b.rest_taken){t.spec.rest={o[0],o[1],o[2]};t.b.rest_taken=true;}   // first binding of this zone visit
            resolve_sounds(t);
            logger::write("xml1 turrets: %s bound %08X at %.1f/%.1f/%.1f orientation %.3f/%.3f/%.3f rest %.3f/%.3f/%.3f flags %08X",t.spec.instance.c_str(),h,
                at<float>(self,0x20),at<float>(self,0x24),at<float>(self,0x28),o[0],o[1],o[2],t.spec.rest[0],t.spec.rest[1],t.spec.rest[2],at<u32>(self,4));
        }
        const bool enabled=(at<u32>(self,4)&enabled_bit)!=0;
        // The three events in due order, each at most once per native dispatch.
        for(int pass=0;pass<3;++pass){
            float* due[3]{&t.s.acquire_due,&t.s.rotate_due,&t.s.fire_due};
            int next=-1;for(int i=0;i<3;++i)if(*due[i]<=time&&(next<0||*due[i]<*due[next]))next=i;
            if(next<0)break;
            if(next==0){
                turret_rules::sight seen;
                // 0xa9000: inactive unless enabled; the awake bit (XBE +4 bit 15) is not mapped and counts as set.
                if(enabled)seen=look(t,self);
                const int before=t.s.mode;
                t.s.acquire_due=time+turret_rules::acquire(t.s,t.spec.scan,time,enabled,seen);
                if(before!=t.s.mode)logger::write("xml1 turrets: %s mode %d -> %d at %.3f target %08X",t.spec.instance.c_str(),before,t.s.mode,time,t.s.target);
            }else if(next==1){
                vec o=orientation(self);bool moved=false,turning=false;
                const float wait=turret_rules::rotate(t.s,t.spec.scan,t.spec.rest,time,true,position(self),o,moved,turning);
                t.s.rotate_due=time+wait;
                if(moved)at<orient_t>(at<const void*>(self,0),0x2c)(self,nullptr,o.data());
                if(turning)start_loop(t,self);
                else if(wait<.5f)stop_loop(t);          // a step that arrived (paused / idle passes keep it)
            }else{
                float delay=1.f;
                const auto r=turret_rules::fire(t.s,time,1,t.spec.gun.recoil,delay);
                t.s.fire_due=time+delay;
                if(r==turret_rules::fire_result::shot){
                    shoot(t,self);warn_ai(t,t.spec.gun.recoil);
                    // a take-hit can run a deathscript: use the turret again only through its handle
                    self=resolve(t.handle);
                    if(!self||!mold_query::is_a(self,physent_class)||!(health(self)>0)){self=nullptr;break;}
                    if(t.fire_sound)at<entity_sound_t>(at<const void*>(self,0),0x8c)(self,nullptr,t.fire_sound);
                }
                else if(r==turret_rules::fire_result::burst_started)logger::write("xml1 turrets: %s burst armed at %.3f",t.spec.instance.c_str(),time);
            }
        }
        if(self&&trace&&time>t.b.trace_at){
            t.b.trace_at=time+1;const auto o=orientation(self);
            logger::write("xml1 turrets trace: %s t %.3f mode %d enabled %d yaw %.3f pitch %.3f target %08X seen %.2f burst %d health %.1f",
                t.spec.instance.c_str(),time,t.s.mode,int(enabled),o[2],o[1],t.s.target,t.s.seen,t.s.burst,health(self));
        }
    }
}
// The original projectile update's homing (0x96ba0): toward the target's origin, the velocity
// direction plus f x the unit direction to the target, f = (10 + age) x 0.05 clamped to
// [0.001, 1.5], renormalised at the same speed; nozhoming (+0x335 bit 0) keeps it level. PC
// projectileent has no in-flight homing (only the guided class), so the adapter applies it to the
// turret's own projectiles each dispatch: velocity +0x120, setter 0x473640.
void watch_flights(float time){
    for(auto it=flights.begin();it!=flights.end();){
        auto* e=resolve(it->handle);
        if(!e||!(health(e)>0)){
            if(trace)logger::write("xml1 turrets flight: %08X ended after %.3f s at %.1f/%.1f/%.1f",it->handle,time-it->born,it->last[0],it->last[1],it->last[2]);
            it=flights.erase(it);continue;
        }
        it->last[0]=at<float>(e,0x20);it->last[1]=at<float>(e,0x24);it->last[2]=at<float>(e,0x28);
        auto* target=resolve(it->target);
        void* data=at<void*>(e,0x320);
        if(target&&data){
            const vec v{at<float>(e,0x120),at<float>(e,0x124),at<float>(e,0x128)};
            const vec d{at<float>(target,0x20)-it->last[0],at<float>(target,0x24)-it->last[1],at<float>(target,0x28)-it->last[2]};
            const vec w=turret_rules::home(v,d,time-it->born,(at<unsigned char>(data,0xac)&4)!=0);   // +0xac bit 2: nozhoming
            if(w!=v)fn<void(__fastcall*)(void*,void*,const float*)>(0x473640)(e,nullptr,w.data());
        }
        ++it;
    }
}
void __fastcall process_hook(void* self,void* edx,float time){
    real_process(self,edx,time);
    if(installed&&self==reinterpret_cast<void*>(scheduler)){update(time);watch_flights(time);}
}
u32 fnv(const std::vector<std::string>& all){
    u32 h=2166136261u;
    for(const auto& line:all){for(unsigned char c:line){h^=c;h*=16777619u;}h^=0x0a;h*=16777619u;}
    return h;
}
}
bool requested(){return was_requested;}
xml1_capability_rules::registration capability(){
    return xml1_capability_rules::turrets(was_requested,installed,static_cast<std::uint32_t>(turrets.size()),fnv(lines));
}
void install(HMODULE game){
    actor_ai::register_consumer(actor_ai::consumer::turrets);
    was_requested=ini::flag(L"Game",L"Xml1Turrets",false);installed=false;
    if(!was_requested)return;
    trace=ini::flag(L"Test",L"TurretTrace",false);
    if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base){logger::write("xml1 turrets: XMen2.exe is not at its image base; turrets stay inert");return;}
    const int count=ini::number(L"Xml1Turrets",L"Count",0);
    if(count<1||count>32){logger::write("xml1 turrets: no placements; turrets stay inert");return;}
    for(int i=1;i<=count;++i){
        wchar_t key[16]{};std::swprintf(key,16,L"T%d",i);
        const auto text=ini::text(L"Xml1Turrets",key);
        const auto spec=text?turret_rules::parse(*text):std::nullopt;
        if(!spec){logger::write("xml1 turrets: placement %d malformed; turrets stay inert",i);turrets.clear();lines.clear();return;}
        turrets.push_back({*spec});lines.push_back(*text);
    }
    constexpr std::array<limits_rules::guard,36> guards{{
        {native_process,"83c104e9","scheduler dispatch thunk"},
        {0x4b2d70,"83ec08578bf98b876c9f0000","scheduler dispatch"},
        {0x46f0d9,"e8e23e04008b8ee8030000","game update calls the scheduler dispatch"},
        {0x4b2fe2,"c70540d3750014de6800","scheduler singleton vtable store"},
        {0x45fe60,"8b0d348471008b01ffa060010000","game clock"},
        {0x4654b0,"568bf1e8682a0600","handle resolver"},
        {0x41b120,"56578b7c240c85ff","string handle constructor"},
        {0x429860,"8b44241c8b5424145650","attack constructor"},
        {0x4510a0,"81ec8001000053555657","native projectile service"},
        {0x450030,"8a0da07a7100b801000000","projectile system getter"},
        {0x4625c0,"a1cc8f710085c07537","entity definition manager"},
        {0x564a80,"8bc18a480880e1fe","XML reader constructor"},
        {0x564eb0,"81ec08050000","XML reader loader"},
        {0x462610,"5153555657","definition registration"},
        {0x68698c,"10264600","definition registration vtable"},
        {0x560190,"8a44240484c07405e9f3feffffe93eff","native random stream"},
        {0x41fbb0,"568b711885f67507b8681968005ec3e8","instance name"},
        {0x473640,"83ec18568bf18b4604c1e805a801570f","entity velocity setter"},
        {0x683ed0,"e0934a00","distance ordering metric vtable"},
        {0x68202c,"30bb4d00","direction ordering metric vtable"},
        {0x68d684,"809a4a00","contact ordering metric vtable"},
        {0x407750,"64a1000000008a0de08e6f00","effect manager getter"},
        {0x407692,"c7060c196800","effect manager vtable store"},
        {0x681928,"a0794000","effect manager resolve slot"},
        {0x681940,"b0b24000","effect manager play slot"},
        {0x4079a0,"8b4424088b4c24045051e821","effect resolve"},
        {0x40b2b0,"83ec6855568be957896c240c","effect play"},
        {0x5252e0,"8a0d9c887900b80100000084","AI manager getter"},
        {0x525302,"c70510ba7800bc5c6900","AI manager vtable store"},
        {0x695efc,"503c5200","AI manager danger notice slot"},
        {0x592480,"64a1000000008a0da0d87f00","sound system getter"},
        {0x58f7da,"c706acd16900","sound system vtable store"},
        {0x69d1e4,"d00b5900","sound system resolve slot"},
        {0x69d1f8,"f0175900","sound system loop slot"},
        {0x69d220,"80075900","sound system stop slot"},
        {0x5917f0,"5356578bf98a87aed000","sound loop"}}};
    for(const auto& g:guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),actor_ai::guard(g.va,g.hex,actor_ai::installed()))){
        logger::write("xml1 turrets: guard failed %08X (%s); turrets stay inert",g.va,g.what);turrets.clear();lines.clear();return;}
    if(at<u32>(reinterpret_cast<const void*>(process_slot),0)!=native_process||at<u32>(reinterpret_cast<const void*>(0x4b2fe8),0)!=scheduler_vtable){
        logger::write("xml1 turrets: scheduler dispatch slot differs; turrets stay inert");turrets.clear();lines.clear();return;}
    if(!mold_query::supported()){logger::write("xml1 turrets: native query layout unavailable; turrets stay inert");turrets.clear();lines.clear();return;}
    real_process=fn<process_t>(native_process);
    using xml1_hook_transaction::pointer_bytes;
    const std::vector<xml1_hook_transaction::patch> writes{{process_slot,pointer_bytes(reinterpret_cast<void*>(native_process)),pointer_bytes(reinterpret_cast<void*>(&process_hook))}};
    xml1_hook_transaction::memory memory;bool safe=true;
    if(xml1_hook_transaction::apply(memory,writes,&safe)){installed=true;logger::write("xml1 turrets: %d placements on the native scheduler dispatch",count);}
    else if(safe){turrets.clear();lines.clear();logger::write("xml1 turrets: dispatch hook refused; turrets stay inert");}
    else{game_version::refuse_startup();logger::write("xml1 turrets: rollback/protection verification failed; process startup refused");}
}
}
