#include <WinSock2.h>
#include "blob_rules.hpp"
#include "magneto_rules.hpp"
#include "boss_wave3_runtime.hpp"
#include "xml1_capability_rules.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
struct fixture {
    bool slots=true,node=true,aligned=true,visible_value=true,blocked=false,valid_prop=true,arrival=true,aimed=true;
    float dist=120,roll=.5f,waited=0,jumped=0,taunted=-1,tolerance=0;
    std::uint32_t bits=0,prop=71,carrying=71;int slot=0,stopped=0,approached=0,dropped=0,thrown=0,cleared=0,random_calls=0;
    bool alt=false;std::string played;
    bool available(int n,bool a=false){slot=n;alt=a;return slots;}
    bool play(const char* n){played=n;return node;}
    void buttons(std::uint32_t n){bits|=n;}
    void taunt(float p){taunted=p;}
    void wait(float t){waited=t;}
    void jump(float t){jumped=t;}
    float distance(){return dist;}
    bool visible(){return visible_value;}
    bool face(float t){tolerance=t;return aligned;}
    bool face_prop(std::uint32_t,float t){return face(t);}
    void stop(){++stopped;}
    bool target_blocked(){return blocked;}
    float random(){++random_calls;return roll;}
    std::uint32_t query(){return prop;}
    bool valid(std::uint32_t p){return p&&valid_prop;}
    void approach(std::uint32_t){++approached;}
    bool arrived(std::uint32_t){return arrival;}
    void clear_navigation_counter(){++cleared;}
    std::uint32_t carried(){return carrying;}
    void drop_other(){++dropped;}
    bool aim(){return aimed;}
    void throw_object(){++thrown;bits|=0x10;}
};
void magneto(){
    using namespace magneto_rules;
    state s;CHECK(!s.shield&&s.off&&!s.radial&&s.environment&&!s.floor&&s.smash&&!s.attack);
    CHECK(s.smash_interval==5&&s.attack_interval==6);
    s.smash_at=20;s.attack_at=22;s.shield=true;
    request(s,"mag2");CHECK(s.shield&&s.off&&s.radial&&s.environment&&!s.floor&&s.smash&&s.attack);
    CHECK(s.smash_at==20&&s.attack_at==22);
    request(s,"mag3");CHECK(s.shield&&s.off&&!s.radial&&s.environment&&!s.floor&&s.smash&&!s.attack&&s.smash_interval==4);
    s.off=false;request(s,"mag4");CHECK(s.shield&&!s.off&&s.radial&&!s.environment&&!s.floor&&s.smash&&!s.attack&&s.smash_interval==3);
    request(s,"magshield");request(s,"magshield");CHECK(s.smash_interval==12&&s.attack_interval==24&&!s.off);
    request(s,"mag5");CHECK(s.shield&&s.off&&s.radial&&s.environment&&s.floor&&s.smash&&s.attack&&s.smash_interval==2&&s.attack_interval==3);
    const auto old=s;request(s,"unrelated");CHECK(s.smash_at==old.smash_at&&s.off==old.off);
    fixture g;CHECK(pending(s,1,g)&&g.played=="balls_return"&&g.waited==.5f&&!s.shield&&s.off);
    CHECK(!hurt(s,1.49f)&&hurt(s,1.5f)&&!hurt(s,1.74f)&&hurt(s,1.75f));
    CHECK(pending(s,2,g)&&g.played=="balls_off"&&!s.off&&!s.shield_fx&&g.waited==1);
    CHECK(!hurt(s,3));CHECK(pending(s,3,g)&&g.bits==0xc0&&!s.radial&&g.taunted==1);
    s.shield=s.off=s.radial=true;g={};g.node=false;g.slots=false;
    CHECK(!pending(s,4,g)&&s.shield&&s.off&&s.radial);
    g.node=true;CHECK(pending(s,4,g)&&!s.shield&&s.off&&s.radial);
    s={};s.off=false;s.attack=true;g={};CHECK(powers(s,1,g)&&g.slot==6&&g.bits==0xa0&&s.smash_at==6);
    g={};CHECK(powers(s,1,g)&&g.slot==5&&g.bits==0x90&&s.attack_at==7);
    g={};CHECK(!powers(s,6,g)&&!g.bits);CHECK(powers(s,6.01f,g)&&s.smash_at==11.01f);
    s={};g={};g.slots=false;CHECK(!powers(s,9,g)&&s.smash_at==0);
    g.slots=true;g.visible_value=false;CHECK(!powers(s,9,g));g.visible_value=true;g.aligned=false;CHECK(!powers(s,9,g));
    CHECK(!release_tile(true,99,100)&&!release_tile(true,100,100)&&release_tile(true,100.01f,100)&&release_tile(false,0,100));
    for(unsigned i=0;i<s.tiles.size();++i)s.tiles[i]={i+1,10};CHECK(tile_slot(s,3)==2&&tile_slot(s,99)==-1);
    state other;CHECK(other.tiles[0].handle==0&&other.smash_at==0); // owner states never alias
    std::array<bool,5> valid{};s={};s.floor=true;
    auto p=plan_environment(s,0,77,true,true,valid);CHECK(p.kind==environmental::none); // strict deadline
    p=plan_environment(s,1,77,true,true,valid);CHECK(p.kind==environmental::general&&p.deadline==6&&p.wait==1);
    CHECK(plan_environment(s,1,77,true,false,valid).kind==environmental::none);
    p=plan_environment(s,1,77,false,false,valid);CHECK(p.kind==environmental::floor&&p.slot==0&&p.deadline==1.3f&&p.wait==.1f);
    for(unsigned i=0;i<5;++i){s.tiles[i]={i+10,10};valid[i]=true;}
    p=plan_environment(s,1,12,false,false,valid);CHECK(p.kind==environmental::tracked&&p.deadline==1.2f&&!p.wait);
    p=plan_environment(s,1,99,false,false,valid);CHECK(p.kind==environmental::full&&p.deadline==1.1f&&!p.wait);
    valid[3]=false;p=plan_environment(s,1,99,false,false,valid);CHECK(p.kind==environmental::floor&&p.slot==3);
    s.floor=false;CHECK(plan_environment(s,1,99,false,false,valid).kind==environmental::none);
    s.environment=false;CHECK(plan_environment(s,1,99,true,true,valid).kind==environmental::none);
}
void blob(){
    using namespace blob_rules;
    for(unsigned op:{3u,4u,5u,6u,7u}){
        state s;fixture g;g.slots=false;update(s,op,10,g);CHECK(s.pc==0&&!g.bits&&!g.waited&&!g.jumped);
        g.slots=true;update(s,op,10,g);CHECK(s.pc==1&&g.waited==.5f&&s.advanced_at==10&&s.scratch==0);
        CHECK(g.slot==int(op==3?5:op==4?7:op==6?8:6));CHECK(g.taunted==(op==7?-1:.4f));
    }
    for(float d:{169.99f,170.f,170.01f}){state s;fixture g;g.dist=d;update(s,7,5,g);
        CHECK(g.bits==(d<170?0xa0u:0x90u)&&g.jumped==(d<170?5.6f:5.3f));}
    for(unsigned op:{2u,8u,9u,10u,14u,15u,0u,99u}){
        state s;s.pc=5;s.scratch=9;fixture g;update(s,op,12,g);
        CHECK(s.pc==(op==15?0u:6u));
        if(op==8||op==9)CHECK(s.until==12+(op==8?10:5));
        if(op==10)CHECK(s.hero_index==1);
    }
    {state s;s.until=12;fixture g;update(s,14,12,g);CHECK(!s.pc);update(s,14,12.01f,g);CHECK(s.pc==1);}
    {state s;s.until=12;fixture g;g.dist=300;update(s,1,12,g);CHECK(!s.pc&&!g.bits);update(s,1,12.01f,g);CHECK(s.pc==1);}
    {state s;s.until=100;fixture g;g.dist=100;update(s,1,1,g);CHECK(!s.close&&!g.stopped);
        g.dist=99.99f;update(s,1,1,g);CHECK(s.close&&g.bits==0x10&&g.tolerance==.97f);
        g={};g.dist=139.99f;update(s,1,1,g);CHECK(s.close&&g.bits==0x10);
        g={};g.dist=140;update(s,1,1,g);CHECK(s.close&&!g.bits);
        g.dist=140.01f;update(s,1,1,g);CHECK(!s.close);
        g.dist=110;update(s,1,1,g);CHECK(!s.close&&!g.bits);
        g.dist=90;g.blocked=true;update(s,1,1,g);CHECK(s.melee_at==3.1f&&!g.bits);
        g.blocked=false;update(s,1,3.1f,g);CHECK(!g.bits);g.roll=.1f;update(s,1,3.11f,g);CHECK(g.bits==0x20);}
    for(bool found:{false,true}){state s;fixture g;g.prop=found?71:0;update(s,11,2,g);
        CHECK(s.pc==1&&s.prop==g.prop&&g.waited==.2f&&g.approached==int(found));}
    {state s;s.prop=71;fixture g;g.arrival=false;update(s,12,3,g);CHECK(!s.pc&&g.approached==1);
        g.arrival=true;g.aligned=false;update(s,12,3,g);CHECK(!s.pc);
        g.aligned=true;g.slots=false;update(s,12,3,g);CHECK(!s.pc);
        g.slots=true;update(s,12,3,g);CHECK(s.pc==1&&g.bits==0x100&&g.alt&&g.slot==3&&g.taunted==1&&g.waited==.5f&&g.cleared==1);}
    {state s;s.prop=71;fixture g;g.valid_prop=false;update(s,12,3,g);CHECK(s.pc==1&&!g.bits);}
    {state s;s.prop=71;fixture g;g.aimed=false;update(s,13,4,g);CHECK(!s.pc&&!g.thrown);
        g.aimed=true;g.slots=false;update(s,13,4,g);CHECK(!s.pc);
        g.slots=true;update(s,13,4,g);CHECK(s.pc==1&&g.thrown==1&&g.alt&&g.slot==1&&g.waited==.2f);}
    {state s;s.prop=71;fixture g;g.carrying=72;update(s,13,4,g);CHECK(!s.pc&&g.dropped==1);
        g.carrying=0;update(s,13,4,g);CHECK(s.pc==1);}
    {state s;s.prop=71;fixture g;g.valid_prop=false;update(s,13,4,g);CHECK(s.pc==1);}
    std::array<prop,3> props{{{1,0,6,2,true},{2,0,0,4,true},{3,0,0,4,true}}};
    CHECK(select(props)==2);props[1].flags=1u<<17;CHECK(select(props)==3);props[2].liftable=false;CHECK(select(props)==1);
    props[0].dz=6.01f;CHECK(!select(props));
    props[0].dz=-6;CHECK(select(props)==1);props[0].dz=-6.01f;CHECK(!select(props));
    CHECK(heaviness(4,3)==4&&heaviness(5,3)==5&&heaviness(4,2)==2&&heaviness({},2)==3);
    weight_store<4> weights;CHECK(!weights.get(1,101));weights.set(1,101,4);CHECK(weights.get(1,101)==4);
    weights.set(1,201,5);CHECK(!weights.get(1,101)&&weights.get(1,201)==5); // new generation at same slot
    weights.set(1,201,-1);CHECK(!weights.get(1,201));weights.set(10,101,4);CHECK(!weights.get(10,101));
    jump j{1,2,10};CHECK(!jump_due(j,10)&&jump_due(j,10.01f)&&!jump_stale(j,11)&&jump_stale(j,11.01f));
    CHECK(indexed_target(true,399.99f)&&!indexed_target(true,400)&&!indexed_target(true,400.01f)&&!indexed_target(false,1));
    CHECK(jump_candidate(true,true,499.99f)&&!jump_candidate(true,true,500)&&!jump_candidate(true,true,500.01f));
    CHECK(!jump_candidate(true,false,1)&&!jump_candidate(false,true,1));
    for(bool mode:{false,true}){state s;const unsigned size=mode?13:19;
        for(unsigned i=0;i<size;++i){s.pc=i;CHECK(opcode(s,mode)==(mode?astral[i]:nyc[i]));}CHECK(opcode(s,mode)==15);}
}
void agreement(){using namespace xml1_capability_rules;
    CHECK(boss_bit("magneto")==64&&boss_bit("blob")==128);
    const auto all=bosses(true,255,255,1);CHECK(all.verified&&all.values[0]==255&&all.values[2]==1&&all.values[3]==0);
    CHECK(bosses(true,192,192,0).verified);CHECK(!bosses(true,192,64,0).verified);
    CHECK(bosses(true,64,64,0).values[1]!=bosses(true,128,128,0).values[1]);
    using boss_owner_rules::reset_required;
    CHECK(!reset_required(1,2,3,10,1,2,3,10));
    CHECK(!reset_required(1,2,3,10,1,0,3,11)); // request callback has no brain pointer
    CHECK(reset_required(1,2,3,10,2,2,3,11));
    CHECK(reset_required(1,2,3,10,1,4,3,11));
    CHECK(reset_required(1,2,3,10,1,2,4,11)); // same handle/brain reused in another allowed zone
    CHECK(reset_required(1,2,3,10,1,2,3,9.99f));
}
void original_programs(const char* path){
    std::ifstream f(path,std::ios::binary);std::vector<unsigned char>d((std::istreambuf_iterator<char>(f)),{});
    auto read32=[&](std::size_t o){std::uint32_t v=0;if(o+4<=d.size())std::memcpy(&v,d.data()+o,4);else CHECK(false);return v;};
    CHECK(d.size()>0x124);if(d.size()<0x124)return;
    const auto base=read32(0x104),n=read32(0x11c),table=read32(0x120)-base;
    auto offset=[&](unsigned va)->std::size_t{for(unsigned i=0;i<n;++i){const auto o=table+i*56,v=read32(o+4),size=read32(o+16);if(va>=v&&va-v<size)return read32(o+12)+va-v;}return d.size();};
    for(unsigned i=0;i<30;++i){CHECK(read32(offset(0x4532f8)+4*i)==(i<blob_rules::nyc.size()?blob_rules::nyc[i]:0));
        CHECK(read32(offset(0x453370)+4*i)==(i<blob_rules::astral.size()?blob_rules::astral[i]:0));}
    std::cout<<"60 original program DWORDs checked\n";
}
void retail(const char* path){
    std::ifstream f(path,std::ios::binary);std::vector<unsigned char>d((std::istreambuf_iterator<char>(f)),{});
    auto read32=[&](std::size_t o){std::uint32_t v=0;if(o+4<=d.size())std::memcpy(&v,d.data()+o,4);return v;};
    const auto pe=read32(0x3c);CHECK(pe+24<d.size());if(pe+24>=d.size())return;
    const unsigned count=d[pe+6]|(d[pe+7]<<8),opt=d[pe+20]|(d[pe+21]<<8);
    auto file=[&](unsigned va)->const unsigned char*{for(unsigned i=0;i<count;++i){const unsigned o=pe+24+opt+i*40,base=read32(o+12)+0x400000,n=read32(o+16),raw=read32(o+20);if(va>=base&&va-base+16<=n&&raw+va-base+16<=d.size())return d.data()+raw+va-base;}return nullptr;};
    for(const auto& g:boss_wave3_runtime::guards){auto* p=file(g.va);CHECK(p&&limits_rules::matches(p,g.hex));}
    for(const auto& g:std::array<limits_rules::guard,3>{{{0x54f900,"83ec54558be9","hurt entry"},{0x4989a8,"8b17682c906800","heaviness observation"},{0x5173f3,"8b460883f840","sole dispatcher"}}}){
        auto* p=file(g.va);CHECK(p&&limits_rules::matches(p,g.hex));}
    std::cout<<"28 wave-three retail guards checked\n";
}
}
int main(int argc,char** argv){magneto();blob();agreement();if(argc>1)retail(argv[1]);if(argc>2)original_programs(argv[2]);std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
