#include "shared_handler_fixture.hpp"
#include "shared_handlers_c.hpp"
#include "shared_handler_c_rules.hpp"
#include "shared_handler_c_bridges.hpp"
#include "tk_guard_bridges.hpp"
#include "colossus_context_fixture.hpp"
#include "gameplay_capabilities.hpp"
#include <optional>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <float.h>
using namespace handler_fixture;
constexpr u32 bias=0x10000000;
extern "C" unsigned char fixture_space[0x400000];
namespace ini {
 std::array<std::optional<std::string>,11> settings{};
 std::optional<std::string> text(const wchar_t*,const wchar_t* key) {for(unsigned i=0;i<settings.size();++i)if(!std::wcscmp(key,shared_handler_c_rules::keys[i]))return settings[i];return {};}
}
namespace logger {void write(const char*,...) {}}
namespace entity_capacity {
 struct ownership {u32 site=0,target=0;};std::array<ownership,16> owners{};
 u32 installed_target(u32 site){for(const auto& owner:owners)if(owner.site==site)return owner.target;return 0;}
}
namespace game_version {bool refused=false;void refuse_startup(){refused=true;}}
namespace xml1_grab_carry {bool available=true;bool installed(){return available;}}
namespace lift_rule {int heaviness(void* object){return handler_fixture::field<unsigned char>(object,0x31d);}}
namespace {
using shared_handler_bridges::entry;using shared_handler_bridges::input;
using shared_handler_c_bridges::contact_callback;
void seed(const image& pc){for(const auto& g:shared_handler_c_rules::guards)pc.load(g.bytes.va,g.bytes.hex.size()/2);}
void keys(u32 mask,const char* v="xml1"){for(unsigned i=0;i<ini::settings.size();++i)ini::settings[i]=(mask&(1u<<i))?std::optional<std::string>(v):std::nullopt;}
void load_x1(const image& x1,u32 a,std::size_t n) {
 x1.load(a,n,bias);
 constexpr u32 references[]{0x3d64e0,0x3c97d0,0x3ca9dc,0x3c6e40,0x485878,0x4c0a60,0x4997ec,0x3d6438,0x3d643c,0x3d6430,0x3d642c,0x450e48,0x5bc9d0,0x3c6de4,0x3cb1f0,0x498d90,0x3c8244,0x3d6664,0x4c2aa0,0x4c2aa4};
 for(std::size_t i=0;i+4<=n;++i) {
  u32 v=field<u32>(at(a+bias),static_cast<unsigned>(i));
  if(std::find(std::begin(references),std::end(references),v)!=std::end(references)) {field<u32>(at(a+bias),static_cast<unsigned>(i))=v+bias;i+=3;}
 }
}
std::array<unsigned char,0x900> actor{},original_actor{},target{},stats{},move{},handler{},ev{},contact{},type{};
std::array<void*,160> actor_vt{},move_vt{},handler_vt{},event_vt{},manager_vt{};
void** manager=manager_vt.data();
bool original=false;float clock_now=100.f,energy=100.f,cost=10.f,damage_after=5.f;
bool event_present=true,default_result=false,is_level=false;u32 hit_kinds=0;
void* selected=move.data();void* recipient=nullptr;std::vector<int> calls;
float observed_velocity[3]{};int action_id=-99;
bool shifted_contact=false,contact_phase=false;std::array<float,6> effect_vectors{};
void* __fastcall type_info(void*,void*) {return type.data();}
void* __cdecl actor_cast(void* p){return p&&(hit_kinds&4)?p:nullptr;}
void* __cdecl damageable_cast(void* p){return p&&(hit_kinds&1)?p:nullptr;}
float __cdecl clock_value(){return clock_now;}
float __fastcall energy_value(void*,void*){calls.push_back(10);return energy;}
float __fastcall event_cost(void*,void*){calls.push_back(11);return cost;}
void* __fastcall get_event(void*,void*,u32 id){check(id==100,"Colossus reads event 100 from current move");calls.push_back(12);return event_present?ev.data():nullptr;}
void* __fastcall action(void*,void*,u32 id){const int logical=original&&id==0x1a?0x1b:static_cast<int>(id);action_id=logical;calls.push_back(20+logical);return selected;}
void __fastcall exit_handler(void*,void*,void*){calls.push_back(30);}
bool __fastcall fallback(void*,void*,void*,void*){calls.push_back(31);return default_result;}
void __fastcall dispatch(void*,void*,u32 id,void* a,void* b) {
 check(id==1&&a==(original?original_actor.data():actor.data()),"contact event ID and source actor");recipient=b;calls.push_back(40);
 field<float>(target.data(),original?0x240:0x27c)=damage_after;
}
void __fastcall velocity(void*,void*,const float* v){std::copy_n(v,3,observed_velocity);calls.push_back(41);}
bool __fastcall level(void*,void*){return is_level;}
const float* __fastcall position(void*,void*){return reinterpret_cast<float*>(contact.data()+(shifted_contact&&contact_phase?0x60:0x20));}
const float* __fastcall rebound(void*,void*){return reinterpret_cast<float*>(contact.data()+0x10);}
const float* __fastcall normal(void*,void*){return reinterpret_cast<float*>(contact.data()+(shifted_contact&&contact_phase?0x70:0x30));}
void* __cdecl get_manager(){return &manager;}
void __fastcall camera(void*,void*,float a,float b){check(a==3.f&&b==.5f,"charge camera impulse");calls.push_back(42);}
void __fastcall full_play(void*,void*,void* m,int priority,float speed,int a,int b,int c){check(m==selected&&priority==-1&&speed==-1.f&&!a&&!b&&!c,"charge source full move dispatch");calls.push_back(43);contact_phase=true;}
u32 __fastcall x1_effect(void*,void*,int id,int priority,const float* p,const float* n,int zero){std::copy_n(p,3,effect_vectors.begin());std::copy_n(n,3,effect_vectors.begin()+3);check(id==16&&priority==-1&&!zero,"source crash effect arguments");calls.push_back(44);return 123;}
u32 __fastcall pc_effect(void*,void*,int id,int priority,const float* p,const float* n,int zero,int extra){std::copy_n(p,3,effect_vectors.begin());std::copy_n(n,3,effect_vectors.begin()+3);check(id==15&&priority==-1&&!zero&&!extra,"mapped crash effect arguments");calls.push_back(44);return 123;}
void __fastcall attach_effect(void*,void*,u32 id){check(id==123,"charge attaches crash effect");calls.push_back(45);}
bool native_material_proof=false;
void* __fastcall impact(void*,void*){calls.push_back(46);return ev.data();}
void __fastcall material_type(void* self,void*,int kind){check(kind==16,"native material accessor selects default type");field<unsigned char>(self,0)=16;calls.push_back(46);}
void __fastcall apply_impact(void* self,void*,int flags,const float* at_position,float strength,u32 material){
 check(flags==0&&at_position==reinterpret_cast<float*>(contact.data()+(shifted_contact&&contact_phase?0x60:0x20))&&strength==1.f&&material==77,"charge contact sound receives four mapped source arguments");
 if(native_material_proof)check(self==at(original?bias+0x4c2aa0:0x756b08),"charge sound consumes actual native material accessor result");calls.push_back(47);
}
void reset() {
 actor.fill(0);original_actor.fill(0);target.fill(0);stats.fill(0);move.fill(0);handler.fill(0);ev.fill(0);contact.fill(0);type.fill(0);calls.clear();recipient=nullptr;contact_phase=false;effect_vectors.fill(0.f);
 actor_vt[0]=reinterpret_cast<void*>(&type_info);actor_vt[0x80/4]=actor_vt[0x84/4]=reinterpret_cast<void*>(&attach_effect);
 move_vt[0x14/4]=reinterpret_cast<void*>(&dispatch);move_vt[0x1c/4]=reinterpret_cast<void*>(&get_event);
 handler_vt[3]=reinterpret_cast<void*>(&exit_handler);event_vt[8]=reinterpret_cast<void*>(&event_cost);
 for(auto* a:{actor.data(),original_actor.data()}) {field<void*>(a,0)=actor_vt.data();field<void*>(a,0x378)=move.data();field<void*>(a,0x2f4)=move.data();field<void*>(a,0x35c)=stats.data();field<void*>(a,0x2d8)=stats.data();}
 field<void*>(target.data(),0)=actor_vt.data();field<void*>(target.data(),0x35c)=stats.data();field<void*>(target.data(),0x2d8)=stats.data();
 field<void*>(move.data(),0)=move_vt.data();field<void*>(handler.data(),0)=handler_vt.data();field<void*>(ev.data(),0)=event_vt.data();
 field<float>(target.data(),0x240)=field<float>(target.data(),0x27c)=10.f;
 hit_kinds=0;is_level=false;energy=100;cost=10;event_present=true;default_result=false;selected=move.data();action_id=-99;
 std::fill_n(observed_velocity,3,999.f);
}
void bind() {
 jump(0x402b60,&actor_cast);jump(0x429110,&damageable_cast);jump(0x45fe60,&clock_value);jump(bias+0x66ab0,&clock_value);
 jump(0x41d5d0,&energy_value);jump(bias+0x2e8c0,&energy_value);jump(0x427c80,&action);jump(bias+0x381f0,&action);
 jump(0x4fce50,&fallback);jump(bias+0xe7460,&fallback);jump(0x473640,&velocity);jump(bias+0x760d0,&velocity);
 jump(0x4a95c0,&level);jump(bias+0xa07b0,&level);jump(0x4a95f0,&position);jump(bias+0xa07e0,&position);
 jump(0x4a9600,&rebound);jump(bias+0xa07f0,&rebound);jump(0x4a9610,&normal);jump(bias+0xa0800,&normal);
 jump(0x4457e0,&get_manager);jump(bias+0x51100,&get_manager);jump(0x450030,&get_manager);jump(bias+0x59ce0,&get_manager);
 jump(0x432030,&full_play);jump(bias+0x40df0,&full_play);jump(0x4a97a0,&impact);jump(bias+0xa09d0,&impact);jump(0x487970,&apply_impact);jump(bias+0x838c0,&apply_impact);
 manager_vt[0x90/4]=manager_vt[0x98/4]=reinterpret_cast<void*>(&camera);manager_vt[0x10/4]=reinterpret_cast<void*>(&x1_effect);manager_vt[0x14/4]=reinterpret_cast<void*>(&pc_effect);
 field<u32>(at(0x6db5b0),0)=field<u32>(at(bias+0x450e48),0)=77;
 field<u32>(at(0x753f68),0)=field<u32>(at(bias+0x4c0a60),0)=0;
 field<u32>(at(0x72a4d8),0)=field<u32>(at(bias+0x4997ec),0)=1;
 field<u32>(at(0x70b840),0)=field<u32>(at(bias+0x485878),0)=2;
}
void classify(bool source) {field<u32>(type.data(),0x18)=hit_kinds<<(source?1:4);}
// Signed truncation helper for the owner-loaded XML1 producer, retaining its
// x87 calling convention and two-register return without altering host flags.
__declspec(naked) void truncate_x87() {__asm {
 sub esp,16
 fnstcw [esp+8]
 mov ax,[esp+8]
 or ax,0xc00
 mov [esp+10],ax
 fldcw [esp+10]
 fistp qword ptr [esp]
 fldcw [esp+8]
 mov eax,[esp]
 mov edx,[esp+4]
 add esp,16
 ret
}}
float lookup_value=15.f;int lookup_calls=0,reader_row=0;std::array<void*,4> values_vt{};void** values_manager=values_vt.data();
void* __cdecl get_values(){return &values_manager;}
float __fastcall source_value(void*,void*,const char* name,void* maximum){check(!std::strcmp(name,"L3")&&!maximum,"source asks for L3 minimum without randomization");++lookup_calls;return lookup_value;}
void* __fastcall reader_ctor(void* p,void*){return p;}
bool __fastcall reader_load(void*,void*,const char* path,int a,int b,int c){check(!std::strcmp(path,"data/values.xmlb")&&a==1&&b==3&&!c,"PC lookup uses native source values resource");return true;}
void* __fastcall root_iterator(void* p,void*,void*,const char* name){check(!std::strcmp(name,"values"),"values root selected");field<int>(p,0)=10;return p;}
void* __fastcall child_iterator(void* p,void*,void*,const char* name){check(!std::strcmp(name,"value"),"value rows selected");reader_row=0;field<int>(p,0)=20;return p;}
bool __fastcall iterator_valid(void* p,void*){return field<int>(p,0)==10||reader_row<2;}
const char* __fastcall string_attribute(void*,void*,const char* name,const char*){check(!std::strcmp(name,"name"),"value lookup compares names");return reader_row?"L3":"DMG3";}
float __fastcall float_attribute(void*,void*,const char* name,float zero){check(!std::strcmp(name,"min")&&zero==0.f,"PC source lookup reads MIN only");++lookup_calls;return lookup_value;}
void __fastcall iterator_next(void*,void*,int zero){check(!zero,"value iterator advances with native flag");++reader_row;}
void __fastcall reader_destroy(void*,void*){}
u32 damage_site=0;void* damage_actor=nullptr;float timing=0;
__declspec(naked) void damage_done(){__asm {
 add esp,0x100
 popad
 ret
}}
__declspec(naked) void drive_damage(){__asm {
 pushad
 sub esp,0x100
 mov esi,damage_actor
 mov eax,timing
 mov [esp+8],eax
 mov [esp+0x18],eax
 jmp dword ptr [damage_site]
}}
void damage_tests(const image& x1) {
 load_x1(x1,0xdd5d0,0x66);load_x1(x1,0xdd82d,0x1f);
 for(auto a:{0x3c97d0u,0x3ca9dcu,0x3c6e40u})x1.load(a,4,bias);
 x1.load(0x3d64e0,3,bias);jump(bias+0xdd636,&damage_done);jump(bias+0xdd84c,&damage_done);
 jump(0x4ee9ef,&damage_done);jump(0x4eebb9,&damage_done);jump(bias+0xb8550,&get_values);jump(bias+0x1a41f0,&truncate_x87);
 values_vt[2]=reinterpret_cast<void*>(&source_value);
 jump(0x564a80,&reader_ctor);jump(0x564eb0,&reader_load);jump(0x564b00,&root_iterator);jump(0x564df0,&child_iterator);jump(0x564b60,&iterator_valid);jump(0x564b70,&string_attribute);jump(0x564bf0,&float_attribute);jump(0x564e70,&iterator_next);jump(0x564d00,&reader_destroy);
 for(float base:{15.f,15.9f,-15.9f,32768.f,65535.f,2147483648.f,-2147483648.f})for(float t:{-.1f,0.f,.19999999f,.2f,.20000002f,.5f,.99999f,1.f,1.1f,100000.f}) {
  reset();lookup_value=base;timing=t;lookup_calls=0;
  damage_site=bias+0xdd5d0;damage_actor=original_actor.data();drive_damage();const auto expected=field<short>(original_actor.data(),0x294);
  shared_handler_c_bridges::source_loaded=false;damage_site=0x4ee99b;damage_actor=actor.data();drive_damage();
  check(field<float>(actor.data(),0x2b8)==static_cast<float>(expected)&&lookup_calls==2,"jump source lookup and all signed integer stages match owner code");
  shared_handler_c_bridges::source_loaded=false;lookup_calls=0;damage_site=bias+0xdd82d;damage_actor=original_actor.data();drive_damage();
  damage_site=0x4eeb88;damage_actor=actor.data();drive_damage();
  check(field<float>(actor.data(),0x2b8)==static_cast<float>(field<short>(original_actor.data(),0x294))&&lookup_calls==2,"fly source minimum and signed narrowing match owner code");
 }
}
struct native_xml_node {
 const char* tag=nullptr;native_xml_node* next=nullptr;native_xml_node* child=nullptr;int attribute_count=0;
 struct attribute {const char* name;const char* value;} attributes[3]{};
};
struct native_xml_header {u32 prefix[2]{};native_xml_node root;} value_tree;
native_xml_node dmg_row,l3_row;int native_load_calls=0;
bool native_load_success=true;
bool __fastcall native_value_file(void* root,void*,const char* path,int a,int b,int c) {
 check(!std::strcmp(path,"data/values.xmlb")&&a==1&&b==3&&!c,"native values file request preserves engine flags");++native_load_calls;
 if(!native_load_success)return false;
 field<void*>(root,0)=&value_tree;field<unsigned char>(root,8)=1; // synthetic external/non-owning XML tree
 return true;
}
int __cdecl compare_names(const char* a,const char* b){return _stricmp(a,b);}
double __cdecl parse_number(const char* value){return std::strtod(value,nullptr);}
void native_value_reader_tests(const image& pc,const image& x1) {
 // Only file provision and CRT imports are synthetic here. Execute the actual
 // owner's constructor, root/child iterators, binary attribute search, float
 // getter, sibling advancement and destructor, through the production reader.
 pc.load(0x5649c0,0x4e5);jump(0x564eb0,&native_value_file);jump(0x672562,&compare_names);jump(0x672210,&parse_number);
 value_tree.root.tag="values";value_tree.root.child=&dmg_row;
 dmg_row.tag=l3_row.tag="value";dmg_row.next=&l3_row;dmg_row.attribute_count=l3_row.attribute_count=3;
 dmg_row.attributes[0]={"max","5"};dmg_row.attributes[1]={"min","3"};dmg_row.attributes[2]={"name","DMG3"};
 l3_row.attributes[0]={"max","9999"};l3_row.attributes[2]={"name","L3"};
 for(const char* text:{"15","15.9","-15.9","65535","2147483904"}) {
  reset();lookup_value=static_cast<float>(std::strtod(text,nullptr));timing=.5f;l3_row.attributes[1]={"min",text};native_load_calls=0;
  damage_site=bias+0xdd5d0;damage_actor=original_actor.data();drive_damage();
  shared_handler_c_bridges::source_loaded=false;shared_handler_c_bridges::source_minimum=0;
  damage_site=0x4ee99b;damage_actor=actor.data();drive_damage();
  check(field<float>(actor.data(),0x2b8)==static_cast<float>(field<short>(original_actor.data(),0x294))&&native_load_calls==1,"owner-native value API resolves L3 MIN and signed stages through production bridge");
  drive_damage();check(native_load_calls==1,"resolved source value is cached without another engine file read");
 }
 l3_row.attributes[2]={"name","missing"};native_load_calls=0;shared_handler_c_bridges::source_loaded=false;shared_handler_c_bridges::source_minimum=0;
 check(shared_handler_c_bridges::source_l3()==0&&native_load_calls==1,"native reader missing L3 follows source unknown-name zero behavior");
 native_load_success=false;native_load_calls=0;shared_handler_c_bridges::source_loaded=false;shared_handler_c_bridges::source_minimum=0;
 check(shared_handler_c_bridges::source_l3()==0&&!shared_handler_c_bridges::source_loaded&&native_load_calls==1,"native reader failed load remains safely retryable");
 native_load_success=true;l3_row.attributes[2]={"name","L3"};
 const auto* source_scale=x1.read(0x3d64e4,4);const auto* native_scale=pc.read(0x691788,4);
 check(source_scale&&native_scale&&std::equal(source_scale,source_scale+4,native_scale),"retained jump timing multiplier matches source binary");
 check(x1.half(x1.read(0xdd583+7,2)-x1.bytes.data())==250,"source producer retains 250 knockback");
}
void input_tests() {
 for(bool null_action:{false,true})for(bool present:{false,true})for(float e:{9.f,10.f,11.f})for(u32 pressed:{0u,0x40u})for(bool fallback_result:{false,true}) {
  reset();selected=null_action?nullptr:move.data();event_present=present;energy=e;default_result=fallback_result;u32 buttons[12]{};buttons[0]=pressed;
  original=true;bool expected=reinterpret_cast<input>(bias+0xdaa00)(handler.data(),original_actor.data(),buttons);auto order=calls;int choice=action_id;calls.clear();
  original=false;bool got=reinterpret_cast<input>(0x4eced0)(handler.data(),actor.data(),buttons);
  check(expected==got&&order==calls&&choice==action_id&&field<void*>(actor.data(),0x3bc)==field<void*>(original_actor.data(),0x324),"Colossus event-energy input, explicit jump mask and cleanup match XML1");
 }
}
float* __cdecl facing(float* out,void*){out[0]=1.f;out[1]=out[2]=0.f;return out;}
float* __fastcall copy_vector(float* out,void*,const float* in){std::copy_n(in,3,out);return out;}
float __fastcall dot_vector(const float* a,void*,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
float* __fastcall multiply_vector(float* a,void*,float amount){for(int i=0;i<3;++i)a[i]*=amount;return a;}
void __fastcall simple_play(void*,void*,void* m,int flag){check(m==selected&&flag==1,"fastball native response move dispatch");calls.push_back(48);}
void bounce_fastball_tests(const image& pc,const image& x1) {
 pc.load(0x6919d4,4);x1.load(0x3c8244,4,bias);x1.load(0x3d6664,4,bias);
 jump(0x560990,&facing);jump(bias+0x1209c0,&facing);jump(0x42c340,&simple_play);jump(bias+0x3b100,&simple_play);
 field<void*>(at(0x67f9e0),0)=reinterpret_cast<void*>(&copy_vector);field<void*>(at(0x67f8a8),0)=reinterpret_cast<void*>(&dot_vector);field<void*>(at(0x67f8b0),0)=reinterpret_cast<void*>(&multiply_vector);
 for(bool hit:{false,true})for(bool actor_hit:{false,true}) {
  reset();hit_kinds=actor_hit?4:0;void* victim=hit?target.data():nullptr;
  original=true;classify(true);reinterpret_cast<contact_callback>(bias+0xdabe0)(nullptr,original_actor.data(),victim,nullptr);auto order=calls;auto rec=recipient;const float stamp=field<float>(original_actor.data(),0x530);calls.clear();
  original=false;classify(false);reinterpret_cast<contact_callback>(0x4ed1a0)(nullptr,actor.data(),victim,nullptr);
  check(order==calls&&rec==recipient&&stamp==field<float>(actor.data(),0x740)&&observed_velocity[0]==0&&observed_velocity[1]==0&&observed_velocity[2]==0,"bounce dispatch, stop and timestamp match without a contact record");
  for(bool record:{false,true})for(float angle:{-.150001f,-.15f,-.149999f}) {
   for(int i=0;i<3;++i)field<float>(contact.data(),0x10+i*4)=i?0.f:angle;
   calls.clear();original=true;classify(true);std::fill_n(observed_velocity,3,999.f);
   reinterpret_cast<contact_callback>(bias+0xe11d0)(nullptr,original_actor.data(),victim,record?contact.data():nullptr);order=calls;rec=recipient;auto choice=action_id;const std::array<float,3> v{observed_velocity[0],observed_velocity[1],observed_velocity[2]};calls.clear();std::fill_n(observed_velocity,3,999.f);
   original=false;classify(false);reinterpret_cast<contact_callback>(0x4f4ad0)(nullptr,actor.data(),victim,record?contact.data():nullptr);
   check(order==calls&&rec==recipient&&choice==action_id&&std::equal(v.begin(),v.end(),observed_velocity),"fastball event dispatch and retained angle/100-velocity response match XML1");
  }
 }
}
struct charge_vec{float x,y,z;};
charge_vec* __cdecl charge_facing(charge_vec* out,const charge_vec* a){*out={std::cos(a->z),std::sin(a->z),0};return out;}
charge_vec* __fastcall charge_zero(charge_vec* p,void*){*p={};return p;}
charge_vec* __fastcall charge_copy(charge_vec* p,void*,const charge_vec* q){*p=*q;return p;}
charge_vec* __fastcall charge_product(charge_vec* p,void*,const float* q){p->x*=*q;p->y*=*q;p->z*=*q;return p;}
float* __fastcall charge_component(charge_vec* p,void*,int i){return &p->x+i;}
float __fastcall charge_const_component(charge_vec* p,void*,int i){return *(&p->x+i);}
float __fastcall charge_length(charge_vec* p,void*){return std::sqrt(p->x*p->x+p->y*p->y+p->z*p->z);}
void __fastcall charge_scale(charge_vec* p,void*,float v){p->x*=v;p->y*=v;p->z*=v;}
void colossus_trajectory_tests(const image& pc,const image& x1){
 pc.load(0x4ecdc0,0x70);pc.load(0x4ece30,0xa0);load_x1(x1,0xda900,0x57);load_x1(x1,0xda960,0x99);
 for(u32 a:{0x691714u,0x691718u,0x680514u})pc.load(a,4);
 for(u32 a:{0x3d642cu,0x3d6430u,0x3c8244u})x1.load(a,4,bias);
 const u32 imports[]{0x67fa60,0x67fa2c,0x67f9b4,0x67f9c4,0x67f9d8,0x67f9e0,0x67fa50,0x67f8b0};std::array<void*,8> saved{};
 const void* replacements[]{&charge_zero,&charge_copy,&charge_product,&charge_component,&charge_const_component,&charge_copy,&charge_length,&charge_scale};
 for(unsigned i=0;i<8;++i){saved[i]=field<void*>(at(imports[i]),0);field<const void*>(at(imports[i]),0)=replacements[i];}
 jump(0x560990,&charge_facing);jump(bias+0x1209c0,&charge_facing);
 for(float yaw:{0.f,1.5707963f,3.1415927f})for(float z:{-250.f,0.f,100.f,400.f}){
  reset();bind();field<charge_vec>(actor.data(),0x2c)=field<charge_vec>(original_actor.data(),0x2c)={0,0,yaw};field<float>(actor.data(),0x128)=field<float>(original_actor.data(),0x108)=z;
  unsigned before=0,after=0;__asm mov before,esp
  reinterpret_cast<void(__thiscall*)(void*,void*)>(bias+0xda900)(nullptr,original_actor.data());
  const charge_vec source_motion{observed_velocity[0],observed_velocity[1],observed_velocity[2]};
  reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4ecdc0)(nullptr,actor.data(),nullptr);
  __asm mov after,esp
  check(before==after,"source ret4 and PC ret8 charge frame ABIs balance stack");
  check(std::fabs(source_motion.x-observed_velocity[0])<.001f&&std::fabs(source_motion.y-observed_velocity[1])<.001f&&source_motion.z==observed_velocity[2]&&observed_velocity[2]==z,"retail charge frame matches XML1 facing*320 and preserves airborne vertical velocity");
 }
 for(charge_vec v:{charge_vec{},charge_vec{320,0,400},charge_vec{320,0,-250},charge_vec{900,0,400}}){
  reset();bind();field<charge_vec>(actor.data(),0x120)=field<charge_vec>(original_actor.data(),0x100)=v;
  unsigned before=0,after=0;__asm mov before,esp
  reinterpret_cast<void(__thiscall*)(void*,void*)>(bias+0xda960)(nullptr,original_actor.data());const charge_vec source_motion{observed_velocity[0],observed_velocity[1],observed_velocity[2]};
  reinterpret_cast<void(__thiscall*)(void*,void*)>(0x4ece30)(nullptr,actor.data());
  __asm mov after,esp
  check(before==after,"source and PC charge cleanup ret4 ABIs balance stack");
  check(std::fabs(source_motion.x-observed_velocity[0])<.001f&&std::fabs(source_motion.y-observed_velocity[1])<.001f&&std::fabs(source_motion.z-observed_velocity[2])<.001f,"retail charge cleanup matches XML1 Z100 and total-speed450 caps including stopped motion");
 }
 for(unsigned i=0;i<8;++i)field<void*>(at(imports[i]),0)=saved[i];
}
void colossus_tests(const image& pc,const image& x1) {
 colossus_trajectory_tests(pc,x1);
 const float stopped[]{0,0,0},normal_at_rest[]{-1,0,0};_clearfp();
 check(!shared_handler_c_bridges::crash_angle(stopped,normal_at_rest)&&!(_statusfp()&(_SW_INVALID|_SW_ZERODIVIDE)),"stationary charge contact avoids zero reciprocal and leaves FP exception state clean");
 pc.load(0x4a97a0,0xb3);load_x1(x1,0xa09d0,0xb3);
 jump(0x4876e0,&material_type);jump(bias+0x83730,&material_type);native_material_proof=true;
 check(pc.read(0x4a9852,1)[0]==0xc3&&x1.read(0xa0a82,1)[0]==0xc3,"owner material accessors return without consuming stack arguments");
 check(limits_rules::matches(pc.read(0x4879c5,3),"c21000"),"owner contact-sound method consumes four arguments");
 for(bool shifted:{false,true})for(float previous_health:{10.f,10.99f,32768.f})for(u32 flags:{0u,8u})for(unsigned weight:{0u,2u,255u})for(u32 kind:{0u,1u,3u,5u})for(bool level_hit:{false,true})for(bool large:{false,true})for(float after:{5.f,10.f})for(float height:{13.99f,14.f,14.01f})for(float dot:{-.71001f,-.71f,-.70999f}) {
  reset();shifted_contact=shifted;hit_kinds=kind;is_level=level_hit;damage_after=after;
  field<float>(contact.data(),0x60)=33.f;field<float>(contact.data(),0x64)=44.f;field<float>(contact.data(),0x68)=55.f;field<float>(contact.data(),0x70)=-1.f;field<float>(contact.data(),0x74)=.25f;
  target[0x31c]=static_cast<unsigned char>(weight);field<u32>(target.data(),0x1c)=50;field<u32>(actor.data(),0x61c)=50;field<float>(actor.data(),0x620)=clock_now;
  field<u32>(target.data(),4)=flags;field<float>(target.data(),0x240)=field<float>(target.data(),0x27c)=previous_health;stats[0x2ad]=large?8:0;stats[0x3a1]=large?0x10:0;
  for(auto* a:{actor.data(),original_actor.data()}) {field<float>(a,0x28)=5;field<float>(a,0x100)=field<float>(a,0x120)=320;}
  field<float>(contact.data(),0x28)=5+height;field<float>(contact.data(),0x10)=-1.f;field<float>(contact.data(),0x30)=dot;
  original=true;classify(true);reinterpret_cast<contact_callback>(bias+0xdad20)(nullptr,original_actor.data(),target.data(),contact.data());auto order=calls;auto rec=recipient;std::array<float,3> v{observed_velocity[0],observed_velocity[1],observed_velocity[2]};auto choice=action_id;const auto effect_expected=effect_vectors;calls.clear();std::fill_n(observed_velocity,3,999.f);field<float>(target.data(),0x27c)=previous_health;contact_phase=false;effect_vectors.fill(0.f);
  original=false;classify(false);reinterpret_cast<contact_callback>(0x4ed220)(nullptr,actor.data(),target.data(),contact.data());
  const void* source_recipient=(kind&1)&&(kind&4)?target.data():nullptr;
  // Source's unused second argument is not PC's talent-context contract;
  // the real PC tag/punch pipeline below tests that context independently.
  check(order==calls&&rec==source_recipient&&choice==action_id&&std::equal(v.begin(),v.end(),observed_velocity)&&effect_expected==effect_vectors,"Colossus source admission/crash/rebound policy matches; talent context tested natively");
 }
}

bool map_flag=false,invisibility_active=false;int global_calls=0,visibility_queries=0,random_calls=0;u32 selected_name=0;int playback_priority=0;
bool __fastcall map_predicate(void*,void*){++global_calls;return map_flag;}
int __fastcall neutral_team(void*,void*){return original?0x1a:0x1c;}
float __fastcall next_deadline(void*,void*){calls.push_back(60);return 200.f;}
float __fastcall bored_deadline(void*,void*){return 201.f;}
int __cdecl different_name(const void*,const void*){return 1;}
void* __fastcall missing_named(void*,void*,const void*){return nullptr;}
void* __fastcall style_object(void* p,void*){return p;}
void* __fastcall resolve_style(void*,void*,const void* name,int applicability){check(!applicability,"idle original applicability");selected_name=reinterpret_cast<u32>(name);return move.data();}
float __cdecl pc_rate_random(float lo,float hi,int inclusive){check(lo==.8f&&hi==1.2f&&inclusive==1,"PC bored playback random range");++random_calls;return 1.f;}
float __cdecl x1_rate_random(float lo,float hi){check(lo==.8f&&hi==1.2f,"source bored playback random range");++random_calls;return 1.f;}
void __fastcall idle_play(void*,void*,void*,int priority,float rate,int a,int b,int c){check(rate==1.f&&!a&&!b&&!c,"source idle playback parameters");playback_priority=priority;calls.push_back(61);}
float __fastcall move_speed(void*,void*,int index){return index==0?100.f:index==1?200.f:300.f;}
void* __fastcall handler_identity(void*,void*){return selected;}
bool __fastcall invisible(void*,void*){++visibility_queries;return invisibility_active;}
void __fastcall clear_visibility(void*,void*,int flag){check(flag==0,"temporary visibility cleanup clears its flag");calls.push_back(70);}
void __fastcall clear_suppression(void*,void*,int flag){check(flag==0,"temporary suppression cleanup clears its flag");calls.push_back(71);}
void __fastcall publish(void*,void*){calls.push_back(72);}
void __fastcall ground(void*,void*,const void*){calls.push_back(73);}
void idle_tests(const image& pc,const image& x1,const std::vector<xml1_hook_transaction::patch>& writes) {
 pc.load(0x4edfb0,0x51a);load_x1(x1,0xdcf30,0x2b4);
 // Named CName operands are data pointers. The stubs identify their addresses,
 // so only actual read operands need relocation in these original slices.
 for(auto a:{0x3c6de4u,0x3c6e40u})x1.load(a,4,bias);
 for(const auto& p:writes)if(p.address>=0x4edfb0&&p.address<0x4ee4ca)std::memcpy(at(p.address),p.after.data(),p.after.size());
 jump(0x484990,&get_manager);jump(bias+0x80c80,&get_manager);
 manager_vt[0x34/4]=manager_vt[0x3c/4]=reinterpret_cast<void*>(&map_predicate);
 jump(0x42a400,&next_deadline);jump(bias+0x3a1b0,&next_deadline);jump(0x42a430,&bored_deadline);jump(bias+0x3a1e0,&bored_deadline);
 jump(0x672562,&different_name);jump(bias+0x343612,&different_name);jump(0x427ca0,&missing_named);jump(bias+0x38210,&missing_named);
 jump(0x41d0d0,&style_object);jump(bias+0x2e290,&style_object);jump(0x501a50,&resolve_style);jump(bias+0xed6e0,&resolve_style);
 jump(0x5601b0,&pc_rate_random);jump(bias+0x1233a0,&x1_rate_random);jump(0x432030,&idle_play);jump(bias+0x40df0,&idle_play);
 for(bool global:{false,true})for(int controller:{-1,0})for(float clock:{99.f,100.f,100.01f,101.f,101.01f})for(bool nonhuman:{false,true}) {
  reset();map_flag=global;clock_now=clock;stats[0x2ae]=nonhuman?0x80:0;field<int>(actor.data(),0x3d4)=controller;
  field<float>(actor.data(),0x5ec)=field<float>(original_actor.data(),0x3f4)=100.f;
  original=true;global_calls=random_calls=0;reinterpret_cast<void(__thiscall*)(void*,void*)>(bias+0xdd060)(nullptr,original_actor.data());auto order=calls;const auto source_name=selected_name;const auto source_priority=playback_priority;const auto source_random=random_calls;calls.clear();global_calls=random_calls=0;
  original=false;reinterpret_cast<shared_handler_bridges::update>(0x4ee150)(nullptr,actor.data(),nullptr);
  check(order==calls&&random_calls==source_random&&field<float>(actor.data(),0x5ec)==field<float>(original_actor.data(),0x3f4),"idle input global/deadline and humanoid policy match source");
  if(source_random) {
   const bool matching=((source_name==0x4f32cc&&selected_name==0x78a8b8)||(source_name==0x4f32b4&&selected_name==0x78a868))&&source_priority==playback_priority;
   if(!matching)std::printf("idle mismatch clock=%g map=%d controller=%d nonhuman=%d source=%x/%d PC=%x/%d\n",clock,global,controller,nonhuman,source_name,source_priority,selected_name,playback_priority);
   check(matching,"idle selects mapped bored/knockback name and priority");
  }
 }
 for(bool global:{false,true})for(int controller:{-1,0})for(float clock:{99.f,100.f,100.01f}) {
  reset();map_flag=global;clock_now=clock;field<int>(actor.data(),0x3d4)=controller;
  field<float>(actor.data(),0x5ec)=field<float>(original_actor.data(),0x3f4)=100.f;
  std::array<unsigned char,0x80> source_state{},pc_state{};
  original=true;global_calls=0;reinterpret_cast<entry>(bias+0xdcf30)(nullptr,original_actor.data(),nullptr,source_state.data());const auto order=calls;const int queries=global_calls;calls.clear();global_calls=0;
  original=false;reinterpret_cast<entry>(0x4edfb0)(nullptr,actor.data(),nullptr,pc_state.data());
  check(order==calls&&global_calls==queries&&field<float>(actor.data(),0x5ec)==field<float>(original_actor.data(),0x3f4),"idle entry uses source map/deadline policy for controlled and controllerless actors");
 }
 pc.load(0x4ee370,0x160);load_x1(x1,0xdd1f0,0x154);
 for(const auto& p:writes)if(p.address>=0x4ee370&&p.address<0x4ee4d0)std::memcpy(at(p.address),p.after.data(),p.after.size());
 jump(0x41a220,&neutral_team);jump(bias+0x26e60,&neutral_team);
 for(bool global:{false,true})for(float clock:{99.f,100.f,100.01f}) {
  reset();map_flag=global;clock_now=clock;
  field<float>(actor.data(),0x5e8)=field<float>(original_actor.data(),0x3f0)=100.f;
  original=true;global_calls=0;reinterpret_cast<void(__thiscall*)(void*,void*)>(bias+0xdd1f0)(nullptr,original_actor.data());const int queries=global_calls;global_calls=0;
  original=false;reinterpret_cast<shared_handler_bridges::update>(0x4ee370)(nullptr,actor.data(),nullptr);
  check(global_calls==queries&&queries==(clock>100.f?1:0),"bored update invokes mapped source predicate only after its strict deadline");
 }
 reset();std::array<unsigned char,0x80> state{};map_flag=true;
 original=true;reinterpret_cast<entry>(bias+0xdd1a0)(nullptr,original_actor.data(),nullptr,state.data());
 original=false;reinterpret_cast<entry>(0x4ee2d0)(nullptr,actor.data(),nullptr,state.data());
 check(field<float>(actor.data(),0x5e8)==field<float>(original_actor.data(),0x3f0),"bored entry preserves source deadline without controller/global gates");
 // Stun remains exclusively owned by rule 18's flag lifecycle and hooks.
 check(std::equal(pc.read(0x4edff7,0x45),pc.read(0x4edff7,0x45)+0x45,at(0x4edff7)),"idle rule leaves the complete native stun redirection bytes unchanged");
 clock_now=100;bind();
}
void animation_tests(const image& pc,const image& x1,const std::vector<xml1_hook_transaction::patch>& writes) {
 pc.load(0x4f0110,0x167);load_x1(x1,0xde740,0x122);x1.load(0x3cb1f0,4,bias);
 for(const auto& p:writes)if(p.address>=0x4f0110&&p.address<0x4f0277)std::memcpy(at(p.address),p.after.data(),p.after.size());
 jump(0x425b70,&move_speed);jump(bias+0x360a0,&move_speed);
 for(float scale:{1.f,1.15f,1.5f})for(int controller:{-1,0})for(float previous:{.8f,1.f,1.2f}) {
  reset();field<float>(stats.data(),0x2b0)=scale;field<int>(actor.data(),0x3d4)=controller;
  std::array<unsigned char,0x80> native{},source{},buttons{},buttons1{};
  field<float>(buttons.data(),8)=field<float>(buttons1.data(),8)=.5f;
  field<float>(native.data(),0x64)=field<float>(source.data(),0x5c)=previous;
  reinterpret_cast<entry>(bias+0xde740)(nullptr,original_actor.data(),buttons1.data(),source.data());
  reinterpret_cast<entry>(0x4f0110)(nullptr,actor.data(),buttons.data(),native.data());
  check(field<float>(native.data(),0x64)==field<float>(source.data(),0x5c)&&field<float>(buttons.data(),8)==field<float>(buttons1.data(),8),"locomotion rate and movement magnitude match without scale division");
 }
 for(auto a:{0x3d78bcu,0x3d80c4u,0x3d80e8u})check(x1.word(x1.read(a+24,4)-x1.bytes.data())==0xe7460,"source move and both pickup handlers use default input directly");
 move_vt[0x30/4]=reinterpret_cast<void*>(&handler_identity);
 for(bool same:{false,true})for(bool result:{false,true})for(u32 flags:{0u,4u,0x10u}) {
  reset();move_vt[0x30/4]=reinterpret_cast<void*>(&handler_identity);selected=same?handler.data():nullptr;default_result=result;
  field<void*>(actor.data(),0x3bc)=move.data();u32 buttons[12]{};buttons[9]=flags;
  const bool got=reinterpret_cast<input>(0x4f0240)(handler.data(),actor.data(),buttons);
  check(got==result&&buttons[9]==flags&&calls==std::vector<int>{31},"move and pickup input retain default result and animation flags");
 }
}
void visibility_tests(const image& pc,const image& x1,const std::vector<xml1_hook_transaction::patch>& writes) {
 pc.load(0x4efc70,0x2f);pc.load(0x4f4470,0x54);load_x1(x1,0xde470,0x24);load_x1(x1,0xe0d20,0x4a);
 for(const auto& p:writes)if(p.address>=0x4efc70&&p.address<0x4efc9f||p.address>=0x4f4470&&p.address<0x4f44c4)std::memcpy(at(p.address),p.after.data(),p.after.size());
 jump(0x41d0a0,&invisible);jump(0x41d020,&clear_visibility);jump(bias+0x2e220,&clear_visibility);jump(0x41d060,&clear_suppression);jump(bias+0x2e260,&clear_suppression);
 jump(0x45fe40,&publish);jump(bias+0x66a90,&publish);jump(0x419f20,&ground);jump(bias+0x27260,&ground);
 for(bool active:{false,true})for(bool suppressed:{false,true})for(bool stomp:{false,true}) {
  reset();invisibility_active=active;visibility_queries=0;
  actor[0x3db]=suppressed?4:0;original_actor[0x337]=suppressed?0x10:0;
  original=true;reinterpret_cast<void(__thiscall*)(void*,void*)>(bias+(stomp?0xe0d20:0xde470))(nullptr,original_actor.data());const auto order=calls;calls.clear();
  original=false;reinterpret_cast<void(__thiscall*)(void*,void*)>(stomp?0x4f4470:0x4efc70)(nullptr,actor.data());
  check(order==calls&&visibility_queries==0,"teleport cleanup matches source with active and absent invisibility affecter");
  if(stomp)check(field<float>(actor.data(),0x278)==field<float>(original_actor.data(),0x478)&&!(field<u32>(actor.data(),4)&4),"stomp cleanup retains ground/coefficient state");
 }
}

float source_opacity=.25f,pc_opacity=.25f;
void __fastcall set_opacity(void* a,void*,float opacity){check(opacity==1.f,"source visual cleanup restores model opacity to one");(a==actor.data()?pc_opacity:source_opacity)=opacity;calls.push_back(70);}
void native_visibility_state_tests(const image& pc,const image& x1) {
 pc.load(0x41d020,0xa2);load_x1(x1,0x2e220,0x5c);
 jump(0x474790,&set_opacity);jump(bias+0x76b40,&set_opacity);
 for(bool active:{false,true})for(bool suppressed:{false,true})for(bool stomp:{false,true}) {
  reset();actor[0x3da]=original_actor[0x336]=0xa5;actor[0x3db]=static_cast<unsigned char>(0xa0|(suppressed?4:0));original_actor[0x337]=static_cast<unsigned char>(0xa0|(suppressed?0x10:0));actor[0x578]=static_cast<unsigned char>(0xa0|(active?1:0));
  source_opacity=pc_opacity=.25f;
  const auto native_invisible=reinterpret_cast<bool(__thiscall*)(void*)>(0x41d0a0);check(native_invisible(actor.data())==active,"owner-native invisibility cache starts active/absent as selected");
  original=true;reinterpret_cast<void(__thiscall*)(void*,void*)>(bias+(stomp?0xe0d20:0xde470))(nullptr,original_actor.data());const auto order=calls;calls.clear();
  original=false;reinterpret_cast<void(__thiscall*)(void*,void*)>(stomp?0x4f4470:0x4efc70)(nullptr,actor.data());
  check(order==calls&&source_opacity==pc_opacity&&pc_opacity==(suppressed?1.f:.25f),"owner-native visual setters match source opacity with active invisibility retained");
  check(actor[0x3da]==static_cast<unsigned char>(0xa5&~(suppressed?1:0))&&original_actor[0x336]==static_cast<unsigned char>(0xa5&~(suppressed?4:0))&&actor[0x3db]==0xa0&&original_actor[0x337]==0xa0,"native temporary hide and suppression bits clear with unrelated flags preserved");
  check(native_invisible(actor.data())==active&&actor[0x578]==static_cast<unsigned char>(0xa0|(active?1:0)),"visual cleanup preserves actual cached invisibility affecter state");
 }
}
u32 abi_site=0,abi_kind=0,abi_before_esp=0,abi_after_esp=0,abi_before_flags=0,abi_after_flags=0,abi_regs[7]{};
void* abi_actor=actor.data();void* abi_target=target.data();float abi_seed=3.25f,abi_sse=0.f,abi_fpu=0.f;
__declspec(naked) void abi_done(){__asm {
 mov abi_after_esp,esp
 mov abi_regs[0],eax
 mov abi_regs[4],ebx
 mov abi_regs[8],ecx
 mov abi_regs[12],edx
 mov abi_regs[16],esi
 mov abi_regs[20],edi
 mov abi_regs[24],ebp
 pushfd
 pop abi_after_flags
 fstp abi_fpu
 movss abi_sse,xmm0
 add esp,0x100
 popad
 ret
}}
__declspec(naked) void drive_abi(){__asm {
 pushad
 sub esp,0x100
 fld1
 movss xmm0,abi_seed
 mov eax,0x3f000000
 mov [esp+0x18],eax
 mov esi,abi_actor
 mov edi,abi_actor
 cmp abi_kind,2
 jne ready
 mov esi,abi_target
 ready:
 mov ebx,0x11223344
 mov ecx,0x22334455
 mov edx,0x33445566
 mov ebp,0x55667788
 mov eax,0x44556677
 cmp eax,eax
 pushfd
 pop abi_before_flags
 mov abi_before_esp,esp
 jmp dword ptr [abi_site]
}}
void abi_tests() {
 using namespace shared_handler_c_bridges;
 jump_resume=fly_resume=fastball_resume=reinterpret_cast<u32>(&abi_done);
 for(abi_kind=0;abi_kind<3;++abi_kind) {
  reset();original=false;hit_kinds=4;source_loaded=true;source_minimum=15.f;
  abi_site=reinterpret_cast<u32>(abi_kind==0?&jump_damage_bridge:abi_kind==1?&fly_damage_bridge:&fastball_bridge);
  drive_abi();
  check(abi_before_esp==abi_after_esp&&abi_before_flags==abi_after_flags,"interior C bridges preserve exact stack depth and flags");
  check(abi_fpu==1.f&&abi_sse==abi_seed,"interior C bridges preserve live x87 and SSE sentinels");
  check(abi_regs[0]==0x44556677&&abi_regs[1]==0x11223344&&abi_regs[2]==0x22334455&&abi_regs[3]==0x33445566&&abi_regs[4]==reinterpret_cast<u32>(abi_kind==2?abi_target:abi_actor)&&abi_regs[5]==reinterpret_cast<u32>(abi_actor)&&abi_regs[6]==0x55667788,"interior C bridges preserve every live integer register");
 }
 jump_resume=0x4ee9ef;fly_resume=0x4eebb9;fastball_resume=0x4f4b16;
}
}
int main(int argc,char** argv) {
 std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 AddVectoredExceptionHandler(1,[](PEXCEPTION_POINTERS e)->LONG{std::printf("exception %08lX at %p EAX=%08lX ECX=%08lX ESI=%08lX ESP=%08lX\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,e->ContextRecord->Eax,e->ContextRecord->Ecx,e->ContextRecord->Esi,e->ContextRecord->Esp);return EXCEPTION_CONTINUE_SEARCH;});
 if(argc<3)return 2;
 if(reinterpret_cast<std::uintptr_t>(fixture_space)>0x4ebe20)return 2;
 DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old))return 2;
 if(VirtualAlloc(at(bias),0x600000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)!=at(bias))return 2;
 image pc(argv[1],pc_hash),x1(argv[2],x1_hash);if(failures)return 1;
 if(argc>3&&!std::strcmp(argv[3],"--off-proof")){
  seed(pc);const auto before=std::vector<unsigned char>(at(0x401000),at(0x801000));
  gameplay_capabilities::record reference;check(reference.finalize(),"OFF reference agreement finalizes");
  const auto module=GetModuleHandleW(nullptr);
  for(const char* choice:{"xml2",""}){
   keys(shared_handler_c_rules::all,choice);DWORD protection=0,unused=0;
   check(VirtualProtect(at(0x401000),0x400000,PAGE_NOACCESS,&protection)!=0,"OFF proof image made inaccessible");
   shared_handlers_c::install(module);
   check(VirtualProtect(at(0x401000),0x400000,protection,&unused)!=0,"OFF proof image protection restored");
   check(!shared_handlers_c::requested()&&!shared_handlers_c::installed()&&shared_handlers_c::verified(),"all eleven native choices remain OFF without image reads");
   check(std::equal(before.begin(),before.end(),at(0x401000)),"all eleven OFF choices are byte-identical across four MB");
   gameplay_capabilities::record actual;
   if(shared_handlers_c::requested())actual.add(46,shared_handlers_c::revision,shared_handlers_c::values(),shared_handlers_c::verified());
   check(actual.finalize()&&actual.bytes().size()==reference.bytes().size()&&std::equal(actual.bytes().begin(),actual.bytes().end(),reference.bytes().begin()),"OFF agreement bytes identical to rule46 absent");
  }
  keys(0);shared_handlers_c::install(nullptr);check(!shared_handlers_c::requested()&&shared_handlers_c::verified(),"missing keys preserve OFF");
  std::printf("OFF proof: %d checks, %d failures; 4194304 image bytes and agreement bytes identical\n",checks,failures);return failures?1:0;
 }
 const u32 mask=argc>3?static_cast<u32>(std::stoul(argv[3],nullptr,0)):2047;
 if(!mask||mask>2047)return 2;
 for(const auto& g:shared_handler_c_rules::guards){auto* b=pc.read(g.bytes.va,g.bytes.hex.size()/2);check(b&&limits_rules::matches(b,g.bytes.hex),g.bytes.what);}
 if(failures)return 1;
 seed(pc);const auto baseline=std::vector<unsigned char>(at(0x401000),at(0x801000));
 for(const char* off:{"xml2",""}) {keys(2047,off);shared_handlers_c::install(nullptr);check(!shared_handlers_c::requested()&&shared_handlers_c::verified()&&std::equal(baseline.begin(),baseline.end(),at(0x401000)),"OFF does not access image or change agreement");}
 keys(0);shared_handlers_c::install(nullptr);check(!shared_handlers_c::requested()&&shared_handlers_c::verified(),"absent selection stays OFF");
 keys(mask,"invalid");shared_handlers_c::install(GetModuleHandleW(nullptr));check(!shared_handlers_c::verified()&&!shared_handlers_c::installed()&&std::equal(baseline.begin(),baseline.end(),at(0x401000)),"invalid selection refuses without writes");
 if(mask&1536){keys(mask);xml1_grab_carry::available=false;shared_handlers_c::install(GetModuleHandleW(nullptr));check(!shared_handlers_c::verified()&&!shared_handlers_c::installed()&&std::equal(baseline.begin(),baseline.end(),at(0x401000)),"missing lifecycle dependency refuses the complete transaction");xml1_grab_carry::available=true;}
 keys(mask);shared_handlers_c::install(reinterpret_cast<HMODULE>(0x500000));check(!shared_handlers_c::verified()&&!shared_handlers_c::installed(),"relocated image refused");
 for(const auto& g:shared_handler_c_rules::guards)if(g.parts&mask) {seed(pc);*at(g.bytes.va)^=1;auto before=std::vector<unsigned char>(at(0x401000),at(0x801000));shared_handlers_c::install(GetModuleHandleW(nullptr));check(!shared_handlers_c::installed()&&!shared_handlers_c::verified()&&std::equal(before.begin(),before.end(),at(0x401000)),"selected dependency failure refuses atomically");}
 if(mask&shared_handler_c_rules::colossus)for(u32 site:{0x4a95c0u,0x4a95f0u,0x4a9600u,0x4a9610u,0x4a97a0u}) {
  seed(pc);for(auto& owner:entity_capacity::owners)owner={};
  entity_capacity::owners[0]={site,0x10001230};jump(site,reinterpret_cast<void*>(0x10001234));
  const auto before=std::vector<unsigned char>(at(0x401000),at(0x801000));
  shared_handlers_c::install(GetModuleHandleW(nullptr));
  check(!shared_handlers_c::installed()&&!shared_handlers_c::verified()&&std::equal(before.begin(),before.end(),at(0x401000)),"capacity query accepts only its exact published target, never a foreign jump");
 }
 for(auto& owner:entity_capacity::owners)owner={};
 if(mask&1536)for(u32 site:{0x4ab6d0u,0x4ab720u,0x44fe10u,0x4a9570u,0x4a9590u,0x4a95c0u,0x4a9600u}){
  seed(pc);entity_capacity::owners[0]={site,0x10001230};jump(site,reinterpret_cast<void*>(0x10001234));
  const auto before=std::vector<unsigned char>(at(0x401000),at(0x801000));shared_handlers_c::install(GetModuleHandleW(nullptr));
  check(!shared_handlers_c::installed()&&!shared_handlers_c::verified()&&std::equal(before.begin(),before.end(),at(0x401000)),"TK/guard query service accepts only its exact published capacity target");
  entity_capacity::owners[0]={};
 }
 seed(pc);
 // Other owners may replace fly-charge collision and pickup cancellation.
 // These slots are outside C's producer/input responsibilities and must remain
 // compatible regardless of install order.
 auto installation_baseline=baseline;
 for(u32 slot:{0x6932c8u,0x693840u,0x693864u}) {
  const u32 external_callback=0x12345000u+slot;
  field<u32>(at(slot),0)=external_callback;
  std::memcpy(installation_baseline.data()+slot-0x401000,&external_callback,4);
 }
 if(mask&shared_handler_c_rules::colossus) {
  // BalanceParity owns the preceding store/context-stats load. C's contract
  // guards must coexist with its nine-byte CALL/NOP span in the full INI.
  pc.load(0x4dc37b,9);at(0x4dc37b)[0]=0xe8;field<u32>(at(0x4dc37b),1)=0x10006000u-0x4dc380u;std::fill_n(at(0x4dc380),4,0x90);
  std::copy_n(at(0x4dc37b),9,installation_baseline.begin()+0x4dc37b-0x401000);
  constexpr u32 sites[]{0x4a95c0,0x4a95f0,0x4a9600,0x4a9610,0x4a97a0};
  for(unsigned i=0;i<5;++i) {
   entity_capacity::owners[i]={sites[i],0x10004000u+0x100u*i};
   jump(sites[i],reinterpret_cast<void*>(entity_capacity::owners[i].target));
   std::copy_n(at(sites[i]),5,installation_baseline.begin()+sites[i]-0x401000);
  }
 }
 if(mask&1536){unsigned index=5;for(u32 site:{0x4ab6d0u,0x4ab720u,0x44fe10u,0x4a9570u,0x4a9590u,0x4a95c0u,0x4a9600u}){
  // Reuse an existing claim when a selected C callback shares this service.
  if(entity_capacity::installed_target(site))continue;
  entity_capacity::owners[index]={site,0x10005000u+0x100u*index};++index;const u32 destination=entity_capacity::installed_target(site);
  jump(site,reinterpret_cast<void*>(destination));std::copy_n(at(site),5,installation_baseline.begin()+site-0x401000);
 }}
 shared_handlers_c::install(GetModuleHandleW(nullptr));check(shared_handlers_c::installed()&&shared_handlers_c::verified()&&shared_handlers_c::values()[0]==mask,"exact selected rule-46 mask installed");
 using namespace shared_handler_c_bridges;
 const shared_handler_c_rules::targets targets{&jump_damage_bridge,&fly_damage_bridge,&colossus_input,&colossus_contact,&bounce_contact,&fastball_bridge,
  &tk_guard_bridges::tk_entry,&tk_guard_bridges::tk_update,&tk_guard_bridges::tk_cancel,&tk_guard_bridges::tk_input,&tk_guard_bridges::tk_filter,&tk_guard_bridges::guard_entry,&tk_guard_bridges::tk_parse,&tk_guard_bridges::guard_input};
 auto expected=installation_baseline;auto writes=shared_handler_c_rules::patches(mask,targets);
 for(const auto& p:writes){check(p.before.size()==p.after.size()&&std::equal(p.before.begin(),p.before.end(),pc.read(p.address,p.before.size())),"every written byte is guarded");std::copy(p.after.begin(),p.after.end(),expected.begin()+p.address-0x401000);}
 check(std::equal(expected.begin(),expected.end(),at(0x401000)),"production installer exact footprint preserves other owners' collision/cancel hooks");
 gameplay_capabilities::record off,on,other;check(off.finalize(),"OFF agreement finalizes");check(gameplay_capabilities::shared_handlers_c_id==46,"coordinator rule 46");
 check(on.add(46,1,shared_handlers_c::values(),true)&&on.finalize()&&on.compare(off.bytes())!=gameplay_capabilities::result::match,"selected C rules refuse OFF peer");
 check(other.add(46,1,{mask^2047u,0,0,0},true)&&other.finalize()&&other.compare(on.bytes())!=gameplay_capabilities::result::match,"different C selections refuse peer");
 if(mask!=2047){std::printf("shared handlers C mask %u: %d checks, %d failures\n",mask,checks,failures);return failures?1:0;}
 pc.load(0x4f4ad0,0xc3);load_x1(x1,0xdabe0,0x65);load_x1(x1,0xe11d0,0x100);load_x1(x1,0xdaa00,0x8c);load_x1(x1,0xdad20,0x2e7);
 for(auto a:{0x3c6de4u,0x3c6e40u,0x3d6438u,0x3d643cu,0x3d6430u})x1.load(a,4,bias);
 for(const auto& p:writes)std::memcpy(at(p.address),p.after.data(),p.after.size());
 bind();jump(bias+0x1a41f0,&truncate_x87);
 std::puts("damage");damage_tests(x1);std::puts("native value reader");native_value_reader_tests(pc,x1);std::puts("input");input_tests();std::puts("bounce/fastball");bounce_fastball_tests(pc,x1);std::puts("Colossus");colossus_tests(pc,x1);native_material_proof=false;shifted_contact=false;
 std::puts("idle");idle_tests(pc,x1,writes);std::puts("animation");animation_tests(pc,x1,writes);std::puts("visibility");visibility_tests(pc,x1,writes);std::puts("native visibility state");native_visibility_state_tests(pc,x1);std::puts("ABI");abi_tests();
 check(!game_version::refused,"guard refusal retains safe startup");
 std::puts("Colossus native talent context");colossus_context_fixture::run(pc,&truncate_x87);
 std::printf("shared handlers C: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
