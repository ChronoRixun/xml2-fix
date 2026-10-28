#pragma once
#include "limits_rules.hpp"
#include "gameplay_capabilities.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
namespace gameplay_agreement_rules
{
 constexpr std::size_t request_size=0x74,reply_size=0x50,info_size=0x30,ack_size=8;
 // The queued sends (0x1a info, 0x1b follow-up, 0x1f reply: 0x610f30 mode 0 -> 0x60f620) stay one message only up
 // to 300 bytes (0x60f6ef: cmp word [edi+8],0x12c; ja 0x60f7bb). A longer body goes out as 0x12 + 0x13 fragments,
 // and the receiver's fragment handler 0x6101b0 dereferences null at 0x61020c for a stream it has not opened (the
 // 1.5 pre-release host crash). Protocol 3's record has one fixed size, so every body is bounded here, with headroom.
 constexpr std::size_t queued_limit=0x12c;
 constexpr std::size_t largest_queued_body=std::max({info_size,ack_size,reply_size})+gameplay_capabilities::record_size;
 static_assert(largest_queued_body<=queued_limit-64,"every queued agreement body must stay a single native message, with headroom");
 static_assert(request_size+gameplay_capabilities::record_size<=0x514,"the direct 0x18 body fits the native direct buffer");
 constexpr std::uint32_t request_send=0x607d9d;
 constexpr std::uint32_t reply_broadcast=0x608477;
 constexpr std::uint32_t reply_send=0x6085f5;
 constexpr std::uint32_t info_send=0x6077c1;
 constexpr std::uint32_t ack_send=0x609a5d;
 constexpr std::uint32_t request_registration=0x6096a3;
 constexpr std::uint32_t reply_registration=0x609ee2;
 constexpr std::uint32_t info_registration=0x609cc3;
 constexpr std::uint32_t ack_registration=0x609dec;
 constexpr std::uint32_t native_direct=0x60d9c0;
 constexpr std::uint32_t native_send=0x610f30;
 constexpr std::uint32_t native_broadcast=0x611270;
 constexpr std::uint32_t native_request=0x6090d0;
 constexpr std::uint32_t native_reply=0x6079b0;
 constexpr std::uint32_t native_info=0x609970;
 constexpr std::uint32_t native_ack=0x607810;
 constexpr std::uint32_t native_join=0x609b80;
 constexpr std::uint32_t native_failure=0x609ad0;
 constexpr std::uint32_t failure_text=0x609b0e;
 constexpr std::uint32_t native_cleanup=0x6097f0;
 constexpr std::uint32_t native_dialog=0x5eb300;
 constexpr std::uint32_t native_menu=0x5d8920;
 constexpr std::uint32_t native_address=0x4bdfa0;
 constexpr std::uint32_t native_port=0x6106c0;
 constexpr std::uint32_t native_peer_disconnect=0x6106f0;
 constexpr std::uint32_t rejection_registration=0x609e3e;
 constexpr std::uint32_t join_resume=native_join+5;
 inline constexpr std::array<limits_rules::guard,16> transport_guards{{
  {request_send,"e81e5c0000","request send"},
  {reply_broadcast,"e8f48d0000","reply broadcast"},
  {reply_send,"e836890000","reply send"},
  {info_send,"e86a970000","info send"},
  {ack_send,"e8ce740000","ack send"},
  {request_registration,"68d09060006a18","request registration"},
  {reply_registration,"68b07960006a1f","reply registration"},
  {info_registration,"68709960006a1a","info registration"},
  {ack_registration,"68107860006a1b","ack registration"},
  {native_direct,"81ec18050000a1f8","native direct"},
  {native_send,"568bf18b8e1c040000","native send"},
  {native_broadcast,"558be98b851c0400","native broadcast"},
  {native_request,"558bec6aff68d0926700","native request"},
  {native_reply,"83ec14a1f8386f00538b5c24","native reply"},
  {native_info,"83ec1c53578d4c2410","native info"},
  {native_ack,"83ec2453558b6c2430","native ack"},
 }};
 inline constexpr std::array<limits_rules::guard,10> failure_guards{{
  {native_peer_disconnect,"8a442404884119c20400","native refused peer connected flag"},
  {rejection_registration,"68d09a60006a1d","native incompatibility registration"},
  {native_join,"83ec1c53555657","native join"},
  {native_failure,"56578b7c240c8b770c","native failure"},
  {failure_text,"689c466a00","failure text"},
  {native_cleanup,"51a1f8386f00535689442408","native cleanup"},
  {native_dialog,"a1ec138b0085c0750a","native dialog"},
  {native_menu,"a118ff8a0085c0750a","native menu"},
  {native_address,"8d410cc3","native address"},
  {native_port,"668b4108c3","native port"},
 }};
 // ---- Fragment guard (optional) ----
 // Native fragment receiver 0x6101b0 (registered for 0x12 and 0x13 at 0x6105d4/0x6105ec): for 0x13 it reads the
 // stream byte (0x6101d8), walks the open streams at +0x30b0 and, when none matches, stores 0 at [esp+0x34]
 // (0x610200) and dereferences it at 0x61020c. The miss jumps to the handler's own drop exit 0x6102b6 (pop ebx,
 // esi, ebp; eax 0; ret) - the path it takes for a fragment that does not complete a message.
 constexpr std::uint32_t fragment_stream_lookup=0x6101d8,fragment_stream_miss=0x610200,fragment_drop=0x6102b6;
 inline constexpr std::array<limits_rules::guard,3> fragment_guards{{
  {fragment_stream_lookup,"8a168d81b0300000","fragment receiver reads the stream byte and its open-stream list"},
  {fragment_stream_miss,"c7442434000000008b4424348b5810","fragment stream miss stores and dereferences null"},
  {fragment_drop,"5b5e33c05d83c424c3","fragment receiver drop exit"},
 }};
 // ---- Host refusal notice (non-modal) ----
 // Host setup 0x609c80 registers host-info 0x1a at 0x609cca (CALL 0x60edb0, result kept at +0x22c8); host
 // teardown 0x6074b0 releases that handle at 0x6074fc (CALL 0x60f1e0). The host never registers 0x1d natively,
 // so a refusing joiner's 0x1d was dropped. The wrapped pair registers/releases a host-only 0x1d receiver with
 // exactly the lifetime of the host's 0x1a registration. Joiner setup keeps its own 0x1d at +0x22ec.
 constexpr std::uint32_t host_info_register_call=0x609cca;
 constexpr std::uint32_t host_info_unregister_call=0x6074fc;
 constexpr std::uint32_t native_register=0x60edb0;   // thiscall(dispatcher, opcode, handler, context, flags, limit, 0) ret 0x18
 constexpr std::uint32_t native_unregister=0x60f1e0; // thiscall(dispatcher, opcode, handle) ret 8
 constexpr std::uint32_t joiner_rejection_slot=0x22ec;
 constexpr std::uint8_t rejection_opcode=0x1d;
 // Dispatch 0x60dbe0 calls a handler when (its flags & event+0x18) == event+0x18. A peer that has only queried
 // this host (Connect by IP, LAN list) is not connected yet, so a flags-0 handler never sees its 0x1d; flags 3
 // - as native 0x14, 0x18 and 0x19 use - takes it from a queried or a connected peer alike.
 constexpr std::uint32_t host_rejection_flags=3;
 // CMenuMgr (getter native_menu) keeps the current menu at +0x86090 (0x5d8870). The host lobby is a CMenuHost
 // (vtable 0x69f274, RTTI .?AVCMenuHost@@). Every frame its draw (vt+0x30 = 0x5babc0, only in that vtable) looks
 // up the lobby's advice paragraph "ONLINE_ADVICE" (0x5bac28: CALL 0x629bf0, cdecl(key) -> text), wraps it at
 // 450 px and draws it under the player list. That one call is wrapped: while a notice is current the lobby
 // draws the notice in that place instead; afterwards the advice is back. No item, focus or input changes.
 constexpr std::uint32_t menu_current=0x86090;
 constexpr std::uint32_t host_menu_vtable=0x69f274,host_advice_call=0x5bac28,native_text_lookup=0x629bf0;
 constexpr unsigned long long notice_ms=60000;
 // In play: the HUD (getter 0x59ee20) timed message vt+0x30 = 0x59cdb0, thiscall(text, seconds, rgba, kind)
 // ret 0x1c - what script hudMessage (0x49eb60) calls with kind 3. It fades by itself and takes no input.
 constexpr std::uint32_t native_hud=0x59ee20,native_hud_message=0x59cdb0,hud_message_slot=0x30;
 constexpr float hud_notice_seconds=8.0f;
 // Two optional sets, checked and installed separately: the receiver (the host hears a joiner's refusal and
 // logs it) and the presentation (the lobby paragraph / HUD line). Either can be missing without the other.
 inline constexpr std::array<limits_rules::guard,4> receiver_guards{{
  {host_info_register_call,"e8e15000008986c8220000","host-info registration call keeps its handle at +0x22c8"},
  {host_info_unregister_call-0x10,"8b86c82200003bc774118b4e04506a1ae8df7c0000","host-info release call releases the +0x22c8 handle"},
  {native_register,"64a1000000006aff68a19367","native message registration"},
  {native_unregister,"568b74240c85f6578bf9","native message release"},
 }};
 inline constexpr std::array<limits_rules::guard,7> presentation_guards{{
  {native_hud,"64a1000000008a0d94618a00","native HUD getter"},
  {native_hud_message,"56578bf983bf707908000a","native HUD timed message"},
  {host_menu_vtable-4,"608f6b00","CMenuHost vtable (complete object locator of .?AVCMenuHost@@)"},
  {host_menu_vtable+0x30,"c0ab5b00","CMenuHost draw slot holds the lobby draw 0x5babc0"},
  {host_advice_call-7,"68f0fe6900f3abe8c3ef0600","lobby draw looks up its advice paragraph"},
  {0x69fef0,"4f4e4c494e455f41445649434500","\"ONLINE_ADVICE\""},
  {native_text_lookup,"8b0d74b1a60085c974","native localized text lookup"},
 }};

