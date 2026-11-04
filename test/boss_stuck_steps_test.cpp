#include "blob_rules.hpp"
#include "dopple_rules.hpp"
#include "magneto_rules.hpp"
#include "mold_combat_rules.hpp"
#include "shadowking_rules.hpp"
#include "avalanche_rules.hpp"
#include "pyro_rules.hpp"
#include "mystique_rules.hpp"
#include <iostream>
#include <string>

namespace {
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
struct game {
    bool slots=false,aligned=false,valid_prop=true,arrival=false,aimed=false;
    float dist=200;unsigned bits=0,stops=0,throws=0;
    bool available(int,bool=false){return slots;}
    bool play(const char*){return slots;}
    bool visible(){return true;}
    bool face(float){return aligned;}
    bool face_prop(unsigned,float){return aligned;}
    void buttons(unsigned b){bits|=b;}
    void taunt(float){} void wait(float){} void jump(float){}
    float distance(){return dist;} void stop(){++stops;}
    bool target_blocked(){return false;} float random(){return .5f;}
    unsigned query(){return 71;} bool valid(unsigned){return valid_prop;}
    void approach(unsigned){} bool arrived(unsigned){return arrival;}
    void clear_navigation_counter(){} unsigned carried(){return 0;}
    void drop_other(){} bool aim(){return aimed;} void throw_object(){++throws;}
};
void blob_waits(float fps){
    // A permanently unavailable attack must relinquish its opcode. No hits or damage in this fixture.
    for(unsigned op:{3u,4u,5u,6u,7u}){
        blob_rules::state s;s.advanced_at=100;game g;float escaped=-1;
        for(unsigned frame=0;frame<=unsigned(6*fps);++frame){
            const float now=100+frame/fps;blob_rules::update(s,op,now,g);
            if(s.pc){escaped=now-100;break;}
        }
        CHECK(s.pc==1&&escaped>5&&escaped<=5+1.01f/fps&&!g.bits);
        s={};s.advanced_at=100;g.slots=true;blob_rules::update(s,op,100.25f,g);
        CHECK(s.pc==1&&g.bits&&s.wait_until==100.75f); // normal success stays immediate
    }
    // One rejected attack cannot prevent the following (available) attack in the NYC program.
    blob_rules::state s;s.pc=3;s.advanced_at=100;game g;unsigned attacks=0;
    for(unsigned frame=0;frame<=unsigned(30*fps);++frame){const float now=100+frame/fps;
        if(now<s.wait_until)continue;
        const unsigned op=blob_rules::opcode(s,false);g.slots=op!=3;g.bits=0;
        blob_rules::update(s,op,now,g);if(g.bits&0x80)++attacks;
    }
    CHECK(attacks>0);
}
void dopple_waits(float fps){
    {dopple_rules::shared s;s.ticked=true;s.last_tick=1000;s.fastball_started=990;s.rearm_at=995;
        s.wolverine=s.colossus=s.jean=s.cyclops=2;
        CHECK(dopple_rules::tick(s,100,true,200));
        CHECK(s.fastball_started==100&&s.rearm_at==105&&s.wolverine==1&&s.colossus==1&&s.jean==0&&s.cyclops==0);
    }
    for(auto who:{dopple_rules::who::jean,dopple_rules::who::cyclops}){
        for(int state:{1,2,3}){
            if((who==dopple_rules::who::jean&&state==1)||(who==dopple_rules::who::cyclops&&state==3))continue;
            dopple_rules::shared s;
            (who==dopple_rules::who::jean?s.jean:s.cyclops)=state;
            dopple_rules::view v;v.partner_valid=true;v.now=100;v.hero=false;v.slot_available=false;
            CHECK(dopple_rules::update(s,who,v).handled);
            float escaped=-1;
            for(unsigned frame=1;frame<=unsigned(6*fps);++frame){v.now=100+frame/fps;
                const auto a=dopple_rules::update(s,who,v);
                if(!a.handled){escaped=v.now-100;break;}
            }
            CHECK(escaped>5&&escaped<=5+1.01f/fps);
            CHECK((who==dopple_rules::who::jean?s.jean:s.cyclops)==0);
        }
    }
    // Existing fastball watchdog bounds unreachable partners and an unavailable throw, at both rates.
    for(int state:{1,2}){dopple_rules::shared s;s.wolverine=s.colossus=state;s.fastball_started=100;
        dopple_rules::view v;v.partner_valid=true;v.partner_distance=200;
        bool escaped=false;
        for(unsigned frame=0;frame<=unsigned(11*fps);++frame){v.now=100+frame/fps;
            if(!dopple_rules::update(s,dopple_rules::who::colossus,v).handled){escaped=true;break;}}
        CHECK(escaped&&s.colossus==0&&s.wolverine==0);
    }
    {dopple_rules::shared s;unsigned native=0,run=0,longest=0;
        for(unsigned frame=0;frame<unsigned(90*fps);++frame){const float now=100+frame/fps;
            dopple_rules::tick(s,now,true,200);dopple_rules::view v;v.now=now;v.partner_valid=true;v.partner_distance=200;
            const auto w=dopple_rules::update(s,dopple_rules::who::wolverine,v);
            const auto c=dopple_rules::update(s,dopple_rules::who::colossus,v);
            if(!w.handled&&!c.handled){++native;++run;if(run>longest)longest=run;}else run=0;
        }
        CHECK(native>unsigned(20*fps)&&longest>=unsigned(4.9f*fps)); // timeout must not immediately rearm
    }
    {dopple_rules::shared s;dopple_rules::view v;v.now=100;v.partner_valid=true;v.hero=true;
        auto a=dopple_rules::update(s,dopple_rules::who::jean,v);CHECK(a.handled&&s.jean==3);
        v.now+=1/fps;a=dopple_rules::update(s,dopple_rules::who::jean,v);
        CHECK(a.activate_hazard&&s.jean==0&&a.wait==1);}
    {dopple_rules::shared s;s.cyclops=1;dopple_rules::view v;v.now=100;v.partner_valid=true;v.slot_available=true;
        const auto a=dopple_rules::update(s,dopple_rules::who::cyclops,v);CHECK(a.buttons==0x90&&s.cyclops==0&&a.wait==.4f);}
}
void magneto_waits(float fps){
    magneto_rules::state s;game g;float fired=-1;
    for(unsigned frame=0;frame<=unsigned(6*fps);++frame){const float now=100+frame/fps;g.slots=true;
        if(magneto_rules::powers(s,now,g)){fired=now-100;break;}}
    CHECK(fired>5&&fired<=5+1.01f/fps&&g.bits==0xa0);
    s={};g={};g.slots=g.aligned=true;CHECK(magneto_rules::powers(s,100,g)&&g.bits==0xa0);
    // A tile whose native motion never ends and whose hero never retreats must still release its slot.
    float released=-1;
    for(unsigned frame=0;frame<=unsigned(11*fps);++frame){const float now=100+frame/fps;
        if(magneto_rules::release_tile(true,90,100,100,now)){released=now-100;break;}}
    CHECK(released>10&&released<=10+1.01f/fps);
    CHECK(magneto_rules::release_tile(false,90,100,100,100.1f));
    CHECK(magneto_rules::release_tile(true,101,100,100,100.1f));
}
struct core_state {int step=0;unsigned target=0;float step_started=0,retry_at=0,shot_until=0;};
struct core_game {
    bool living=true,aligned=false,node=false;unsigned fires=0;float waited=0;
    bool valid(unsigned){return living;} bool face(unsigned){return aligned;}
    bool node_available(){return node;} void fire(unsigned){++fires;} void wait(float t){waited=t;}
};
void mold_waits(float fps){
    for(int step:{1,2}){core_state s;s.step=step;s.target=71;s.step_started=100;core_game g;float escaped=-1;
        for(unsigned frame=0;frame<=unsigned(6*fps);++frame){const float now=100+frame/fps;
            if(!mold_combat_rules::update_core(s,now,g)){escaped=now-100;break;}}
        CHECK(escaped>5&&escaped<=5+1.01f/fps&&s.step==0&&s.target==0&&s.retry_at>100&&!g.fires);
    }
    core_state s;core_game g;g.aligned=g.node=true;
    mold_combat_rules::begin_core(s,71,100);
    CHECK(mold_combat_rules::update_core(s,100.2f,g)&&s.step==2&&!g.fires);
    CHECK(mold_combat_rules::update_core(s,100.3f,g)&&s.step==3&&g.fires==1&&g.waited==4&&s.shot_until==104.3f&&s.retry_at==109.3f);
    mold_combat_rules::begin_core(s,71,200);g.living=false;
    CHECK(!mold_combat_rules::update_core(s,200.1f,g)&&s.step==0&&!s.target&&g.fires==1);
    // Run the production waypoint decision across failed travel, failed facing,
    // and another movement attempt. Cooldown/slot failures must still rearm travel.
    for(bool can_fire:{false,true}){mold_combat_rules::travel_state travel;float attempted=-1;unsigned attempts=0;
        for(unsigned frame=0;frame<unsigned(16*fps);++frame){const float now=100+frame/fps;
            const auto a=mold_combat_rules::waypoint(travel,now,false,[]{return false;});
            if(a==mold_combat_rules::travel_action::attack){attempted=now-100;++attempts;
                CHECK(mold_combat_rules::waypoint(travel,now+1/fps,false,[]{return false;})==mold_combat_rules::travel_action::move);
                if(can_fire)++g.fires;
                break;
            }
        }
        CHECK(attempts==1&&attempted>15&&attempted<=15+2.02f/fps);
    }
    mold_combat_rules::travel_state travel;
    CHECK(mold_combat_rules::waypoint(travel,100,true,[]{return true;})==mold_combat_rules::travel_action::attack);
}
void waiting_windows(float fps){
    // Used by Mold's marker/waypoint facing and travel, and Magneto's power facing.
    // Same paused timestamp can be sampled any number of times without expiring.
    for(float limit:{5.f,10.f}){boss_wait_rules::window w;
        CHECK(!w.ready(100,false,limit));
        for(unsigned frame=0;frame<unsigned(90*fps);++frame)if(w.ready(100,false,limit)){CHECK(false);break;}
        CHECK(!w.ready(100+limit,false,limit));
        CHECK(w.ready(100+limit+1/fps,false,limit));
        CHECK(w.ready(200,true,limit)&&w.started<0); // normal arrival/facing resets the attempt
        CHECK(!w.ready(201,false,limit)&&w.started==201);
    }
}
void shadowking_waits(float fps){
    auto s=shadowking_rules::initial(90,0);s.xtreme=true;float fired=-1;
    for(unsigned frame=0;frame<=unsigned(6*fps);++frame){const float now=100+frame/fps;
        const auto a=shadowking_rules::update(s,now,200,[](int slot){return slot==5;});
        if(a.buttons){fired=now-100;CHECK(a.buttons==0x90&&!s.xtreme);break;}}
    CHECK(fired>5&&fired<=5+1.01f/fps);
    s=shadowking_rules::initial(90,0);s.xtreme=true;
    const auto a=shadowking_rules::update(s,100,200,[](int){return true;});
    CHECK(a.buttons==0xc0&&!s.xtreme&&a.wait==.3f); // successful xtreme retains priority this tick
}
void avalanche_waits(float fps){
    avalanche_rules::state s;avalanche_rules::view v;v.health=v.health_max=100;v.nearest=50;v.slot_available=false;
    float melee=-1;
    for(unsigned frame=0;frame<=unsigned(6*fps);++frame){v.now=100+frame/fps;
        const auto a=avalanche_rules::update(s,v);if(a.roll_smash){melee=v.now-100;break;}}
    CHECK(melee>5&&melee<=5+1.01f/fps&&s.close_at>100);
    s={};v.now=100;v.slot_available=true;
    const auto a=avalanche_rules::update(s,v);CHECK(a.buttons==0xa0&&!a.roll_smash&&s.close_at==104);
}
void passive_patterns(float fps){
    // Fixed living targets, no incoming hit or damage callback: deadlines alone
    // must keep the reactive brains selecting attacks for ninety game seconds.
    for(bool up:{false,true}){pyro_rules::state s;s.fire_up=up;unsigned attacks=0;
        for(unsigned frame=0;frame<unsigned(90*fps);++frame)
            if(pyro_rules::update(s,100+frame/fps,150,true,true).buttons)++attacks;
        CHECK(attacks>=(up?29u:17u)&&attacks<=(up?30u:18u));
    }
    for(int mode:{0,1}){auto s=shadowking_rules::initial(100,mode);unsigned attacks=0;
        for(unsigned frame=0;frame<unsigned(90*fps);++frame)
            if(shadowking_rules::update(s,100+frame/fps,150,[](int){return true;}).buttons)++attacks;
        CHECK(attacks>=13&&attacks<=18);
    }
    {avalanche_rules::state s;avalanche_rules::view v;v.nearest=150;v.health=v.health_max=100;v.slot_available=true;unsigned attacks=0;
        for(unsigned frame=0;frame<unsigned(90*fps);++frame){v.now=100+frame/fps;if(avalanche_rules::update(s,v).buttons)++attacks;}
        CHECK(attacks>=22&&attacks<=23);
    }
    {mystique_rules::state s;mystique_rules::request(s,"myst2",100);unsigned grenades=0;
        for(unsigned frame=0;frame<unsigned(90*fps);++frame){const float now=100+frame/fps;
            if(mystique_rules::grenade_due(s,now)){++grenades;s.grenade_at=now+4;}}
        CHECK(grenades>=21&&grenades<=22);
        mystique_rules::copy(s,200,10,71);CHECK(mystique_rules::restore_due(s,200,false));
        CHECK(!mystique_rules::restore_due(s,209.9f,true)&&mystique_rules::restore_due(s,210,true));
    }
}
}
int main(){for(float fps:{30.f,60.f}){blob_waits(fps);dopple_waits(fps);magneto_waits(fps);mold_waits(fps);waiting_windows(fps);shadowking_waits(fps);avalanche_waits(fps);passive_patterns(fps);}
    std::cout<<checks<<" stuck-step checks, "<<failures<<" failures\n";return failures?1:0;}
