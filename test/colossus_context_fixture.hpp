#pragma once
#include <array>
#include <cmath>

// Retail tag lookup, event dispatch, punch builder and talent evaluators.
// The terminal area query/HP sink and talent registry are invented services.
namespace colossus_context_fixture {
using namespace handler_fixture;
struct vec{float x,y,z;};
inline std::array<unsigned char,0x900> caster{},victim{},caster_stats{},victim_stats{},move{},event{},info{},style{},combat{},talents{},camera{},type{};
inline std::array<void*,128> caster_vt{},victim_vt{},move_vt{},event_vt{},style_vt{},combat_vt{},talent_vt{},camera_vt{};
inline u32 key=1;inline vec zero{},normal{1,0,0};inline void* evaluated_stats=nullptr;
inline float damage=0,range=0;inline short knockback=0;inline int volumes=0;
inline void* __fastcall kind(void*,void*){return type.data();}
inline void* __cdecl actor_cast(void* p){return p==caster.data()||p==victim.data()?p:nullptr;}
inline void* __cdecl physical_cast(void* p){return p==victim.data()?p:nullptr;}
inline void* __fastcall none(void*,void*){return nullptr;}
inline void* __fastcall get_style(void*,void*){return style.data();}
inline const u32* __fastcall get_key(void*,void*){return &key;}
inline int __fastcall style_mode(void*,void*){return 11;}
inline void* __fastcall event_at(void*,void*,int index){return index==0?event.data():nullptr;}
inline void __fastcall pre_event(void*,void*,void*,void*){}
inline bool __fastcall modifiers(void*,void*,int,void*,int){return true;}
inline bool __cdecl no_mode(){return false;}
inline void* __cdecl get_combat(){return combat.data();}
inline void* __cdecl get_talents(){return talents.data();}
inline void* __cdecl get_camera(){return camera.data();}
inline void __fastcall shake(void*,void*,float,float){}
inline const vec* __fastcall contact_normal(void*,void*){return &normal;}
inline vec* __fastcall copy(vec* a,void*,const vec* b){*a=*b;return a;}
inline float __fastcall talent_range(void*,void*,void* stats,u32* id,float* high,int mode){
 check(mode==1&&(*id==7||*id==8||*id==9),"native punch resolves damage/range/knockback talent references");evaluated_stats=stats;
 const float lo=stats==caster_stats.data()?(*id==7?9.f:*id==8?40.f:190.f):0.f;
 if(high)*high=stats==caster_stats.data()&&*id==7?11.f:lo;return lo;
}
inline float __cdecl choose_low(float lo,float hi,int){check(hi>=lo,"native damage range keeps evaluated bounds");return lo;}
inline bool __fastcall volume(void*,void*,void* actor,void* attack,float arc,float max_range,int,int,u32,u32,int,int,int,float){
 check(actor==caster.data()&&arc<0.f,"native tag/punch keeps caster and parsed180-degree arc");
 ++volumes;damage=field<float>(attack,4);range=max_range;knockback=field<short>(attack,8);
 if(max_range>=20.f)field<float>(victim.data(),0x27c)-=damage;
 return true;
}
inline void setup(){
 for(auto* p:{&caster,&victim,&caster_stats,&victim_stats,&move,&event,&info,&style,&combat,&talents,&camera,&type})p->fill(0);
 caster_vt.fill(nullptr);victim_vt.fill(nullptr);move_vt.fill(nullptr);event_vt.fill(nullptr);style_vt.fill(nullptr);combat_vt.fill(nullptr);talent_vt.fill(nullptr);camera_vt.fill(nullptr);
 field<void*>(caster.data(),0)=caster_vt.data();field<void*>(victim.data(),0)=victim_vt.data();caster_vt[0]=victim_vt[0]=reinterpret_cast<void*>(&kind);
 field<void*>(move.data(),0)=move_vt.data();field<void*>(event.data(),0)=event_vt.data();field<void*>(style.data(),0)=style_vt.data();
 field<void*>(combat.data(),0)=combat_vt.data();field<void*>(talents.data(),0)=talent_vt.data();field<void*>(camera.data(),0)=camera_vt.data();
 field<void*>(caster.data(),0x35c)=caster_stats.data();field<void*>(victim.data(),0x35c)=victim_stats.data();field<void*>(caster.data(),0x378)=move.data();field<u32>(caster.data(),0x1c)=111;
 field<unsigned char>(caster.data(),0x3d8)=1; // renderer-free branch; context stats path is identical
 field<float>(victim.data(),0x27c)=100;field<vec>(caster.data(),0x120)={320,0,0};
 field<u32>(at(0x70b840),0)=0;field<u32>(at(0x753f68),0)=1;field<u32>(at(0x72a4d8),0)=2;field<u32>(type.data(),0x18)=0x30;field<unsigned char>(at(0x70b6fd),0)=0;
 field<int>(move.data(),0x11c)=1;move_vt[0x14/4]=reinterpret_cast<void*>(0x4f6450);move_vt[0x1c/4]=reinterpret_cast<void*>(0x4f5b20);move_vt[0x18/4]=reinterpret_cast<void*>(&event_at);
 move_vt[0xe0/4]=reinterpret_cast<void*>(&get_style);move_vt[0xd0/4]=move_vt[0xcc/4]=reinterpret_cast<void*>(&get_key);move_vt[0x114/4]=move_vt[0x110/4]=reinterpret_cast<void*>(&none);
 style_vt[4/4]=reinterpret_cast<void*>(&style_mode);style_vt[0x18/4]=reinterpret_cast<void*>(&none);style_vt[0x1c/4]=reinterpret_cast<void*>(&get_key);
 field<void*>(event.data(),4)=move.data();field<void*>(event.data(),0x14)=info.data();field<unsigned char>(event.data(),0xd)=1;
 event_vt[4/4]=reinterpret_cast<void*>(0x4dcbd0);event_vt[0x2c/4]=reinterpret_cast<void*>(0x4dbd70);
 field<unsigned short>(info.data(),0)=0x8007;field<unsigned short>(info.data(),4)=0x8009;field<unsigned short>(info.data(),0x24)=0x8008;
 field<u32>(info.data(),8)=1;field<u32>(info.data(),0xc)=0x80;field<unsigned char>(info.data(),0x1c)=10;field<float>(info.data(),0x3c)=-1;
 combat_vt[0x30/4]=reinterpret_cast<void*>(&volume);combat_vt[0x5c/4]=reinterpret_cast<void*>(&none);talent_vt[4/4]=reinterpret_cast<void*>(&talent_range);camera_vt[0x98/4]=reinterpret_cast<void*>(&shake);
 volumes=0;damage=range=0;knockback=0;evaluated_stats=nullptr;
}
inline void run(const image& pc,const void* numeric_truncate){
 for(auto [a,n]:std::array<std::pair<u32,unsigned>,11>{{{0x4f6450,0x30},{0x4f5b20,0x40},{0x4f5e80,0x130},{0x4dcbd0,0xc0},{0x4dc3e0,0x160},{0x4dc230,0x1b0},{0x4dbd70,0x130},{0x4b39b0,0xa0},{0x4dad20,0x50},{0x453a00,0x60},{0x4297f0,0xf0}}})pc.load(a,n);
 pc.load(0x4539a0,0x60);pc.load(0x4dbc70,0x50);pc.load(0x6909ac,4);pc.load(0x680514,4);
 field<void*>(at(0x67f9e0),0)=field<void*>(at(0x67fa2c),0)=reinterpret_cast<void*>(&copy);field<void*>(at(0x67fa54),0)=&zero;
 jump(0x67217c,numeric_truncate);jump(0x5601b0,&choose_low);jump(0x4daa30,&pre_event);jump(0x54f900,&modifiers);jump(0x4c87a0,&no_mode);jump(0x4c1dd0,&get_talents);jump(0x41d0d0,&none);
 jump(0x450030,&get_combat);jump(0x48de40,&get_combat);jump(0x4457e0,&get_camera);jump(0x402b60,&actor_cast);jump(0x429110,&physical_cast);jump(0x4a95c0,&none);jump(0x4a9610,&contact_normal);
 for(bool correct:{false,true}){
  setup();shared_handler_c_bridges::dispatch(caster.data(),1,correct?caster.data():victim.data());
  std::printf("native charge context %s: damage %.0f range %.0f knockback %d HP %.0f\n",correct?"caster":"soldier",damage,range,knockback,field<float>(victim.data(),0x27c));
  check(volumes==1&&damage==(correct?9.f:0.f)&&range==(correct?40.f:0.f)&&knockback==(correct?190:0),"retail tag/punch evaluates converted talent values from second actor's stats");
 }
 setup();shared_handler_c_bridges::colossus_contact(nullptr,nullptr,caster.data(),victim.data(),nullptr);
 std::printf("native charge collision: damage %.0f range %.0f knockback %d HP %.0f\n",damage,range,knockback,field<float>(victim.data(),0x27c));
 check(volumes==1&&damage==9.f&&range==40.f&&knockback==190&&field<float>(victim.data(),0x27c)==91.f,"Colossus native tagged punch produces caster damage/range/knockback and eligible area hit");
 check(evaluated_stats==caster_stats.data(),"Colossus collision supplies caster evaluation context rather than soldier stats");
}
}