 // ---- Games-list refusal notice (the querying PC; optional) ----
 // A PC that refuses a host's 0x18 game info (Connect by IP, room-code join, games-list search) files nothing, so the
 // games list shows its native empty row. The native popup raised from inside that network receive was found shown
 // in the popup manager but not drawn on the list (082f A2/A3: state popup=1, the frame "No Games Found" only): the
 // popup attaches its panels to whatever menu is current at that moment (0x5ea1b0 -> 0x5e91c0, menu vt+0x108).
 // CMenuGamesList (vtable 0x69f8b4, RTTI .?AVCMenuGamesList@@): vt+0x30 = 0x5b9f00 runs while it is the current menu
 // and, on its timer, refreshes the list (0x5b9f27: CALL 0x5b9900). An empty table makes that refresh write one row:
 // string 0xfc5 for the first five refreshes, then string 0xfe4 ("No Games Found"; 0x5b9a1f, and 0x5b9b9b once every
 // listed game went away): `call 0x4bdb30; mov edx,[eax]; push 0xfe4; mov ecx,eax; call [edx+8]` -> sprintf "%s" into a
 // 256-byte stack row -> the list's add-row (vt+0x84). Two writes: the two 5-byte `mov ecx,eax; call [edx+8]` become
 // a CALL to a thunk that returns the refusal row while it is current (else the native lookup), and the update slot
 // raises the deferred dialog on the list's own update, so it is attached to the games list. No new message, no
 // field: the row and the dialog use what the refused 0x18 already carried (the rule-summary difference).
 constexpr std::uint32_t games_list_vtable=0x69f8b4,games_list_update_slot=games_list_vtable+0x30,games_list_update=0x5b9f00;
 constexpr std::uint32_t games_empty_lookup_first=0x5b9a2b,games_empty_lookup_emptied=0x5b9ba7; // `mov ecx,eax; call [edx+8]`
 constexpr std::uint32_t games_empty_string=0xfe4;
 constexpr std::size_t games_row_limit=64;       // one list row (the row buffer itself is 256 bytes)
 constexpr unsigned long long browser_notice_ms=120000;
 inline constexpr std::array<limits_rules::guard,5> browser_guards{{
  {games_list_vtable-4,"e0916b00","CMenuGamesList vtable (complete object locator of .?AVCMenuGamesList@@)"},
  {games_list_update_slot,"009f5b00","CMenuGamesList update slot holds 0x5b9f00"},
  {games_list_update,"51568bf1e80717faff8b106a008bc8ff5228d95c2404d9056c828a00d85c2404dfe0f6c4057a178bcee8d2f9ffff","the games-list update refreshes the list on its timer (0x5b9f27: CALL 0x5b9900)"},
  {games_empty_lookup_first-0xc,"e80c41f0ff8b1068e40f00008bc8ff520850","the empty list looks up its No Games Found row (0x5b9a1f)"},
  {games_empty_lookup_emptied-0xc,"e8903ff0ff8b1068e40f00008bc8ff520850","the emptied list looks up its No Games Found row (0x5b9b9b)"},
 }};

