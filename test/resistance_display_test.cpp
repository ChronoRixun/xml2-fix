#include "resistance_display_rules.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <limits>

namespace R=resistance_display_rules;
namespace
{
 struct query
 {
  std::map<std::pair<int,unsigned>,R::range> values;
  int calls=0;
  R::range operator()(int attribute,unsigned mask,int mode)
  {
   ++calls;
   assert((attribute==39 || attribute==56) ? mode==1 : mode==0);
   auto it=values.find({attribute,mask});
   return it==values.end() ? R::range{false,mode==1 ? 1.f : 0.f,mode==1 ? 1.f : 0.f} : it->second;
  }
 };
 struct text
 {
  std::string operator()(int format,int word)
  {
   if (!word) {assert(format==103);return "many channels";}
   assert(word>=210 && word<=212);
   return std::string(format==201 ? "blocked " : "reduced ")+std::to_string(word);
  }
 };
 struct memory
 {
  std::map<std::uint32_t,unsigned char> bytes;
  int writes=0,fail_write=0;bool bad_readback=false,fail_flush=false;
  bool read(std::uint32_t a,void* out,std::size_t n)
  {
   for(std::size_t i=0;i<n;++i) {if(!bytes.contains(a+std::uint32_t(i)))return false;static_cast<unsigned char*>(out)[i]=bytes[a+std::uint32_t(i)];}
   return true;
  }
  bool write(std::uint32_t a,const void* in,std::size_t n)
  {
   if(++writes==fail_write)return false;
   for(std::size_t i=0;i<n;++i)bytes[a+std::uint32_t(i)]=static_cast<const unsigned char*>(in)[i];
   if(bad_readback){bad_readback=false;bytes[a]^=1;}
   return true;
  }
  bool acquire(std::uint32_t,std::uint32_t& old){old=1;return true;}
  bool restore(std::uint32_t,std::uint32_t old){return old==1;}
  bool flush(){if(fail_flush){fail_flush=false;return false;}return true;}
 };
}
int main()
{
 assert(R::parse("")==R::choice::off && R::parse(" xml2 ; stock")==R::choice::off);
 assert(R::parse(" XmL1 ")==R::choice::xml1 && R::parse("bad")==R::choice::invalid);
 query q;text t;
 assert(R::render(R::sample(q),t).empty() && q.calls==9);
 // A scoped physical reduction and an energy immunity have distinct labels,
 // colors and order. Native resistance bytes need not be present.
 q.values[{56,0x80}]={true,.2f,.2f};q.values[{56,0x8000}]={true,0,0};
 assert(R::render(R::sample(q),t)=="~22reduced 210\n~24blocked 212\n");
 // Removal/expiry is observed on the next sample, with no retained actor state.
 q.values.erase({56,0x80});
 assert(R::render(R::sample(q),t)=="~24blocked 212\n");
 // Unscoped original modifiers match all three scopes, as in the XBE.
 q.values.clear();for(const auto& s:R::scopes)q.values[{39,s.mask}]={true,.5f,.5f};
 assert(R::render(R::sample(q),t)=="many channels");
 // Existing PC gear contributes once, without applying another powerup.
 q.values.clear();q.values[{13,0x80}]={true,.75f,.75f};
 assert(R::render(R::sample(q),t)=="~22reduced 210\n");
 q.values[{56,0x80}]={true,.5f,.5f};auto values=R::sample(q);
 assert(values[0].low==.125f && values[0].high==.125f);
 // Ranged affecters are classified only when both bounds have the same
 // meaning. The renderer never samples RNG to decide a label.
 assert(R::classify({true,.2f,.9f})==R::label::resistant);
 assert(R::classify({true,-1,0})==R::label::immune);
 assert(R::classify({true,.2f,1.2f})==R::label::none);
 assert(R::classify({true,0,.5f})==R::label::none);
 assert(R::classify({true,std::numeric_limits<float>::quiet_NaN(),1})==R::label::none);
 // Every displaced byte is guarded. Failed writes, readback and flush restore
 // the entire original loop; no partially installed HUD bridge is published.
 const auto plan=R::patches(reinterpret_cast<void*>(0x12345678));
 auto retail=[&]{memory m;for(const auto& p:plan)for(std::size_t i=0;i<p.before.size();++i)m.bytes[p.address+std::uint32_t(i)]=p.before[i];return m;};
 for(std::size_t i=0;i<plan[0].before.size();++i)
 {
  auto m=retail();m.bytes[R::site+std::uint32_t(i)]^=1;const auto before=m.bytes;bool safe=false;
  assert(!xml1_hook_transaction::apply(m,plan,&safe) && safe && m.bytes==before && m.writes==0);
 }
 for(int failure=0;failure<3;++failure)
 {
  auto m=retail();const auto before=m.bytes;m.fail_write=failure==0 ? 1 : 0;m.bad_readback=failure==1;m.fail_flush=failure==2;bool safe=false;
  assert(!xml1_hook_transaction::apply(m,plan,&safe) && safe && m.bytes==before);
 }
 auto m=retail();bool safe=false;assert(xml1_hook_transaction::apply(m,plan,&safe) && safe);
 for(std::size_t i=0;i<plan[0].after.size();++i)assert(m.bytes[R::site+std::uint32_t(i)]==plan[0].after[i]);
 std::cout<<"resistance display: scopes, removal, ranges, labels and transactional guards PASS\n";
}
