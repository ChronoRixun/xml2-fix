#include "mold_query.hpp"
#include "mold_target_rules.hpp"
#include "mold_combat_rules.hpp"
#include "mold_query_rules.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "entity_query.hpp"
#include "xml1_hook_memory.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include <algorithm>
#include <array>

namespace mold_query {
namespace {
using u32=std::uint32_t;
template<class T>T at(const void* p,u32 offset){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+offset,sizeof v);return v;}
template<class T>T fn(u32 address){return reinterpret_cast<T>(address);}
using getter_t=void*(__cdecl*)();
using lookup_t=u32*(__cdecl*)(u32*,const char*);
using resolve_t=void*(__fastcall*)(const u32*,void*);
using query_t=void(__fastcall*)(void**,void*,const void*,const u32*);
using filter_t=void(__fastcall*)(void*,void*,int,int);
using sphere_t=void(__fastcall*)(void*,void*,const float*,float,u32,unsigned char);
using results_t=void*(__cdecl*)(void*,const void*);
using count_t=int(__fastcall*)(void*,void*);
using item_t=void*(__fastcall*)(void*,void*,int);
using activate_t=unsigned char(__fastcall*)(void*,void*,float);
using radius_t=float(__fastcall*)(void*,void*);
struct metric{u32 vtable;float position[3];u32 axes;};
struct query {
    void* value=nullptr;
    void* results=nullptr;
    ~query(){
        if(results)fn<void(__cdecl*)(void*)>(0x4ab7d0)(results);
        if(value)fn<void(__fastcall*)(void**,void*)>(0x4ab720)(&value,nullptr);
    }
};
void* find(const char* name,u32& handle){
    return fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&handle,name),nullptr);
}
float distance2(const void* a,const void* b){
    float result=0;
    for(u32 i=0;i<3;++i){const auto d=at<float>(a,0x20+4*i)-at<float>(b,0x20+4*i);result+=d*d;}
    return result;
}
bool session_active(){
    // Session static 0xa53058 (built by 0x612be0); +0x420 is set while an
    // online/LAN game is hosted or joined (0x610d20).
    const auto* session=reinterpret_cast<const void*>(0xa53058);
    return at<u32>(session,0)==0x6a481c&&at<unsigned char>(session,0x420)!=0;
}
// The installed entity layout decides how the native query entries behave
// (mold_query_rules.hpp); the capacity module publishes it after the startup
// transaction, before this controller installs.
bool raised(){return entity_capacity::installed().raised;}
u32 world(){return entity_capacity::installed().world;}
bool pool_available(){
    // Never reach an acquisition that would fail: the retail one assumes a free
    // query and order, and the raised owner fails fast when all queries are busy.
    if(raised())return mold_query_rules::pool_free(true,nullptr,entity_query::QueryApi_004ab740(),entity_query::free_order_count());
    return mold_query_rules::pool_free(false,static_cast<const unsigned char*>(fn<getter_t>(0x4ab550)()),0,0);
}
}
bool is_a(void* entity,u32 descriptor){
    if(!entity)return false;
    const auto* concrete=at<void*(__fastcall*)(void*,void*)>(at<const void*>(entity,0),0)(entity,nullptr);
    const auto bit=at<u32>(reinterpret_cast<const void*>(descriptor),4)+0x24;
    return (at<u32>(concrete,0x14+4*(bit>>5))&(1u<<(bit&31)))!=0;
}
bool supported(){
    constexpr std::array<limits_rules::guard,10> guards{{
        {0x4a1759,"e872070b008b108bc8ff526083f8ff7e","active hero follows the player manager's index"},
        {0x4a176d,"8d048514b87000","per-player hero handle table"},
        {0x612c0a,"b95830a500","online session static"},
        {0x610d20,"8a8120040000c3","online session active flag"},
        {0x4aa100,"568bf1e8182cfaff8a4c","native spatial query"},
        {0x4aacf0,"8b54240856578b7c240c","query flags"},
        {0x4a93e0,"8b411083ec0c83f80775","query ordering metric"},
        {0x41a820,"51568bf18b4604c1e814","activation eligibility"},
        {0x679e90,"6824206800" "6830206800","actionent class registration"},
        {0x67af40,"68302068006890f5680068f4000000","waypoint class registration"}}};
    xml1_hook_transaction::memory memory;
    for(const auto& g:guards){
        std::array<unsigned char,16> bytes{};
        if(g.hex.size()>bytes.size()*2||!memory.read(g.va,bytes.data(),g.hex.size()/2)||!limits_rules::matches(bytes.data(),g.hex)){
            logger::write("mold query: guard failed %08X",g.va);return false;
        }
    }
    // Query entries: retail bytes on the stock layout; under EntityCapacity=1 the
    // exact replacement jumps the capacity transaction installed, whose manager
    // the native getter must return.
    const bool layout_raised=raised();
    if(layout_raised&&!entity_query::installed_manager()){logger::write("mold query: installed query owner unavailable");return false;}
    for(const auto& e:mold_query_rules::entries){
        std::array<unsigned char,16> bytes{};
        const u32 installed=layout_raised?entity_capacity::installed_target(e.va):0;
        if(!memory.read(e.va,bytes.data(),e.retail.size()/2)||!mold_query_rules::entry_ok(layout_raised,bytes.data(),e,installed)){
            logger::write("mold query: %s entry %08X does not match the %s layout",e.what,e.va,layout_raised?"raised":"stock");return false;
        }
    }
    if(layout_raised&&fn<getter_t>(0x4ab550)()!=entity_query::installed_manager()){logger::write("mold query: native getter does not return the installed query manager");return false;}
    return true;
}
u32 select(void* actor,bool forced){
    u32 exclusion=at<u32>(actor,0x1c);
    auto* origin=actor;
    float hero_distance=0;
    if(!forced){
        origin=active_hero(exclusion);
        if(!origin||at<float>(origin,0x27c)<=0)return 0;
        hero_distance=distance2(actor,origin);
        if(!(hero_distance<380.f*380.f))return 0;
    }
    // The native pool constructors assume availability. Reserve neither pool
    // when either is full; all calls run synchronously on the native AI thread.
    if(!pool_available())return 0;
    query q;
    fn<query_t>(0x4ab6d0)(&q.value,nullptr,reinterpret_cast<const void*>(0x70b678),&exclusion);
    if(!q.value)return 0; // raised owner: every query busy
    const auto* vt=at<const void*>(q.value,0);
    const auto filter=at<filter_t>(vt,0x40);
    filter(q.value,nullptr,1,30); // XML1 enemy 28 -> PC enemy 30
    filter(q.value,nullptr,1,28); // XML1 none 26 -> PC none 28
    filter(q.value,nullptr,1,31); // XML1 altenemy 29 -> PC altenemy 31
    filter(q.value,nullptr,2,24); // XML1 aishootme 23 -> PC 24
    metric order{0x683ed0,{},3};
    std::memcpy(order.position,static_cast<const unsigned char*>(origin)+0x20,12);
    at<sphere_t>(vt,0x68)(q.value,nullptr,order.position,forced?800.f:400.f,0,0);
    q.results=fn<results_t>(0x4ab770)(q.value,&order);
    if(!q.results)return 0;
    const auto* result_vt=at<const void*>(q.results,0);
    const int count=at<count_t>(result_vt,0)(q.results,nullptr);
    if(!mold_query_rules::result_count_ok(count,world()))return 0;
    std::array<u32,3> cores{};
    for(unsigned i=0;i<3;++i){char name[]="warp1";name[4]=char('1'+i);find(name,cores[i]);}
    // Sized for the largest World; only the native AI thread calls this.
    static std::array<mold_target_rules::candidate,entity_capacity_layout::max_world> candidates;
    std::fill_n(candidates.begin(),count,mold_target_rules::candidate{});
    for(int i=0;i<count;++i){
        auto* entity=at<item_t>(result_vt,4)(q.results,nullptr,i);
        if(!entity)continue;
        auto& c=candidates[i];
        c.handle=at<u32>(entity,0x1c);c.flags=at<u32>(entity,4);
        for(const auto h:cores)if(h&&h==c.handle)c.core=true;
        if(!c.core)continue;
        c.can_activate=fn<activate_t>(0x41a820)(entity,nullptr,0)!=0;
        c.boss_distance_squared=distance2(actor,entity);
        c.activation_distance=at<radius_t>(at<const void*>(entity,0),0xe0)(entity,nullptr);
    }
    return mold_target_rules::choose(forced,hero_distance,{candidates.data(),static_cast<std::size_t>(count)});
}
void* active_hero(u32& handle){
    handle=0;
    if(!session_active())return find("_ACTIVE_HERO_",handle);
    std::array<mold_combat_rules::player_hero,4> slots{};std::array<void*,4> entities{};
    for(u32 i=0;i<4;++i){
        slots[i].handle=at<u32>(reinterpret_cast<const void*>(0x70b814),4*i);
        entities[i]=slots[i].handle?fn<resolve_t>(0x4654b0)(&slots[i].handle,nullptr):nullptr;
        slots[i].living=is_a(entities[i],0x70b83c)&&at<float>(entities[i],0x27c)>0;
    }
    const int chosen=mold_combat_rules::session_hero(slots);
    if(chosen<0)return nullptr;
    handle=slots[chosen].handle;return entities[chosen];
}
u32 waypoint(void* hero){
    if(!hero||!pool_available())return 0;
    query q;u32 exclusion=0;
    fn<query_t>(0x4ab6d0)(&q.value,nullptr,reinterpret_cast<const void*>(0x778b3c),&exclusion);
    if(!q.value)return 0;
    metric order{0x683ed0,{},3};
    std::memcpy(order.position,static_cast<const unsigned char*>(hero)+0x20,12);
    at<sphere_t>(at<const void*>(q.value,0),0x68)(q.value,nullptr,order.position,800.f,0,0);
    q.results=fn<results_t>(0x4ab770)(q.value,&order);
    if(!q.results)return 0;
    const auto* vt=at<const void*>(q.results,0);
    const auto count=at<count_t>(vt,0)(q.results,nullptr);
    if(!mold_query_rules::result_count_ok(count,world()))return 0;
    for(int i=0;i<count;++i){
        auto* entity=at<item_t>(vt,4)(q.results,nullptr,i);
        if(entity){
            const auto* name=fn<const char*(__fastcall*)(void*,void*)>(0x41fbb0)(entity,nullptr);
            if(name&&std::strstr(name,"moldyspot"))return at<u32>(entity,0x1c);
        }
    }
    return 0;
}
}