 // Why a join was refused. Sent as the 8-byte body of 0x1d, relative to the SENDER; an absent or foreign body
 // (older builds, the native version refusal) reads as unspecified. Native 0x609ad0 ignores the body.
 enum class refusal : std::uint8_t { unspecified=0, settings=1, sender_install=2, foreign_record=3 };
 constexpr std::size_t refusal_size=8;
 constexpr std::array<std::uint8_t,4> refusal_magic{'G','R','E','F'};
 constexpr refusal classify(bool local_ready,gameplay_capabilities::result r)
 {
  using gameplay_capabilities::result;
  if(!local_ready || r==result::unavailable)return refusal::sender_install;
  if(r==result::malformed || r==result::protocol)return refusal::foreign_record;
  return refusal::settings;
 }
 constexpr std::array<std::uint8_t,refusal_size> refusal_body(refusal why)
 { return {refusal_magic[0],refusal_magic[1],refusal_magic[2],refusal_magic[3],static_cast<std::uint8_t>(why),0,0,0}; }
 inline refusal read_refusal(const std::uint8_t* body,std::size_t size)
 {
  if(!body || size<refusal_size || std::memcmp(body,refusal_magic.data(),refusal_magic.size())!=0 || body[5] || body[6] || body[7])return refusal::unspecified;
  return body[4]<=static_cast<std::uint8_t>(refusal::foreign_record) ? static_cast<refusal>(body[4]) : refusal::unspecified;
 }
 // Protocol 3 appends the differing rule-summary buckets (u64, bit b = bucket b of gameplay_capabilities) after the
 // 8-byte reason: older readers check only the first 8 bytes. Zero when unknown or not a settings refusal.
 constexpr std::size_t refusal_detail_size=refusal_size+8;
 constexpr std::array<std::uint8_t,refusal_detail_size> refusal_body(refusal why,std::uint64_t differing)
 {
  std::array<std::uint8_t,refusal_detail_size> b{refusal_magic[0],refusal_magic[1],refusal_magic[2],refusal_magic[3],static_cast<std::uint8_t>(why),0,0,0};
  for(std::size_t i=0;i<8;++i)b[refusal_size+i]=static_cast<std::uint8_t>(differing>>(i*8));
  return b;
 }
 inline std::uint64_t read_refusal_buckets(const std::uint8_t* body,std::size_t size)
 {
  if(read_refusal(body,size)!=refusal::settings || size<refusal_detail_size)return 0;
  std::uint64_t v=0;for(std::size_t i=0;i<8;++i)v|=std::uint64_t(body[refusal_size+i])<<(i*8);return v;
 }
 // The host's words. remote: the joining player decided (its 0x1d), so "sender" means the joining PC.
 constexpr const char* host_notice_text(refusal why,bool remote)
 {
  switch(why){
  case refusal::settings: return "Join refused: different gameplay settings";
  case refusal::sender_install: return remote ? "Join refused: fix not loaded on their PC" : "Join refused: fix not loaded on this PC";
  case refusal::foreign_record: return remote ? "Join refused: other build, or fix not loaded here" : "Join refused: other build, or their fix not loaded";
  default: return "Join refused: incompatible game setup";
  }
 }

