#include <WinSock2.h>
#include "gameplay_agreement.hpp"
#include "gameplay_agreement_rules.hpp"
#include "conversations_rules.hpp"
#include <tuple>
#include "game_version.hpp"
#include "game_version_rules.hpp"
#include "explosive_first_hit.hpp"
#include "npc_power.hpp"
#include "stun_rule.hpp"
#include "world_impact.hpp"
#include "world_impact_rules.hpp"
#include "xml1_capability_rules.hpp"
#include "script_functions_rules.hpp"
#include <cstdio>
#include <string>
#include <cstdarg>
#include <utility>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include <cwchar>

namespace ini {
 std::optional<std::string> impact;
 bool requested=false,lc=true,key=true,testing_forge=false,testing_hotkeys=false;
 bool flag(const wchar_t* section,const wchar_t* name,bool){
  if(std::wcscmp(section,L"TestMode")==0)return std::wcscmp(name,L"ForgeEverywhere")==0 ? testing_forge : std::wcscmp(name,L"Hotkeys")==0 && testing_hotkeys;
  return requested;
 }
 std::optional<std::string> text(const wchar_t* section,const wchar_t* name){
  if(std::wcscmp(section,L"Save")==0)return lc ? std::optional<std::string>("lc") : std::nullopt;
  if(std::wcscmp(section,L"Online")==0)return lc ? std::optional<std::string>("LC24") : std::nullopt;
  if(std::wcscmp(name,L"ObjectWorldImpact")==0)return impact;
  if(std::wcscmp(name,L"ExplosiveFirstHit")==0 && key)return requested ? "1" : "0";
  return std::nullopt;
 }
}
namespace logger {
 std::vector<std::string> logged;
 void write(const char* format,...){va_list a;va_start(a,format);char b[1024];std::vsnprintf(b,sizeof(b),format,a);va_end(a);logged.emplace_back(b);}
 void write_once(std::string_view,const char*,...){}
}
bool has_log(std::string_view text){for(const auto& l:logger::logged)if(l.find(text)!=std::string::npos)return true;return false;}
namespace iat_hook {
 void* captured=nullptr;bool fail=false;int calls=0;
 SOCKET WSAAPI fake_socket(int,int,int){++calls;return 73;}
 void* hook(HMODULE,const char*,const char*,WORD,void* replacement){captured=replacement;return fail ? nullptr : reinterpret_cast<void*>(&fake_socket);}
}
#pragma section(".peerfix",read)
__declspec(allocate(".peerfix")) std::uint8_t fixture_space[0x400000]{};
using namespace gameplay_agreement_rules;
using gameplay_capabilities::record;
using gameplay_capabilities::result;
int failures=0;
void check(bool value,const char* message){std::printf("%s %s\n",value ? "PASS" : "FAIL",message);failures+=!value;}
void check(bool value,const std::string& message){check(value,message.c_str());}
void* at(std::uint32_t address){return reinterpret_cast<void*>(address);}
void writable(std::uint32_t address,std::size_t size){DWORD old;VirtualProtect(at(address),size,PAGE_EXECUTE_READWRITE,&old);}
void jump(std::uint32_t site,const void* target){writable(site,5);auto* code=static_cast<std::uint8_t*>(at(site));code[0]=0xe9;const auto delta=std::int32_t(reinterpret_cast<std::uintptr_t>(target)-site-5);std::memcpy(code+1,&delta,4);}
void ret(std::uint32_t site){writable(site,1);*static_cast<std::uint8_t*>(at(site))=0xc3;}
std::vector<std::uint8_t> binary;
std::size_t file_offset(std::uint32_t va){
 auto u16=[](std::size_t o){std::uint16_t n;std::memcpy(&n,binary.data()+o,2);return n;};
 auto u32=[](std::size_t o){std::uint32_t n;std::memcpy(&n,binary.data()+o,4);return n;};
 const auto pe=u32(0x3c);const auto sections=pe+24+u16(pe+20);
 for(unsigned i=0;i<u16(pe+6);++i){const auto row=sections+i*40;const auto rva=u32(row+12);if(va-0x400000>=rva && va-0x400000<rva+u32(row+16))return u32(row+20)+va-0x400000-rva;}
 return binary.size();
}
void native_bytes(std::uint32_t va,std::size_t n){const auto offset=file_offset(va);if(offset+n>binary.size()){++failures;return;}writable(va,n);std::memcpy(at(va),binary.data()+offset,n);}
template<std::size_t N> void guards(const std::array<limits_rules::guard,N>& list){for(const auto&g:list)native_bytes(g.va,g.hex.size()/2);}
bool rx(std::uint32_t page){MEMORY_BASIC_INFORMATION info{};return VirtualQuery(at(page),&info,sizeof(info)) && info.Protect==PAGE_EXECUTE_READ;}
void make_rx(){for(auto page:{0x607000u,0x608000u,0x609000u}){DWORD old;VirtualProtect(at(page),4096,PAGE_EXECUTE_READ,&old);}}
record peer(bool on,bool verified=true){record r;r.add(2,1,{on ? 1u : 0u,0,0,0},verified);r.finalize();return r;}
void records(){
 auto on=peer(true),off=peer(false),host_failure=peer(false,false),client_failure=peer(true,false);
 check(on.compare(off.bytes())!=result::match,"host on/client off refuses");
 check(off.compare(on.bytes())!=result::match,"host off/client on refuses");
 check(host_failure.compare(on.bytes())==result::unavailable && on.compare(host_failure.bytes())!=result::match,"failed host install refuses both directions");
 check(client_failure.compare(on.bytes())==result::unavailable && on.compare(client_failure.bytes())!=result::match,"failed client install refuses both directions");
 check(on.compare(peer(true).bytes())==result::match,"both installed on accept");
 check(off.compare(peer(false).bytes())==result::match,"both verified off accept");
 record native;native.finalize();check(native.bytes().size()==108 && native.count()==1 && native.rule(0).values==std::array<std::uint32_t,4>{32,224,256,8},"default record contains explicit retail entity layout");
 auto bytes=std::vector<std::uint8_t>(off.bytes().begin(),off.bytes().end());
 auto reset=[&]{bytes.assign(off.bytes().begin(),off.bytes().end());};
 check(bytes[0]=='G' && bytes[1]=='C' && bytes[2]=='A' && bytes[3]=='P' && bytes[4]==4 && bytes[5]==0 && bytes[6]==2 && bytes[8]==108 && bytes[9]==0 && bytes[10]==0 && bytes[11]==0,"protocol 4 header: GCAP, revision 4, two rules, 108 bytes, reserved zero");
 bytes[4]=5;check(off.compare(bytes)==result::protocol,"unknown record revision refuses");reset();bytes[4]=1;check(off.compare(bytes)==result::protocol,"protocol revision 1 peers (0.2.0i and older) refuse");reset();
 bytes[4]=2;check(off.compare(bytes)==result::protocol,"protocol revision 2 peers (1.4.0, the 1.5 pre-release) refuse");reset();
 bytes[4]=3;check(off.compare(bytes)==result::protocol,"protocol revision 3 cannot restore fresh-tail provenance and refuses");reset();
 bytes[0]='X';check(off.compare(bytes)==result::protocol,"a foreign tag refuses");reset();
 bytes[12]^=1;check(off.compare(bytes)==result::value,"one changed digest bit refuses");reset();
 bytes[6]=3;bytes[12]^=1;check(off.compare(bytes)==result::entries,"another rule count refuses");reset();
 bytes[6]=3;check(off.compare(bytes)==result::malformed,"an equal digest with another rule count is not a record this protocol writes");reset();
 bytes[6]=0;check(off.compare(bytes)==result::malformed,"a zero rule count is malformed");reset();
 bytes[8]--;check(off.compare(bytes)==result::malformed,"invalid declared record length refuses");reset();
 bytes[10]=1;check(off.compare(bytes)==result::malformed,"unknown reserved header flags refuse");reset();
 bytes[44+2]^=0x10;check(off.compare(bytes)==result::malformed,"an equal digest with another rule summary is not a record this protocol writes");reset();
 bytes.push_back(0);check(off.compare(bytes)==result::malformed,"an oversized record refuses");reset();
 bytes.pop_back();check(off.compare(bytes)==result::malformed,"a truncated record refuses");reset();
 check(off.compare({})==result::malformed && off.compare({bytes.data(),11})==result::malformed,"missing or short old-peer record refuses");
 // A protocol-2 record (what 1.4.0 and the 1.5 pre-release send): GCAP, revision 2, the full entry list.
 {std::vector<std::uint8_t> v2{'G','C','A','P',2,0,2,0,52,0,0,0};v2.resize(52);check(off.compare(v2)==result::protocol,"a full protocol-2 record is another protocol, never compared entry by entry");}
 record layout;layout.layout(2,{64,448,512,16},true);layout.add(2,1,{0,0,0,0},true);layout.finalize();check(off.compare(layout.bytes())==result::value,"different entity/token layout revision refuses");
 record capacity;capacity.layout(1,{64,192,256,8},true);capacity.add(2,1,{0,0,0,0},true);capacity.finalize();check(off.compare(capacity.bytes())==result::value,"different effective capacity refuses");
 record token;token.layout(1,{32,224,256,16},true);token.add(2,1,{0,0,0,0},true);token.finalize();check(off.compare(token.bytes())==result::value,"different token width refuses");
 record revised;revised.add(2,2,{0,0,0,0},true);revised.finalize();check(off.compare(revised.bytes())==result::value,"another rule revision refuses (protocol 3 folds revision into value: both are different gameplay settings)");
 record missing;missing.add(3,1,{1,0,0,0},true);missing.add(2,1,{0,0,0,0},true);missing.finalize();check(off.compare(missing.bytes())==result::entries,"missing controller entry refuses");
 // Rule 9, the death threshold ([Game] DeathRule): a peer that kills below 1 and one that kills at 0 never play together.
 auto death=[](bool on,bool verified=true){record r;r.add(2,1,{1,0,0,0},true);r.add(gameplay_capabilities::death_threshold_id,1,{on ? 1u : 0u,0,0,0},verified);r.finalize();return r;};
 check(gameplay_capabilities::death_threshold_id==9 && death(true).compare(death(false).bytes())==result::value && death(false).compare(death(true).bytes())==result::value,"death threshold on/off refuses both directions");
 check(death(true).compare(death(true).bytes())==result::match && death(false).compare(death(false).bytes())==result::match,"same death threshold accepts");
 check(death(true,false).compare(death(true).bytes())==result::unavailable && peer(true).compare(death(false).bytes())==result::entries,"failed death threshold install or a peer without the rule refuses");
 // Rule 17 is optional, named, and canonical with rule 18 in either order.
 auto impact=[](bool requested,bool installed,bool verified){record r;if(requested)r.add(gameplay_capabilities::object_world_impact_id,1,{installed ? 1u : 0u,0,0,0},verified);r.finalize();return r;};
 const auto impact_on=impact(true,true,true),impact_off=impact(false,false,true);
 check(impact_on.compare(impact(true,true,true).bytes())==result::match,"matching ObjectWorldImpact accepts");
 check(std::equal(impact_off.bytes().begin(),impact_off.bytes().end(),native.bytes().begin(),native.bytes().end()),"ObjectWorldImpact OFF digest remains byte-identical");
 gameplay_capabilities::difference impact_diff;char impact_name[128];
 check(impact_on.compare(impact_off.bytes(),&impact_diff)==result::entries && impact_off.compare(impact_on.bytes())==result::entries,"ObjectWorldImpact on/off refuses both directions");
 impact_on.describe(impact_diff.buckets,impact_diff.local_only,false,impact_name,sizeof(impact_name));
 check(std::string(impact_name)=="ObjectWorldImpact (not on the other PC)","refusal names ObjectWorldImpact and explains the absent peer rule");
 impact_off.describe(impact_diff.buckets,0,false,impact_name,sizeof(impact_name));
 check(std::string(impact_name)=="ObjectWorldImpact (disabled on this PC)","an OFF peer still names the known optional rule without registering it");
 native.describe(std::uint64_t(1)<<gameplay_capabilities::bucket_of(18),0,false,impact_name,sizeof(impact_name));
 check(std::string(impact_name)=="StunRule (disabled on this PC)","an OFF peer names StunRule without changing its released digest");
 check(!impact(true,false,false).ready(),"failed ObjectWorldImpact cannot advertise a valid record");
 auto stun=[](bool on,bool verified=true,int rev=stun_rule::revision,bool with17=false){
  record r;if(on || !verified)r.add(gameplay_capabilities::stun_rule_id,static_cast<std::uint16_t>(rev),{on ? 1u : 0u,0,0,0},verified);
  if(with17)r.add(17,1,{1,0,0,0},true);r.finalize();return r;};
 check(gameplay_capabilities::stun_rule_id==18 && std::string(gameplay_capabilities::rule_name(18))=="StunRule","stun uses assigned rule 18");
 check(stun(true).compare(stun(false).bytes())==result::entries && stun(false).compare(stun(true).bytes())==result::entries,"stun on/off refuses both directions");
 check(stun(true).compare(stun(true).bytes())==result::match && stun(false).compare(stun(false).bytes())==result::match,"same stun rules agree, including off");
 check(stun(true).compare(stun(true,true,1).bytes())==result::value && stun(true,true,1).compare(stun(true).bytes())==result::value,"stun revision 1 mismatch refuses both directions");
 check(!stun(true,false).ready(),"failed stun install poisons agreement");
 {
  // XML2 Fix 1.7.0 shipped StunRule revision 2: with 1.7.1 (revision 3) the two PCs must refuse each other, naming rule 18.
  gameplay_capabilities::difference rev2;char rev2_name[160];
  check(stun_rule::revision==3,"StunRule is revision 3");
  check(stun(true).compare(stun(true,true,2).bytes(),&rev2)==result::value && stun(true,true,2).compare(stun(true).bytes())==result::value,"released StunRule revision 2 peer refuses both directions");
  stun(true).describe(rev2.buckets,rev2.local_only,true,rev2_name,sizeof(rev2_name));
  check(std::string(rev2_name).find("18 StunRule")!=std::string::npos,std::string("the revision 2 refusal names the rule: ")+rev2_name);
 }
 {
  // Rule 10: XML2 Fix 1.5.0 to 1.8.0-beta.7 registered RevivalCost=xml1 as revision 2 and charged 200 * level^2; 1.8.0
  // charges the first game's 200 * G^2 as revision 3 (same values), so the two refuse each other naming rule 10.
  // Without the key the entry keeps the released revision 2.
  auto services=[](std::uint16_t rev,std::array<std::uint32_t,4> v){record r;r.add(gameplay_capabilities::xtraction_services_id,rev,v,true);r.finalize();return r;};
  const std::array<std::uint32_t,4> lc{1,0x3f,1,1},shop{1,0x3f,1,0};
  const auto fixed=services(gameplay_capabilities::xtraction_services_revival_revision,lc),old=services(2,lc);
  gameplay_capabilities::difference d;char name[160];
  check(gameplay_capabilities::xtraction_services_revision==2 && gameplay_capabilities::xtraction_services_revival_revision==3,"rule 10: released revision 2, revival revision 3");
  check(fixed.compare(old.bytes(),&d)==result::value && old.compare(fixed.bytes())==result::value,"rule 10 revival revision 3 and the 200 * level^2 revision 2 refuse both directions");
  fixed.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name).find("10 Xtraction services/RevivalCost")!=std::string::npos,std::string("the revival refusal names rule 10: ")+name);
  check(services(2,shop).compare(services(gameplay_capabilities::xtraction_services_revision,shop).bytes())==result::match,"rule 10 without RevivalCost keeps the released revision 2");
 }
 record future;future.add(17,1,{1,0,0,0},true);future.add(18,stun_rule::revision,{1,0,0,0},true);future.finalize();
 check(future.compare(stun(true,true,stun_rule::revision,true).bytes())==result::match,"later rule 17 registration agrees in either order");
 check(stun(true).compare(future.bytes())==result::entries,"missing future rule 17 is refused, not silently ignored");
 // New requested-only rules preserve the pre-integration OFF bytes and name both mismatch directions.
 for(const auto id:{gameplay_capabilities::danger_room_party_id,gameplay_capabilities::lockon_aim_id,gameplay_capabilities::box_harm_origin_id,gameplay_capabilities::danger_room_healthbars_id,gameplay_capabilities::harm_single_chain_id,gameplay_capabilities::online_conversations_id,gameplay_capabilities::model_pools_id,gameplay_capabilities::tick_rate_id,gameplay_capabilities::world_backfaces_id}) {
  auto optional=[&](bool wanted,bool verified=true){record r;if(wanted)r.add(id,1,{1,0,0,0},verified);r.finalize();return r;};
  const auto yes=optional(true),no=optional(false),failed=optional(true,false);
  check(yes.compare(optional(true).bytes())==result::match,"matching new requested-only rule accepts");
  check(std::equal(no.bytes().begin(),no.bytes().end(),native.bytes().begin(),native.bytes().end()),"new rule OFF record is byte-identical to base");
  gameplay_capabilities::difference d;char name[160];
  check(yes.compare(no.bytes(),&d)==result::entries && no.compare(yes.bytes())==result::entries,"new rule on/off refuses in both directions");
  yes.describe(d.buckets,d.local_only,false,name,sizeof(name));
  check(std::string(name)==std::string(gameplay_capabilities::rule_name(id))+" (not on the other PC)","enabled peer names missing new rule");
  no.describe(d.buckets,0,false,name,sizeof(name));
  check(std::string(name)==std::string(gameplay_capabilities::rule_name(id))+" (disabled on this PC)","disabled peer names known new rule");
  check(!failed.ready() && failed.compare(yes.bytes())==result::unavailable && yes.compare(failed.bytes())!=result::match,"failed new request closes host/join");
 }
 // [Online] FastTransfer is rule 24: an unassigned (0) ID is refused by add(); the assigned rule is checked in both mismatch
 // directions and for its record values.
 {
  record unassigned;check(!unassigned.add(0,1,{1,32,300,64},true),"a rule without an assigned ID cannot join a record");
  if constexpr(gameplay_capabilities::fast_transfer_id!=0) {
   const auto id=gameplay_capabilities::fast_transfer_id;
   auto fast=[&](bool wanted,std::uint32_t window=32){record r;if(wanted)r.add(id,gameplay_capabilities::fast_transfer_revision,{1,window,300,64},true);r.finalize();return r;};
   const auto on=fast(true),off=fast(false);
   check(on.compare(fast(true).bytes())==result::match,"FastTransfer ON peers agree");
   check(std::equal(off.bytes().begin(),off.bytes().end(),native.bytes().begin(),native.bytes().end()),"FastTransfer OFF record is byte-identical to base");
   gameplay_capabilities::difference d;char name[160];
   check(on.compare(off.bytes(),&d)==result::entries && off.compare(on.bytes())==result::entries,"FastTransfer ON/OFF refuses in both directions");
   on.describe(d.buckets,d.local_only,true,name,sizeof(name));
   check(std::string(name)==std::to_string(id)+" FastTransfer (not on the other PC)","the ON peer names FastTransfer: "+std::string(name));
   off.describe(d.buckets,0,true,name,sizeof(name));
   check(std::string(name)==std::to_string(id)+" FastTransfer (disabled on this PC)","the OFF peer names FastTransfer: "+std::string(name));
   check(on.compare(fast(true,5).bytes())==result::value,"a different FastTransfer window refuses");
  }
 }
 // Rule 27, [Game] OnlineConversations=lockstep: requested only, revision 1, {lockstep, AutoAdvance, ReplyVoices, 0}. A peer
 // without it (1.6.0, or the key off) or with another conversation decision refuses; OFF keeps the released bytes.
 {
  const auto id=gameplay_capabilities::online_conversations_id;
  check(id==27 && std::string(gameplay_capabilities::rule_name(id))=="OnlineConversations","online conversations use assigned rule 27");
  auto talk=[&](bool wanted,bool auto_advance=true,bool reply_voices=true){record r;if(wanted)r.add(id,1,conversations_rules::online_record(true,auto_advance,reply_voices),true);r.finalize();return r;};
  const auto on=talk(true),off=talk(false);
  check(on.compare(talk(true).bytes())==result::match,"OnlineConversations ON peers agree");
  check(std::equal(off.bytes().begin(),off.bytes().end(),native.bytes().begin(),native.bytes().end()),"OnlineConversations OFF record is byte-identical to base");
  check(on.compare(off.bytes())==result::entries && off.compare(on.bytes())==result::entries,"OnlineConversations ON/OFF refuses in both directions");
  check(on.compare(talk(true,false).bytes())==result::value && on.compare(talk(true,true,false).bytes())==result::value,"another AutoAdvance/ReplyVoices state refuses");
  gameplay_capabilities::difference d;char name[160];on.compare(off.bytes(),&d);on.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="27 OnlineConversations (not on the other PC)","the ON peer names rule 27: "+std::string(name));
 }
 // Rule 28, [Engine] ModelPools: requested only, revision 1, {installed, model handles, IGB models, actors}. XML2 Fix
 // 1.8.0-beta.1 (no rule 28) and a beta.2 peer with a raise refuse each other; x2 and x4 refuse; OFF keeps the released bytes.
 {
  const auto id=gameplay_capabilities::model_pools_id;
  check(id==28 && std::string(gameplay_capabilities::rule_name(id))=="ModelPools","model pools use assigned rule 28");
  check(gameplay_capabilities::bucket_of(id)!=gameplay_capabilities::bucket_of(gameplay_capabilities::online_conversations_id),"rule 28 has a bucket of its own");
  auto pools=[&](bool wanted,std::uint32_t handles=4096,std::uint32_t igb=2048,std::uint32_t actors=128){record r;if(wanted)r.add(id,1,{1,handles,igb,actors},true);r.finalize();return r;};
  const auto x4=pools(true),beta1=pools(false),x2=pools(true,2048,1024,64);
  check(x4.compare(pools(true).bytes())==result::match,"ModelPools x4 peers agree");
  check(x4.compare(beta1.bytes())==result::entries && beta1.compare(x4.bytes())==result::entries,"ModelPools x4 and a peer without rule 28 refuse both ways");
  check(x4.compare(x2.bytes())==result::value && x2.compare(x4.bytes())==result::value,"ModelPools x4 and x2 refuse both ways");
  gameplay_capabilities::difference d;char name[160];x4.compare(beta1.bytes(),&d);x4.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="28 ModelPools (not on the other PC)","the x4 peer names rule 28: "+std::string(name));
  beta1.compare(x4.bytes(),&d);beta1.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="28 ModelPools (disabled on this PC)","a beta.2 peer with the pools off names rule 28: "+std::string(name));
  x4.compare(x2.bytes(),&d);x4.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="28 ModelPools","x4 against x2 names rule 28: "+std::string(name));
 }
 // Rule 29, [Online] TickRate: {rate in effect, 0, 0, 0} whenever the key is there, revision 1. A 60 and a 30 PC refuse
 // by name both ways; a PC without the key (the game's 30, no entry) and a 60 PC too; absent keeps the released bytes.
 {
  const auto id=gameplay_capabilities::tick_rate_id;
  check(id==29 && std::string(gameplay_capabilities::rule_name(id))=="TickRate","the tick rate uses assigned rule 29");
  check(gameplay_capabilities::bucket_of(id)!=gameplay_capabilities::bucket_of(gameplay_capabilities::model_pools_id),"rule 29 has a bucket of its own");
  auto tick=[&](unsigned rate){record r;r.add(gameplay_capabilities::model_pools_id,1,{1,4096,2048,128},true);if(rate)r.add(id,1,{rate,0,0,0},true);r.finalize();return r;};
  const auto sixty=tick(60),thirty=tick(30),beta2=tick(0);
  check(sixty.compare(tick(60).bytes())==result::match && thirty.compare(tick(30).bytes())==result::match,"same tick rates agree");
  check(sixty.compare(thirty.bytes())==result::value && thirty.compare(sixty.bytes())==result::value,"60 and 30 refuse both ways");
  check(sixty.compare(beta2.bytes())==result::entries && beta2.compare(sixty.bytes())==result::entries,"60 and a PC without rule 29 refuse both ways");
  check(thirty.compare(beta2.bytes())==result::entries,"an explicit 30 and a PC without the key refuse (other builds)");
  gameplay_capabilities::difference d;char name[160];
  sixty.compare(thirty.bytes(),&d);sixty.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="29 TickRate","60 against 30 names rule 29: "+std::string(name));
  thirty.compare(sixty.bytes(),&d);thirty.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="29 TickRate","30 against 60 names rule 29: "+std::string(name));
  sixty.compare(beta2.bytes(),&d);sixty.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="29 TickRate (not on the other PC)","60 against a beta.2 peer: "+std::string(name));
  beta2.compare(sixty.bytes(),&d);beta2.describe(d.buckets,d.local_only,true,name,sizeof(name));
  check(std::string(name)=="29 TickRate (disabled on this PC)","a beta.3 peer without the key against 60: "+std::string(name));
 }
 // Rule 16, the NPC power rule ([Game] NpcPowerRule): enemies that pay for powers and decide every 2 s never share a
 // lockstep game with enemies that don't; revision 3 adds the visibility word; absent is {0,0,0,0}, a failed install unverified.
 auto npc=[](bool on,bool verified=true){record r;r.add(2,1,{1,0,0,0},true);r.add(gameplay_capabilities::npc_power_rule_id,npc_power::revision_for(on || !verified),{on ? 1u : 0u,on ? 1u : 0u,0,0},verified);r.finalize();return r;};
 check(gameplay_capabilities::npc_power_rule_id==16 && npc_power::revision==11 && npc(true).compare(npc(false).bytes())==result::value && npc(false).compare(npc(true).bytes())==result::value,"NPC power rule on/off refuses both directions");
 check(npc(true).compare(npc(true).bytes())==result::match && npc(false).compare(npc(false).bytes())==result::match,"same NPC power rule accepts");
 record npc_previous; npc_previous.add(2,1,{1,0,0,0},true);npc_previous.add(gameplay_capabilities::npc_power_rule_id,2,{1,0,0,0},true);npc_previous.finalize();
 check(npc(true).compare(npc_previous.bytes())!=result::match && npc_previous.compare(npc(true).bytes())!=result::match,"NPC visibility policy rejects the previous power/combo-only revision both ways");
 record npc_visibility; npc_visibility.add(2,1,{1,0,0,0},true);npc_visibility.add(gameplay_capabilities::npc_power_rule_id,3,{1,1,0,0},true);npc_visibility.finalize();
 check(npc(true).compare(npc_visibility.bytes())!=result::match && npc_visibility.compare(npc(true).bytes())!=result::match,"NPC combo timing rejects revision 3 peers both ways");
 record npc_timers; npc_timers.add(2,1,{1,0,0,0},true);npc_timers.add(gameplay_capabilities::npc_power_rule_id,4,{1,1,0,0},true);npc_timers.finalize();
 check(npc(true).compare(npc_timers.bytes())!=result::match && npc_timers.compare(npc(true).bytes())!=result::match,"NPC reaction-spent combo steps reject revision 4 peers both ways");
 // Revisions 5-9 were unreleased candidates (5: the first reaction-spent build and a withdrawn role experiment; 6-9:
 // officer weapon candidates, 9 with words 2-3 set). Revisions 10 and 11 carry both and refuse every one of them.
 for(const std::uint16_t candidate:{5,6,7,8,9})for(const auto words:{std::array<std::uint32_t,4>{1,1,0,0},std::array<std::uint32_t,4>{1,1,1,0x6BED0A4Cu}}){
  record peer;peer.add(2,1,{1,0,0,0},true);peer.add(gameplay_capabilities::npc_power_rule_id,candidate,words,true);peer.finalize();
  check(npc(true).compare(peer.bytes())!=result::match && peer.compare(npc(true).bytes())!=result::match,"revision 11 refuses candidate revisions 5-9 both ways");
 }
 // Revision 10 is XML2 Fix 1.8.0-beta.6 (released): its enemies do not spend the call-gap re-checks/decisions and
 // still roll power attacks, so a beta 7 peer refuses it both ways, with and without the weapon brain words.
 for(const auto words:{std::array<std::uint32_t,4>{1,1,0,0},std::array<std::uint32_t,4>{1,1,1,0x6BED0A4Cu}}){
  record beta6;beta6.add(2,1,{1,0,0,0},true);beta6.add(gameplay_capabilities::npc_power_rule_id,10,words,true);beta6.finalize();
  check(npc(true).compare(beta6.bytes())!=result::match && beta6.compare(npc(true).bytes())!=result::match,"revision 11 refuses the released revision 10 (1.8.0-beta.6) both ways");
 }
 // The officer weapon brain (words 2-3) under the same revision: with and without it refuse each other by value.
 record officer;officer.add(2,1,{1,0,0,0},true);officer.add(gameplay_capabilities::npc_power_rule_id,npc_power::revision,{1,1,1,0x6BED0A4Cu},true);officer.finalize();
 check(npc(true).compare(officer.bytes())==result::value && officer.compare(npc(true).bytes())==result::value,"officer weapon brain on/off refuses both ways by value");
 // Released OFF peers retain the exact record, including rule 16's old revision and no rule 18.
 record released_off;released_off.add(2,1,{1,0,0,0},true);released_off.add(16,3,{0,0,0,0},true);released_off.finalize();
 const auto current_off=npc(false),stun_off=stun(false);
 check(std::equal(current_off.bytes().begin(),current_off.bytes().end(),released_off.bytes().begin(),released_off.bytes().end()) && released_off.compare(current_off.bytes())==result::match,"released revision 3 OFF digest is unchanged");
 check(std::equal(stun_off.bytes().begin(),stun_off.bytes().end(),native.bytes().begin(),native.bytes().end()),"disabled new stun rule adds no record entry");
 check(!stun(false,false).ready() && !npc(false,false).ready(),"failed requests with zero effective values still poison agreement");
 check(npc(true,false).compare(npc(true).bytes())==result::unavailable && peer(true).compare(npc(false).bytes())==result::entries,"failed NPC power rule install or a peer without the rule refuses");
 check(std::string(gameplay_capabilities::rule_name(16))=="NpcPowerRule" && gameplay_capabilities::bucket_of(16)!=gameplay_capabilities::bucket_of(15),"rule 16 is named and has a bucket of its own");
 record sorted;sorted.add(2,1,{0,0,0,0},true);sorted.add(3,1,{1,0,0,0},true);sorted.finalize();check(sorted.compare(missing.bytes())==result::match,"registration order canonicalizes by explicit rule id");
 // Rule 14, the script function limit: peers agree on the installed capacity; plain XML2 advertises {0,0,0,0}.
 {
  using namespace script_functions_rules;
  auto functions=[](agreement a){record r;r.add(2,1,{0,0,0,0},true);r.add(gameplay_capabilities::script_functions_id,agreement_revision,a.values,a.verified);r.finalize();return r;};
  const auto raised=functions(agreement_entry(false,true,1024,1024)),raised_too=functions(agreement_entry(true,false,1024,1024));
  const auto stock=functions(agreement_entry(true,true,320,320)),plain=functions(agreement_entry(false,false,320,320)),plain_too=functions(agreement_entry(false,false,320,320));
  const auto bigger=functions(agreement_entry(true,true,2048,2048)),refused=functions(agreement_entry(false,true,1024,320));
  check(gameplay_capabilities::script_functions_id==14 && agreement_revision==1,"script function limit is rule 14 revision 1");
  check(raised.compare(raised_too.bytes())==result::match && plain.compare(plain_too.bytes())==result::match,"rule 14: equal installed capacities match (default 1024 and explicit 1024 alike; plain XML2 peers match)");
  check(raised.compare(stock.bytes())==result::value && stock.compare(raised.bytes())==result::value,"rule 14: a raised peer and a ScriptFunctions=320 peer refuse each other");
  check(raised.compare(bigger.bytes())==result::value && raised.compare(plain.bytes())==result::value && plain.compare(stock.bytes())==result::value,"rule 14: another capacity or plain XML2 against a participating peer refuses");
  check(!refused.ready() && refused.bytes().empty() && refused.compare(raised.bytes())==result::unavailable && raised.compare(refused.bytes())!=result::match,"rule 14: a requested raise that didn't install is unverified - the record is poisoned and host/join close both ways");
  check(off.compare(raised.bytes())==result::entries && raised.compare(off.bytes())==result::entries,"rule 14: a peer without the rule (older fix) refuses");
 }
 record duplicate;duplicate.add(2,1,{0,0,0,0},true);check(!duplicate.add(2,1,{0,0,0,0},true) && !duplicate.finalize(),"duplicate registration poisons finalization");
 record late;late.finalize();check(!late.add(2,1,{0,0,0,0},true) && !late.ready() && late.bytes().empty(),"late registration cannot change a frozen advertised record");
 record invalid;check(!invalid.layout(1,{32,224,256,7},true) && !invalid.finalize(),"token capacity overflow refuses");
 record sum;check(!sum.layout(1,{32,225,256,8},true),"actor+generic above world refuses");record headroom;check(headroom.layout(3,{32,768,1024,10},true) && headroom.finalize(),"actor+generic below world (224 registrations of headroom) is accepted");
 record twice;twice.layout(1,{32,224,256,8},true);check(!twice.layout(1,{32,224,256,8},true) && !twice.finalize(),"duplicate layout owner cannot overwrite an earlier effective layout");
 record bad_layout;check(!bad_layout.layout(2,{64,448,512,16},false) && !bad_layout.finalize(),"failed entity installer cannot silently advertise retail");
 // The wire record has one size whatever the number of rules: 16, 64 and 256 (the registration bound) all fit with
 // every queued admission body at or under the native 300-byte single-message limit, with headroom.
 for(std::uint16_t rules:{std::uint16_t(1),std::uint16_t(15),std::uint16_t(16),std::uint16_t(64),std::uint16_t(256)}){
  record a,b;for(std::uint16_t id=2;id<=rules;++id){a.add(id,1,{id,0,0,0},true);b.add(id,1,{id,0,0,0},true);}
  const bool fits=a.finalize() && b.finalize() && a.bytes().size()==gameplay_capabilities::record_size && a.count()==rules
   && reply_size+a.bytes().size()<=300-64 && info_size+a.bytes().size()<=300-64 && ack_size+a.bytes().size()<=300-64 && a.compare(b.bytes())==result::match;
  char what[160];std::snprintf(what,sizeof(what),"%u rules: 108-byte record, 0x1f %zu / 0x1a %zu / 0x1b %zu / 0x18 %zu bytes (queued <= 236), equal records match",unsigned(rules),reply_size+a.bytes().size(),info_size+a.bytes().size(),ack_size+a.bytes().size(),request_size+a.bytes().size());
  check(fits,what);
  if(rules>=2){record c;for(std::uint16_t id=2;id<=rules;++id)c.add(id,1,{id==rules ? 0u : id,0,0,0},true);c.finalize();gameplay_capabilities::difference d;
   check(a.compare(c.bytes(),&d)==result::value && d.buckets==(std::uint64_t(1)<<gameplay_capabilities::bucket_of(rules)) && !d.local_only && !d.peer_only,"the last rule's value differs: refused, and only its summary bucket differs");}
 }
 record overflow;for(std::uint16_t id=2;id<=256;++id)overflow.add(id,1,{id,0,0,0},true);check(!overflow.add(257,1,{0,0,0,0},true) && !overflow.finalize(),"the 257th rule entry fails closed");
 static_assert(gameplay_agreement_rules::largest_queued_body==188 && gameplay_capabilities::record_size==108,"protocol 3 sizes");
 // Naming: which rule differs, for the log and the dialog.
 {
  auto wave=[](std::uint32_t death,bool solo){record r;for(std::uint16_t id=2;id<=15;++id)if(id!=8 || solo)r.add(id,1,{id==9 ? death : 0u,0,0,0},true);r.finalize();return r;};
  const auto base=wave(1,true),death=wave(0,true),no_solo=wave(1,false);gameplay_capabilities::difference d;char text[256];
  check(base.compare(death.bytes(),&d)==result::value && d.buckets==(std::uint64_t(1)<<8),"DeathRule (rule 9) off on the peer: a value difference in bucket 8 only");
  base.describe(d.buckets,d.local_only,true,text,sizeof(text));check(std::string(text)=="9 DeathRule","the log names rule 9 DeathRule");
  base.describe(d.buckets,d.local_only,false,text,sizeof(text));check(std::string(text)=="DeathRule","the dialog names DeathRule");
  check(base.compare(no_solo.bytes(),&d)==result::entries && d.local_only==(std::uint64_t(1)<<7),"a peer without rule 8 is an entries difference, rule 8 only here");
  base.describe(d.buckets,d.local_only,true,text,sizeof(text));check(std::string(text)=="8 SoloMode (not on the other PC)","the log says SoloMode is not on the other PC");
  check(no_solo.compare(base.bytes(),&d)==result::entries && d.peer_only==(std::uint64_t(1)<<7),"seen from the other side, rule 8 is only on the peer");
  no_solo.describe(d.buckets,d.local_only,true,text,sizeof(text));check(std::string(text)=="8 a rule this PC does not have","a rule only the peer has is named by its ID");
  base.describe(0,0,true,text,sizeof(text));check(text[0]==0,"no differing bucket names nothing");
  base.describe(~std::uint64_t(0),0,true,text,24);check(std::strlen(text)==23,"naming is bounded by its buffer");
 }
 // SHA-256 is FIPS 180-4's (the digest is the admission decision).
 {const std::uint8_t abc[]{'a','b','c'};const auto h=sha256_fips::digest(abc,3);check(h[0]==0xba && h[1]==0x78 && h[30]==0x15 && h[31]==0xad,"SHA-256(\"abc\") test vector");
  const auto e=sha256_fips::digest(nullptr,0);check(e[0]==0xe3 && e[1]==0xb0 && e[31]==0x55,"SHA-256 of nothing test vector");}
 // Rule details (beta 7 review finding 1): a record without one hashes the canonical list exactly as protocol 4 always
 // has (the released OFF digest), a detail appends "GDTL", its count and (ID, digest) after the entries.
 {
  auto le=[](std::vector<std::uint8_t>& v,std::uint32_t x,int n){for(int i=0;i<n;++i)v.push_back(std::uint8_t(x>>(8*i)));};
  std::vector<std::uint8_t> list;le(list,0x50414347,4);le(list,4,2);le(list,2,2);
  for(const auto& [id,rev,w]:{std::tuple<std::uint16_t,std::uint16_t,std::array<std::uint32_t,4>>{1,1,{32,224,256,8}},{16,11,{1,1,1,0x12345678}}}){le(list,id,2);le(list,rev,2);for(auto x:w)le(list,x,4);}
  record plain,detailed,other;gameplay_capabilities::detail_digest d{},d2{};for(std::size_t i=0;i<32;++i){d[i]=std::uint8_t(i*7+1);d2[i]=d[i];}d2[31]^=1;
  plain.add(16,11,{1,1,1,0x12345678},true);detailed.add(16,11,{1,1,1,0x12345678},true,&d);other.add(16,11,{1,1,1,0x12345678},true,&d2);
  plain.finalize();detailed.finalize();other.finalize();
  const auto h=sha256_fips::digest(list.data(),list.size());
  check(std::memcmp(plain.bytes().data()+gameplay_capabilities::digest_at,h.data(),32)==0,"a record without details keeps the protocol 4 canonical digest");
  le(list,0x4c544447,4);le(list,1,2);le(list,16,2);list.insert(list.end(),d.begin(),d.end());
  const auto hd=sha256_fips::digest(list.data(),list.size());
  check(std::memcmp(detailed.bytes().data()+gameplay_capabilities::digest_at,hd.data(),32)==0,"a detail extends the canonical list after the entries");
  check(detailed.compare(plain.bytes())==result::value && plain.compare(detailed.bytes())==result::value,"a detail difference refuses both ways as a value difference");
  check(detailed.compare(other.bytes())==result::value,"one detail bit refuses");
  record full;bool all=true;for(std::uint16_t id=2;id<2+gameplay_capabilities::max_details;++id)all&=full.add(id,1,{},true,&d);
  check(all && !full.add(40,1,{},true,&d) && !full.finalize(),"more details than the record holds poison it");
 }
 // Rules 11 and 12 (xml1_capability_rules::installed_switch): no key = verified off {0,0,0,0}, so a peer whose fix
 // has neither key still matches one whose keys are absent or set to the game's own value; requested and installed
 // = {1,0,0,0}; requested but not installed = unverified, which poisons the record and closes host and join.
 using xml1_capability_rules::installed_switch;
 const auto off_switch=installed_switch(false,false),ignored_switch=installed_switch(false,true),on_switch=installed_switch(true,true),failed_switch=installed_switch(true,false);
 check(off_switch.verified && off_switch.values==std::array<std::uint32_t,4>{0,0,0,0} && ignored_switch.verified && ignored_switch.values==off_switch.values,"PlacementCrush/DoubleHeal not requested register verified off");
 check(on_switch.verified && on_switch.values==std::array<std::uint32_t,4>{1,0,0,0} && !failed_switch.verified,"requested and installed is {1,0,0,0}; requested but not installed is unverified");
 check(gameplay_capabilities::placement_crush_id==11 && gameplay_capabilities::double_heal_id==12 && xml1_capability_rules::placement_crush_revision==1 && xml1_capability_rules::double_heal_revision==1,"rule IDs 11 and 12, revision 1");
 auto with=[&](const xml1_capability_rules::registration& crush,const xml1_capability_rules::registration& heal){
  record r;r.add(2,1,{0,0,0,0},true);r.add(gameplay_capabilities::placement_crush_id,1,crush.values,crush.verified);r.add(gameplay_capabilities::double_heal_id,1,heal.values,heal.verified);r.finalize();return r;};
 const auto both_off=with(off_switch,off_switch),both_on=with(on_switch,on_switch),crush_only=with(on_switch,off_switch),heal_only=with(off_switch,on_switch);
 check(both_off.compare(with(off_switch,off_switch).bytes())==result::match && both_on.compare(with(on_switch,on_switch).bytes())==result::match,"equal effective PlacementCrush/DoubleHeal match");
 check(both_off.compare(crush_only.bytes())==result::value && both_on.compare(crush_only.bytes())==result::value && both_off.compare(heal_only.bytes())==result::value && crush_only.compare(heal_only.bytes())==result::value,"a PlacementCrush or DoubleHeal difference refuses with a value mismatch");
 record failed_crush;failed_crush.add(gameplay_capabilities::placement_crush_id,1,failed_switch.values,failed_switch.verified);check(!failed_crush.finalize() && !failed_crush.ready() && failed_crush.bytes().empty() && failed_crush.compare(both_off.bytes())==result::unavailable,"a requested PlacementCrush that failed to install closes host and join");
 record failed_heal;failed_heal.add(gameplay_capabilities::double_heal_id,1,failed_switch.values,failed_switch.verified);check(!failed_heal.finalize() && failed_heal.compare(both_off.bytes())==result::unavailable,"a requested DoubleHeal that failed to install closes host and join");
 check(!native.ready() || native.finalize(),"finalization is idempotent");
}
struct event {std::uint32_t type;void* body;std::uint16_t length,reserved;void* context;void* sender;};
std::array<std::uint8_t,0x2310> context{};
std::array<std::uint8_t,32> sender{},descriptor{};
void put(std::size_t offset,std::uint32_t value){std::memcpy(context.data()+offset,&value,4);}
std::uint32_t get(std::size_t offset){std::uint32_t value;std::memcpy(&value,context.data()+offset,4);return value;}
int popups=0,menus=0,statuses=0,freed=0,timers=0,unregistered=0,forwarded=0;
void* last_freed=nullptr;void* last_timer=nullptr;
std::string visible;
void* popup_vt[4]{};void* menu_vt[30]{};void** popup_object=popup_vt;
std::vector<std::uint8_t> manager_space(menu_current+4);std::array<std::uint8_t,0x40> host_lobby{},join_menu{};
void* popup_get(){return &popup_object;}void* menu_get(){const auto vt=reinterpret_cast<std::uintptr_t>(menu_vt);std::memcpy(manager_space.data(),&vt,4);return manager_space.data();}
void current_menu(void* menu){const auto m=reinterpret_cast<std::uintptr_t>(menu);std::memcpy(manager_space.data()+menu_current,&m,4);}
int lookups=0;const char* __cdecl lookup_fake(const char* key){++lookups;return std::strcmp(key,"ONLINE_ADVICE")==0 ? "native advice" : "other text";}
// What the lobby draw gets from its (patched) advice lookup this frame.
std::string lobby(){std::int32_t rel;std::memcpy(&rel,static_cast<const std::uint8_t*>(at(host_advice_call))+1,4);return reinterpret_cast<const char*(__cdecl*)(const char*)>(host_advice_call+5+rel)("ONLINE_ADVICE");}
void* hud_vt[0x40]{};void** hud_object=hud_vt;void* hud_get(){return &hud_object;}
int hud_messages=0,hud_kind=0;float hud_seconds=0,hud_alpha=0;std::string hud_visible;
int __fastcall hud_message_fake(void*,void*,const char* text,float seconds,float,float,float,float alpha,int kind){++hud_messages;hud_visible=text;hud_seconds=seconds;hud_alpha=alpha;hud_kind=kind;return 7;}
struct registration{std::uint32_t opcode;void* handler;void* owner;std::uint32_t flags,limit,handle;};
std::vector<registration> registered;std::vector<std::pair<std::uint32_t,std::uint32_t>> released;void* last_dispatcher=nullptr;
std::uint32_t __fastcall register_fake(void* self,void*,std::uint32_t op,void* handler,void* owner,std::uint32_t flags,std::uint32_t limit,std::uint32_t){last_dispatcher=self;const auto h=0x9000+std::uint32_t(registered.size());registered.push_back({op&0xff,handler,owner,flags,limit,h});return h;}
void __fastcall release_fake(void* self,void*,std::uint32_t op,std::uint32_t handle){last_dispatcher=self;released.emplace_back(op&0xff,handle);}
void __fastcall show_popup(void*,void*,const char* text,float wait,int flags){++popups;visible=text;check(wait==-1.0f && flags==0,"native dialog receives actionable non-timeout prompt");}
void __fastcall close_menu(void*,void*,int a,int b){++menus;check(a==0 && b==0,"native failure closes pending join menu");}
void __cdecl status(int reason,void*,void*){statuses+=reason==9;}
std::uint8_t* selected_descriptor=descriptor.data();void* selected_session=context.data();
void* descriptor_get(){return selected_descriptor;}void* session_get(){return selected_session;}
int __fastcall no_peer(void*,void*){return 0;}void __fastcall noop(void*,void*){}
void __cdecl free_buffer(void* p){++freed;last_freed=p;}void* __stdcall timer_get(void* p){return p;}
void __fastcall timer_destroy(void* p,void*){++timers;last_timer=p;}void __fastcall unregister(void*,void*){++unregistered;}
int admitted(void*){++forwarded;return 73;}
int timeout_trap(){++failures;return 0;}
void pending_context(){context.fill(0);put(4,0x12345678);put(0x2250,0x22222222);context[0x2294]=1;for(auto n:{0x21f4,0x21f8,0x21fc,0x2200,0x2204,0x222c,0x2234,0x2230})put(n,1);put(0x22bc,0x33333333);put(0x2298,std::uint32_t(reinterpret_cast<std::uintptr_t>(&status)));descriptor[0]=1;}
bool cleaned(){return get(0x2250)==0 && context[0x2294]==0 && get(0x21f4)==0 && get(0x21f8)==0 && get(0x21fc)==0 && get(0x2200)==0 && get(0x2204)==0 && get(0x222c)==0 && get(0x2234)==0 && get(0x2230)==0 && get(0x22bc)==0 && get(0x221c)==0xffffffff;}
std::array<std::uint8_t,request_size+512> output{},input{};
std::array<std::uint8_t,0x14> header{};
std::uint16_t output_size=0;std::uint8_t output_type=0;std::uint32_t output_this=0,output_arg=0;int output_extra=0,send_count=0;
void capture(void* self,const std::uint8_t* h){++send_count;output_this=std::uint32_t(reinterpret_cast<std::uintptr_t>(self));std::memcpy(&output_size,h+8,2);output_type=h[0x10];if(output_size<=output.size() && output_size)std::memcpy(output.data(),*reinterpret_cast<const void*const*>(h),output_size);}
int __fastcall direct_fake(void* self,void*,const void* address,int port,const std::uint8_t* h){output_arg=std::uint32_t(reinterpret_cast<std::uintptr_t>(address));output_extra=port;capture(self,h);return 73;}
void __fastcall single_fake(void* self,void*,void* transport,const std::uint8_t* h,int flags){output_arg=std::uint32_t(reinterpret_cast<std::uintptr_t>(transport));output_extra=flags;capture(self,h);}
int __fastcall broadcast_fake(void* self,void*,const std::uint8_t* h,int exclude,int participants,int mode){output_arg=std::uint32_t(exclude);output_extra=participants+mode;capture(self,h);return 73;}
std::uint32_t entry;const std::uint8_t* header_pointer=header.data();
__declspec(naked) void invoke_direct(){__asm {push dword ptr [header_pointer]} __asm {push 123} __asm {push 0x23456789} __asm {mov ecx,0x12345678} __asm {jmp dword ptr [entry]}}
__declspec(naked) void invoke_single(){__asm {push 0} __asm {push dword ptr [header_pointer]} __asm {push 0x23456789} __asm {mov ecx,0x12345678} __asm {jmp dword ptr [entry]}}
__declspec(naked) void invoke_broadcast(){__asm {push 1} __asm {push 1} __asm {push -1} __asm {push dword ptr [header_pointer]} __asm {mov ecx,0x12345678} __asm {jmp dword ptr [entry]}}
// Native 0x1a admission as retail does it: the participant is taken, then the 0x1b follow-up leaves through the
// patched ack_send site (0x609a5d) - the only place retail sends 0x1b, and only on that path.
std::array<std::uint8_t,ack_size> ack_body{};
int admitting(void*){++forwarded;header.fill(0);const auto* body=ack_body.data();std::memcpy(header.data(),&body,4);const std::uint16_t size=ack_size;std::memcpy(header.data()+8,&size,2);header[0x10]=0x1b;header[0x11]=1;entry=ack_send;invoke_single();return 73;}
__declspec(naked) void join_continuation(){__asm {pop ebp} __asm {pop ebx} __asm {add esp,0x1c} __asm {mov eax,73} __asm {ret 8}}
void notice_standins(){
 const std::uint32_t host_vt=host_menu_vtable,other_vt=0x69f000;std::memcpy(host_lobby.data(),&host_vt,4);std::memcpy(join_menu.data(),&other_vt,4);current_menu(host_lobby.data());
 hud_vt[hud_message_slot/4]=at(native_hud_message);jump(native_text_lookup,&lookup_fake);jump(native_hud,&hud_get);jump(native_hud_message,&hud_message_fake);
 jump(native_register,&register_fake);jump(native_unregister,&release_fake);
}
void boundary_standins(){
 popup_vt[3]=reinterpret_cast<void*>(&show_popup);menu_vt[0x74/4]=reinterpret_cast<void*>(&close_menu);notice_standins();
 jump(native_dialog,&popup_get);jump(native_menu,&menu_get);jump(0x6097b0,&descriptor_get);jump(0x612be0,&session_get);jump(0x6111f0,&no_peer);jump(0x560700,&free_buffer);
 for(auto va:{0x608d10u,0x6088b0u,0x608b40u,0x6159b0u,0x6074b0u,0x607530u,0x607630u,0x672161u})jump(va,&noop);
 jump(0x60b210,&timer_get);jump(0x60afe0,&timer_destroy);jump(0x60f070,&unregister);jump(0x609900,&timeout_trap);
 jump(native_direct,&direct_fake);jump(native_send,&single_fake);jump(native_broadcast,&broadcast_fake);
 for(auto va:{native_request,native_reply,native_info,native_ack})jump(va,&admitted);
 jump(join_resume,&join_continuation);
}
record runtime_peer(bool on){record r;r.add(2,1,{on ? 1u : 0u,0,0,0},true);r.add(3,7,{1,3,0,0},true);r.add(17,1,{1,0,0,0},true);r.add(18,1,{1,0,0,0},true);r.finalize();return r;}
void separate_host_joiner()
{
 auto info_receiver=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(info_registration+1)));
 auto rejection_receiver=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(rejection_registration+1)));
 for(bool legacy:{false,true}){
  pending_context();put(0x22bc,0);for(std::size_t n=0x22c4;n<=0x22d8;n+=4)put(n,0x77777777);
  std::array<std::uint8_t,32> existing_participant{};existing_participant.fill(0x5a);existing_participant[0x19]=1;
  sender[0x19]=1;const auto host_snapshot=context;const auto descriptor_snapshot=descriptor;const auto participant_snapshot=existing_participant;
  auto joiner=context;std::uint32_t timer=0x55555555,buffer=0x44444444;std::memcpy(joiner.data()+0x22bc,&timer,4);std::memcpy(joiner.data()+0x2250,&buffer,4);
  std::array<std::uint8_t,32> joiner_sender{},joiner_descriptor{};joiner_sender[0x19]=1;joiner_descriptor[0]=1;
  const auto off=runtime_peer(false);input.fill(0);std::memcpy(input.data()+info_size,off.bytes().data(),off.bytes().size());
  event incoming{0x1a,input.data(),std::uint16_t(info_size+(legacy ? 0 : off.bytes().size())),0,context.data(),sender.data()};
  const auto callbacks_before=statuses,free_before=freed,timer_before=timers,unregister_before=unregistered,menu_before=menus,popup_before=popups;send_count=0;forwarded=0;
  check(info_receiver(&incoming)==0 && context==host_snapshot && descriptor==descriptor_snapshot && existing_participant==participant_snapshot && sender[0x19]==0 && forwarded==0,
        legacy ? "legacy 0x1a keeps active host/session/participants; disconnects only rejected peer" : "mismatched 0x1a keeps active host/session/participants; disconnects only rejected peer");
  check(statuses==callbacks_before && freed==free_before && timers==timer_before && unregistered==unregister_before && menus==menu_before && popups==popup_before,
        "host refusal opens no popup and causes no status9, buffer release, callback teardown or menu close");
  check(lobby()==host_notice_detail(legacy ? refusal::foreign_record : refusal::settings,false) && hud_messages==0,
        legacy ? "host lobby draws the other-build refusal in place of its advice" : "host lobby draws the different-gameplay-settings refusal in place of its advice");
  check(send_count==1 && output_type==0x1d && output_size==refusal_detail_size && read_refusal(output.data(),output_size)==(legacy ? refusal::foreign_record : refusal::settings) && output_arg==reinterpret_cast<std::uintptr_t>(sender.data()+12),
        "host sends native rejection with its reason to refused joining endpoint");
  selected_descriptor=joiner_descriptor.data();selected_session=joiner.data();
  event remote{output_type,nullptr,0,0,joiner.data(),joiner_sender.data()};
  check(rejection_receiver(&remote)==2 && statuses==callbacks_before+1 && freed==free_before+1 && timers==timer_before+1 && unregistered==unregister_before+1 && menus==menu_before+1 && popups==popup_before+1,
        "separate pending joiner receives rejection and executes actual native cleanup/dialog");
  auto field=[&](std::size_t at){std::uint32_t v;std::memcpy(&v,joiner.data()+at,4);return v;};
  check(field(0x2250)==0 && field(0x22bc)==0 && field(0x2230)==0 && field(0x21f4)==0 && field(0x221c)==0xffffffff && joiner_sender[0x19]==0 && last_freed==reinterpret_cast<void*>(buffer) && last_timer==reinterpret_cast<void*>(timer),
        "cleanup releases only joiner-owned transfer buffer/timer and clears joiner state");
  check(context==host_snapshot && descriptor==descriptor_snapshot && existing_participant==participant_snapshot,"remote joiner cleanup cannot mutate distinct host state or participants");
  selected_descriptor=descriptor.data();selected_session=context.data();
 }
}
void notice_rules(){
 using gameplay_capabilities::result;
 check(classify(true,result::value)==refusal::settings && classify(true,result::revision)==refusal::settings && classify(true,result::entries)==refusal::settings,"different rule values, revisions and entries name gameplay settings");
 check(classify(false,result::value)==refusal::sender_install && classify(true,result::unavailable)==refusal::sender_install,"an unverified local installation names this side's fix");
 check(classify(true,result::malformed)==refusal::foreign_record && classify(true,result::protocol)==refusal::foreign_record,"a missing or foreign record names another build");
 for(auto why:{refusal::unspecified,refusal::settings,refusal::sender_install,refusal::foreign_record}){const auto b=refusal_body(why);check(read_refusal(b.data(),b.size())==why,"refusal reason body round-trips");}
 auto b=refusal_body(refusal::settings);
 {const auto d=refusal_body(refusal::settings,0x8000000000000101ull);check(d.size()==16 && read_refusal(d.data(),d.size())==refusal::settings && read_refusal_buckets(d.data(),d.size())==0x8000000000000101ull && std::memcmp(d.data(),b.data(),8)==0,
   "protocol 3's 0x1d appends the differing summary buckets after the unchanged 8-byte reason (older readers read the reason)");
  check(read_refusal_buckets(b.data(),b.size())==0 && read_refusal_buckets(refusal_body(refusal::foreign_record,5).data(),16)==0,"no buckets from an 8-byte body or a non-settings reason");}
 check(read_refusal(b.data(),b.size()-1)==refusal::unspecified && read_refusal(nullptr,8)==refusal::unspecified,"short or absent rejection body is an unspecified (legacy) refusal");
 b[0]='X';check(read_refusal(b.data(),b.size())==refusal::unspecified,"foreign rejection body is never read as a reason");
 b=refusal_body(refusal::settings);b[4]=9;check(read_refusal(b.data(),b.size())==refusal::unspecified,"unknown reason code is unspecified");
 b=refusal_body(refusal::settings);b[6]=1;check(read_refusal(b.data(),b.size())==refusal::unspecified,"nonzero reserved bytes are unspecified");
 std::vector<std::string> texts;
 for(bool remote:{false,true})for(auto why:{refusal::unspecified,refusal::settings,refusal::sender_install,refusal::foreign_record})texts.emplace_back(host_notice_text(why,remote));
 check(std::string(host_notice_text(refusal::sender_install,true))!=host_notice_text(refusal::sender_install,false) && std::string(host_notice_text(refusal::foreign_record,true))!=host_notice_text(refusal::foreign_record,false),"install/build refusals say which PC");
 bool short_lines=true;for(const auto&t:texts)short_lines&=t.size()<=52 && t.rfind("Join refused: ",0)==0;check(short_lines,"every HUD notice is one short line naming the refusal");
 std::vector<std::string> details;bool paragraphs=true;
 for(bool remote:{false,true})for(auto why:{refusal::unspecified,refusal::settings,refusal::sender_install,refusal::foreign_record}){details.emplace_back(host_notice_detail(why,remote));paragraphs&=details.back().size()<400 && details.back().rfind("A player could not join: ",0)==0;}
 check(paragraphs,"every lobby notice fits the 500-byte advice buffer and starts with what happened");
 check(std::string(host_notice_detail(refusal::settings,false)).find("gameplay settings")!=std::string::npos && std::string(host_notice_detail(refusal::sender_install,true)).find("their PC")!=std::string::npos && std::string(host_notice_detail(refusal::sender_install,false)).find("this PC")!=std::string::npos,
       "lobby notices name the reason and which PC");
}
void host_notice_tests(){
 // Execute retail host setup 0x609c80 and teardown 0x6074b0; only the dispatcher boundary is a stand-in.
 auto info_receiver=reinterpret_cast<void*>(*reinterpret_cast<std::uint32_t*>(at(info_registration+1)));
 auto joiner_rejection=reinterpret_cast<void*>(*reinterpret_cast<std::uint32_t*>(at(rejection_registration+1)));
 context.fill(0);put(4,0x12345678);registered.clear();released.clear();
 using method=void(__thiscall*)(void*);reinterpret_cast<method>(0x609c80)(context.data());
 const registration* info=nullptr;const registration* host_rejection=nullptr;
 for(const auto&r:registered){if(r.opcode==0x1a)info=&r;if(r.opcode==0x1d)host_rejection=&r;}
 check(registered.size()==7 && info && info->handler==info_receiver && get(0x22c8)==info->handle && last_dispatcher==at(0x12345678),"retail host setup registers host-info through the wrapped call and keeps its native handle");
 check(host_rejection && host_rejection->owner==context.data() && host_rejection->flags==host_rejection_flags && host_rejection->handler!=joiner_rejection && host_rejection->handler!=at(native_failure) && host_rejection->limit==0x7fffffff,
       "host setup adds a host-only 0x1d receiver with the host's context, never the joiner failure handler");
 check(get(joiner_rejection_slot)==0 && get(0x22dc)==0 && get(0x22e4)==0 && get(0x22fc)==0,"host registration writes no joiner slots");
 if(!host_rejection)return;
 auto host_receiver=reinterpret_cast<int(__cdecl*)(void*)>(host_rejection->handler);
 const auto rejection_handle=host_rejection->handle,info_handle=info->handle;
 // Active host with an existing participant; a pending joiner refuses this game and says why.
 std::array<std::uint8_t,32> existing{};existing.fill(0x5a);existing[0x19]=1;
 put(0x2250,0x22222222);put(0x22bc,0x33333333);current_menu(host_lobby.data());
 struct reasoned{refusal why;bool body;const char* text;const char* name;};
 for(const auto& c:std::array<reasoned,5>{{
  {refusal::settings,true,"gameplay settings differ","joiner's settings refusal shows the host the gameplay-settings reason"},
  {refusal::sender_install,true,"did not load correctly on their PC","joiner's unverified install shows the host whose fix failed"},
  {refusal::foreign_record,true,"or the fix did not load correctly on this PC","joiner's foreign-record refusal shows the host it may be this PC"},
  {refusal::unspecified,false,"setup is incompatible","older joiner's bodiless rejection still gives the host a notice"},
  {refusal::unspecified,true,"setup is incompatible","unknown rejection body gives the generic notice"}}}){
  auto body=refusal_body(c.why);if(c.why==refusal::unspecified && c.body)body[4]=0x7f;
  event incoming{0x1d,c.body ? body.data() : nullptr,std::uint16_t(c.body ? body.size() : 0),0,context.data(),sender.data()};
  sender[0x19]=1;const auto snapshot=context;const auto participant=existing;
  const auto shown=popups,closed=menus,status_before=statuses,free_before=freed,timer_before=timers,unregister_before=unregistered,huds=hud_messages;send_count=0;forwarded=0;
  check(host_receiver(&incoming)==0 && lobby()==host_notice_detail(c.why==refusal::unspecified ? refusal::unspecified : c.why,true) && lobby().find(c.text)!=std::string::npos,c.name);
  check(popups==shown && menus==closed && hud_messages==huds && send_count==0 && forwarded==0,"host notice is non-modal: no popup, no menu close, no echo, no admission");
  check(statuses==status_before && freed==free_before && timers==timer_before && unregistered==unregister_before && context==snapshot && existing==participant && sender[0x19]==0,
        "host keeps session, timer, buffer and participants; only the refusing peer's connected flag clears");
 }
 // Drawn every frame from the lookup: it survives cursor moves and list refreshes, and only the lobby that was
 // current shows it; any other menu (or a later lobby) gets the native text back.
 {const auto notice=lobby();const auto native_before=lookups;
  check(notice==host_notice_detail(refusal::unspecified,true) && lobby()==notice && lookups==native_before,"the lobby keeps drawing the notice frame after frame without the native lookup");
  current_menu(join_menu.data());check(lobby()=="native advice" && lookups==native_before+1,"while another menu is current the native lookup answers");
  current_menu(host_lobby.data());check(lobby()==notice,"back in the same lobby the notice is drawn again");}
 // The context also owns a joiner-side 0x1d: the joiner path handles it, the host receiver stays out.
 put(joiner_rejection_slot,0x4444);sender[0x19]=1;
 {auto body=refusal_body(refusal::settings);event incoming{0x1d,body.data(),std::uint16_t(body.size()),0,context.data(),sender.data()};const auto before=lobby();const auto huds=hud_messages;
  check(host_receiver(&incoming)==0 && lobby()==before && hud_messages==huds && sender[0x19]==1,"host receiver ignores 0x1d on a context whose joiner setup owns it");}
 put(joiner_rejection_slot,0);
 // In play (no host lobby on screen) the HUD's own timed message carries it; any other menu is left alone.
 for(void* menu:{static_cast<void*>(nullptr),static_cast<void*>(join_menu.data())}){
  current_menu(menu);auto body=refusal_body(refusal::settings);event incoming{0x1d,body.data(),std::uint16_t(body.size()),0,context.data(),sender.data()};
  sender[0x19]=1;const auto huds=hud_messages,shown=popups;
  check(host_receiver(&incoming)==0 && hud_messages==huds+1 && hud_visible=="Join refused: different gameplay settings" && hud_seconds==hud_notice_seconds && hud_alpha==1.0f && hud_kind==3 && popups==shown,
        menu ? "another menu is never rewritten; the HUD timed message carries the notice" : "in play the host sees the HUD timed message, not a popup");
 }
 current_menu(host_lobby.data());
 // A later compatible join on this host clears the stale lobby notice.
 {auto local=runtime_peer(true);input.fill(0);std::memcpy(input.data()+info_size,local.bytes().data(),local.bytes().size());
  event e{0x1a,input.data(),std::uint16_t(info_size+local.bytes().size()),0,context.data(),sender.data()};
  auto info_entry=reinterpret_cast<int(__cdecl*)(void*)>(info_receiver);forwarded=0;
  {auto body=refusal_body(refusal::settings);event refused{0x1d,body.data(),std::uint16_t(body.size()),0,context.data(),sender.data()};host_receiver(&refused);}
  check(lobby()==host_notice_detail(refusal::settings,true),"a refusal notice is up in the lobby before the next join");
  check(info_entry(&e)==73 && forwarded==1 && lobby()==host_notice_detail(refusal::settings,true),"a capability-matching join the game then refuses (full session, no participant) keeps the notice");
  jump(native_info,&admitting);forwarded=0;send_count=0;
  check(info_entry(&e)==73 && forwarded==1 && send_count==1 && output_type==0x1b && lobby()=="native advice","the 0x1b follow-up, sent only once the participant is taken, ends the notice; the lobby draws its native advice again");
  check(info_entry(&e)==73 && lobby()=="native advice","a further admitted join leaves the native advice alone");jump(native_info,&admitted);}
 // Retail teardown releases the host-info handle; the wrapper releases the host 0x1d with it.
 native_bytes(0x6074b0,5);released.clear();reinterpret_cast<method>(0x6074b0)(context.data());
 bool info_released=false,rejection_released=false;for(const auto&[op,h]:released){info_released|=op==0x1a && h==info_handle;rejection_released|=op==0x1d && h==rejection_handle;}
 check(info_released && rejection_released && get(0x22c8)==0,"retail host teardown releases host-info and the host 0x1d receiver together");
 // Repeated host sessions reuse the wrapper's slots; nothing leaks.
 bool cycles=true;
 for(int i=0;i<6;++i){context.fill(0);put(4,0x12345678);registered.clear();released.clear();reinterpret_cast<method>(0x609c80)(context.data());reinterpret_cast<method>(0x6074b0)(context.data());
  std::size_t adds=0,drops=0;for(const auto&r:registered)adds+=r.opcode==0x1d;for(const auto&[op,h]:released)drops+=op==0x1d;cycles&=adds==1 && drops==1;}
 check(cycles,"six host setup/teardown cycles each add and release exactly one host 0x1d receiver");
 jump(0x6074b0,&noop);
}
// Its own process (the agreement's install state is process-wide): a receiver guard mismatched at the first
// preparation leaves only the receiver out; the agreement and the lobby notice install; the receiver joins later.
int receiver_guard_mismatch(){
 guards(failure_guards);guards(transport_guards);guards(receiver_guards);guards(presentation_guards);guards(game_version_rules::guards);
 native_bytes(0x609c80,0x100);native_bytes(0x6074b0,0x75);
 native_bytes(native_failure,0xa7);native_bytes(native_cleanup,0x10c);native_bytes(native_peer_disconnect,10);
 const auto native_advice_rel=0x0006efc3u,native_register_rel=0x000050e1u;
 const auto rel=[](std::uint32_t call){return *reinterpret_cast<std::uint32_t*>(at(call+1));};
 const auto native_unregister_rel=rel(host_info_unregister_call);
 writable(host_info_register_call+6,1);*static_cast<std::uint8_t*>(at(host_info_register_call+6))^=1; // inside the guard, outside the write
 make_rx();check(gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && rx(0x607000) && rx(0x608000) && rx(0x609000),
       "receiver guard mismatched: native refusal and transport still install and every shared RX page is restored");
 check(*static_cast<std::uint8_t*>(at(native_join))==0xe9 && *static_cast<std::uint8_t*>(at(reply_send))==0xe8,"receiver guard mismatched: refusal and transport sites are written");
 check(rel(host_info_register_call)==native_register_rel && rel(host_info_unregister_call)==native_unregister_rel && has_log("receiver guard failed"),
       "receiver guard mismatched: host-info registration and release stay native, and the log says the joiner's refusal is not received");
 check(rel(host_advice_call)!=native_advice_rel,"receiver guard mismatched: the lobby notice still installs (the parts are independent)");
 const auto installed_advice=rel(host_advice_call);
 writable(host_info_register_call+6,1);*static_cast<std::uint8_t*>(at(host_info_register_call+6))^=1;make_rx();logger::logged.clear();
 check(gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && rx(0x609000) && rel(host_info_register_call)!=native_register_rel && rel(host_info_unregister_call)!=native_unregister_rel,
       "with the receiver guard back a later preparation installs host-info registration and release in a transaction of its own");
 check(rel(host_advice_call)==installed_advice && *static_cast<std::uint8_t*>(at(native_join))==0xe9 && !has_log("late host notice installation"),
       "the late receiver install leaves the notice and agreement sites as they were");
 std::printf("Receiver guard mismatch: %d failure(s)\n",failures);return failures ? 1 : 0;
}
void receiver_guard_child(const char* path){
 // Run receiver_guard_mismatch in a fresh process of this test and require it to pass.
 char self[MAX_PATH]{};GetModuleFileNameA(nullptr,self,MAX_PATH);
 std::string command=std::string("\"")+self+"\" \""+path+"\" --receiver-guard-mismatch";
 STARTUPINFOA si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};DWORD code=1;
 if(CreateProcessA(self,command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&si,&pi)){
  WaitForSingleObject(pi.hProcess,120000);GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
 check(code==0,"receiver guard mismatch (own process): only the receiver is left out and it installs later");
}
// The native fragment receiver (0x6101b0, opcodes 0x12/0x13) run on a 0x13 fragment for a stream it never opened:
// retail dereferences null at 0x61020c; with the fragment guard it drops the fragment. 1 = returned, 2 = faulted.
std::array<std::uint8_t,0x3100> fragment_context{};std::array<std::uint8_t,8> fragment_body{0x05,0x11,0x22};
struct fragment_event{std::uint8_t opcode,pad[3];void* body;std::uint16_t length,reserved;std::uint32_t unused;void* context;};
int run_fragment(int& value){
 fragment_event e{0x13,{},fragment_body.data(),std::uint16_t(fragment_body.size()),0,0,fragment_context.data()};
 __try { value=reinterpret_cast<int(__cdecl*)(void*)>(0x6101b0)(&e);return 1; }
 __except(EXCEPTION_EXECUTE_HANDLER){return 2;}
}
void runtime(){
 notice_rules();
 guards(failure_guards);guards(transport_guards);guards(receiver_guards);guards(presentation_guards);guards(game_version_rules::guards);
 native_bytes(0x609c80,0x100);native_bytes(0x6074b0,0x75);
 native_bytes(0x6101b0,0x110);guards(fragment_guards);
 {int value=-1;check(run_fragment(value)==2,"negative control: retail's fragment receiver faults on a 0x13 fragment of a stream it never opened");}
 native_bytes(native_failure,0xa7);native_bytes(native_cleanup,0x10c);native_bytes(native_peer_disconnect,10);
 const auto original=*static_cast<std::uint8_t*>(at(native_join));const auto original_send=*static_cast<std::uint8_t*>(at(reply_send));
 check(!gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400001)) && *static_cast<std::uint8_t*>(at(native_join))==original,"wrong base refuses native refusal hooks");
 writable(failure_text,1);*static_cast<std::uint8_t*>(at(failure_text))^=1;
 check(!gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && *static_cast<std::uint8_t*>(at(native_join))==original && *static_cast<std::uint8_t*>(at(reply_send))==original_send,"one native failure guard mismatch prevents all writes");native_bytes(failure_text,5);
 // One transaction owns refusal and transport sites: a transport guard mismatch leaves the refusal sites untouched too.
 writable(reply_send,1);*static_cast<std::uint8_t*>(at(reply_send))^=1;make_rx();
 check(!gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && *static_cast<std::uint8_t*>(at(native_join))==original && rx(0x607000) && rx(0x608000) && rx(0x609000),"one transport guard mismatch prevents all writes and preserves every RX page");
 guards(transport_guards);native_bytes(reply_send,5);
 // First with a presentation guard mismatched: the receiver still installs, a joiner's refusal is heard and logged.
 writable(0x69fef0,1);*static_cast<std::uint8_t*>(at(0x69fef0))^=1;
 make_rx();check(gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && rx(0x607000) && rx(0x608000) && rx(0x609000),"native refusal and transport preparation (presentation guard mismatched) restores every shared RX page");
 {int value=-1;check(*static_cast<std::uint8_t*>(at(fragment_stream_miss))==0xe9 && run_fragment(value)==1 && value==0 && has_log("large-message fragment for a stream this PC never opened was dropped"),
        "fragment guard: the same fragment is dropped (returns 0) and logged instead of faulting");}
 check(*static_cast<std::uint8_t*>(at(native_join))==0xe9 && *static_cast<std::uint8_t*>(at(reply_send))==0xe8,"refusal and transport sites are written together");
 const auto native_advice_rel=0x0006efc3u,native_register_rel=0x000050e1u;
 check(*reinterpret_cast<std::uint32_t*>(at(host_advice_call+1))==native_advice_rel && *reinterpret_cast<std::uint32_t*>(at(host_info_register_call+1))!=native_register_rel && has_log("notice guard failed"),
       "a presentation guard mismatch leaves the lobby draw native and still wraps host-info registration");
 {jump(native_register,&register_fake);jump(native_unregister,&release_fake);
  context.fill(0);put(4,0x12345678);registered.clear();released.clear();
  using method=void(__thiscall*)(void*);reinterpret_cast<method>(0x609c80)(context.data());
  const registration* r=nullptr;for(const auto&x:registered)if(x.opcode==0x1d)r=&x;
  check(r && r->flags==host_rejection_flags && r->owner==context.data(),"the host 0x1d receiver is registered although the notice cannot be drawn");
  if(r){auto body=refusal_body(refusal::settings);event incoming{0x1d,body.data(),std::uint16_t(body.size()),0,context.data(),sender.data()};sender[0x19]=1;logger::logged.clear();const auto huds=hud_messages,shown=popups;
   check(reinterpret_cast<int(__cdecl*)(void*)>(r->handler)(&incoming)==0 && sender[0x19]==0 && hud_messages==huds && popups==shown
         && has_log("a joining player refused this game (reason=1)") && has_log("host notice: Join refused: different gameplay settings") && has_log("not drawn"),
         "with the notice unavailable a remote refusal is still received, logged with its reason, and only that peer disconnected");
   const auto h=r->handle;reinterpret_cast<method>(0x6074b0)(context.data());bool dropped=false;for(const auto&[op,x]:released)dropped|=op==0x1d && x==h;
   check(dropped,"retail teardown still releases the receiver-only registration");}}
 *static_cast<std::uint8_t*>(at(0x69fef0))^=1;make_rx();
 check(gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && rx(0x609000) && *reinterpret_cast<std::uint32_t*>(at(host_advice_call+1))!=native_advice_rel,
       "with the presentation guards back a later preparation installs the lobby draw hook in a transaction of its own without touching the rest");
 check(!game_version::needs_gameplay_agreement(),"no participating installer leaves native protocol unenlisted");
 check(gameplay_agreement::register_entity_layout(1,32,224,256,8,true),"public capacity API registers verified explicit retail layout");
 check(game_version::needs_gameplay_agreement() && !game_version::gameplay_online_allowed() && iat_hook::captured,"capacity registration automatically enlists socket exclusion before finalization");
 check(gameplay_agreement::register_rule(2,1,{explosive_first_hit::installed() ? 1u : 0u,0,0,0},explosive_first_hit::verified()),"installed explosion registers its actual effective installer snapshot");
 check(gameplay_agreement::register_rule(3,7,{1,3,0,0},true),"synthetic controller registers revision/values through public API without transport changes");
 check(gameplay_agreement::register_rule(gameplay_capabilities::object_world_impact_id,1,{world_impact::installed() ? 1u : 0u,0,0,0},world_impact::verified()),"actual installed ObjectWorldImpact registers rule 17 without poisoning online");
 check(gameplay_agreement::register_rule(gameplay_capabilities::stun_rule_id,1,{1,0,0,0},true),"synthetic verified StunRule participates alongside rule 17");

 // Only failure-path boundaries are isolated here. Execute retail failure + cleanup code.
 popup_vt[3]=reinterpret_cast<void*>(&show_popup);menu_vt[0x74/4]=reinterpret_cast<void*>(&close_menu);
 jump(native_dialog,&popup_get);jump(native_menu,&menu_get);jump(0x6097b0,&descriptor_get);jump(0x612be0,&session_get);jump(0x6111f0,&no_peer);jump(0x560700,&free_buffer);
 for(auto va:{0x608d10u,0x6088b0u,0x608b40u,0x6159b0u,0x6074b0u,0x607530u,0x607630u,0x672161u})jump(va,&noop);
 jump(0x60b210,&timer_get);jump(0x60afe0,&timer_destroy);jump(0x60f070,&unregister);jump(0x609900,&timeout_trap);
 pending_context();using join=int(__thiscall*)(void*,const void*,int);
 const auto begin=reinterpret_cast<join>(native_join);
 const auto shown=popups;
 check(begin(context.data(),sender.data(),123)==0 && cleaned() && popups==shown+1 && statuses>0 && freed>0 && timers>0 && unregistered>0,"failed local transport executes actual native join failure/cleanup immediately");
 check(visible.find("Incompatible gameplay")!=std::string::npos && visible.find("unverified installation")!=std::string::npos,"native popup vtable receives incompatibility/install-failure explanation");
 // The record freezes after every registration; no further bytes are written at finalization.
 make_rx();check(gameplay_agreement::finalize(reinterpret_cast<HMODULE>(0x400000)),"final shared record verifies with the installed transport");
 check(rx(0x607000) && rx(0x608000) && rx(0x609000),"finalization writes nothing and every shared page stays RX");
 boundary_standins();
 game_version::install(reinterpret_cast<HMODULE>(0x400000));
 auto socket=reinterpret_cast<SOCKET(WSAAPI*)(int,int,int)>(iat_hook::captured);const auto before=popups;
 check(socket && socket(AF_INET,SOCK_DGRAM,0)==INVALID_SOCKET && iat_hook::calls==0 && WSAGetLastError()==WSAEACCES && popups==before+1,"unbound actual socket gate presents native install failure without opening socket");
 check(std::memcmp(at(game_version_rules::version_va),"LC24",5)==0,"release version installer retains LC24");
 game_version::bind_gameplay_agreement(true);check(socket(AF_INET,SOCK_DGRAM,0)==73 && iat_hook::calls==1,"verified final record opens actual socket gate");
 check(begin(context.data(),sender.data(),123)==73,"verified pending join forwards native continuation with original ABI");
 auto local=runtime_peer(true);
 for(const auto [site,opcode,base,mode]:std::array<std::array<std::uint32_t,4>,5>{{{request_send,0x18,request_size,0},{reply_broadcast,0x1f,reply_size,2},{reply_send,0x1f,reply_size,1},{info_send,0x1a,info_size,1},{ack_send,0x1b,ack_size,1}}}){
  input.fill(0x5a);header.fill(0);const auto* body=input.data();std::memcpy(header.data(),&body,4);const auto length=std::uint16_t(base);std::memcpy(header.data()+8,&length,2);header[0x10]=std::uint8_t(opcode);header[0x11]=1;entry=site;ret(site+5);send_count=0;
  if(mode==0)invoke_direct();else if(mode==1)invoke_single();else invoke_broadcast();
  check(send_count==1 && output_type==opcode && output_size==base+local.bytes().size() && std::memcmp(output.data(),input.data(),base)==0 && local.compare({output.data()+base,local.bytes().size()})==result::match,"patched native sender appends full canonical record and preserves original body/opcode");
  check(output_this==0x12345678 && (mode==2 ? output_arg==0xffffffffu && output_extra==2 : output_arg==0x23456789 && output_extra==(mode==0 ? 123 : 0)),"native sender adapter preserves this, stack arguments and cleanup");
  check(header[8]==base && *reinterpret_cast<const std::uint8_t*const*>(header.data())==input.data(),"native original header/body storage remains intact");
 }
 separate_host_joiner();
 host_notice_tests();
 for(const auto [registration,opcode,base,joining]:std::array<std::array<std::uint32_t,4>,4>{{{request_registration,0x18,request_size,0},{reply_registration,0x1f,reply_size,1},{info_registration,0x1a,info_size,0},{ack_registration,0x1b,ack_size,1}}}){
  auto receiver=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(registration+1)));
  input.fill(0);std::memcpy(input.data()+base,local.bytes().data(),local.bytes().size());event e{opcode,input.data(),std::uint16_t(base+local.bytes().size()),0,context.data(),sender.data()};
  put(0x22bc,0);forwarded=0;check(receiver(&e)==73 && forwarded==1,"matching full record forwards registered native admission once");
  auto off=runtime_peer(false);std::memcpy(input.data()+base,off.bytes().data(),off.bytes().size());
  if(opcode==0x18){
   // Game info answering this PC's query (Connect by IP, games list): an incompatible host is not filed; this
   // PC shows the native dialog and tells the host why. No join is pending, so nothing is cleaned up.
   context.fill(0);put(4,0x12345678);put(0x2250,0x22222222);put(0x22bc,0x33333333);sender[0x19]=1;const auto snapshot=context;
   const auto shown=popups,closed=menus,huds=hud_messages,status_before=statuses,free_before=freed,timer_before=timers;const auto lobby_before=lobby();send_count=0;forwarded=0;
   check(receiver(&e)==0 && forwarded==0 && popups==shown+1 && menus==closed && hud_messages==huds && lobby()==lobby_before,"incompatible 0x18 game info is not filed; the querying PC sees the native dialog");
   check(send_count==1 && output_type==0x1d && output_size==refusal_detail_size && read_refusal(output.data(),output_size)==refusal::settings && sender[0x19]==0,"querying PC tells the host why with a reasoned 0x1d");
   check(statuses==status_before && freed==free_before && timers==timer_before && context==snapshot,"0x18 refusal runs no join failure cleanup on the querying PC");
   e.length=std::uint16_t(base);forwarded=0;send_count=0;check(receiver(&e)==0 && forwarded==0 && send_count==1 && read_refusal(output.data(),output_size)==refusal::foreign_record,"0x18 game info without a record (older host) is refused as another build");
   continue;
  }
  if(joining){pending_context();if(opcode==0x1b)put(0x22bc,0);}else {context.fill(0);put(4,0x12345678);put(0x2250,0x22222222);put(0x22bc,0x33333333);}
  sender[0x19]=1;const auto shown_before=popups;const auto lobby_before=lobby();send_count=0;logger::logged.clear();const auto response=receiver(&e);
  check(response==(joining ? 2 : 0) && forwarded==1 && (joining ? popups==shown_before+1 && lobby()==lobby_before : popups==shown_before && lobby()==host_notice_detail(refusal::settings,false)) && send_count==1 && output_type==0x1d && output_size==refusal_detail_size && read_refusal(output.data(),output_size)==refusal::settings,
        joining ? "joiner mismatch sends reasoned rejection and shows its native failure without admission" : "host mismatch sends reasoned rejection and shows the non-modal lobby notice without admission");
  check(read_refusal_buckets(output.data(),output_size)==(std::uint64_t(1)<<gameplay_capabilities::bucket_of(2)) && has_log("differs: 2 ExplosiveFirstHit") && (!joining || visible.find("Incompatible gameplay settings: ExplosiveFirstHit.")!=std::string::npos),
        "the refusal names the differing rule: its 0x1d summary buckets, the log line and the joining PC's dialog");
  check(sender[0x19]==0 && (joining ? cleaned() : get(0x2250)==0x22222222 && get(0x22bc)==0x33333333),"retail peer flag clears; joining state cleans while active host preserves existing participants");
  if(!joining){pending_context();if(opcode==0x1a)put(0x22bc,0);const auto remote_body=output;event remote{output_type,const_cast<std::uint8_t*>(remote_body.data()),output_size,0,context.data(),sender.data()};auto remote_receiver=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(rejection_registration+1)));const auto visible_before=popups;logger::logged.clear();check(remote_receiver(&remote)==2 && cleaned() && popups==visible_before+1,"remote native incompatibility reception ends pending join through actual failure cleanup");
   check(has_log("the other PC refused this join (reason=1; differs: 2 ExplosiveFirstHit)") && visible.find("Incompatible gameplay settings: ExplosiveFirstHit.")!=std::string::npos,"the joining PC names the setting the host found different (log and dialog)");}
  e.length=std::uint16_t(base);put(0x22bc,0);const auto legacy_before=forwarded;check(receiver(&e)==(joining ? 2 : 0) && forwarded==legacy_before,"legacy body without full record refuses before native admission");
  // A protocol-2 peer (1.4.0, the 1.5 pre-release) appends its full 312-byte list: read far enough to see it is another
  // protocol, refused as another build, never forwarded; a body longer than any record is malformed, never forwarded.
  {std::vector<std::uint8_t> v2{'G','C','A','P',2,0,15,0,0x38,1,0,0};v2.resize(312);input.fill(0);std::memcpy(input.data()+base,v2.data(),v2.size());
   if(joining)pending_context();e.length=std::uint16_t(base+v2.size());put(0x22bc,0);send_count=0;logger::logged.clear();const auto v2_before=forwarded;
   check(receiver(&e)==(joining ? 2 : 0) && forwarded==v2_before && send_count==1 && read_refusal(output.data(),output_size)==refusal::foreign_record && has_log("refused (reason=3)"),
         "a protocol-2 body (312-byte record) is read as another protocol and refused as another build before native admission");
   if(joining)pending_context();e.length=std::uint16_t(base+513);put(0x22bc,0);send_count=0;logger::logged.clear();
   check(receiver(&e)==(joining ? 2 : 0) && forwarded==v2_before && send_count==1 && read_refusal(output.data(),output_size)==refusal::foreign_record && has_log("refused (reason=2)"),
         "a body longer than any record is malformed and never reaches native admission");}
 }
 for(const auto omitted:{17,18}){
  record other;other.add(2,1,{1,0,0,0},true);other.add(3,7,{1,3,0,0},true);
  for(const auto id:{17,18})if(id!=omitted)other.add(static_cast<std::uint16_t>(id),1,{1,0,0,0},true);
  other.finalize();input.fill(0);std::memcpy(input.data()+request_size,other.bytes().data(),other.bytes().size());
  context.fill(0);put(4,0x12345678);sender[0x19]=1;
  event e{0x18,input.data(),std::uint16_t(request_size+other.bytes().size()),0,context.data(),sender.data()};
  auto receiver=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(request_registration+1)));
  const auto shown=popups;logger::logged.clear();forwarded=0;
  check(receiver(&e)==0 && forwarded==0 && popups==shown+1 && visible.find(gameplay_capabilities::rule_name(static_cast<std::uint16_t>(omitted)))!=std::string::npos,"actual native admission refuses and names missing ObjectWorldImpact/StunRule");
 }
 ini::lc=false;ini::requested=false;game_version::install(reinterpret_cast<HMODULE>(0x400000));check(game_version::needs_gameplay_agreement() && !game_version::gameplay_online_allowed(),"explicit non-LC off configuration still requires verified shared agreement");
 game_version::bind_gameplay_agreement(true);check(socket(AF_INET,SOCK_DGRAM,0)==73,"verified non-LC off gate retains native socket forwarding");
 ini::key=false;iat_hook::captured=nullptr;game_version::install(reinterpret_cast<HMODULE>(0x400000));check(!game_version::needs_gameplay_agreement() && !iat_hook::captured,"unopted vanilla leaves native socket untouched");
 for(int option=0;option<2;++option){
  ini::testing_forge=option==0;ini::testing_hotkeys=option==1;
  game_version::install(reinterpret_cast<HMODULE>(0x400000));game_version::bind_gameplay_agreement(true);
  check(game_version::test_mode_active() && game_version::needs_gameplay_agreement() && !game_version::gameplay_online_allowed(),"either testing option blocks even a verified agreement in non-LC installs");
  const auto calls=iat_hook::calls;
  socket=reinterpret_cast<SOCKET(WSAAPI*)(int,int,int)>(iat_hook::captured);
  check(socket(AF_INET,SOCK_DGRAM,0)==INVALID_SOCKET && iat_hook::calls==calls,"testing socket exclusion opens no socket");
  check(game_version::take_test_mode_refusal() && std::strstr(game_version::test_mode_reason(),"[TestMode]"),"testing socket refusal supplies the named explanation to the popup adapter");
  check(!game_version::take_test_mode_refusal(),"testing refusal is consumed once per failed attempt");
  socket(AF_INET,SOCK_DGRAM,0);game_version::clear_test_mode_refusal();
  check(!game_version::take_test_mode_refusal(),"a refused attempt with no popup expires before an unrelated dialog");
  pending_context();const auto testing_join_shown=popups;
  check(begin(context.data(),sender.data(),123)==0 && cleaned() && popups==testing_join_shown+1 && visible.find("[TestMode]")!=std::string::npos,"testing join exclusion cleans pending join and names [TestMode]");
 }
 ini::testing_forge=ini::testing_hotkeys=false;
 ini::key=true;iat_hook::fail=true;game_version::install(reinterpret_cast<HMODULE>(0x400000));check(!game_version::startup_allowed(),"failed socket exclusion blocks startup safely");
 game_version::bind_gameplay_agreement(true);check(game_version::gameplay_online_allowed(),"gate is verified immediately before late registration");
 check(!gameplay_agreement::register_rule(4,1,{1,0,0,0},true) && !game_version::gameplay_online_allowed(),"public API rejects late controller registration and closes actual gate");
 pending_context();const auto final_before=popups;check(begin(context.data(),sender.data(),123)==0 && cleaned() && popups==final_before+1,"unverified effective record rejects retry through actual native failure path");
}
int main(int argc,char** argv){
 std::setbuf(stdout,nullptr);
 records();
 const char* path=argc>1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");if(!path){std::puts("Requires a private retail executable for native refusal-code fixture; no game launch.");return 2;}
 std::ifstream file(path,std::ios::binary);binary.assign(std::istreambuf_iterator<char>(file),{});if(binary.size()<0x1000)return 2;
 const auto begin=reinterpret_cast<std::uintptr_t>(fixture_space);if(GetModuleHandleW(nullptr)!=reinterpret_cast<HMODULE>(0x400000) || native_request<begin || native_broadcast+16>begin+sizeof(fixture_space))return 2;
 if(argc>2 && std::string(argv[2])=="--receiver-guard-mismatch")return receiver_guard_mismatch();
 explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));check(!explosive_first_hit::installed() && explosive_first_hit::verified(),"actual disabled installer reports verified off");
 ini::requested=true;explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400001));check(!explosive_first_hit::installed() && !explosive_first_hit::verified(),"actual failed explosion install remains unverified");
 native_bytes(0x49ab51,28);explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));check(explosive_first_hit::installed() && explosive_first_hit::verified(),"actual explosion patch reports installed effective state");
 ini::requested=false;explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));check(explosive_first_hit::installed(),"later INI drift cannot relabel installed code");
 world_impact::install(reinterpret_cast<HMODULE>(0x400000));
 check(!world_impact::requested() && world_impact::verified(),"actual absent ObjectWorldImpact is verified OFF");
 ini::impact="xml1";world_impact::install(reinterpret_cast<HMODULE>(0x400001));
 check(world_impact::requested() && !world_impact::installed() && !world_impact::verified(),"actual failed ObjectWorldImpact is unverified");
 for(const auto& g:world_impact_rules::guards)native_bytes(g.va,g.hex.size()/2);
 world_impact::install(reinterpret_cast<HMODULE>(0x400000));
 check(world_impact::requested() && world_impact::installed() && world_impact::verified(),"actual guarded ObjectWorldImpact install is verified");
 runtime();receiver_guard_child(path);std::printf("Agreement/native refusal: %d failure(s)\n",failures);return failures ? 1 : 0;
}
