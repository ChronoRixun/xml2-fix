#include "trivia_rules.hpp"
#include <array>
#include <cstdio>
int main()
{
 int failures=0;
 auto check=[&](bool value,const char* name){if(!value){++failures;std::printf("FAIL %s\n",name);}};
 trivia_rules::cursors c;
 std::array<unsigned,8> words{};
 auto answered=[&](int q){return (words[(42+q)/32]&(1u<<((42+q)%32)))!=0;};
 check(c.get(9,8,answered)==0,"fresh late-act cursor");
 words[1]|=1u<<10;
 c.set(9,8,1);
 check(c.get(9,8,answered)==1 && words[0]==0 && words[1]==1024,"late cursor cannot overwrite native bitmap");
 c.set(8,8,4);
 check(c.get(8,8,[](int){return false;})==4 && c.get(9,8,answered)==1,"act cursors are independent");
 c.clear();
 check(c.get(9,8,answered)==1,"cold load derives first unanswered question");
 c.set(9,8,8);check(c.get(9,8,answered)==8,"native end-of-list sentinel survives until menu wraps it");
 c.set(9,8,9);check(c.get(9,8,answered)==8,"invalid cursor is refused");
 c.clear();check(c.get(9,8,[](int){return true;})==0,"all answered questions remain browsable without rewards");
 c.clear();check(c.get(9,8,[](int q){return q!=2;})==2,"first unanswered identity determines resume position");
 check(c.get(0,8,answered)==0 && c.get(9,51,answered)==0,"invalid act and question capacity cannot index memory");
 std::printf("trivia cursors: %d failures\n",failures);return failures?1:0;
}