 // The lobby's paragraph: what happened, then what to do. Plain text (the lobby wraps it, 450 px at 0.8).
 constexpr const char* host_notice_detail(refusal why,bool remote)
 {
  switch(why){
  case refusal::settings: return "A player could not join: your gameplay settings differ. Both players need the same build and the same gameplay settings in xml2-fix.ini; match them, restart the game and join again.";
  case refusal::sender_install: return remote ? "A player could not join: the fix did not load correctly on their PC. They need to repair or reinstall it and restart the game before joining."
                                              : "A player could not join: the fix did not load correctly on this PC. Repair or reinstall it and restart the game before hosting again.";
  case refusal::foreign_record: return remote ? "A player could not join: their game reports another build, or the fix did not load correctly on this PC. Both players need the same build."
                                              : "A player could not join: they use another build, or the fix did not load correctly on their PC. Both players need the same build.";
  default: return "A player could not join: their game setup is incompatible with yours. Both players need the same build and the same gameplay settings.";
  }
 }

 // The joining / querying PC's dialog for a settings refusal: that the host's gameplay settings differ, which ones
 // when the refusal carried them ("entity layout, ActorAI (disabled on this PC)"), then what to do.
 inline std::string joiner_settings_text(const char* names)
 {
  std::string words("The host uses different gameplay settings");
  if(names && *names){words+=": ";words+=names;}
  return words+". Both players need the same build and the same gameplay settings in xml2-fix.ini; match them, restart the game and join again.";
 }
 // The games list's row in place of "No Games Found" while a refusal is current: one line (games_row_limit).
 inline std::string games_row_text(refusal why,const char* names)
 {
  switch(why){
  case refusal::settings:{
   if(names && *names)for(const char* lead:{"Host's gameplay settings differ: ","Settings differ: "}){
    const std::string named=std::string(lead)+names;if(named.size()<=games_row_limit)return named;}
   return "Host's gameplay settings differ - match xml2-fix.ini, restart";}
  case refusal::sender_install: return "Host found, but the fix did not load correctly on this PC";
  case refusal::foreign_record: return "Host found, but it runs another build or its fix did not load";
  default: return "Host found, but its game setup is incompatible with yours";
  }
 }

 template<std::size_t N> inline const limits_rules::guard* first_mismatch(const std::uint8_t* image,const std::array<limits_rules::guard,N>& guards)
 {
  for(const auto&g:guards)if(!limits_rules::matches(image+g.va-limits_rules::image_base,g.hex))return &g;
  return nullptr;
 }
}
