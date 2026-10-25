// Execute our real installer, shared lookup and entry against invented native
// services in this test's own reserved image. No game process or retail code runs.
#include <Windows.h>
#include "xml1_rogue_decide.hpp"
#include "xml1_rogue_decide_rules.hpp"
#include "xml1_cling.hpp"
#include "entity_capacity.hpp"
#include "entity_query.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "game_version.hpp"
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace xml1_rogue_decide_rules;
using u32=std::uint32_t;
int failures=0, checks=0;
std::string rogue_key, cling_key;
bool refused=false;
bool raised=false;
void check(bool b,const char* what){++checks;if(!b){++failures;std::printf("FAIL %s\n",what);}}
template<class T> void put(void* p,u32 off,T value){std::memcpy(static_cast<unsigned char*>(p)+off,&value,sizeof value);}
template<class T> T get(const void* p,u32 off){T value{};std::memcpy(&value,static_cast<const unsigned char*>(p)+off,sizeof value);return value;}
void* ptr(u32 va){return reinterpret_cast<void*>(va);}
void signature(u32 va,std::string_view hex){for(std::size_t i=0;i<hex.size();i+=2){auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};put<unsigned char>(ptr(va),u32(i/2),static_cast<unsigned char>(digit(hex[i])*16+digit(hex[i+1])));}}
template<class T> void jump(u32 va,T target){put<unsigned char>(ptr(va),0,0xe9);put<u32>(ptr(va),1,u32(reinterpret_cast<std::uintptr_t>(target))-va-5);}
std::array<unsigned char,0x900> actor{}, victim{}, source{}, current{}, drain{}, no_target{}, style{}, query{}, session{}, ai{};
std::array<void*,80> actor_vt{}, source_vt{}, current_vt{}, drain_vt{}, style_vt{}, query_vt{}, session_vt{}, ai_vt{};
int firsts=0, releases=0, tags=0, starts=0, locks=0, feedbacks=0, sounds=0, may_calls=0, team_calls=0, excludes=0;
bool hit=true, character=true, target_valid=true, may=true, special_exists=true, neutral_excluded=false;
bool session_friendly=false;
int victim_team=30, free_queries=1;
void* started=nullptr;
void* __cdecl manager(){return query.data();}
int __cdecl free_count(){return free_queries;}
void** __fastcall acquire(void** out,void*,const void* token,const u32* excluded){check(token==ptr(0x70b83c)&&*excluded==0x1234,"actor query token and excluded handle");*out=query.data();return out;}
void __fastcall release(void** out,void*){++releases;check(*out==query.data(),"release acquired query");*out=nullptr;}
vec* __fastcall center(void* self,void*,vec* out){check(self==actor.data(),"bounds center on attacker");*out={5,8,10};return out;}
vec* __cdecl facing(vec* out,const void* angles){check(angles==actor.data()+0x2c,"facing uses angles +2c");*out={0.6f,0.8f,0};return out;}
void __fastcall filter(void* self,void*,int mode,int type){check(self==query.data()&&mode==1&&type==1,"virtual filter (1,1)");}
void __fastcall mode(void* self,void*,int value){check(self==query.data()&&value==0,"virtual sweep mode zero");}
void __fastcall sweep(void* self,void*,const vec* start,const vec* end,const vec* box){check(self==query.data()&&start->x==5&&start->y==8&&start->z==10&&end->x==41&&end->y==56&&end->z==10&&box->x==12&&box->y==12&&box->z==12,"virtual sweep center to 60 facing, three arguments, box12");}
void** __fastcall first(void* self,void*,void** out){check(self==query.data(),"first uses acquired query");++firsts;*out=hit?victim.data():nullptr;return out;}
void* __fastcall contact(void** self,void*){return *self;}
void* __cdecl cast(void* entity){return character?entity:nullptr;}
int __fastcall team(void* entity,void*){++team_calls;return entity==actor.data()?29:victim_team;}
void* __cdecl session_get(){return session.data();}
bool __fastcall friendly(void*,void*){return session_friendly;}
void* __cdecl ai_get(){return ai.data();}
bool __fastcall excluded(void*,void*,u32 id){++excludes;check(id==0x9876,"neutral AI id +7dc");return neutral_excluded;}
void* __fastcall owner(void* self,void*){check(self==source.data(),"owner from context source move");return style.data();}
const u32* __fastcall chain(void* self,void*,int action){check(self==current.data(),"chain from actor +3c0 current move");static u32 key;key=u32(action);return &key;}
void* __fastcall find(void* self,void*,const u32* key){check(self==style.data(),"find on source owner style");return *key==24?(special_exists?drain.data():nullptr):no_target.data();}
bool __fastcall valid(void* self,void*){check(self==victim.data(),"native validity tests victim");return target_valid;}
void __fastcall lock(void* self,void*,u32 handle){++locks;check(self==actor.data()&&handle==0x5678,"lock handle is by value");}
bool __fastcall may_start(void* self,void*,void* who){++may_calls;check(self==no_target.data()&&who==actor.data(),"mayStart only no-target");return may;}
void __fastcall feedback(void* self,void*){++feedbacks;check(self==actor.data(),"failure feedback actor");}
void __cdecl sound(){++sounds;}
bool __fastcall tag(void* self,void*,unsigned char which,void* a,void* b){++tags;check(self==source.data()&&which==1&&a==actor.data()&&b==a,"source dispatch tag1 actor twice");if(!may&&(!hit||!special_exists))check(feedbacks==1&&sounds==1,"feedback and sound precede tag1");return false;}
int __fastcall animation(void* self,void*){return self==drain.data()?101:202;}
std::array<unsigned char,0x80> context{};
void __fastcall start(void* self,void*,void* move,int reset){++starts;started=move;check(self==actor.data()&&reset==1&&tags==1&&get<int>(context.data(),0)==(move==drain.data()?101:202),"animation stored before start(actor,move,1), after tag");}
void bind_services(){
    jump(0x4ab550,&manager);jump(0x4ab740,&free_count);jump(0x4ab6d0,&acquire);jump(0x4ab720,&release);jump(0x4a9590,&contact);
    jump(0x560990,&facing);jump(0x402b60,&cast);jump(0x41a220,&team);jump(0x48de40,&session_get);jump(0x5252e0,&ai_get);
    jump(0x429210,&valid);jump(0x42d8e0,&lock);jump(0x421000,&feedback);jump(0x4fbe10,&sound);jump(0x42c340,&start);
    actor_vt[0x24/4]=reinterpret_cast<void*>(&center);source_vt[0xe0/4]=reinterpret_cast<void*>(&owner);source_vt[0x14/4]=reinterpret_cast<void*>(&tag);
    current_vt[0x118/4]=reinterpret_cast<void*>(&chain);style_vt[0x14/4]=reinterpret_cast<void*>(&find);
    drain_vt[0xc/4]=reinterpret_cast<void*>(&may_start);drain_vt[0xe8/4]=reinterpret_cast<void*>(&animation);
    query_vt[0x40/4]=reinterpret_cast<void*>(&filter);query_vt[0x80/4]=reinterpret_cast<void*>(&mode);query_vt[0x74/4]=reinterpret_cast<void*>(&sweep);query_vt[0x20/4]=reinterpret_cast<void*>(&first);
    session_vt[0x5c/4]=reinterpret_cast<void*>(&friendly);ai_vt[0x78/4]=reinterpret_cast<void*>(&excluded);
    put(actor.data(),0,actor_vt.data());put(source.data(),0,source_vt.data());put(current.data(),0,current_vt.data());put(style.data(),0,style_vt.data());
    put(drain.data(),0,drain_vt.data());put(no_target.data(),0,drain_vt.data());put(query.data(),0,query_vt.data());put(session.data(),0,session_vt.data());put(ai.data(),0,ai_vt.data());
    put(actor.data(),0x3c0,current.data());put(actor.data(),0x378,ptr(1));put(actor.data(),0x1c,0x1234u);put(victim.data(),0x1c,0x5678u);put(victim.data(),0x7dc,0x9876u);
    put(context.data(),4,source.data());
}
using entry_t=void(__fastcall*)(void*,void*,void*,void*,void*);
void run(void* handler){
    firsts=releases=tags=starts=locks=feedbacks=sounds=may_calls=team_calls=excludes=0;started=nullptr;
    put<unsigned char>(context.data(),0x6c,0x80);put<unsigned char>(context.data(),0x6d,0xa5);
    auto* slots=get<void**>(handler,0);reinterpret_cast<entry_t>(slots[1])(handler,nullptr,actor.data(),nullptr,context.data());
    check(tags==1,"tag1 exactly once per entry");check(firsts==(free_queries?1:0)&&releases==(free_queries?1:0),"one first result, one query release");
    check(get<unsigned char>(context.data(),0x6d)==0xa5,"flags do not overwrite adjacent context byte");
}
}
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t* key){if(std::wstring(key)==L"Xml1RogueDecide")return rogue_key;return cling_key;}
bool flag(const wchar_t*,const wchar_t*,bool fallback){return fallback;}
}
namespace logger {void write(const char*,...){} }
namespace game_version {void refuse_startup(){refused=true;} }
namespace entity_capacity {
const entity_capacity_layout::descriptor& installed(){static auto d=entity_capacity_layout::stock_descriptor;d.raised=raised;return d;}
u32 installed_target(u32 va){
    if(!raised)return 0;
    switch(va){
    case 0x4ab550:return u32(reinterpret_cast<std::uintptr_t>(&manager));
    case 0x4ab740:return u32(reinterpret_cast<std::uintptr_t>(&free_count));
    case 0x4ab6d0:return u32(reinterpret_cast<std::uintptr_t>(&acquire));
    case 0x4ab720:return u32(reinterpret_cast<std::uintptr_t>(&release));
    case 0x4a9590:return u32(reinterpret_cast<std::uintptr_t>(&contact));
    default:return 0;
    }
}
}
namespace entity_query {void* installed_manager(){return raised?query.data():nullptr;} }
int main(int argc,char** argv){
    DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"own executable fixture writable");
    for(const auto& g:xml1_cling_rules::guards)signature(g.va,g.hex);
    for(const auto& g:guards)signature(g.va,g.hex);
    for(const auto& g:query_guards)signature(g.va,g.hex);
    for(const auto& e:query_entries)signature(e.va,e.retail);
    std::vector<unsigned char> before(0x400000);std::memcpy(before.data(),ptr(0x401000),before.size());
    const auto game=reinterpret_cast<HMODULE>(0x400000);
    xml1_cling::install(game);xml1_rogue_decide::install(game);
    check(!xml1_cling::requested()&&!xml1_rogue_decide::requested()&&xml1_rogue_decide::verified(),"OFF neither feature requested");
    check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"OFF entire 4MB image byte identical");
    rogue_key="typo";xml1_rogue_decide::install(game);check(xml1_rogue_decide::requested()&&!xml1_rogue_decide::verified(),"invalid key unverified");
    check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"invalid key writes nothing");
    rogue_key="xml1";
    // Each native service or query guard mismatch must refuse before the hook.
    for(const auto& g:guards){const auto saved=get<unsigned char>(ptr(g.va),0);put<unsigned char>(ptr(g.va),0,saved^0xff);xml1_rogue_decide::install(game);check(!xml1_rogue_decide::installed(),"service mismatch refuses installation");put<unsigned char>(ptr(g.va),0,saved);}
    for(const auto& e:query_entries){const auto saved=get<unsigned char>(ptr(e.va),0);put<unsigned char>(ptr(e.va),0,saved^0xff);xml1_rogue_decide::install(game);check(!xml1_rogue_decide::installed(),"query bridge mismatch refuses installation");put<unsigned char>(ptr(e.va),0,saved);}
    for(const auto& g:query_guards){const auto saved=get<unsigned char>(ptr(g.va),0);put<unsigned char>(ptr(g.va),0,saved^0xff);xml1_rogue_decide::install(game);check(!xml1_rogue_decide::installed(),"query method mismatch refuses installation");put<unsigned char>(ptr(g.va),0,saved);}
    const bool both=argc>1&&std::string(argv[1])=="both";
    if(both){cling_key="xml1";xml1_cling::install(game);check(xml1_cling::installed(),"cling installed first");}
    if(argc>1&&std::string(argv[1])=="raised"){
        raised=true;
        jump(0x4ab550,&manager);jump(0x4ab740,&free_count);jump(0x4ab6d0,&acquire);jump(0x4ab720,&release);jump(0x4a9590,&contact);
        // Retail query bodies/vtable are intentionally unusable: the callback
        // must dispatch the acquired replacement object's virtual methods.
        for(const auto& g:query_guards)put<unsigned char>(ptr(g.va),0,0xcc);
        jump(0x4a9590,&first); // foreign jump must fail even in the raised layout
        xml1_rogue_decide::install(game);check(!xml1_rogue_decide::installed(),"foreign capacity accessor jump refused");
        jump(0x4a9590,&contact);
    }
    xml1_rogue_decide::install(game);check(xml1_rogue_decide::installed()&&!refused,"Rogue lookup installed");
    put<unsigned char>(ptr(0x4fd860),0,0xcc);check(!xml1_cling::register_lookup(game,nullptr),"shared lookup rejects a replaced hook");put<unsigned char>(ptr(0x4fd860),0,0xe9);
    auto lookup=reinterpret_cast<void*(__cdecl*)(const char*)>(0x4fd860);
    void* handler=lookup("CH_ROGUEDECIDE");check(handler!=nullptr,"shared lookup dispatches Rogue case-insensitively");
    if(!handler)return 1;
    check(get<void**>(handler,0)[6]==ptr(0x4fce50),"generic update retained in slot6");
    using destructor_t=void*(__fastcall*)(void*,void*,unsigned);
    check(reinterpret_cast<destructor_t>(get<void**>(handler,0)[0])(handler,nullptr,1)==handler,"persistent handler destructor never frees");
    // Let native fallback finish in our invented continuation, after displaced
    // push esi / mov esi,[esp+8]. No original retail implementation executes.
    const unsigned char fallback[]{0x33,0xc0,0x5e,0xc3};std::memcpy(ptr(0x4fd865),fallback,sizeof fallback);
    check((lookup("ch_clingwalldecide")!=nullptr)==both,"cling independently enabled");
    check(lookup("invented_handler")==nullptr,"unrecognized names use native continuation");
    bind_services();run(handler);check(started==drain.data()&&locks==1&&may_calls==0,"victim drain");
    hit=false;run(handler);check(started==no_target.data()&&locks==0&&may_calls==1,"no victim selects no-target");
    hit=true;character=false;run(handler);check(started==no_target.data(),"noncharacter first hit rejected without searching again");character=true;
    victim_team=29;run(handler);check(started==no_target.data(),"friendly first hit refused");
    put<unsigned char>(ptr(0x70b6fd),0,1);run(handler);check(started==drain.data(),"global friendly-hit override");put<unsigned char>(ptr(0x70b6fd),0,0);
    session_friendly=true;run(handler);check(started==drain.data(),"session friendly-hit override");session_friendly=false;
    victim_team=28;neutral_excluded=true;run(handler);check(started==no_target.data()&&excludes==1,"neutral excluded first hit");
    put<unsigned char>(actor.data(),0x3d8,1);run(handler);check(started==drain.data()&&team_calls==0&&excludes==0,"AI flag bypasses both filters");put<unsigned char>(actor.data(),0x3d8,0);
    victim_team=30;target_valid=false;run(handler);check(started==drain.data()&&locks==0,"invalid victim still drains without lock");target_valid=true;
    hit=false;may=false;run(handler);check(starts==0&&feedbacks==1&&sounds==1&&get<unsigned char>(context.data(),0x6c)==0x83,"failure flags feedback sound and no start");
    may=true;hit=true;special_exists=false;run(handler);check(started==no_target.data()&&locks==1,"missing Special falls through after lock");
    free_queries=0;run(handler);check(started==no_target.data(),"exhausted query pool avoids unsafe acquisition");
    std::printf("%s real Rogue installer/entry (%s): %d checks, %d failures\n",failures?"FAIL":"PASS",raised?"raised bridges":both?"both":"Rogue only",checks,failures);return failures?1:0;
}
