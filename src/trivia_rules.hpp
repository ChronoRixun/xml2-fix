#pragma once
// The native question cursor table has room for acts 1..5 at CGame+0x5e1..0x5e5.
// Its unbounded accessors (0x46a690/0x46a6a0) use +0x5e0+act, so acts 8/9
// overlap the answered bitmap at +0x5e8. Keep cursors outside the native stream;
// reconstruct a cold-loaded cursor from the permanent answered identities.
#include <map>
namespace trivia_rules
{
 class cursors
 {
  std::map<int,int> positions;
 public:
  void clear() { positions.clear(); }
  template<typename Answered> int get(int act,int count,Answered answered)
  {
   if(act<1 || count<1 || count>50) return 0;
   const auto found=positions.find(act);
   if(found!=positions.end()) return found->second;
   int first=0;
   while(first<count && answered(first)) ++first;
   if(first==count) first=0;
   positions[act]=first;
   return first;
  }
  void set(int act,int count,int question)
  {
   if(act<1 || count<1 || count>50 || question<0 || question>count) return;
   positions[act]=question;
  }
 };
}
