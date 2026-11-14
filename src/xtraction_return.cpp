#include "xtraction_return.hpp"
#include "xtraction_return_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include <cstring>

namespace xtraction_return {
namespace {
bool wanted=false,active=false;
thread_local unsigned depth=0;
unsigned refusals=0;
using restore_t=void(__fastcall*)(void*,void*);
using query_t=bool(__fastcall*)(void*,void*,const float*,float*,int,int);
void __fastcall restore(void* snapshot,void*) {
 ++depth;
 __try {reinterpret_cast<restore_t>(xtraction_return_rules::restore_actors)(snapshot,nullptr);}
 __finally {--depth;}
}
bool __fastcall query(void* self,void*,const float* input,float* output,int a,int b) {
 const bool found=reinterpret_cast<query_t>(xtraction_return_rules::nav_query)(self,nullptr,input,output,a,b);
 const bool accepted=xtraction_return_rules::accept(depth!=0,found,input,output);
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
 logger::write("xtraction return: guarded side-return NAV filter installed with XtractionShop; a projection beyond the native +/-24 floor trace keeps its requested position");
 return true;
}
bool verified(){return !wanted||active;}
}
