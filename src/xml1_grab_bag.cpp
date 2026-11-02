#include "xml1_grab_bag.hpp"
#include "xml1_grab_bag_rules.hpp"
#include "xml1_economy.hpp"
#include "xml1_economy_digest.hpp"
#include "item_capacity_rules.hpp"
#include <cstdio>
#include <cstring>
#include <xmmintrin.h>

namespace xml1_economy::grab_bag {
namespace {
bool applied=false;
template<class T>T native(unsigned va){return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
template<class T>T read(const void* p,unsigned off=0){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>T method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/4]);}
struct selection_precision {
 unsigned short saved=0;
 unsigned sse=0;
 selection_precision(){
  unsigned short current=0;__asm fnstcw current
  saved=current;sse=_mm_getcsr();
  const unsigned short selected=static_cast<unsigned short>((current&~0x0f00u)|0x0200u);
  __asm fldcw selected
  _mm_setcsr(sse&~0x6000u);
 }
 ~selection_precision(){const unsigned short current=saved;__asm fldcw current
  _mm_setcsr(sse);
 }
};
struct services {
 std::array<bool,1024> owned{};
 std::vector<xml1_grab_bag_rules::course> courses;
 // Same x87 return ABI and synchronized stream as x1_roll. No premature
 // binary32 rounding of the random result, even with incoming PC24 mode.
 double unit(){return native<double(__cdecl*)(char)>(0x560190)(1);}
 bool obtained(const xml1_economy_rules::record& r){return owned[r.definition];}
};
const char* interned(unsigned id){
 if(!id)return "";
 auto* names=native<unsigned char*(__cdecl*)()>(0x602140)();
 return reinterpret_cast<const char*>(names+0x8008+read<unsigned>(names,4+(id&0xffffff)*4));
}
struct position {float x=0,y=0,z=0;};
}
bool installed(){return applied;}
bool __fastcall x1_grab_bag(void* shop,void*){
 if(!xml1_economy::installed())return false;
 auto* manager=native<void*(__cdecl*)()>(0x480a00)();
 auto* roster=native<void*(__cdecl*)()>(0x44b8f0)();
 const int highest=method<int(__fastcall*)(void*,void*)>(roster,0x50)(roster,nullptr);
 services s;
 // Snapshot native state before precision changes. Only the lockstep RNG is
 // called inside the selection scope; all getters/awards use incoming controls.
 for(const auto& r:records())if(r.flags&1){
  auto* def=static_cast<unsigned char*>(manager)+item_capacity_rules::definitions+r.definition*0x3c;
  s.owned[r.definition]=method<bool(__fastcall*)(void*,void*)>(def,0x54)(def,nullptr);
 }
 const unsigned count=native<unsigned char(__cdecl*)()>(0x4c8bf0)();
 std::vector<unsigned> course_ids;course_ids.reserve(count);s.courses.reserve(count);
 for(unsigned i=0;i<count;++i){auto* c=native<void*(__cdecl*)(unsigned)>(0x4c9c10)(i);
  s.courses.push_back({read<unsigned char>(c),read<unsigned short>(c,0x4c),read<unsigned char>(c,0x4f)});
  course_ids.push_back(read<unsigned>(c,0x44));
 }
 xml1_grab_bag_rules::result result;
 {selection_precision precision;result=xml1_grab_bag_rules::x1_grab_bag(records(),highest,s);}
 if(result.course_index>=0){
  char command[256]{};
  std::snprintf(command,sizeof command,"loadDangerRoomCourse('%s')",interned(course_ids[static_cast<unsigned>(result.course_index)]));
  auto* scripts=native<void*(__cdecl*)()>(0x4a1670)();
  method<void(__fastcall*)(void*,void*,const char*)>(scripts,0xc)(scripts,nullptr,command);
  scripts=native<void*(__cdecl*)()>(0x4a1670)();
  method<void(__fastcall*)(void*,void*,const char*,int)>(scripts,0x3c)(scripts,nullptr,command,1);
  digest::bag_result("bag-course");return true;
 }
 if(!result.item){digest::bag_result("bag-empty");return false;} // native caller ignores failure; no refund
 auto* def=static_cast<unsigned char*>(manager)+item_capacity_rules::definitions+result.item->definition*0x3c;
 descriptor item{};make_instance(manager,nullptr,&item,def,1,0,0,0,0);
 if(!item.pool||!method<bool(__fastcall*)(void*,void*,unsigned)>(item.pool,0)(item.pool,nullptr,item.handle)){digest::bag_result("bag-capacity-refused");return false;}
 // Exactly the native bag's vt+0xec inventory insertion and vt+0x140
 // notification arguments. Construction is the agreed plan A/accepted fallback.
 method<bool(__fastcall*)(void*,void*,descriptor,int,int)>(manager,0xec)(manager,nullptr,item,1,2);
 if(result.item->flags&1)native<void(__fastcall*)(void*,void*,int)>(0x47ac50)(def,nullptr,1);
 method<void(__fastcall*)(void*,void*,descriptor,int,position,int)>(manager,0x140)(manager,nullptr,item,1,position{},1);
 native<void(__fastcall*)(void*,void*,int)>(0x5d35d0)(shop,nullptr,1);
 native<void(__fastcall*)(void*,void*,unsigned,descriptor*)>(0x5d2460)(shop,nullptr,read<unsigned>(native<void*>(0x6e6fa0)),&item);
 const unsigned refresh=0;
 method<void(__fastcall*)(void*,void*,const unsigned*)>(shop,0x40)(shop,nullptr,&refresh);
 digest::bag_result("bag-equipment");return true;
}
bool contribute(patch_transaction::transaction& tx,std::string& failure){
 patch_transaction::contribution c;c.owner="Xml1Economy Grab Bag reward (single caller debit)";
 c.guards.push_back({0x5d3720,patch_transaction::bytes_of("81ec1c010000"),"complete bag reward entry before vt+0x138"});
 if(!patch_transaction::verify_expectations(c,failure))return false;
 c.writes.push_back(patch_transaction::relocation(0x5d3720,"e9",true,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&x1_grab_bag)),1,"XML1 Grab Bag with no internal debit"));
 c.publish=[]{applied=true;};c.abandon=[]{applied=false;};c.retain=[]{applied=false;};
 if(!tx.add(std::move(c))){failure="Grab Bag startup transaction sealed";return false;}
 return true;
}
}
