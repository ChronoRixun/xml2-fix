#pragma once
#include "xml1_economy_rules.hpp"
#include <algorithm>

namespace xml1_grab_bag_rules {
struct course {unsigned char level=0;unsigned short available=0;unsigned char flags=0;};
struct result {
 const xml1_economy_rules::record* item=nullptr;
 int course_index=-1;
 unsigned draws=0,passes=0;
};
// Ordered resolved rows, synchronized unit() stream and snapshotted native
// state. Construction/awards and the caller's single debit are separate.
template<class Services>
result x1_grab_bag(const std::vector<xml1_economy_rules::record>& rows,int highest_level,Services& s){
 result out;
 auto unit=[&]{++out.draws;return static_cast<double>(s.unit());};
 bool flagged=unit()<.33f;
 const int group=std::min(5,highest_level/10+1);
 for(unsigned pass=0;pass<2;++pass){
  ++out.passes;
  const xml1_economy_rules::record* selected=nullptr;
  unsigned eligible=0;
  for(const auto& r:rows){
   if(!(r.flags&0x40)||bool(r.flags&2)!=flagged||!r.group||*r.group!=group)continue;
   if((r.flags&1)&&s.obtained(r))continue;
   ++eligible;
   // XBE 0x17c5d9-0x17c5e5 accepts equality; the reciprocal is
   // stored binary32 at 0x17c5fa, as in x1_roll's reservoir.
   const float chance=static_cast<float>(1.0/static_cast<double>(eligible));
   if(unit()<=chance)selected=&r;
  }
  const bool course_roll=unit()<.1f; // always, including empty pools/retry
  if(course_roll||(flagged&&!selected)){
   for(unsigned i=0;i<s.courses.size();++i){const auto& c=s.courses[i];
    if(!(c.flags&1)&&c.available&&int(c.level)<highest_level-5){out.course_index=static_cast<int>(i);return out;}
   }
  }
  if(selected){out.item=selected;return out;}
  flagged=false; // retry has no pool draw and cannot debit
 }
 return out;
}
}
