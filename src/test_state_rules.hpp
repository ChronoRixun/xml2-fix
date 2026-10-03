#pragma once
// Test-only observation of retail XMen2.exe (image base 0x400000). No engine writes/calls.
// Rechecked with Ghidra on the unpacked executable, 2026-10-02:
// game 0x729960: vtable 0x686e1c; party name handles +0x14 (0x46c510), act byte
// +0x5e0 (0x469c40), game clock float +0x3e8 (0x469740).
// Name pool 0xa0a820: 0x425bc0 resolves h to +0x8008 + [pool+4+(h&0xffffff)*4].
// zones 0x72a578: path +0x1e0, load flags +0x220 & 3 (0x483f30/0x483e90).
// Entity table 0x778b70: 256 pointers +4, live bitmap +0x818, generation handles
// +0x83c (0x4c6bd0/0x4c6c20; the pool subobject is table+4). Require entity+0x1c == generation handle.
// Entity virtual slot 0 returns class metadata; only the literal mov eax,imm;ret
// getter is decoded, never executed. Class bit at metadata+0x14 (id+0x24),
// character class id [0x70b840] (0x41fb10). Character: origin floats +0x20
// (0x4a1ac0); health +0x27c, max +0x284 (0x4a36f0/0x4a3660); stats ptr +0x35c,
// stats identifier +0x150 (0x46c590). AI branch: +0x3d8 bit 0 OR +0x768 < -0.5
// OR +0x768 > game clock (0x434540). This is control routing, not a promise of motion.
// Conversation singleton [0x717aac]: +0x21b24 bit 1 active; signed short +0x21b28
// responses; signed short +0x21b26 selected response; +0x4bc line id; +0x2399c talk-animation entity handle (0x45bddd).
// Speaker: 0x4573f0 resolves the current file via the tree subobject CS+4:
// root +8, nodes +0x14 (stride 20, left/right/name at +0/+4/+8), value pointers
// CS+0x32c. Current filename key [CS+0x4b0] points to a char* (0x456440).
// File line pool: mask +0x59c, generation +0x4fc, bitmap +0x4f0, node ptr +0x39c.
// Current node+0x64 is a char* speaker key (0x45a340); may be the X-Team placeholder.
// Traverse at most 40 nodes; cycles/bad pointers/absent keys return null.
// Menus/popups: reuse the audited constants in frame_rate_rules.hpp. Menu name
// accessor 0x5d8640 and vtable slot +0x214 are verified separately below.
// Mission singleton [0x72b108], vtable 0x6892d4 (0x48a0e0/0x489ff0): +0x7d78
// current act's objective count <=75. Records at +8, stride 0x1ac (0x488d90):
// name char[32], global state index byte +0x1a9, target count +0x1aa &63.
// State byte [0x72b118+index]: count &63, complete bit6, hidden/disabled bit7
// (0x488aa0/0x488ab0/0x488ad0). enabled/shown both mean !hidden; no second bit.
// Vertical speed is sampled dz / wall seconds in the SAME zone and SAME entity;
// not engine ground contact. on_floor and airborne_ms remain null. Flight, lifts
// and scripted movement need client context. Unknowns never become false/zero.
// Events are bounded sampled transitions at keyboard polls (100ms), not hooks:
// short transitions between samples may be missed. Script errors are unavailable.
#include "frame_rate_rules.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>
#include <cstdio>
#include <type_traits>
#include <charconv>
namespace test_state_rules {
using address = std::uint32_t;
inline std::string lower(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
inline std::string quote(const std::string& s) {
 std::string r="\"";
 for (const unsigned char c:s) {
  if(c=='"'||c=='\\') {r+='\\';r+=c;}
  else if(c<32 || c>=127) {char b[7]; std::snprintf(b,sizeof b,"\\u%04x",unsigned(c));r+=b;}
  else r+=c;
 } return r+'"';
}
inline std::string json(const std::optional<std::string>& x) {return x?quote(*x):"null";}
inline std::string json(const std::optional<bool>& x) {return x?(*x?"true":"false"):"null";}
template<class T> std::string json(const std::optional<T>& x) {
 if(!x)return "null";
 if constexpr(std::is_floating_point_v<T>) {if(!std::isfinite(*x))return "null";}
 char b[64];const auto result=std::to_chars(b,b+sizeof b,*x);
 return result.ec==std::errc()?std::string(b,result.ptr):"null";
}
struct actor {
 std::optional<std::string> name;
 address id=0;
 std::array<std::optional<float>,3> pos;
 std::optional<float> health,max_health,vz;
 std::optional<bool> ai;
 std::optional<bool> alive() const {return health?std::optional<bool>(*health>0):std::nullopt;}
};
struct objective {
 std::string name; unsigned count=0,goal=0; bool complete=false,shown=false;
 bool operator==(const objective&)const=default;
};
struct snapshot {
 std::uint64_t ms=0;
 std::optional<std::string> zone,mode,menu,speaker;
 std::optional<int> act,responses,selected;
 std::optional<bool> loading,conversation,popup,menu_open;
 std::optional<address> line;
 std::array<actor,4> party;
 std::vector<actor> actors;
 std::optional<std::vector<objective>> objectives;
};
template<class Memory> class reader {
 Memory& m;
public:
 explicit reader(Memory& mem):m(mem){}
 template<class T> std::optional<T> get(address a) {
  T v{}; if(a<0x10000 || a>0x7fffffff-sizeof(T) || !m.read(a,&v,sizeof v))return {};
  if constexpr(std::is_floating_point_v<T>) {if(!std::isfinite(v))return {};}
  return v;
 }
 std::optional<std::string> text(address a,unsigned n) {
  std::string s;
  for(unsigned i=0;i<n;++i){auto c=get<unsigned char>(a+i);if(!c)return {};if(!*c)return s;s+=char(*c);}return {};
 }
 std::optional<std::string> name(address h) {
  if(!h)return std::string();
  if((h&0xffffff)>=8192)return {};
  auto o=get<address>(0xa0a824+(h&0xffffff)*4);
  if(!o || *o>0x1bff8)return {};
  return text(0xa12828+*o,128);
 }
 std::optional<bool> bit(address a,unsigned mask){auto b=get<unsigned char>(a);return b?std::optional<bool>((*b&mask)!=0):std::nullopt;}
 std::optional<std::string> speaker(address cs,address line) {
  auto key=get<address>(cs+0x4b0);auto root=get<address>(cs+8);
  if(!key||!*key||!root)return {};
  auto str=get<address>(*key);if(!str)return {};
  auto wanted=text(*str,128);if(!wanted)return {};
  address ix=*root;
  for(unsigned steps=0;steps<40 && ix<40;++steps) {
   auto ptr=get<address>(cs+0x1c+ix*20);if(!ptr)return {};
   auto nm=text(*ptr,128);if(!nm)return {};
   int cmp=lower(*wanted).compare(lower(*nm));
   if(!cmp) {
    auto file=get<address>(cs+0x32c+ix*4);if(!file||!*file)return {};
    auto mask=get<address>(*file+0x59c);if(!mask)return {};
    address k=line & *mask;if(k>=40 || get<address>(*file+0x4fc+k*4)!=line)return {};
    auto live=get<address>(*file+0x4f0+(k/32)*4);if(!live||!(*live&(1u<<(k%32))))return {};
    auto node=get<address>(*file+0x39c+k*4);if(!node||!*node)return {};
    auto sp=get<address>(*node+0x64);return sp?text(*sp,64):std::nullopt;
   }
   auto next=get<address>(cs+0x14+ix*20+(cmp>0?4:0));if(!next)return {};ix=*next;
  }
  return {};
 }
 snapshot read(std::uint64_t ms) {
  snapshot s; s.ms=ms;
  if(get<address>(0x729960)!=0x686e1cu || get<address>(0x72a578)!=0x68878cu)return s;
  s.zone=text(0x72a758,64);s.loading=bit(0x72a798,3);
  if(auto a=get<unsigned char>(0x729f40))s.act=*a;
  auto clock=get<float>(0x729d48);
  for(unsigned i=0;i<4;++i)if(auto h=get<address>(0x729974+4*i))s.party[i].name=name(*h);
  const auto cs=get<address>(0x717aac);
  if(cs && *cs && get<address>(*cs)==0x685e04u) {
   s.conversation=bit(*cs+0x21b24,2);s.line=get<address>(*cs+0x4bc);
   if(s.conversation==true && s.line)s.speaker=speaker(*cs,*s.line);
   if(auto n=get<short>(*cs+0x21b28);n && *n>=0 && *n<=256)s.responses=*n;
   if(auto n=get<short>(*cs+0x21b26);n && *n>=0 && *n<=256)s.selected=*n;
  }
  namespace f=frame_rate_rules;
  auto menus=get<address>(f::menu_manager_cell);
  std::optional<bool> movie;
  if(menus && *menus && get<address>(*menus)==0x6a236cu) {
   movie=bit(*menus+f::menu_flags,f::menu_movie_bit);
   auto count=get<int>(*menus+f::menu_stack_count); auto first=get<address>(*menus+f::menu_stack_first);
   auto cur=get<address>(*menus+f::menu_current);auto pending=get<unsigned char>(*menus+f::menu_pending);
   if(count && first && cur && pending)s.menu_open=*count>0 && *first && (*cur || *pending);
   // 0x5d8640: name is current menu+0xc, empty with no current menu.
   if(cur)s.menu=*cur?text(*cur+0xc,128):std::optional<std::string>("");
   if(cur && *cur && get<address>(*cur)==f::loading_menu_vtable)s.loading=true;
  }
  auto pop=get<address>(f::popups_cell);
  if(pop && *pop) {
   if(auto i=get<int>(*pop+f::popup_current)) {
    int ix=(*i>=0&&*i<3)?*i:0;
    s.popup=bit(*pop+f::popup_first+ix*f::popup_stride+f::popup_active,1);
   }
  }
  if(s.loading==true)s.mode="loading";
  else if(movie==true)s.mode="movie";
  else if(s.zone && (s.zone->empty() || s.zone->rfind("menu/",0)==0))s.mode="menu";
  else if(s.zone && movie && s.loading)s.mode="in-zone";
  // Enumerate existing entities only. No name lookup that interns strings or cached party queries.
  if(s.loading==false) for(unsigned i=0;i<256;++i) {
   auto bits=get<address>(0x778b70+0x818+4*(i/32));if(!bits||!(*bits&(1u<<(i%32))))continue;
   auto e=get<address>(0x778b74+4*i);auto h=get<address>(0x778b70+0x83c+4*i);
   if(!e||!*e||!h||get<address>(*e+0x1c)!=h)continue;
   auto vt=get<address>(*e);if(!vt||*vt<0x680000||*vt>0x6f0000)continue;
   auto fn=get<address>(*vt);if(!fn||*fn<0x401000||*fn>0x680000)continue;
   // Decode a constant metadata getter; never call an entity's function pointer.
   if(get<unsigned char>(*fn)!=0xb8 || get<unsigned char>(*fn+5)!=0xc3)continue;
   auto cls=get<address>(*fn+1);auto id=get<address>(0x70b840);if(!cls||!id||*id>512)continue;
   auto flags=get<address>(*cls+0x14+4*((*id+0x24)/32));if(!flags||!(*flags&(1u<<((*id+0x24)%32))))continue;
   actor a;a.id=*h;
   if(auto stats=get<address>(*e+0x35c);stats&&*stats)a.name=text(*stats+0x150,128);
   for(unsigned j=0;j<3;++j)a.pos[j]=get<float>(*e+0x20+4*j);
   a.health=get<float>(*e+0x27c);a.max_health=get<float>(*e+0x284);
   auto af=bit(*e+0x3d8,1);auto until=get<float>(*e+0x768);
   if(af&&until&&clock)a.ai=*af || *until<-.5f || *until>*clock;
   s.actors.push_back(a);
   for(auto& slot:s.party)if(slot.name && a.name && !slot.name->empty() && lower(*slot.name)==lower(*a.name)){auto saved=slot.name;slot=a;slot.name=saved;}
  }
  auto mission=get<address>(0x72b108);
  if(mission&&*mission&&get<address>(*mission)==0x6892d4u) {
   auto n=get<unsigned>(*mission+0x7d78);
   if(n&&*n<=75) {
    std::vector<objective> out;bool valid=true;
    for(unsigned i=0;i<*n;++i) {
     address rec=*mission+8+i*0x1ac;
     auto nm=text(rec,32);auto ix=get<unsigned char>(rec+0x1a9);auto goal=get<unsigned char>(rec+0x1aa);
     auto b=ix?get<unsigned char>(0x72b118+*ix):std::nullopt;
     if(!nm||!b||!goal){valid=false;break;}
     out.push_back({*nm,unsigned(*b&63),unsigned(*goal&63),bool(*b&64),!bool(*b&128)});
    }
    if(valid)s.objectives=out;
   }
  }
  return s;
 }
};
inline std::string actor_json(const actor& a) {
 return "{\"name\":"+json(a.name)+",\"entity_id\":"+std::to_string(a.id)+",\"x\":"+json(a.pos[0])+",\"y\":"+json(a.pos[1])+",\"z\":"+json(a.pos[2])+",\"health\":"+json(a.health)+",\"max_health\":"+json(a.max_health)+",\"alive\":"+json(a.alive())+",\"ai_controlled\":"+json(a.ai)+",\"z_velocity\":"+json(a.vz)+",\"on_floor\":null,\"airborne_ms\":null}";
}
inline std::string objectives_json(const snapshot& s) {
 std::string r="{\"schema\":1,\"sampled_ms\":"+std::to_string(s.ms)+",\"act\":"+json(s.act)+",\"objectives\":";
 if(!s.objectives)return r+"null}";
 r+='[';bool first=true;
 for(auto& o:*s.objectives){if(!first)r+=',';first=false;r+="{\"name\":"+quote(o.name)+",\"enabled\":"+(o.shown?"true":"false")+",\"shown\":"+(o.shown?"true":"false")+",\"complete\":"+(o.complete?"true":"false")+",\"count\":"+std::to_string(o.count)+",\"count_goal\":"+std::to_string(o.goal)+'}';}
 return r+"]}";
}
inline std::string state_json(const snapshot& s) {
 std::string r="{\"schema\":1,\"sampled_ms\":"+std::to_string(s.ms)+",\"mode\":"+json(s.mode)+",\"zone\":"+json(s.zone)+",\"act\":"+json(s.act)+",\"loading\":"+json(s.loading)+",\"menu\":"+json(s.menu)+",\"menu_open\":"+json(s.menu_open)+",\"popup\":"+json(s.popup)+",\"conversation\":{\"open\":"+json(s.conversation)+",\"speaker\":"+json(s.speaker)+",\"responses\":"+json(s.responses)+",\"selected\":"+json(s.selected)+",\"line_id\":"+json(s.line)+"},\"party\":[";
 for(unsigned i=0;i<4;++i){if(i)r+=',';r+=actor_json(s.party[i]);}
 r+="],\"actors\":[";bool first=true;for(auto& a:s.actors){if(!first)r+=',';first=false;r+=actor_json(a);}
 return r+"],\"script_errors_available\":false,\"events_source\":\"sampled\"}";
}
class tracker {
 std::optional<snapshot> last;
 std::deque<std::string> events;
 unsigned dropped=0;
public:
 void emit(const snapshot& s,const std::string& type,const std::string& detail="") {
  if(events.size()==512){events.pop_front();++dropped;}
  events.push_back("{\"ms\":"+std::to_string(s.ms)+",\"type\":"+quote(type)+",\"zone\":"+json(s.zone)+",\"detail\":"+quote(detail)+'}');
 }
 void observe(snapshot& s) {
  if(last) {
   auto edge=[&](auto old,auto now,const char* on,const char* off){if(old&&now&&old!=now)emit(s,*now?on:off);};
   edge(last->loading,s.loading,"zone_load_start","zone_load_end");
   if(last->zone&&s.zone&&last->zone!=s.zone)emit(s,"zone_changed");
   edge(last->conversation,s.conversation,"conversation_start","conversation_end");
   if(s.menu&&last->menu!=s.menu) {
    auto menu=*s.menu;for(auto& c:menu)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    emit(s,"menu_changed",menu);
    if(menu=="team_menu")emit(s,"team_menu_open");
    if(menu=="gameover" || menu=="game_over")emit(s,"game_over");
   }
   if(last->act==s.act&&last->objectives&&s.objectives&&last->objectives!=s.objectives)emit(s,"objectives_changed");
   for(unsigned i=0;i<4;++i) {
    auto& a=s.party[i];const auto& b=last->party[i];
    if(s.loading==false&&last->loading==false&&s.zone==last->zone&&a.id&&a.id==b.id) {
     if(b.alive() && a.alive() && b.alive()!=a.alive())emit(s,*a.alive()?"hero_revive":"hero_death",a.name.value_or(""));
     if(a.pos[2]&&b.pos[2]&&s.ms>last->ms&&s.ms-last->ms<=1000)a.vz=(*a.pos[2]-*b.pos[2])*1000.f/float(s.ms-last->ms);
    }
   }
  } else emit(s,"observer_started");
  last=s;
 }
 std::string drain() {
  std::string r="{\"schema\":1,\"dropped\":"+std::to_string(dropped)+",\"events\":[";bool first=true;
  for(auto& e:events){if(!first)r+=',';first=false;r+=e;}
  events.clear();dropped=0;return r+"]}";
 }
};
}
