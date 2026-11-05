// Execute the production Mold adapter with invented entities and native-service
// stand-ins. This process contains no retail image and never launches a game.
#include "../src/mold_controller.cpp"
#include <iostream>
#include <map>
#include <string>

// Synthetic space precedes executable functions so the adapter's fixed native
// service addresses can be replaced inside this fixture's own image. No game
// bytes are mapped. Keeping the ordinary image base also prevents Windows NLS
// mappings from taking those addresses before main runs.
#pragma section(".fixture",read,execute)
extern "C" __declspec(allocate(".fixture")) const unsigned char service_padding[0x400000]{};

namespace fixture {
using u32=std::uint32_t;
int checks=0,failures=0,moves=0,stops=0,plays=0;
float time=100;int turn=1;bool nodes=true,slots=true,hero_present=true,marker_present=false;
std::array<unsigned char,0x900> actor{},hero{},core{},point{},marker{};
std::array<unsigned char,0x600> brain{};
std::array<u32,100> vtable{};
std::map<u32,void*> entities;
constexpr u32 page_bases[]{0x410000u,0x420000u,0x450000u,0x460000u,0x4a0000u,0x500000u,0x520000u,0x530000u,0x560000u,0x700000u,0x720000u};
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
template<class T> void put(void* p,u32 offset,T value){std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof value);}
template<class F> void stub(u32 address,F callback){
    auto* code=reinterpret_cast<unsigned char*>(address);code[0]=0xe9;
    const auto delta=u32(reinterpret_cast<std::uintptr_t>(callback))-address-5;std::memcpy(code+1,&delta,4);
}
float __cdecl clock(){return time;}
void* __fastcall resolve(const u32* h,void*){const auto found=entities.find(*h);return found==entities.end()?nullptr:found->second;}
u32* __cdecl lookup(u32* h,const char* name){
    *h=std::strcmp(name,"mastermold")==0?41:std::strcmp(name,"Centre")==0?74:
        marker_present&&std::strcmp(name,"A1")==0?75:0;return h;
}
u32* __fastcall intern(u32* h,void*,const char*){*h=1;return h;}
void* __fastcall node(void*,void*,const u32*){return nodes?core.data():nullptr;}
void __fastcall play(void*,void*,void*,int){++plays;}
void __fastcall target(void*,void*,u32){}
void __fastcall wait(void*,void*,float){}
void __fastcall voice(void*,void*,float){}
void* __cdecl manager(){return nullptr;}
int __fastcall face(void*,void*,void*,void*,unsigned char,float){return turn;}
unsigned char __fastcall available(void*,void*,int){return slots;}
void __fastcall stop(void*,void*){++stops;}
void __fastcall movement(void*,void*,const float*,u32,float){++moves;}
unsigned char __fastcall movement_target(void*,void*,u32){return 1;}
float __cdecl random(unsigned char){return .5f;}
int __stdcall saved_bit(const char*,int bit){return bit==2;} // synthetic saved stage2 -> phase1

