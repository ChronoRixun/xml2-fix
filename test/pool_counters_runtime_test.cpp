// Production D0/D1 callback on an owned retail-image fixture. No game entry point.
#include "pool_counters.cpp"
#include "limits_rules.hpp"
#include "model_pools_rules.hpp"
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <vector>
#include <map>
#include <new>
extern "C" unsigned char fixture_space[0x700000];
static bool track_allocations=false;
static unsigned allocations=0;
void* operator new(std::size_t n){if(track_allocations)++allocations;if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
namespace ini {
bool observe=false;
bool flag(const wchar_t*,const wchar_t* key,bool fallback){
    if(!std::wcscmp(key,L"PoolSampling"))return false;
    if(!std::wcscmp(key,L"PoolCounters"))return observe;
    return fallback;
}
}
namespace logger {void write(const char*,...) {}}
namespace game_version {void refuse_startup(){std::abort();}}
namespace limits {
pool_diagnostics diagnostic_sources(){
    const auto l=limits_rules::style_layout_for(19);
    return {{0x78a800,l.count,19,0,true,false},{0x78a800,l.auxiliary+0x16ee0,1064,0,true,false},
        {0x706440,0,900,0x706678,false,false},{0x78a800,l.styles,l.bitmap,0xec8,0xc60,19}};
}
}
namespace model_pools {
std::array<pool_counter_rules::source,10> diagnostic_sources(){
    using namespace model_pools_rules;
    const auto a=aggregate_layout_for(retail_capacities);
    std::array<pool_counter_rules::source,10> out{};
    for(unsigned i=0;i<9;++i)out[i]={0x7bf6d0+pool_start(a,static_cast<kind>(i))+(i==7?0x24:a.pool[i].live),0,static_cast<DWORD>(retail_capacities[i]),0x7dad68,false,false};
    out[9]={0x7dad78,a.psys.count,40,0x7dc868,false,true};return out;
}
}
// Limits wave 3 owners at stock (their rows absent): D0 must read exactly the generated retail readers.
namespace script_pools {const std::array<pool_layout,4>& descriptor(){return stock;}}
namespace fight_moves {fight_moves_layout::pool_descriptor descriptor(){return {};}}
namespace effect_pools {
namespace {const effect_pools_rules::descriptor stock_defs=effect_pools_rules::describe(pool::primitive_defs,816),stock_vectors=effect_pools_rules::describe(pool::vectors,710);}
const descriptor& layout(pool p){return p==pool::vectors?stock_vectors:stock_defs;}
std::uint32_t object_address(pool p){return p==pool::vectors?0x706680:0;}
}
namespace primitives_per_effect {unsigned capacity(){return 10;}}
namespace effect_pools {bool installed(pool){return false;}}
namespace particle_scratch {bool clamp_installed(){return false;}}
static void put(unsigned a,unsigned v){std::memcpy(reinterpret_cast<void*>(a),&v,4);}
int main(){
    unsigned checks=0,failures=0;
    const auto check=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}};
    DWORD old=0;check(VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old)!=0,"fixture writable");
    std::memset(fixture_space,0,sizeof fixture_space);
    std::memcpy(reinterpret_cast<void*>(0x4d6570),"\x8b\x91\x90\xc0\x00\x00\x33",7);
    std::vector<unsigned char> image(fixture_space,fixture_space+sizeof fixture_space);
    pool_counters::install(reinterpret_cast<HMODULE>(0x400000));
    check(!std::memcmp(image.data(),fixture_space,image.size()),"default-off observer install changes zero image bytes");
    using namespace pool_counters;
    check(counters.state.size==63,"44 shared counters and all 19 retail style move lists published");
    for(const auto& r:pool_counter_sites::readers) {
        const auto i=find(r.name);const auto& s=sources[i];
        check(i<counters.state.size && s.root==r.root && s.offset==r.offset && s.cap==r.cap && s.indirect==r.indirect && counters.state.pools[i].cap==r.cap,
              "stock W3 owner layouts equal the generated reader");
    }
    check(move_free.root==0x789e1c && move_free.offset==0x1aec8 && move_free.cap==332,"stock FightMoves free count");
    check(effect_primitives.root==0x6f3c3c && effect_primitives.records==0x4874 && effect_primitives.bitmap==0x4874+0x3e0c && effect_primitives.stride==0x44 &&
          effect_primitives.field==0x30 && effect_primitives.cap==220 && counters.state.pools[primitive_max].cap==10,"stock effect definition range");
    sampling=true;
    auto* script=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(script!=nullptr,"script fixture");put(0x787740,address(script));
    put(address(script)+0x15914,619);put(address(script)+0xc090,1555);put(address(script)+0x261a8,119);
    put(address(script)+0x26550,31);put(address(script)+0x26dd8,99);
    put(address(script)+0x3a07c,29);
    std::strcpy(reinterpret_cast<char*>(0x72a758),"invented/zone");
    put(0x6f3c50+0x17e0,300);put(0x6f8ee8+0x17e0,200);
    put(0x7dad68,1);put(0x7dc868,1);put(0x7dad78+0xa4,7);
    auto* style=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x30000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    const auto sl=limits_rules::style_layout_for(19);put(0x78a800,address(style));
    put(address(style)+sl.bitmap,1);put(address(style)+sl.styles+0xc60,149);
    put(address(style)+sl.styles+0xec8+0xc60,999); // a dead record must not affect the maximum
    auto* effect=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x9000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    put(0x6f3c3c,address(effect));put(address(effect)+0x4874+0x3e0c,1);
    put(address(effect)+0x4874+0x30,9);put(address(effect)+0x4874+0x44+0x30,77);
    image.assign(fixture_space,fixture_space+sizeof fixture_space);
    track_allocations=true;sample();track_allocations=false;
    check(allocations==0,"production frame sample allocates nothing");
    check(!std::memcmp(image.data(),fixture_space,image.size()),"production D0 sample changes zero image bytes");
    check(counters.state.active && counters.state.epoch==1,"zone observed");
    check(counters.state.pools[find("statements")].live==619,"script statements live");
    check(counters.state.pools[find("values")].live==1555,"script values live");
    check(counters.state.pools[find("objects")].live==119,"script objects live");
    check(counters.state.pools[find("contexts")].live==29 && counters.state.pools[find("contexts")].cap==30,"running contexts read from +3a07c with capacity 30");
    check(counters.state.pools[model_ids[9]].live==33,"particle free count inverted");
    check(counters.state.pools[style_max].live==149,"only active styles contribute move maximum");
    const auto slot0=find("style_moves_0"),slot1=find("style_moves_1");
    check(slot0<counters.state.size && counters.state.pools[slot0].live==149,"individual active style move list sampled");
    check(slot1<counters.state.size && counters.state.pools[slot1].live==-1,"inactive style move list remains unavailable");
    check(counters.state.pools[primitive_max].live==9,"only active effect definitions contribute primitive maximum");
    const auto fx=find("fxA");put(0x6f3c50+0x17e0,374);sample();
    put(0x6f3c50+0x17e0,2);sample();
    check(counters.state.pools[fx].live==2 && counters.state.pools[fx].peak_zone==374,"sample retains high water");
    const auto prim=find("prims");
    ++refusals[prim]; // refusal during the final simulation step of the old zone
    *reinterpret_cast<unsigned char*>(0x72a798)=1;
    track_allocations=true;sample();track_allocations=false;
    check(allocations==0 && !counters.state.active && head-tail==1,"unload queues fixed snapshot without allocations");
    check(completed[tail%completed.size()].pools[fx].peak_zone==374,"unload summary preserves zone high water");
    check(completed[tail%completed.size()].pools[prim].refused_zone==1,"final-step refusal belongs to unloaded zone");
    ++refusals[prim]; // requests during loading must survive until the new zone begins
    sample();
    *reinterpret_cast<unsigned char*>(0x72a798)=0;sample();
    check(counters.state.epoch==2 && counters.state.pools[fx].peak_zone==2 && counters.state.pools[fx].peak_session==374,"same-name reload resets zone, retains session");
    check(counters.state.pools[prim].refused==2 && counters.state.pools[prim].refused_zone==1,"loading refusal belongs to the destination zone without loss");
    put(0x787740,1);sample();check(counters.state.pools[find("statements")].live==-1,"invalid owner skipped");
    put(0x787740,address(script));
    DWORD protect=0;VirtualProtect(script,0x40000,PAGE_NOACCESS,&protect);sample();
    check(counters.state.pools[find("values")].live==-1,"inaccessible owner skipped");
    VirtualProtect(script,0x40000,protect,&protect);
    // D1-only operation counters are absent from the default catalog. Populate
    // their declared names as the opt-in installer will, then bind real callbacks.
    for(const auto& s:pool_counter_sites::observers)if(s.condition==1)counters.add(s.name,40);
    bind_observers();
    unsigned r[9]{};
    track_allocations=true;
    for(unsigned i=0;i<pool_counter_sites::observers.size();++i) {
        const std::string_view name=pool_counter_sites::observers[i].name;
        if(name=="fx_entries"){r[0]=0x6f8ee8;refused(i,r);check(refusals[find("fxB")]==1 && refusals[fx]==0,"FX owners counted independently");}
        if(name=="curves") {
            r[7]=0;r[8]=0;refused(i,r);check(refusals[find("curves")]==0,"preset zero is not a refusal");
            r[8]=0x40;refused(i,r);check(refusals[find("curves")]==1,"native full zero counted");
        }
        if(name=="particle_systems"){
            r[1]=4;refused(i,r);check(refusals[model_ids[9]]==0,"successful particle pop not refused");
            r[1]=0;refused(i,r);check(refusals[model_ids[9]]==1,"NULL particle pop counted");
        }
        if(name=="model_component"){
            r[1]=7;refused(i,r);check(refusals[model_ids[6]]==1,"native component index attributed to clouds");
        }
        if(name=="values") {r[7]=0;refused(i,r);}
        if(pool_counter_sites::observers[i].condition==1) {
            for(unsigned count:{39u,40u,41u,300u,2u}){r[7]=count;refused(i,r);}
        }
    }
    sample();
    track_allocations=false;
    check(allocations==0,"refusal/particle callbacks and event sampling allocate nothing");
    const auto text=json();
    check(text.find("\"statements\":{\"cap\":620,\"live\":619")!=std::string::npos,"pipe JSON uses sampled data");
    check(text.find("\"refusals_observed\":false")!=std::string::npos,"disabled observers disclosed");
    check(text.find("\"null_returns\":2,\"null_returns_zone\":2")!=std::string::npos,"value-pool NULL returns explicitly exposed");
    unsigned particle_ops=0;
    for(const auto& s:pool_counter_sites::observers)if(s.condition==1) {
        const auto id=find(s.name);
        check(counters.state.pools[id].peak_zone==300 && counters.state.pools[id].peak_session==300,"each particle operation retains the largest count between frames");
        check(counters.state.pools[id].refused==0,"particle excess is measured without pretending a native refusal");
        const auto start=text.find(std::string("\"")+s.name+"\":{");
        const auto object=text.substr(start,text.find('}',start)-start);
        check(object.find("\"exceeded\":2,\"exceeded_zone\":2")!=std::string::npos,"each particle operation counts 41/300 above scratch capacity 40");
        ++particle_ops;
    }
    check(particle_ops==11,"all eleven particle scratch operations covered");
    const auto particle_site=static_cast<unsigned>(std::find_if(pool_counter_sites::observers.begin(),pool_counter_sites::observers.end(),[](const auto& s){return s.condition==1;})-pool_counter_sites::observers.begin());
    const auto particle_id=find(pool_counter_sites::observers[particle_site].name);
    *reinterpret_cast<unsigned char*>(0x72a798)=1;sample();
    r[7]=41;refused(particle_site,r);sample(); // during destination loading
    *reinterpret_cast<unsigned char*>(0x72a798)=0;sample();
    check(counters.state.pools[particle_id].peak_zone==41 && counters.state.pools[particle_id].peak_session==300,"particle zone peak resets while preserving loading/session peaks");
    check(counters.state.pools[particle_id].exceeded_zone==1 && counters.state.pools[particle_id].exceeded==3,"particle excess totals survive zone loading");
    const auto idle_id=find("particle_array_578e10");
    check(counters.state.pools[idle_id].peak_zone==-1 && counters.state.pools[idle_id].exceeded_zone==0,"a new zone does not inherit stale particle-operation samples");
    *reinterpret_cast<unsigned char*>(0x72a798)=1;sample();
    r[7]=300;refused(particle_site,r);sample();
    *reinterpret_cast<unsigned char*>(0x72a798)=0;*reinterpret_cast<char*>(0x72a758)=0;sample(); // load aborted to menu
    std::strcpy(reinterpret_cast<char*>(0x72a758),"invented/destination");sample();
    check(counters.state.pools[particle_id].peak_zone==-1 && counters.state.pools[particle_id].exceeded_zone==0,"aborted load particle samples are not charged to a later zone");
    auto* moves_pool=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1b000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    put(0x789e1c,address(moves_pool));put(address(moves_pool)+0x1aef8,333);put(address(moves_pool)+0x1aec8,0xffffffff);sample();
    check(counters.state.pools[moves].corrupt,"unchecked FightMove aliasing exposed");
    std::printf("JSON=%s\n",json().c_str());
    // Every guarded byte must refuse before any game-image write. In particular,
    // a wrong byte in the last observer cannot leave the first one installed.
    ini::observe=true;
    for(const auto& s:pool_counter_sites::observers) {
        const auto bytes=patch_transaction::bytes_of(s.hex);
        std::memcpy(reinterpret_cast<void*>(s.va),bytes.data(),bytes.size());
    }
    image.assign(fixture_space,fixture_space+sizeof fixture_space);
    unsigned mutations=0;
    for(const auto& s:pool_counter_sites::observers)for(std::size_t k=0;k<s.hex.size()/2;++k) {
        auto* byte=reinterpret_cast<unsigned char*>(s.va+k);*byte^=1;
        install_observers();
        check(!observers,"mutated guard refuses observer transaction");
        *byte^=1;
        check(!std::memcmp(image.data(),fixture_space,image.size()),"guard refusal leaves all image bytes identical");
        ++mutations;
    }
    patch_transaction::hooks fault;
    unsigned flushes=0;
    fault.flush=[&](void*,std::size_t){return ++flushes!=1;};
    install_observers(&fault);
    check(!observers && !std::memcmp(image.data(),fixture_space,image.size()),"failed flush rolls back every observer byte");
    install_observers();
    check(observers,"all 49 observers publish together");
    std::vector<bool> expected(image.size(),false);
    for(const auto& s:pool_counter_sites::observers) {
        check(*reinterpret_cast<unsigned char*>(s.va)==(std::string_view(s.name)=="model_preflight"?0xe8:0xe9),"observer uses guarded branch/call span");
        for(std::size_t k=0;k<s.hex.size()/2;++k)expected[s.va-0x401000+k]=true;
    }
    bool outside=false;
    for(std::size_t k=0;k<image.size();++k)if(!expected[k] && image[k]!=fixture_space[k])outside=true;
    check(!outside,"D1 writes stay inside generated observer ledger");
    std::printf("guard mutations: %u\n",mutations);
    VirtualFree(script,0,MEM_RELEASE);VirtualFree(style,0,MEM_RELEASE);VirtualFree(effect,0,MEM_RELEASE);VirtualFree(moves_pool,0,MEM_RELEASE);
    std::printf("pool runtime: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
