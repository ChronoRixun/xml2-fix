// ZF-3P-E2p: execute retail 0x466bf0 itself on production 1024-slot World/Grid/Query owners.
// Native vector-library imports are replaced by arithmetic services; entity activation is observed without gameplay.
namespace visibility_pattern {
using vec = entity_grid::vec3;
void* world=nullptr;void* grid=nullptr;void* query_manager=nullptr;
std::vector<unsigned> activated,deactivated;
void* __cdecl world_get(){return world;}
void* __cdecl grid_get(){return grid;}
void* __cdecl manager_get(){return query_manager;}
void __fastcall party(void*,void*,void* list){put(list,0,0x420u);put(list,0x14,1u);}
vec* __fastcall ctor(vec* self,void*){*self={};return self;}
vec* __fastcall ctor3(vec* self,void*,float x,float y,float z){*self={x,y,z};return self;}
void __fastcall row(const float* self,void*,unsigned i,vec* out){*out={self[4*i],self[4*i+1],self[4*i+2]};}
vec* __fastcall assign(vec* self,void*,const vec* rhs){*self=*rhs;return self;}
vec* __fastcall times_assign(vec* self,void*,const float* rhs){self->x*=*rhs;self->y*=*rhs;self->z*=*rhs;return self;}
vec* __fastcall plus_assign(vec* self,void*,const vec* rhs){self->x+=rhs->x;self->y+=rhs->y;self->z+=rhs->z;return self;}
vec* __fastcall minus_assign(vec* self,void*,const vec* rhs){self->x-=rhs->x;self->y-=rhs->y;self->z-=rhs->z;return self;}
vec* __fastcall times(const vec* self,void*,vec* out,const float* rhs){*out={self->x*(*rhs),self->y*(*rhs),self->z*(*rhs)};return out;}
vec* __fastcall plus(const vec* self,void*,vec* out,const vec* rhs){*out={self->x+rhs->x,self->y+rhs->y,self->z+rhs->z};return out;}
vec* __fastcall minus(const vec* self,void*,vec* out,const vec* rhs){*out={self->x-rhs->x,self->y-rhs->y,self->z-rhs->z};return out;}
void __fastcall minimum(vec* self,void*,const vec* rhs){self->x=(std::min)(self->x,rhs->x);self->y=(std::min)(self->y,rhs->y);self->z=(std::min)(self->z,rhs->z);}
void __fastcall maximum(vec* self,void*,const vec* rhs){self->x=(std::max)(self->x,rhs->x);self->y=(std::max)(self->y,rhs->y);self->z=(std::max)(self->z,rhs->z);}
void __fastcall active(void* self,void*,unsigned value){
    const unsigned token=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(self)+0x1c);
    (value?activated:deactivated).push_back(token&0x3ff);
    auto& flags=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(self)+4);
    if(value)flags|=1u<<16;else flags&=~(1u<<16);
    entity_world::property_update(world,nullptr,token,reinterpret_cast<unsigned*>(static_cast<unsigned char*>(self)+4));
}
void __fastcall unexpected_store(void*,void*){assert(!"unexpected smartent cull in visibility fixture");}
void __cdecl unexpected_remove(unsigned){assert(!"unexpected world removal in visibility fixture");}
const std::vector<unsigned> existing{0x116,0x117,0x119,0x11a,0x11d,0x11f,0x120};
const std::vector<unsigned> newcomer{0x115,0x117,0x119,0x11d,0x11e,0x120,0x121};
bool contains(const std::vector<unsigned>& values,unsigned i){return std::find(values.begin(),values.end(),i)!=values.end();}
struct outcome{std::vector<unsigned> on,off;std::vector<unsigned char> windows;std::vector<unsigned> active_set;};
outcome run_peer(bool is_newcomer,bool am,bool already_clear=false){
    using namespace entity_capacity_layout;
    auto d=raised_descriptor_for(0,0,0,0);
    world=entity_world::create(d);assert(world);d.world_object=unsigned(reinterpret_cast<std::uintptr_t>(world));
    grid=entity_grid::create(d);assert(grid);d.grid_object=unsigned(reinterpret_cast<std::uintptr_t>(grid));
    entity_capacity::fixture_descriptor=d;
    const auto WL=entity_world::layout_for(d.world);const auto GL=entity_grid::layout_for(d.world);
    const vec lo{-20000,-20000,-1000},hi{20000,20000,1000},cell{1000,1000,1000};
    entity_grid::init(grid,nullptr,&lo,&hi,&cell);
    std::array<std::array<unsigned char,0x900>,15> entities{};
    const auto& history=is_newcomer?newcomer:existing;
    // Seed handle-table membership directly: registration callbacks are deliberately outside this fixture.
    for(unsigned k=0;k<entities.size();++k){
        const unsigned i=k==14?0x20:0x114+k,token=0x400+i;auto* e=entities[k].data();
        put(e,0,join_in_progress_rules::actor_vtable);put(e,0x1c,token);put(e,0x3d4,k==14?0u:0xffffffffu);put(e,0x6c,1u);
        unsigned flags=(i==0x114?0u:1u<<15)|(contains(history,i)||k==14?1u<<16:0u)|(i==0x117?1u<<17:0u);
        put(e,4,flags);put(e,8,1u<<4); // property 36 = the character class; 15/17 are authored predicates
        put(world,d.world_core.entities+i*4,unsigned(reinterpret_cast<std::uintptr_t>(e)));
        auto& live=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(world)+d.world_core.live_bits+(i>>5)*4);live|=1u<<(i&31);
        entity_world::property_update(world,nullptr,token,reinterpret_cast<unsigned*>(e+4));
        const float x=k==14||i==0x114||contains(existing,i)?0.f:10000.f;
        put(e,0x20,x);put(e,0x24,0.f);put(e,0x28,0.f);
        const entity_grid::bounds box{{x-1,-1,-1},{x+1,1,1}};entity_grid::update(grid,nullptr,token,&box);
        if(already_clear&&k!=14)put(e,4,flags&~(1u<<16));
        if(already_clear&&k!=14){
            unsigned stale[4];std::memcpy(stale,entity_world::property_record(world,nullptr,i),16);
            stale[0]^=1u<<(is_newcomer?15:17); // stale cached predicates; authored object bits stay unchanged
            entity_world::property_update(world,nullptr,token,stale);
        }
    }
    const entity_grid::bounds old=is_newcomer?entity_grid::bounds{{9000,-1000,-1000},{11000,1000,1000}}:entity_grid::bounds{{-1000,-1000,-1000},{1000,1000,1000}};
    entity_grid::cache_update(grid,nullptr,0,&old);entity_grid::cache_update(grid,nullptr,1,&old);
    const auto hero_flags=*reinterpret_cast<unsigned*>(entities[14].data()+4);
    const std::vector<unsigned char> before(static_cast<unsigned char*>(grid),static_cast<unsigned char*>(grid)+GL.size);
    if(am){
        join_in_progress::canonical_npc_move_intent("ZF exact-pattern fixture epoch",true);
        join_in_progress::canonical_visibility_queries("ZF exact-pattern fixture epoch",true);
        for(unsigned k=0;k<14;++k){
            const unsigned i=0x114+k;auto* e=entities[k].data();
            assert((*reinterpret_cast<unsigned*>(e+4)&(1u<<16))==0);
            assert(std::memcmp(e+4,entity_world::property_record(world,nullptr,i),16)==0);
            assert((entity_world::property_members(world,nullptr,16)[i>>5]&(1u<<(i&31)))==0);
            assert((*reinterpret_cast<unsigned*>(e+4)&(1u<<15))==(i==0x114?0u:1u<<15));
            assert((*reinterpret_cast<unsigned*>(e+4)&(1u<<17))==(i==0x117?1u<<17:0u));
            for(unsigned flag:{15u,17u})assert(((entity_world::property_members(world,nullptr,flag)[i>>5]>>(i&31))&1u)==((*reinterpret_cast<unsigned*>(e+4)>>flag)&1u));
        }
        for(unsigned c=0;c<2;++c){
            const auto* cache=static_cast<unsigned char*>(grid)+GL.caches+c*GL.cache_stride;
            assert(cache[0]==0xff&&cache[1]==0&&cache[2]==0xff&&cache[3]==0);
            for(unsigned w=0;w<GL.set_words;++w)assert(entity_grid::cache_set_ptr(grid,nullptr,c)[w]==0);
        }
        // The epoch clear touched only cache extents/sets: physical cells, entity records and old bounds stay intact.
        for(unsigned off=0;off<GL.size;++off){
            bool clearable=false;for(unsigned c=0;c<2;++c){const auto base=GL.caches+c*GL.cache_stride;clearable|=(base<=off&&off<base+4)||(base+0x1c<=off&&off<base+GL.cache_stride);}
            if(!clearable)assert(before[off]==static_cast<unsigned char*>(grid)[off]);
        }
    }else for(unsigned k=0;k<14;++k){auto& flags=*reinterpret_cast<unsigned*>(entities[k].data()+4);flags&=~(1u<<16);} // AL: object only
    assert(*reinterpret_cast<unsigned*>(entities[14].data()+4)==hero_flags);
    query_manager=entity_query::create_manager(d);assert(query_manager);entity_query::set_manager_getter(&manager_get);
    const auto QL=entity_query::installed_layout();
    // Different stale scratch query bitmaps/order counts are not previous-result lists: acquire/fill overwrite them.
    for(unsigned q=0;q<3;++q){
        auto* p=static_cast<unsigned char*>(query_manager)+QL.m_queries+q*QL.q_stride;
        std::memset(p+QL.q_bitmap,is_newcomer?0x5a:0xa5,QL.set_bytes);
        put(static_cast<unsigned char*>(query_manager)+QL.m_orders+q*QL.o_stride,QL.o_count,is_newcomer?17u:31u);
    }
    activated.clear();deactivated.clear();
    put(reinterpret_cast<void*>(0x756138),0,is_newcomer?0xcafebabeu:0xdeadcafeu); // setInvisible's old scratch list is not an input
    const float matrix[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    reinterpret_cast<void(__thiscall*)(void*,const float*)>(0x466bf0)(nullptr,matrix); // actual frame-1 visibility code
    outcome result{activated,deactivated};
    const auto* start=static_cast<unsigned char*>(grid)+GL.caches;result.windows.assign(start,start+2*GL.cache_stride);
    const auto* bits=entity_world::property_members(world,nullptr,16);result.active_set.assign(bits,bits+GL.set_words);
    assert(*reinterpret_cast<unsigned*>(entities[14].data()+4)==hero_flags);
    assert(entity_query::QueryApi_004ab740()==3&&entity_query::free_order_count()==3);
    entity_query::destroy_manager(query_manager);query_manager=nullptr;entity_grid::destroy(grid);grid=nullptr;entity_world::destroy(world);world=nullptr;
    return result;
}
}
void run_visibility_pattern(){
    using namespace visibility_pattern;
    redirect(entity_capacity_layout::native_world_getter,world_get);redirect(entity_capacity_layout::native_grid_getter,grid_get);
    join_in_progress::world_getter_at=unsigned(reinterpret_cast<std::uintptr_t>(&world_get));
    join_in_progress::visibility_grid_getter_at=unsigned(reinterpret_cast<std::uintptr_t>(&grid_get));
    anchor_vtable[join_in_progress_rules::game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&party));
    put(reinterpret_cast<void*>(0x718448),0,0u);
    redirect(0x4ab670,entity_query::QueryApi_004ab670);redirect(0x4ab770,entity_query::QueryApi_004ab770);
    redirect(0x4ab720,entity_query::QueryApi_004ab720);redirect(0x4ab7d0,entity_query::QueryApi_004ab7d0);
    put(reinterpret_cast<void*>(join_in_progress_rules::actor_vtable),0x5c,unsigned(reinterpret_cast<std::uintptr_t>(&active)));
    put(reinterpret_cast<void*>(join_in_progress_rules::actor_vtable),0x7c,unsigned(reinterpret_cast<std::uintptr_t>(&unexpected_store)));
    redirect(0x45fef0,reinterpret_cast<const void*>(&unexpected_remove));
    const auto bind=[](unsigned iat,const void* fn){put(reinterpret_cast<void*>(iat),0,unsigned(reinterpret_cast<std::uintptr_t>(fn)));};
    bind(0x67fa60,reinterpret_cast<const void*>(&ctor));bind(0x67f9c8,reinterpret_cast<const void*>(&ctor3));
    bind(0x67f9d4,reinterpret_cast<const void*>(&row));bind(0x67fa2c,reinterpret_cast<const void*>(&assign));
    bind(0x67f9b4,reinterpret_cast<const void*>(&times_assign));bind(0x67f9c0,reinterpret_cast<const void*>(&minus_assign));
    bind(0x67f9f4,reinterpret_cast<const void*>(&plus_assign));bind(0x67f9f8,reinterpret_cast<const void*>(&times));
    bind(0x67fa0c,reinterpret_cast<const void*>(&minus));bind(0x67fa10,reinterpret_cast<const void*>(&plus));
    bind(0x67f8cc,reinterpret_cast<const void*>(&minimum));bind(0x67f8c8,reinterpret_cast<const void*>(&maximum));
    const auto a_old=run_peer(false,false),b_old=run_peer(true,false);
    assert(a_old.on.empty()&&(b_old.on==std::vector<unsigned>{0x116,0x11a,0x11f}));
    const auto a=run_peer(false,true),b=run_peer(true,true);
    assert(a.on==existing&&b.on==a.on&&a.off.empty()&&b.off.empty()&&a.windows==b.windows&&a.active_set==b.active_set);
    const auto a_stale=run_peer(false,true,true),b_stale=run_peer(true,true,true);
    assert(a_stale.on==a.on&&b_stale.on==a.on&&a_stale.windows==b_stale.windows&&a_stale.active_set==b_stale.active_set);
    entity_capacity::fixture_descriptor={};entity_query::set_manager_getter(nullptr);
    std::cout<<"PASS ZF visibility pattern: real retail 0x466bf0 on production 1024-slot owners reproduces AL peer-A none / peer-B 116,11a,11f; AM peers both activate 116,117,119,11a,11d,11f,120; equal grid windows and indexed flags; stale inactive records repaired; scratch query/order history ignored; authored 15/17 and party hero kept\n";
}