void setup(){
    time=100;turn=1;nodes=slots=hero_present=true;marker_present=false;moves=stops=plays=0;
    actor={};hero={};core={};point={};marker={};brain={};vtable={};
    entities={{41,actor.data()},{71,hero.data()},{72,core.data()},{74,point.data()},{75,marker.data()}};
    put(actor.data(),0,vtable.data());vtable[0x160/4]=0x424a90;
    put(actor.data(),0x1c,41u);put(actor.data(),0x27c,100.f);put(actor.data(),0x378,actor.data());
    put(hero.data(),0x1c,71u);put(hero.data(),0x27c,100.f);put(hero.data(),0x20,500.f);
    put(core.data(),0x27c,100.f);put(point.data(),0x20,1000.f);
    put(brain.data(),0x10,actor.data());
    mold_controller::enabled=true;mold_controller::trace=false;
    mold_controller::saved_stage={"fictional_stage",1,2};
    mold_controller::reset(41);mold_controller::boss.catwalk_latch=true;
}
void travel(float fps,bool due,bool available_slot){
    setup();slots=available_slot;
    if(!due)mold_controller::boss.attacks.value[0]=10000;
    unsigned attacks=0,recoveries=0;bool resumed=false;int moves_at_recovery=-1;
    for(unsigned frame=0;frame<unsigned(90*fps);++frame){time=100+frame/fps;u32 output[8]{};
        const auto previous=mold_controller::boss.travel.approach.started;
        mold_controller::tick(brain.data(),actor.data(),output);
        if(output[0]&0x80)++attacks;
        if(previous>=0&&mold_controller::boss.travel.approach.started<0){++recoveries;moves_at_recovery=moves;}
        else if(moves_at_recovery>=0&&moves>moves_at_recovery)resumed=true;
    }
    CHECK(recoveries>=5&&resumed&&moves>0&&stops>0);
    CHECK(due&&available_slot?attacks>=5:attacks==0);
}
void core_waits(float fps){
    for(int step:{1,2}){setup();nodes=false;mold_controller::boss.step=step;
        mold_controller::boss.target=72;mold_controller::boss.step_started=100;
        float abandoned=-1;
        for(unsigned frame=0;frame<unsigned(6*fps);++frame){time=100+frame/fps;u32 output[8]{};
            mold_controller::tick(brain.data(),actor.data(),output);
            if(!mold_controller::boss.step){abandoned=time-100;break;}}
        CHECK(abandoned>5&&abandoned<=5+1.01f/fps&&!mold_controller::boss.target&&mold_controller::boss.retry_at>100&&plays==0&&moves>0);
    }
    setup();turn=0;mold_controller::boss.step=1;mold_controller::boss.target=72;mold_controller::boss.step_started=100;
    u32 output[8]{};mold_controller::tick(brain.data(),actor.data(),output);CHECK(mold_controller::boss.step==2&&plays==0);
    time+=1/fps;mold_controller::tick(brain.data(),actor.data(),output);
    CHECK(mold_controller::boss.step==3&&plays==1&&mold_controller::boss.wait_until==time+4);
    time+=4+1/fps;mold_controller::tick(brain.data(),actor.data(),output);CHECK(!mold_controller::boss.step);
    setup();mold_controller::boss.step=1;mold_controller::boss.target=72;mold_controller::boss.step_started=100;
    entities.erase(72);mold_controller::tick(brain.data(),actor.data(),output);
    CHECK(!mold_controller::boss.step&&!mold_controller::boss.target&&moves>0);
}
void missing_waypoint(float fps){
    setup();entities.erase(74);float fired=-1;
    for(unsigned frame=0;frame<unsigned(16*fps);++frame){time=100+frame/fps;u32 output[8]{};
        mold_controller::tick(brain.data(),actor.data(),output);
        if(output[0]&0x80){fired=time-100;break;}}
    CHECK(fired>15&&fired<=15+2.02f/fps&&moves==0); // neither queried point nor Centre resolves
}
void marker_facing(float fps){
    setup();marker_present=true;slots=false;mold_controller::boss.phase=0;
    float fired=-1;
    for(unsigned frame=0;frame<unsigned(6*fps);++frame){time=100+frame/fps;turn=frame%2?1:-1;u32 output[8]{};
        mold_controller::tick(brain.data(),actor.data(),output);if(output[0]&0x10){fired=time-100;break;}}
    CHECK(fired>5&&fired<=5+1.01f/fps);
    setup();marker_present=true;slots=false;turn=0;mold_controller::boss.phase=0;u32 output[8]{};
    mold_controller::tick(brain.data(),actor.data(),output);CHECK(output[0]&0x10);
}
void rewind(float fps){
    setup();u32 output[8]{};
    time=1000+1/fps;mold_controller::brain_update(brain.data(),output);
    mold_controller::boss.step=1;mold_controller::boss.target=72;mold_controller::boss.step_started=time;
    mold_controller::boss.wait_until=time+4;
    mold_controller::boss.attacks.value[0]=1200;
    time=100;mold_controller::brain_update(brain.data(),output);
    CHECK(!mold_controller::boss.step&&!mold_controller::boss.target&&mold_controller::boss.wait_until==0);
    CHECK(mold_controller::boss.owner==41&&mold_controller::boss.phase==1&&mold_controller::boss.attacks.value[0]==110);
}
}
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t*){return {};}
bool flag(const wchar_t*,const wchar_t*,bool fallback){return fallback;}
}
namespace logger {void write(const char*,...){} void write_once(std::string_view,const char*,...){} }
namespace mold_beam {bool prepare(xml1_hook_transaction::patch&){return false;} }
namespace mold_effects {bool supported(){return true;}void reset(){}void update(void*){} }
namespace mold_query {
bool supported(){return true;}
bool is_a(void* p,std::uint32_t type){
    using namespace fixture;
    return type==0x70b83c?(p==actor.data()||p==hero.data()):type==0x753f64?(p==core.data()||p==marker.data()):type==0x778b3c&&p==point.data();
}
std::uint32_t select(void*,bool){return 0;}
std::uint32_t waypoint(void*){return 74;}
void* active_hero(std::uint32_t& h){h=fixture::hero_present?71:0;return h?fixture::hero.data():nullptr;}
}
int main(){
    using namespace fixture;
    DWORD protection[11]{};
    const auto begin=reinterpret_cast<std::uintptr_t>(service_padding),end=begin+sizeof service_padding;
    for(unsigned i=0;i<11;++i){CHECK(page_bases[i]>=begin&&page_bases[i]+0x10000<=end);
        if(page_bases[i]<begin||page_bases[i]+0x10000>end){std::cerr<<"fixture padding at "<<std::hex<<begin<<"\n";return 2;}
        if(!VirtualProtect(reinterpret_cast<void*>(page_bases[i]),0x10000,PAGE_EXECUTE_READWRITE,&protection[i]))return 2;
    }
    std::strcpy(reinterpret_cast<char*>(0x72a758),"mastermold/mastermold2");
    stub(0x45fe60,&fixture::clock);stub(0x4654b0,&resolve);stub(0x4a1700,&lookup);stub(0x41b120,&intern);
    stub(0x427ca0,&node);stub(0x42c340,&play);stub(0x42c7c0,&target);stub(0x538290,&target);
    stub(0x502580,&wait);stub(0x5279c0,&voice);stub(0x5252e0,&manager);stub(0x509ba0,&face);
    stub(0x529630,&available);stub(0x529690,&available);stub(0x423c30,&stop);stub(0x424a90,&movement);
    stub(0x4a7da0,&movement_target);stub(0x560190,&random);stub(0x4a0190,&saved_bit);
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    for(float fps:{30.f,60.f}){travel(fps,true,true);travel(fps,false,true);travel(fps,true,false);core_waits(fps);missing_waypoint(fps);marker_facing(fps);rewind(fps);}
    for(unsigned i=0;i<11;++i){DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(page_bases[i]),0x10000,protection[i],&ignored);}
    std::cout<<checks<<" Mold adapter checks, "<<failures<<" failures\n";return failures?1:0;
}
