#include <WinSock2.h>
#include "gameplay_agreement.hpp"
#include "gameplay_agreement_rules.hpp"
#include "game_version.hpp"
#include "game_version_rules.hpp"
#include "explosive_first_hit.hpp"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include <cwchar>

namespace ini {
 bool requested=false,lc=true,key=true;
 bool flag(const wchar_t*,const wchar_t*,bool){return requested;}
 int number(const wchar_t*,const wchar_t*,int fallback){return fallback;} // XML1 diagnostic opt-in stays off
 std::optional<std::string> text(const wchar_t* section,const wchar_t* name){
  if(std::wcscmp(section,L"Save")==0)return lc ? std::optional<std::string>("lc") : std::nullopt;
  if(std::wcscmp(section,L"Online")==0)return lc ? std::optional<std::string>("LC24") : std::nullopt;
  if(std::wcscmp(name,L"ExplosiveFirstHit")==0 && key)return requested ? "1" : "0";
  return std::nullopt;
 }
}
namespace logger { void write(const char*,...){} void write_once(std::string_view,const char*,...){} }
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
 record native;native.finalize();check(native.bytes().size()==32,"default record contains explicit retail entity layout");
 auto bytes=std::vector<std::uint8_t>(off.bytes().begin(),off.bytes().end());
 check(bytes[16]==32 && bytes[20]==224 && bytes[25]==1 && bytes[28]==8,"retail layout is actor32/generic224/world256/token8");
 bytes[4]=2;check(off.compare(bytes)==result::protocol,"unknown record revision refuses");bytes.assign(off.bytes().begin(),off.bytes().end());
 bytes[34]=2;check(off.compare(bytes)==result::revision,"unknown rule revision refuses");bytes.assign(off.bytes().begin(),off.bytes().end());
 bytes[32]=42;check(off.compare(bytes)==result::entries,"unknown rule id refuses");bytes.assign(off.bytes().begin(),off.bytes().end());
 bytes[32]=1;check(off.compare(bytes)==result::entries,"duplicate/unsorted peer id refuses");bytes.assign(off.bytes().begin(),off.bytes().end());
 bytes[8]--;check(off.compare(bytes)==result::malformed,"invalid declared record length refuses");bytes.assign(off.bytes().begin(),off.bytes().end());
 bytes[10]=1;check(off.compare(bytes)==result::malformed,"unknown reserved header flags refuse");
 check(off.compare({})==result::malformed,"missing old-peer record refuses");
 record layout;layout.layout(2,{64,448,512,16},true);layout.add(2,1,{0,0,0,0},true);layout.finalize();check(off.compare(layout.bytes())==result::revision,"different entity/token layout revision refuses");
 record capacity;capacity.layout(1,{64,192,256,8},true);capacity.add(2,1,{0,0,0,0},true);capacity.finalize();check(off.compare(capacity.bytes())==result::value,"different effective capacity refuses");
 record token;token.layout(1,{32,224,256,16},true);token.add(2,1,{0,0,0,0},true);token.finalize();check(off.compare(token.bytes())==result::value,"different token width refuses");
 record missing;missing.add(3,1,{1,0,0,0},true);missing.add(2,1,{0,0,0,0},true);missing.finalize();check(off.compare(missing.bytes())==result::entries,"missing controller entry refuses");
 record sorted;sorted.add(2,1,{0,0,0,0},true);sorted.add(3,1,{1,0,0,0},true);sorted.finalize();check(sorted.compare(missing.bytes())==result::match,"registration order canonicalizes by explicit rule id");
 record duplicate;duplicate.add(2,1,{0,0,0,0},true);check(!duplicate.add(2,1,{0,0,0,0},true) && !duplicate.finalize(),"duplicate registration poisons finalization");
 record late;late.finalize();check(!late.add(2,1,{0,0,0,0},true) && !late.ready() && late.bytes().empty(),"late registration cannot change a frozen advertised record");
 record invalid;check(!invalid.layout(1,{32,224,256,7},true) && !invalid.finalize(),"token capacity overflow refuses");
 record sum;check(!sum.layout(1,{32,225,256,8},true),"inconsistent world capacity refuses");
 record twice;twice.layout(1,{32,224,256,8},true);check(!twice.layout(1,{32,224,256,8},true) && !twice.finalize(),"duplicate layout owner cannot overwrite an earlier effective layout");
 record bad_layout;check(!bad_layout.layout(2,{64,448,512,16},false) && !bad_layout.finalize(),"failed entity installer cannot silently advertise retail");
 record full;for(std::uint16_t id=2;id<=8;++id)full.add(id,1,{id,0,0,0},true);check(full.finalize() && full.bytes().size()==gameplay_capabilities::max_size,"bounded eight-entry record serializes explicit values");
 record overflow;for(std::uint16_t id=2;id<=8;++id)overflow.add(id,1,{id,0,0,0},true);check(!overflow.add(9,1,{0,0,0,0},true) && !overflow.finalize(),"ninth record entry fails closed");
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
void* popup_vt[4]{};void* menu_vt[30]{};void** popup_object=popup_vt;void** menu_object=menu_vt;
void* popup_get(){return &popup_object;}void* menu_get(){return &menu_object;}
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
std::array<std::uint8_t,request_size+gameplay_capabilities::max_size> output{},input{};
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
__declspec(naked) void join_continuation(){__asm {pop ebp} __asm {pop ebx} __asm {add esp,0x1c} __asm {mov eax,73} __asm {ret 8}}
void boundary_standins(){
 popup_vt[3]=reinterpret_cast<void*>(&show_popup);menu_vt[0x74/4]=reinterpret_cast<void*>(&close_menu);
 jump(native_dialog,&popup_get);jump(native_menu,&menu_get);jump(0x6097b0,&descriptor_get);jump(0x612be0,&session_get);jump(0x6111f0,&no_peer);jump(0x560700,&free_buffer);
 for(auto va:{0x608d10u,0x6088b0u,0x608b40u,0x6159b0u,0x6074b0u,0x607530u,0x607630u,0x672161u})jump(va,&noop);
 jump(0x60b210,&timer_get);jump(0x60afe0,&timer_destroy);jump(0x60f070,&unregister);jump(0x609900,&timeout_trap);
 jump(native_direct,&direct_fake);jump(native_send,&single_fake);jump(native_broadcast,&broadcast_fake);
 for(auto va:{native_request,native_reply,native_info,native_ack})jump(va,&admitted);
 jump(join_resume,&join_continuation);
}
record runtime_peer(bool on){record r;r.add(2,1,{on ? 1u : 0u,0,0,0},true);r.add(3,7,{1,3,0,0},true);r.finalize();return r;}
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
  check(statuses==callbacks_before && freed==free_before && timers==timer_before && unregistered==unregister_before && menus==menu_before && popups==popup_before+1,
        "host refusal shows dialog without status9, buffer release, callback teardown or menu close");
  check(send_count==1 && output_type==0x1d && output_size==0 && output_arg==reinterpret_cast<std::uintptr_t>(sender.data()+12),"host sends native rejection to refused joining endpoint");
  selected_descriptor=joiner_descriptor.data();selected_session=joiner.data();
  event remote{output_type,nullptr,0,0,joiner.data(),joiner_sender.data()};
  check(rejection_receiver(&remote)==2 && statuses==callbacks_before+1 && freed==free_before+1 && timers==timer_before+1 && unregistered==unregister_before+1 && menus==menu_before+1 && popups==popup_before+2,
        "separate pending joiner receives rejection and executes actual native cleanup/dialog");
  auto field=[&](std::size_t at){std::uint32_t v;std::memcpy(&v,joiner.data()+at,4);return v;};
  check(field(0x2250)==0 && field(0x22bc)==0 && field(0x2230)==0 && field(0x21f4)==0 && field(0x221c)==0xffffffff && joiner_sender[0x19]==0 && last_freed==reinterpret_cast<void*>(buffer) && last_timer==reinterpret_cast<void*>(timer),
        "cleanup releases only joiner-owned transfer buffer/timer and clears joiner state");
  check(context==host_snapshot && descriptor==descriptor_snapshot && existing_participant==participant_snapshot,"remote joiner cleanup cannot mutate distinct host state or participants");
  selected_descriptor=descriptor.data();selected_session=context.data();
 }
}
void runtime(){
 guards(failure_guards);guards(transport_guards);guards(game_version_rules::guards);
 native_bytes(native_failure,0xa7);native_bytes(native_cleanup,0x10c);native_bytes(native_peer_disconnect,10);
 const auto original=*static_cast<std::uint8_t*>(at(native_join));
 check(!gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400001)) && *static_cast<std::uint8_t*>(at(native_join))==original,"wrong base refuses native refusal hooks");
 writable(failure_text,1);*static_cast<std::uint8_t*>(at(failure_text))^=1;
 check(!gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && *static_cast<std::uint8_t*>(at(native_join))==original,"one native failure guard mismatch prevents all writes");native_bytes(failure_text,5);
 make_rx();check(gameplay_agreement::prepare(reinterpret_cast<HMODULE>(0x400000)) && rx(0x609000),"native refusal preparation restores shared RX page");
 check(!game_version::needs_gameplay_agreement(),"no participating installer leaves native protocol unenlisted");
 check(gameplay_agreement::register_entity_layout(1,32,224,256,8,true),"public capacity API registers verified explicit retail layout");
 check(game_version::needs_gameplay_agreement() && !game_version::gameplay_online_allowed() && iat_hook::captured,"capacity registration automatically enlists socket exclusion before finalization");
 check(gameplay_agreement::register_rule(2,1,{explosive_first_hit::installed() ? 1u : 0u,0,0,0},explosive_first_hit::verified()),"installed explosion registers its actual effective installer snapshot");
 check(gameplay_agreement::register_rule(3,7,{1,3,0,0},true),"synthetic controller registers revision/values through public API without transport changes");
 writable(reply_send,1);*static_cast<std::uint8_t*>(at(reply_send))^=1;make_rx();
 check(!gameplay_agreement::finalize(reinterpret_cast<HMODULE>(0x400000)) && rx(0x607000) && rx(0x608000) && rx(0x609000),"failed transport guard preserves every original RX page");
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
 // Restore boundary signatures for final transport verification; no gameplay is started.
 guards(transport_guards);native_bytes(reply_send,5);make_rx();
 check(gameplay_agreement::finalize(reinterpret_cast<HMODULE>(0x400000)),"final shared record and production transport install verify");
 check(rx(0x607000) && rx(0x608000) && rx(0x609000),"all successful shared-page acquisitions restore final RX protection");
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
 for(const auto [registration,opcode,base,joining]:std::array<std::array<std::uint32_t,4>,4>{{{request_registration,0x18,request_size,0},{reply_registration,0x1f,reply_size,1},{info_registration,0x1a,info_size,0},{ack_registration,0x1b,ack_size,1}}}){
  auto receiver=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(registration+1)));
  input.fill(0);std::memcpy(input.data()+base,local.bytes().data(),local.bytes().size());event e{opcode,input.data(),std::uint16_t(base+local.bytes().size()),0,context.data(),sender.data()};
  put(0x22bc,0);forwarded=0;check(receiver(&e)==73 && forwarded==1,"matching full record forwards registered native admission once");
  auto off=runtime_peer(false);std::memcpy(input.data()+base,off.bytes().data(),off.bytes().size());
  if(joining){pending_context();if(opcode==0x1b)put(0x22bc,0);}else {context.fill(0);put(4,0x12345678);put(0x2250,0x22222222);put(0x22bc,0x33333333);}
  sender[0x19]=1;const auto shown_before=popups;send_count=0;const auto response=receiver(&e);
  check(response==(joining ? 2 : 0) && forwarded==1 && popups==shown_before+1 && send_count==1 && output_type==0x1d && output_size==0,"mismatch sends native remote rejection and immediate visible local failure without admission");
  check(sender[0x19]==0 && (joining ? cleaned() : get(0x2250)==0x22222222 && get(0x22bc)==0x33333333),"retail peer flag clears; joining state cleans while active host preserves existing participants");
  if(!joining){pending_context();if(opcode==0x1a)put(0x22bc,0);event remote{output_type,nullptr,0,0,context.data(),sender.data()};auto remote_receiver=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(rejection_registration+1)));const auto visible_before=popups;check(remote_receiver(&remote)==2 && cleaned() && popups==visible_before+1,"remote native incompatibility reception ends pending join through actual failure cleanup");}
  e.length=std::uint16_t(base);put(0x22bc,0);const auto legacy_before=forwarded;check(receiver(&e)==(joining ? 2 : 0) && forwarded==legacy_before,"legacy body without full record refuses before native admission");
 }
 ini::lc=false;ini::requested=false;game_version::install(reinterpret_cast<HMODULE>(0x400000));check(game_version::needs_gameplay_agreement() && !game_version::gameplay_online_allowed(),"explicit non-LC off configuration still requires verified shared agreement");
 game_version::bind_gameplay_agreement(true);check(socket(AF_INET,SOCK_DGRAM,0)==73,"verified non-LC off gate retains native socket forwarding");
 ini::key=false;iat_hook::captured=nullptr;game_version::install(reinterpret_cast<HMODULE>(0x400000));check(!game_version::needs_gameplay_agreement() && !iat_hook::captured,"unopted vanilla leaves native socket untouched");
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
 explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));check(!explosive_first_hit::installed() && explosive_first_hit::verified(),"actual disabled installer reports verified off");
 ini::requested=true;explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400001));check(!explosive_first_hit::installed() && !explosive_first_hit::verified(),"actual failed explosion install remains unverified");
 native_bytes(0x49ab51,28);explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));check(explosive_first_hit::installed() && explosive_first_hit::verified(),"actual explosion patch reports installed effective state");
 ini::requested=false;explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));check(explosive_first_hit::installed(),"later INI drift cannot relabel installed code");
 runtime();std::printf("Agreement/native refusal: %d failure(s)\n",failures);return failures ? 1 : 0;
}
