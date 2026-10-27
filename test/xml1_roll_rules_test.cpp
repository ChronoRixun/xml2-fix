#include "xml1_roll_rules.hpp"
#include "test_input_rules.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {
using xml1_economy_rules::record;
using xml1_roll_rules::input;
int checks=0, failures=0;
void check(bool value,const char* message){++checks;if(!value){++failures;if(failures<20)std::printf("FAIL %s\n",message);}}
struct stream {
 std::uint32_t state=1; std::vector<double> script; unsigned cursor=0,draws=0;
 std::uint32_t obtained_mask=0; bool full=false,precision24=false;
 std::vector<std::pair<int,int>> ranges;
 // Native Park-Miller state transition and the unrounded x87 ST0 result.
 // 31 state bits scaled by 2^-31 are all exactly representable in double.
 double unit(){++draws;double value;if(cursor<script.size())value=script[cursor++];else{state=static_cast<std::uint32_t>((static_cast<std::uint64_t>(state)*16807u)%2147483647u);value=static_cast<double>(state)*0x1p-31;}return precision24?static_cast<double>(static_cast<float>(value)):value;}
 int integer(int low,int high){ranges.emplace_back(low,high);double product=unit()*static_cast<double>(high-low+1);if(precision24)product=static_cast<float>(product);double sum=product+low;if(precision24)sum=static_cast<float>(sum);return static_cast<int>(sum);}
 bool obtained(const record& r)const{return (obtained_mask&(1u<<(r.definition%32)))!=0;}
 bool xtreme_full()const{return full;}
};
struct oracle_drop {int row=-1;std::string name;int amount=0;};
struct oracle_result {std::vector<oracle_drop> drops;int group=0,bucket=0;unsigned draws=0,eligible=0;};

// Independent register/control-flow transcription of XBE 0x70fb0, re-read with
// inspect_binary.py. It intentionally does not call production helpers. The
// source's byte index limit is removed by the port's ordered sidecar contract.
oracle_result oracle(const std::vector<record>& table,input call,stream& random,bool counterfactual_precision24=false){
 oracle_result out;out.group=std::min(5,call.highest_level/10+1);
 const unsigned initial_draws=random.draws;
 const float gate[]={0.0f,0.15f,0.30f,0.45f,0.65f,1.0f};
 if(!call.leader && random.unit()>gate[call.category]){out.draws=random.draws-initial_draws;return out;}
 const float roll=static_cast<float>(random.unit()); // fstp dword at 0x71134 only
 const float cut[]={0.005f,0.02f,0.07f,0.65f};
 for(int i=0;i!=4;++i)if(roll<cut[i]){out.bucket=i+1;break;}
 int repetitions=1;
 if(call.leader){
  if(out.bucket!=1 && out.bucket!=2){const double next=random.unit();out.bucket=next<0.1f?2:next<0.2f?3:4;}
  repetitions=4;
 }
 if(out.bucket>0 && call.highest_level<5 && out.bucket<3)out.bucket=3;
 int picked=-1,n=1;
 for(int index=0;index<static_cast<int>(table.size());++index){
  const auto& item=table[index];
  if(!item.group || (*item.group!=1 && (*item.group<=0 || *item.group<out.group-2 || *item.group>out.group)))continue;
  if(!item.frequency || *item.frequency!=out.bucket)continue;
  if((item.flags&1) && random.obtained(item))continue;
  float reciprocal=static_cast<float>(1.0/static_cast<double>(n));
  if(random.unit()<=reciprocal)picked=index;
  ++n;++out.eligible;
 }
 bool money=picked<0;std::string name=money?"money1":table[picked].name;
 if(money && random.unit()<0.4f){name="XTREME_PIP";money=false;}
 if(name=="XTREME_PIP" && random.xtreme_full())money=true;
 if(money){
  int value;
  if(call.actor_level){
   const int level=*call.actor_level;
   double product=random.unit()*static_cast<double>(level);
   if(counterfactual_precision24)product=static_cast<float>(product);
   product*=-10.0;
   if(counterfactual_precision24)product=static_cast<float>(product);
   value=level-static_cast<int>(product);
  }else{
   const int squared=out.group*out.group;
   double product=random.unit()*static_cast<double>(squared);
   if(counterfactual_precision24)product=static_cast<float>(product);
   product*=25.0;
   if(counterfactual_precision24)product=static_cast<float>(product);
   value=squared+static_cast<int>(product);
  }
  name="MONEY"+std::to_string(random.integer(1,3));
  repetitions=std::min(random.integer(2,4),value);
  if(repetitions<=0){out.draws=random.draws-initial_draws;return out;}
  value/=repetitions;
  out.drops.push_back({-1,name,value});
  for(int index=1;index<repetitions;++index){
   if(call.leader){const int v=random.integer(0,random.xtreme_full()?1:3);out.drops.push_back({-1,v==0?"HEALTH_ITEM":v==1?"ENERGY_ITEM":"XTREME_PIP",1});}
   else out.drops.push_back({-1,name,value});
  }
 }else{
  out.drops.push_back({picked,name,1});
  for(int index=1;index<repetitions;++index){const int v=random.integer(0,random.xtreme_full()?1:3);out.drops.push_back({-1,v==0?"HEALTH_ITEM":v==1?"ENERGY_ITEM":"XTREME_PIP",1});}
 }
 out.draws=random.draws-initial_draws;return out;
}

