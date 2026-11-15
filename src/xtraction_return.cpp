#include "xtraction_return.hpp"
#include "xtraction_return_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include "game_version.hpp"
#include "entity_capacity.hpp"
#include "log.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <intrin.h>

namespace xtraction_return {
namespace {
bool wanted=false,active=false;
thread_local unsigned depth=0;
unsigned refusals=0;
using restore_t=void(__fastcall*)(void*,void*);
using query_t=bool(__fastcall*)(void*,void*,const float*,float*,int,int);
using vec=std::array<float,3>;
template<class T>T field(const void* p,unsigned offset=0){T value{};std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof value);return value;}
template<class T>T entry(unsigned va){return reinterpret_cast<T>(va);}
bool finite(const vec& v){return std::all_of(v.begin(),v.end(),[](float f){return std::isfinite(f);});}
struct owned_query {
 void* value=nullptr;
 ~owned_query(){if(value)entry<void(__fastcall*)(void**,void*)>(0x4ab720)(&value,nullptr);}
};
struct contact {bool available=false,hit=false,world=false;vec point{},normal{};};
contact sweep(const vec& from,const vec& to,const vec& extent,const unsigned* exclusion){
 if(!finite(from)||!finite(to)||!finite(extent)||entry<int(__cdecl*)()>(0x4ab740)()<=0)return {};
 owned_query q;
 entry<void*(__fastcall*)(void**,void*,const unsigned*)>(0x4ab670)(&q.value,nullptr,exclusion);
 if(!q.value)return {};
 const auto* vt=field<const void*>(q.value);
 field<void(__fastcall*)(void*,void*,int,int)>(vt,0x40)(q.value,nullptr,1,1);
 field<void(__fastcall*)(void*,void*,const float*,const float*,const float*)>(vt,0x74)(q.value,nullptr,from.data(),to.data(),extent.data());
 void* record=nullptr;
 field<void*(__fastcall*)(void*,void*,void**)>(vt,0x20)(q.value,nullptr,&record);
 contact result;result.available=true;
 result.hit=entry<bool(__fastcall*)(void**,void*)>(0x4a9580)(&record,nullptr);
 if(result.hit){
  result.world=entry<bool(__fastcall*)(void**,void*)>(0x4a95c0)(&record,nullptr);
  const auto* point=entry<const float*(__fastcall*)(void**,void*)>(0x4a95f0)(&record,nullptr);
  const auto* normal=entry<const float*(__fastcall*)(void**,void*)>(0x4a9600)(&record,nullptr);
  if(!point||!normal)return {};
  // References belong to the query; copy through native accessors before release.
  std::memcpy(result.point.data(),point,12);std::memcpy(result.normal.data(),normal,12);
 }
 return result;
}
bool supported(const contact& c,const vec& position){
 if(!c.available||!c.hit||!c.world||!finite(c.point)||!finite(c.normal))return false;
 const float norm=c.normal[0]*c.normal[0]+c.normal[1]*c.normal[1]+c.normal[2]*c.normal[2];
 return std::isfinite(norm)&&norm>=.9f&&norm<=1.1f&&c.normal[2]>=.65f&&
  std::fabs(c.point[2]-position[2])<=xtraction_return_rules::floor_span;
}
bool unsupported_projection(const float* input,const float* output){
 const auto original=field<vec>(input),projected=field<vec>(output);
 if(!finite(original)||!finite(projected)||(original[0]==projected[0]&&original[1]==projected[1]))return false;
 const auto* actor=reinterpret_cast<const unsigned char*>(input)-0x20;
 const auto lo=field<vec>(actor,0x158),hi=field<vec>(actor,0x164);
 if(!finite(lo)||!finite(hi))return false;
 for(unsigned i=0;i<3;++i)if(lo[i]>=hi[i])return false;
 // The upward body sweep covers the standing volume from floor+2. A local
 // lower bound below the origin would extend beneath that certified volume.
 if(lo[2]<0)return false;
 vec size{};
 field<void*(__fastcall*)(const void*,void*,float*)>(field<const void*>(actor),0x28)(actor,nullptr,size.data());
 if(!finite(size)||size[0]<=0||size[1]<=0||size[2]<=0)return false;
 const vec extent{size[0],size[1],1.f};
 const unsigned exclusion=field<unsigned>(actor,0x1c);
 const auto floor_at=[&](const vec& p){return sweep({p[0],p[1],p[2]+24.f},{p[0],p[1],p[2]-24.f},extent,&exclusion);};
 const auto original_floor=floor_at(original);
 if(!supported(original_floor,original)||original_floor.point[2]+2.f>original[2]||original_floor.point[2]>original[2]+lo[2])return false;
 const auto projected_floor=floor_at(projected);
 // A hit on a dynamic object or malformed/steep contact cannot certify a gap.
 // Keep native behavior unless the equivalent floor sweep proves no hit.
 if(!projected_floor.available||projected_floor.hit)return false;
 const vec body_extent{(std::max)({size[0],std::fabs(lo[0]),std::fabs(hi[0])}),
                       (std::max)({size[1],std::fabs(lo[1]),std::fabs(hi[1])}),1.f};
 const float bottom=original_floor.point[2]+3.f,top=original[2]+hi[2];
 if(!std::isfinite(bottom)||!std::isfinite(top)||top<=bottom)return false;
 const auto body=sweep({original[0],original[1],bottom},{original[0],original[1],top},body_extent,&exclusion);
 return body.available&&!body.hit;
}
void __fastcall restore(void* snapshot,void*) {
 ++depth;
 __try {reinterpret_cast<restore_t>(xtraction_return_rules::restore_actors)(snapshot,nullptr);}
 __finally {--depth;}
}
bool __fastcall query(void* self,void*,const float* input,float* output,int a,int b) {
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 const bool found=reinterpret_cast<query_t>(xtraction_return_rules::nav_query)(self,nullptr,input,output,a,b);
 bool accepted=xtraction_return_rules::accept(depth!=0,found,input,output);
 if(accepted&&depth&&caller==xtraction_return_rules::floor_nav_caller&&unsupported_projection(input,output))accepted=false;
 if(found&&!accepted&&refusals++<32)
  logger::write("xtraction return: refused NAV projection %.9g %.9g %.9g -> %.9g %.9g %.9g; native floor trace keeps the requested position",
   double(input[0]),double(input[1]),double(input[2]),double(output[0]),double(output[1]),double(output[2]));
 return accepted;
}
std::vector<unsigned char> pointer(const void* p) {
 const auto value=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p));
 std::vector<unsigned char> out(4);std::memcpy(out.data(),&value,4);return out;
}
}
bool install(HMODULE game,bool enabled) {
 wanted=enabled;if(!enabled)return true;if(active)return true;
 if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base)return false;
 for(const auto& g:xtraction_return_rules::guards)
  if(!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va),g.hex)) {
   logger::write("xtraction return: guard differs at 0x%08lX (%s); native placement retained",g.va,g.what);return false;
  }
 for(const auto& g:xtraction_return_rules::query_guards){
  const auto* code=reinterpret_cast<const unsigned char*>(g.va);
  const auto owner=entity_capacity::installed_target(g.va);
  bool matches=false;
  if(owner){
   const auto relative=field<unsigned>(code,1);
   matches=code[0]==0xe9&&g.va+5+relative==owner;
  }else matches=limits_rules::matches(code,g.hex);
  if(!matches){logger::write("xtraction return: query guard differs at 0x%08lX (%s); native placement retained",g.va,g.what);return false;}
 }
 using namespace xtraction_return_rules;
 std::vector<unsigned char> call(5);call[0]=0xe8;
 const auto relative=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&restore)-(restore_call+5));
 std::memcpy(call.data()+1,&relative,4);
 xml1_hook_transaction::memory memory;bool safe=true;
 if(!xml1_hook_transaction::apply(memory,{
  {restore_call,{0xe8,0xf7,0x4d,0xe9,0xff},call},
  {nav_slot,{0x00,0x43,0x49,0x00},pointer(&query)}},&safe)) {
  if(!safe)game_version::refuse_startup();
  logger::write("xtraction return: transaction refused; rollback %s",safe?"verified":"unverified; startup refused");return false;
 }
 active=true;
 logger::write("xtraction return: guarded side-return NAV filter installed with XtractionShop; out-of-span projections and certified floor gaps retain native input fallback");
 return true;
}
bool verified(){return !wanted||active;}
}
