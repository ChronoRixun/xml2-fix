#include <WinSock2.h>
#include "game_version.hpp"
#include "game_version_rules.hpp"
#include "iat_hook.hpp"
#include "save_guard_rules.hpp"
#include "gameplay_agreement.hpp"
#include "explosive_first_hit.hpp"
#include <cstdarg>
#include "gameplay_agreement_rules.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ini {
 bool requested=false,lc=true,key=true;
 bool flag(const wchar_t*,const wchar_t*,bool) { return requested; }
 std::optional<std::string> text(const wchar_t* section,const wchar_t* name) {
  if(std::wcscmp(section,L"Save")==0)return lc ? std::optional<std::string>("lc") : std::nullopt;
  if(std::wcscmp(section,L"Online")==0)return lc ? std::optional<std::string>("LC24") : std::nullopt;
  if(std::wcscmp(name,L"ExplosiveFirstHit")==0 && key)return requested ? "1" : "0";
  return std::nullopt;
 }
}
namespace logger { void write_once(std::string_view,const char*,...) {} std::vector<std::string> messages; void write(const char* fmt,...) { char text[1024]; va_list args; va_start(args,fmt); std::vsnprintf(text,sizeof(text),fmt,args); va_end(args); messages.emplace_back(text); } }
namespace iat_hook {
 void* captured=nullptr;bool fail=false;int socket_calls;
 SOCKET WSAAPI fake_socket(int,int,int) { ++socket_calls;return 73; }
 void* hook(HMODULE,const char*,const char*,WORD,void* replacement) { captured=replacement;return fail ? nullptr : reinterpret_cast<void*>(&fake_socket); }
}
#pragma section(".peerfix",read,write,execute)
__declspec(allocate(".peerfix")) std::uint8_t fixture_space[0x400000]{};
using namespace gameplay_agreement_rules;
struct peer { bool enabled, patched; };
int failures, forwarded;
void check(bool value,const char* text) { std::printf("%s %s\n",value ? "PASS" : "FAIL",text); failures += !value; }
bool effective(peer p) { return p.enabled && p.patched; }
bool verified(peer p) { return !p.enabled || p.patched; }
bool admitted(peer host,peer client)
{
	return save_guard_rules::online_allowed(true,true,true,verified(host)) &&
		   save_guard_rules::online_allowed(true,true,true,verified(client)) &&
		   agrees(effective(host),wire(effective(client))) && agrees(effective(client),wire(effective(host)));
}
void* at(std::uint32_t address) { return reinterpret_cast<void*>(address); }
void reset()
{
	for(const auto& g:guards) {
		auto* p=static_cast<std::uint8_t*>(at(g.va)); DWORD old;
		VirtualProtect(p,g.hex.size()/2,PAGE_EXECUTE_READWRITE,&old);
		for(std::size_t i=0;i<g.hex.size();i+=2) {
			auto digit=[](char c){return c<='9' ? c-'0' : c-'a'+10;};
			p[i/2]=static_cast<std::uint8_t>(digit(g.hex[i])*16+digit(g.hex[i+1]));
		}
	}
}
void jump(std::uint32_t site,const void* target)
{
	auto* code=static_cast<std::uint8_t*>(at(site));code[0]=0xe9;
	auto rel=static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target)-site-5);std::memcpy(code+1,&rel,4);
}
int native_fake(void*) { ++forwarded;return 73; }
// Invoke the actual patched writer and capture its original stack slots/registers.
std::uint32_t captured_word,captured_other,captured_eax,captured_ebp;
const std::uint32_t request_entry=request_writer,reply_entry=reply_writer,info_entry=info_writer;
void capture_request();void capture_reply();
__declspec(naked) void invoke_request()
{
	__asm { sub esp,0x90 } __asm { xor eax,eax }
	__asm { jmp dword ptr [request_entry] }
}
__declspec(naked) void capture_request()
{
	__asm { mov dword ptr [captured_eax],eax }
	__asm { mov eax,[esp+0x80] } __asm { mov dword ptr [captured_word],eax }
	__asm { mov eax,[esp+0x84] } __asm { mov dword ptr [captured_other],eax }
	__asm { add esp,0x90 } __asm { ret }
}
__declspec(naked) void invoke_reply()
{
	__asm { push ebp } __asm { sub esp,0x80 }
	__asm { xor ebp,ebp } __asm { mov eax,0x3432434c }
	__asm { jmp dword ptr [reply_entry] }
}
__declspec(naked) void capture_reply()
{
	__asm { mov dword ptr [captured_eax],eax } __asm { mov dword ptr [captured_ebp],ebp }
	__asm { mov eax,[esp+0x54] } __asm { mov dword ptr [captured_word],eax }
	__asm { mov eax,[esp+0x58] } __asm { mov dword ptr [captured_other],eax }
	__asm { add esp,0x80 } __asm { pop ebp } __asm { ret }
}
std::array<std::uint8_t,0x430> fake_session{};
std::uint32_t captured_ecx;
__declspec(naked) void invoke_info()
{
	__asm { mov eax, offset fake_session } __asm { mov ecx,0x12345678 }
	__asm { jmp dword ptr [info_entry] }
}
__declspec(naked) void capture_info()
{
	__asm { mov dword ptr [captured_eax],eax } __asm { mov dword ptr [captured_ecx],ecx }
	__asm { ret }
}
std::array<std::uint8_t,8> ack_input{1,2,3,4,5,6,7,8};
std::array<std::uint8_t,0x14> ack_header{},sent_header{};
std::array<std::uint8_t,ack_size> ack_output{};
std::uint32_t sent_session,sent_transport;int sent_flags,send_calls;std::uint16_t sent_length;
void __fastcall send_fake(void* session,void*,void* transport,const std::uint8_t* header,int flags)
{
	++send_calls;sent_session=reinterpret_cast<std::uintptr_t>(session);sent_transport=reinterpret_cast<std::uintptr_t>(transport);sent_flags=flags;
	std::memcpy(&sent_length,header+8,2);std::memcpy(sent_header.data(),header,sent_header.size());
	std::memcpy(ack_output.data(),*reinterpret_cast<const void* const*>(header),ack_output.size());
}
const std::uint32_t ack_entry=ack_writer;
std::uint8_t* ack_header_pointer=ack_header.data();
__declspec(naked) void invoke_ack_code()
{
	__asm { push 0 } __asm { push dword ptr [ack_header_pointer] }
	__asm { push 0x23456789 } __asm { mov ecx,0x12345678 }
	__asm { jmp dword ptr [ack_entry] }
}
void invoke_ack()
{
	ack_header.fill(0);const auto* body=ack_input.data();std::memcpy(ack_header.data(),&body,4);
	ack_header[8]=8;ack_header[0x10]=0x1b;send_calls=0;invoke_ack_code();
}
struct event { std::uint32_t opcode; void* body; std::uint16_t length,unused; std::uint32_t context,sender; };
void receivers(bool local)
{
	std::array<std::uint8_t,request_size> request{};
	std::array<std::uint8_t,reply_size> reply{};
	std::array<std::uint8_t,info_size> info{};
	event a{0x18,request.data(),request_size,0,0,0},b{0x1f,reply.data(),reply_size,0,0,0},c{0x1a,info.data(),info_size,0,0,0};
	std::array<std::uint8_t,ack_size> ack{}; event d{0x1b,ack.data(),ack_size,0,0,0};
	auto set=[](auto& body,std::size_t offset,std::uint32_t value){std::memcpy(body.data()+offset,&value,4);};
	auto req=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(request_registration+1)));
	auto rep=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(reply_registration+1)));
	auto inf=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(info_registration+1)));
	auto follow=reinterpret_cast<int(__cdecl*)(void*)>(*reinterpret_cast<std::uint32_t*>(at(ack_registration+1)));
	set(ack,ack_word,wire(!local));
	set(info,info_word,wire(!local));
	set(request,request_word,wire(!local));set(reply,reply_word,wire(!local));forwarded=0;
	check(req(&a)==0 && rep(&b)==0 && inf(&c)==0 && follow(&d)==0 && forwarded==0,"all registered receivers refuse mismatch before native admission");
	set(ack,ack_word,0);set(info,info_word,0);set(request,request_word,0);set(reply,reply_word,wire_base+2);
	check(req(&a)==0 && rep(&b)==0 && inf(&c)==0 && follow(&d)==0 && forwarded==0,"legacy/missing and unknown agreement words refuse");
	set(ack,ack_word,wire(local));set(info,info_word,wire(local));set(request,request_word,wire(local));set(reply,reply_word,wire(local));
	check(req(&a)==73 && rep(&b)==73 && inf(&c)==73 && follow(&d)==73 && forwarded==4,"matching peers call native handlers once in both directions");
	d.length=8;
	check(follow(&d)==0 && forwarded==4,"stock host-info response: legacy eight-byte follow-up cannot admit even with matching adjacent tag");
	d.length=ack_size; set(ack,ack_word,wire(!local));
	check(follow(&d)==0 && forwarded==4,"opposite follow-up rule refuses final admission");
	c.length=info_size-1; check(inf(&c)==0 && forwarded==4,"truncated decoded body refuses before rule read");
	a.body=nullptr;check(req(&a)==0 && forwarded==4,"invalid body refuses without native access");
}
void socket_gate()
{
 for(const auto& g:game_version_rules::guards) {
  auto* code=static_cast<std::uint8_t*>(at(g.va));DWORD old;VirtualProtect(code,g.hex.size()/2,PAGE_EXECUTE_READWRITE,&old);
  for(std::size_t i=0;i<g.hex.size();i+=2)code[i/2]=static_cast<std::uint8_t>(std::stoul(std::string(g.hex.substr(i,2)),nullptr,16));
 }
 ini::lc=true;ini::key=true;iat_hook::socket_calls=0;
 game_version::install(reinterpret_cast<HMODULE>(0x400000));
 auto socket=reinterpret_cast<SOCKET(WSAAPI*)(int,int,int)>(iat_hook::captured);
 check(game_version::startup_allowed() && game_version::needs_gameplay_agreement() && socket && socket(AF_INET,SOCK_DGRAM,0)==INVALID_SOCKET && WSAGetLastError()==WSAEACCES && iat_hook::socket_calls==0,"actual LC socket gate closes before effective-rule binding despite successful version patch");
 check(std::memcmp(at(game_version_rules::version_va),"LC24",5)==0,"actual version installer keeps release token LC24");
 game_version::bind_gameplay_agreement(false);
 check(socket(AF_INET,SOCK_DGRAM,0)==INVALID_SOCKET && iat_hook::socket_calls==0,"actual socket gate refuses failed agreement installation");
 game_version::bind_gameplay_agreement(true);
 check(socket(AF_INET,SOCK_DGRAM,0)==73 && iat_hook::socket_calls==1,"actual socket gate forwards after verified effective-rule binding");
 ini::lc=false;ini::key=true;ini::requested=false;
 game_version::install(reinterpret_cast<HMODULE>(0x400000));
 check(game_version::needs_gameplay_agreement() && socket(AF_INET,SOCK_DGRAM,0)==INVALID_SOCKET,"explicit non-LC off key also requires agreement");
 game_version::bind_gameplay_agreement(true);
 check(socket(AF_INET,SOCK_DGRAM,0)==73,"verified non-LC off peer can use the native socket");
 ini::key=false;iat_hook::captured=nullptr;game_version::install(reinterpret_cast<HMODULE>(0x400000));
 check(!game_version::needs_gameplay_agreement() && !iat_hook::captured,"unopted vanilla leaves socket IAT untouched");
 ini::key=true;iat_hook::fail=true;game_version::install(reinterpret_cast<HMODULE>(0x400000));
 check(!game_version::startup_allowed(),"actual exclusion installation failure blocks startup safely");
}
int main()
{
	std::setbuf(stdout,nullptr);
	check(!admitted({true,true},{false,false}),"host on / client off refuses");
	check(!admitted({false,false},{true,true}),"host off / client on refuses");
	check(!admitted({true,false},{true,true}),"host installation failure refuses");
	check(!admitted({true,true},{true,false}),"client installation failure refuses");
	check(admitted({true,true},{true,true}),"both on accepts");
	check(admitted({false,false},{false,false}),"both off accepts");
	check(!admitted({true,false},{false,false}) && !admitted({false,false},{true,false}),"unverified install never pretends to be a verified off rule");
	check(!save_guard_rules::online_allowed(true,true,true,false),"version success cannot bypass failed gameplay hook installation");
	check(save_guard_rules::online_allowed(false,false,false,false),"ordinary XML2 without the opt-in retains stock online gate");
	const auto begin=reinterpret_cast<std::uintptr_t>(fixture_space);
	if(GetModuleHandleW(nullptr)!=reinterpret_cast<HMODULE>(0x400000) || native_request<begin || reply_registration+8>begin+sizeof(fixture_space))return 2;
	explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));
	check(!explosive_first_hit::installed() && explosive_first_hit::verified(),"actual disabled installer reports verified native rule");
	ini::requested=true;explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400001));
	check(!explosive_first_hit::installed() && !explosive_first_hit::verified(),"actual failed installer is not an advertised installed rule");
	check(!admitted({true,explosive_first_hit::installed()},{true,true}) && !admitted({true,true},{true,explosive_first_hit::installed()}),"actual installation failure refuses on either peer");
	{ const std::string signature="f687ab00000008740d8b4730408946285e5b5fc204008b4f30894e28";
	  auto* code=static_cast<std::uint8_t*>(at(0x49ab51)); DWORD old; VirtualProtect(code,28,PAGE_EXECUTE_READWRITE,&old);
	  for(std::size_t i=0;i<signature.size();i+=2)code[i/2]=static_cast<std::uint8_t>(std::stoul(signature.substr(i,2),nullptr,16)); }
	explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));
	check(explosive_first_hit::installed() && explosive_first_hit::verified(),"actual patched bytes produce installed rule snapshot");
	ini::requested=false;explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400000));
	check(explosive_first_hit::installed(),"later INI change cannot relabel still-patched code as native");
	reset();auto first=*static_cast<std::uint8_t*>(at(request_writer));
	*static_cast<std::uint8_t*>(at(reply_writer))^=1;
	check(!gameplay_agreement::install(reinterpret_cast<HMODULE>(0x400000),true) && *static_cast<std::uint8_t*>(at(request_writer))==first,"one bad guard prevents every handshake write");
	reset();check(!gameplay_agreement::install(reinterpret_cast<HMODULE>(0x400001),true) && *static_cast<std::uint8_t*>(at(request_writer))==first,"wrong image base refuses every handshake write");
	for(bool local:{false,true}) {
		reset();check(gameplay_agreement::install(reinterpret_cast<HMODULE>(0x400000),local),"production agreement installer verifies on/off");
		jump(native_ack,&native_fake);jump(native_send,&send_fake);*static_cast<std::uint8_t*>(at(ack_writer+5))=0xc3;
		jump(native_info,&native_fake);jump(native_request,&native_fake);jump(native_reply,&native_fake);
		jump(info_next,&capture_info);jump(request_next,&capture_request);jump(reply_next,&capture_reply);
		invoke_request();check(captured_word==wire(local) && captured_other==0 && captured_eax==0,"request writer stamps effective rule, preserves reserved word/register");
		invoke_reply();check(captured_word==wire(local) && captured_other==0 && captured_eax==0x3432434c && captured_ebp==0,"reply writer preserves LC24 and native zero/register");
		fake_session.fill(0x5a);invoke_info();std::uint32_t info_word_read=0;
		std::memcpy(&info_word_read,fake_session.data()+0x408,4);
		bool untouched=true;for(std::size_t i=0;i<fake_session.size();++i)if((i<0x408 || i>=0x40c) && fake_session[i]!=0x5a)untouched=false;
		check(info_word_read==wire(local) && untouched && captured_eax==reinterpret_cast<std::uintptr_t>(fake_session.data()+0x3e8) && captured_ecx==0x12345678,"host-info writer changes only compatibility word, preserves address/register");
		invoke_ack(); std::uint32_t ack_tag=0;std::memcpy(&ack_tag,ack_output.data()+8,4);
		check(send_calls==1 && sent_session==0x12345678 && sent_transport==0x23456789 && sent_flags==0 && sent_length==ack_size && ack_tag==wire(local) && std::memcmp(ack_output.data(),ack_input.data(),8)==0,"actual patched follow-up CALL copies original body, appends tag, preserves sender and ABI");
		check(sent_header[0x10]==0x1b && ack_header[8]==8 && *reinterpret_cast<const std::uint8_t* const*>(ack_header.data())==ack_input.data(),"follow-up sender preserves opcode and original header/body storage");
		receivers(local);
	}
	socket_gate();
	bool mismatch_message=false,missing_message=false;
	for(const auto& text:logger::messages) { mismatch_message |= text.find("effective ExplosiveFirstHit rules differ")!=std::string::npos; missing_message |= text.find("no supported ExplosiveFirstHit agreement")!=std::string::npos; }
	check(mismatch_message && missing_message,"clear effective-rule and missing-protocol refusal messages emitted");
	return failures ? 1:0;
}