std::vector<record> fixture(){
 std::vector<record> rows;
 for(int group=-1;group<=5;++group)for(int frequency=1;frequency<=5;++frequency)for(int unique=0;unique<2;++unique){
  record r;r.definition=static_cast<unsigned>(rows.size()+100);r.name="item_"+std::to_string(r.definition);r.group=group;r.frequency=frequency;r.flags=0x40u+static_cast<unsigned>(unique);rows.push_back(r);
 }
 record missing;missing.name="absent_fields";missing.definition=999;rows.push_back(missing);
 return rows;
}
std::pair<unsigned,unsigned> compare(const std::vector<record>& rows,input in,stream rng){
 stream other=rng;
 auto actual=xml1_roll_rules::x1_roll(rows,in,rng);auto expected=oracle(rows,in,other);
 check(actual.count==expected.drops.size(),"differential drop count");
 check(actual.G==expected.group && actual.bucket==expected.bucket,"differential G and bucket");
 check(actual.draws==expected.draws && rng.draws==other.draws && rng.ranges==other.ranges,"differential draw totals and integer ranges");
 check(actual.eligible==expected.eligible,"differential eligible count");
 for(unsigned i=0;i<std::min<unsigned>(actual.count,static_cast<unsigned>(expected.drops.size()));++i){
  const auto& a=actual.drops[i];const auto& e=expected.drops[i];
  check(a.name==e.name && a.amount==e.amount,"differential exact name and money integer");
  check(a.row==(e.row<0?nullptr:&rows[e.row]),"differential selected definition");
  check(a.definition==(e.row<0?0:rows[e.row].definition),"differential definition index");
 }
 return {actual.draws,expected.draws};
}
void boundaries(){
 const std::vector<record> empty;
 input in;in.actor_level=1;stream s;
 // Reachable native state: raw U exceeds .15f, but binary32 would round
 // it down to .15f and accidentally pass the inclusive outer gate.
 in.category=1;s.state=309741327;
 auto raw=s.unit();check(s.state==322122561u && raw>static_cast<double>(.15f) && static_cast<float>(raw)==.15f,"reachable raw31 outer gate rounding witness");
 s={};s.state=309741327;auto native_gate=xml1_roll_rules::x1_roll(empty,in,s);
 check(native_gate.count==0 && native_gate.draws==1,"raw31 outer gate rejects value above .15f");
 // Reachable upper endpoint remains below one in ST0, while binary32
 // would become one. The native actor result is 109, not 110.
 s={};s.state=739806647;raw=s.unit();
 check(s.state==2147483646u && raw<1.0 && static_cast<float>(raw)==1.0f,"reachable raw31 upper endpoint witness");
 in.category=5;in.actor_level=10;s={};s.state=739806647;s.full=true;s.script={0,.9,.9};
 auto native_money=xml1_roll_rules::x1_roll(empty,in,s);
 check(native_money.count>0 && native_money.drops[0].amount==109/static_cast<int>(native_money.count),"reachable native state drives actor money amount109");
 // The first unscripted draw is the amount; following draws choose the
 // money visual and split. Force split2 in a separate equivalent tape.
 s={};s.full=true;s.script={0,.9,.9,raw,0,0};native_money=xml1_roll_rules::x1_roll(empty,in,s);
 check(native_money.count==2 && native_money.drops[0].amount==54 && native_money.drops[1].amount==54,"raw31 actor L10 gives 109 before split2, not rounded110");
 // Test-local negative control: unnormalized PC24 arithmetic would differ.
 // Production always selects under XML1's CRT-initialized 53-bit precision.
 s={};s.precision24=true;s.full=true;s.script={0,.9,.9,raw,0,0};auto negative=oracle(empty,in,s,true);
 check(negative.drops.size()==2 && negative.drops[0].amount==55,"negative control: unnormalized CW24 money110 split2 gives55");
 in.actor_level=1;
 s={};s.script={0,0.9f,0.5f,0,0,0};in.category=0;
 auto r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.count==1 && r.drops[0].name=="MONEY1" && r.drops[0].amount==1,"category zero exact zero passes XBE outer <= gate");
 s={};s.script={std::nextafter(0.0f,1.0f)};
 check(xml1_roll_rules::x1_roll(empty,in,s).count==0 && s.draws==1,"category zero above zero fails");
 const float gates[]={0,.15f,.30f,.45f,.65f,1};
 for(int category=0;category<6;++category)for(float v:{std::nextafter(gates[category],0.0f),gates[category],std::nextafter(gates[category],1.0f)}){in.category=category;s={};s.script={v,.9f,.5f,0,0,0};compare(empty,in,s);}
 auto rows=fixture();in.category=5;in.highest_level=40;in.leader=false;
 for(float boundary:{.005f,.02f,.07f,.65f})for(float v:{std::nextafter(boundary,0.0f),boundary,std::nextafter(boundary,1.0f)}){s={};s.script={0,v};compare(rows,in,s);}
 // The third candidate at a stored float 1/3 is selected on equality.
 std::vector<record> three(3);for(unsigned i=0;i<3;++i){three[i].name="r"+std::to_string(i);three[i].definition=i+1;three[i].group=1;three[i].frequency=3;}
 in.highest_level=5;s={};s.script={0,.03f,0,1,1.0f/3.0f};r=xml1_roll_rules::x1_roll(three,in,s);
 check(r.count==1 && r.drops[0].definition==3 && r.draws==5,"reservoir equality at float reciprocal selects third candidate");
 s={};s.script={0,.03f,0,.99,static_cast<double>(1.0f/3.0f)+0x1p-31};r=xml1_roll_rules::x1_roll(three,in,s);
 check(r.drops[0].definition==1,"reservoir compares raw31 U above float reciprocal without narrowing it");
 in.leader=true;s={};s.script={.9,static_cast<double>(.1f)-0x1p-31};r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.bucket==2,"leader raw31 value below .1f remains below despite float-rounding equality");
 in.leader=false;s={};s.script={0,.9,static_cast<double>(.4f)-0x1p-31};r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.count==1 && r.drops[0].name=="XTREME_PIP","fallback compares raw31 U below .4f without narrowing it");
 s={};s.script={0,static_cast<double>(.02f)-0x1p-31};r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.bucket==3,"bucket alone rounds raw31 U tofloat before threshold comparison");
 for(float b:{.1f,.2f})for(float v:{std::nextafter(b,0.0f),b,std::nextafter(b,1.0f)}){in.leader=true;s={};s.script={.9f,v};compare(rows,in,s);}
 for(float v:{std::nextafter(.4f,0.0f),.4f,std::nextafter(.4f,1.0f)}){in.leader=false;s={};s.script={0,.9f,v};compare(empty,in,s);}
 // First leader money is split before the leader loop. Count is overwritten,
 // not unconditionally four, and later pickups are packs with amount one.
 in.leader=true;in.actor_level=1;s={};s.full=true;s.script={.9f,.9f,.9f,0,0,.99f,0};r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.count==1 && r.drops[0].amount==1 && r.draws==6,"leader money amount one reduces total leader count to one");
 in.actor_level=10;s={};s.full=true;s.script={.9f,.9f,.9f,0,0,0,0};r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.count==2 && r.drops[0].amount==5 && r.drops[1].name=="HEALTH_ITEM" && r.drops[1].amount==1,"leader money split count two, first amount five, second pack one");
 in.leader=false;
 in.actor_level=32767;s={};s.full=true;s.script={0,.9f,.9f,.021189f,0,0};r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.count==2 && r.drops[0].amount==19854 && r.drops[1].amount==19854,"actor x87 product truncates before split; binary32 intermediates would produce 19855");
 in.actor_level.reset();in.highest_level=1;s={};s.full=true;s.script={0,.9f,.9f,.04f,0,0};r=xml1_roll_rules::x1_roll(empty,in,s);
 check(r.count==1 && r.drops[0].amount==1,"nonactor x87 .04f times 25 stays below one at ftol");
 s={};s.precision24=true;s.full=true;s.script={0,.9f,.9f,.04f,0,0};negative=oracle(empty,in,s,true);
 check(negative.drops.size()==2 && negative.drops[0].amount==1,"negative control: unnormalized CW24 nonactor total2");
 // Definitions beyond the original byte-index ceiling must still participate
 // in the port. Preserve order and one draw per eligible row, including the
 // probability-one first candidate.
 std::vector<record> wide(300);
 for(unsigned i=0;i<wide.size();++i){wide[i].definition=i+10;wide[i].name="wide_"+std::to_string(i);wide[i].group=1;wide[i].frequency=3;}
 in.highest_level=5;s={};s.script.assign(302,0);s.script[1]=.03f;r=xml1_roll_rules::x1_roll(wide,in,s);
 check(r.eligible==300 && r.draws==302 && r.drops[0].definition==309,"all 300 ordered definitions visited and zero reservoir draws select last");
 in.actor_level=1;
 for(int level:{1,2,5,9,10,49,50,32767})for(int numerator:{1,2,3,9}){
  in.actor_level=level;const float edge=static_cast<float>(numerator)/static_cast<float>(10*level);
  for(float v:{std::nextafter(edge,0.0f),edge,std::nextafter(edge,1.0f),std::nextafter(1.0f,0.0f)}){s={};s.full=true;s.script={0,.9f,.9f,v,.75f,.75f};compare(empty,in,s);}
 }
 for(int highest:{1,4,5,9,10,19,20,29,30,39,40,49,50}){in.highest_level=highest;in.actor_level.reset();for(float v:{0.0f,.04f,.2f,.33333334f,std::nextafter(1.0f,0.0f)}){s={};s.full=true;s.script={0,.9f,.9f,v,0,.99f};compare(empty,in,s);}}
 // Explicitly protect fold boundary and unique filtering, including absent
 // optional fields, group -1/0, group1 universal and upper/lower group edges.
 for(int highest:{4,5}){in.highest_level=highest;s={};s.script={0,0};r=xml1_roll_rules::x1_roll(rows,in,s);check(r.bucket==(highest==4?3:1),"roster fold edge 4/5");}
 in.highest_level=30;in.leader=false;
 for(std::uint32_t mask:{0u,0xffffffffu,0x55555555u}){s={};s.obtained_mask=mask;s.script={0,.01f};compare(rows,in,s);}
}
}
int main(){
 check(test_input_rules::parse_command("economy rolls").what==test_input_rules::command::kind::economy&&test_input_rules::parse_command("economy rolls").text=="rolls","test-pipe economy roll history command parsed");
 check(test_input_rules::parse_command("economy status").text=="status","test-pipe economy status command parsed");
 boundaries();const auto rows=fixture();
 std::uint64_t production_draws=0,oracle_draws=0;
 for(unsigned n=0;n<100000;++n){
  input in;in.category=static_cast<int>(n%6);in.leader=((n/6)%2)!=0;in.highest_level=1+static_cast<int>((n/12)%50);
  // Native fixtures exercise the incoming-CW guard separately. This corpus
  // receives the normalized raw31 stream specified by XML1's CRT.
  if((n/600)%2)in.actor_level=1+static_cast<int>((n*17u)%50);
  stream s;s.state=static_cast<std::uint32_t>((static_cast<std::uint64_t>(0x9e3779b9u)*(n+1))%2147483646u)+1;s.full=((n/1200)%2)!=0;s.obtained_mask=(n/2400)%3==0?0:(n/2400)%3==1?0xffffffffu:(n*0x45d9f3bu);
  const auto totals=compare(rows,in,s);production_draws+=totals.first;oracle_draws+=totals.second;
 }
 check(production_draws==oracle_draws,"aggregate corpus random draws agree");
 std::printf("xml1_roll_rules: %d checks, %d failures; 100000 differential rolls\n",checks,failures);
 std::printf("100000-roll normalized53 corpus draws: production=%llu oracle=%llu (boundary tests and spawn placement excluded)\n",static_cast<unsigned long long>(production_draws),static_cast<unsigned long long>(oracle_draws));
 return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
