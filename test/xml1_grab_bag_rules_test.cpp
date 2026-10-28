// Independent semantic oracle transcribed from owned XBE 0x17c480.
#include "xml1_grab_bag_rules.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace {
using xml1_economy_rules::record;
using xml1_grab_bag_rules::course;
int checks=0,failures=0;
void check(bool b,const char* s){++checks;if(!b){++failures;std::printf("FAIL %s\n",s);}}
struct stream {
 std::vector<double> script;
 unsigned pos=0,state=1;
 bool scripted=false;
 double unit(){if(scripted){if(pos>=script.size()){std::puts("FAIL script exhausted");std::exit(2);}return script[pos++];}++pos;state=state*214013u+2531011u;return double(state&0x7fffffffu)/2147483648.0;}
};
struct services:stream {
 std::set<unsigned> owned;
 std::vector<course> courses;
 bool obtained(const record& r){return owned.count(r.definition)!=0;}
};
struct answer {int item=-1,lesson=-1;unsigned draws=0,passes=0;};
// This uses an indexed program-counter style walk, independent of the
// production template. XBE debits are deliberately omitted under owner D6.
answer oracle(const std::vector<record>& rows,int level,services& s){
 answer a;
 bool rare=s.unit()<double(.33f);
 int again=0;
restart:
 ++a.passes;
 unsigned ordinal=1;
 float chance=1.f;
 int pick=-1;
 for(unsigned i=0;i<rows.size();++i){
  const auto& r=rows[i];
  if(!(r.flags&0x40)||bool(r.flags&2)!=rare)continue;
  if((r.flags&1)&&s.owned.count(r.definition))continue;
  if(r.group.value_or(-1)!=std::min(5,level/10+1))continue;
  if(s.unit()<=double(chance))pick=int(i);
  ++ordinal;chance=float(1.0/double(ordinal));
 }
 const bool course_roll=s.unit()<double(.1f);
 if(course_roll||(pick<0&&rare)){
  for(unsigned i=0;i<s.courses.size();++i){const auto& c=s.courses[i];
   if(!(c.flags&1)&&c.available&&int(c.level)<level-5){a.lesson=int(i);goto done;}
  }
 }
 if(pick>=0){a.item=pick;goto done;}
 if(!again){again=1;rare=false;goto restart;}
done:
 a.draws=s.pos;return a;
}
record row(unsigned def,int group,unsigned flags){record r;r.definition=def;r.group=group;r.flags=flags;return r;}
void differential(const std::vector<record>& rows,int level,services base,const char* label){
 auto s=base,o=base;const auto expected=oracle(rows,level,o);
 const auto got=xml1_grab_bag_rules::x1_grab_bag(rows,level,s);
 const int selected=got.item?int(got.item-rows.data()):-1;
 check(selected==expected.item&&got.course_index==expected.lesson&&got.draws==expected.draws&&s.pos==o.pos&&got.passes==expected.passes,label);
}
void script(const std::vector<record>& rows,int level,std::vector<double> draws,std::vector<course> courses,int item,int lesson,unsigned passes,const char* label){
 services s;s.scripted=true;s.script=draws;s.courses=courses;
 differential(rows,level,s,label);
 const auto got=xml1_grab_bag_rules::x1_grab_bag(rows,level,s);
 check((got.item?int(got.item-rows.data()):-1)==item&&got.course_index==lesson&&got.draws==draws.size()&&s.pos==draws.size()&&got.passes==passes,label);
}
}
int main(){
 const std::vector<record> rows{row(160,3,0x42),row(161,3,0x40),row(162,1,0x40),row(163,3,0x43),row(164,3,0)};
 const std::vector<course> lessons{{1,1,1},{15,1,0},{14,0,0},{14,0x100,0}};
 script(rows,20,{0,.9,.9,.9},{},0,-1,1,"flagged pool, definition order");
 script(rows,20,{.9,.9,.9},{},1,-1,1,"ordinary exact-group equipment only");
 script(rows,20,{.9,.9,0},lessons,-1,3,1,"course first eligible, raw strict level and word flag");
 script(rows,20,{0,.9,.5,.9},{},3,-1,1,"reservoir equality accepted");
 script(rows,20,{.33f,.9,.1f},lessons,1,-1,1,"pool and course equality fail strict gates");
 script(rows,20,{std::nextafter(double(.33f),0.0),.9,.9,.9},{},0,-1,1,"pool random retains precision below float threshold");
 script(rows,20,{.9,.9,std::nextafter(double(.1f),0.0)},lessons,-1,3,1,"course random retains precision below float threshold");
 script(rows,20,{0,.9,std::nextafter(.5,1.0),.9},{},0,-1,1,"reservoir random above equality is not rounded down to float");
 script({row(200,3,0x40)},20,{0,.9,.9,.9},{},0,-1,2,"retry ordinary skips pool draw, repeats course draw");
 script({},20,{0,.9},lessons,-1,3,1,"empty flagged pool searches course after failed draw");
 script({},20,{.9,.9,0},lessons,-1,3,2,"retry course success");
 script({},20,{0,.9,.9},{},-1,-1,2,"empty after retry, no refund");
 script({},20,{.9,.9,.9},lessons,-1,-1,2,"empty ordinary pool does not force course search");
 services owned;owned.scripted=true;owned.script={0,.9,.9};owned.owned={163};
 differential(rows,20,owned,"obtained unique excluded without draw");
 script({row(201,3,0x40),row(202,3,0x40),row(203,3,0x40)},20,{.9,.9,.9,double(1.f/3.f),.9},{},2,-1,1,"float reciprocal equality third eligible");
 unsigned long long total=0;
 for(unsigned seed=1;seed<=100000;++seed){
  services s;s.state=seed;s.courses=lessons;if(seed&1)s.owned.insert(163);
  const int level=int(seed%51);auto o=s;const auto a=oracle(rows,level,o);total+=a.draws;
  differential(rows,level,s,"100000-case XBE differential");
 }
 std::printf("T-BAG rules: %d checks, 100000 corpus bags, %llu draws/oracle, %d failure(s)\n",checks,total,failures);
 return failures?1:0;
}
