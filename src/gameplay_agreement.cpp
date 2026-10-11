#include "gameplay_agreement.hpp"
#include "gameplay_agreement_rules.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include <cstring>

namespace gameplay_agreement
{
 namespace
 {
  using namespace gameplay_agreement_rules;
  gameplay_capabilities::record capabilities;
  bool failure_ready=false,transport_ready=false,receiver_ready=false,notice_ready=false;
  void* noticed_menu=nullptr;        // the host lobby that shows our notice in place of its advice,
  const char* notice_line=nullptr;   // what it says, until a join is admitted, the host closes
  unsigned long long notice_since=0; // or notice_ms pass
  constexpr char incompatibility[]="Incompatible gameplay or an unverified installation. Both players must use the same build and effective gameplay settings. Restart after fixing the installation.";
  using callback=int(__cdecl*)(void*);
  using direct=int(__thiscall*)(void*,const void*,int,const std::uint8_t*);
  using send=void(__thiscall*)(void*,void*,const std::uint8_t*,int);
  using broadcast=int(__thiscall*)(void*,const std::uint8_t*,int,int,int);
  struct event { std::uint32_t type;void* body;std::uint16_t length,reserved;void* context;void* sender; };
  struct patch { std::uint32_t site;std::array<std::uint8_t,5> bytes;std::size_t size; };
  patch branch(std::uint32_t site,const void* target,std::uint8_t opcode)
  {
   patch p{site,{opcode},5};const auto delta=std::int32_t(reinterpret_cast<std::uintptr_t>(target)-site-5);std::memcpy(p.bytes.data()+1,&delta,4);return p;
  }
  patch pointer(std::uint32_t site,const void* target)
  {
   patch p{site,{},4};const auto address=reinterpret_cast<std::uintptr_t>(target);std::memcpy(p.bytes.data(),&address,4);return p;
  }
  template<std::size_t N> bool apply(const std::array<patch,N>& patches)
  {
   struct page { std::uintptr_t address;DWORD protection; };
   std::array<page,N> pages{};std::size_t count=0;SYSTEM_INFO info{};GetSystemInfo(&info);
   for(const auto&p:patches){const auto address=p.site-p.site%info.dwPageSize;bool found=false;for(std::size_t i=0;i<count;++i)found|=pages[i].address==address;if(found)continue;
    DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(address),info.dwPageSize,PAGE_EXECUTE_READWRITE,&old)){
     for(std::size_t i=count;i-- >0;){DWORD unused;VirtualProtect(reinterpret_cast<void*>(pages[i].address),info.dwPageSize,pages[i].protection,&unused);}return false;
    }pages[count++]={address,old};
   }
   for(const auto&p:patches)std::memcpy(reinterpret_cast<void*>(p.site),p.bytes.data(),p.size);
   bool ok=true;
   for(std::size_t i=count;i-- >0;){ok=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(pages[i].address),info.dwPageSize)!=FALSE && ok;DWORD unused;ok=VirtualProtect(reinterpret_cast<void*>(pages[i].address),info.dwPageSize,pages[i].protection,&unused)!=FALSE && ok;}
   for(const auto&p:patches)ok=std::memcmp(reinterpret_cast<const void*>(p.site),p.bytes.data(),p.size)==0 && ok;
   return ok;
  }
  template<std::size_t N> bool retail(HMODULE game,const std::array<limits_rules::guard,N>& guards)
  {
   if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base)return false;
   __try { return !first_mismatch(reinterpret_cast<const std::uint8_t*>(game),guards); }
   __except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
  void dialog(bool close_menu)
  {
   if(!failure_ready)return;
   using getter=void*(__cdecl*)();using popup=void(__thiscall*)(void*,const char*,float,int);using close=void(__thiscall*)(void*,int,int);
   void* manager=reinterpret_cast<getter>(native_dialog)();auto** vt=*reinterpret_cast<void***>(manager);
   reinterpret_cast<popup>(vt[3])(manager,incompatibility,-1.0f,0);
   if(close_menu){manager=reinterpret_cast<getter>(native_menu)();vt=*reinterpret_cast<void***>(manager);reinterpret_cast<close>(vt[0x74/4])(manager,0,0);}
  }
  // The host's notice: never a popup, never a menu change. In the lobby its advice paragraph says it; in play
  // the HUD's own timed message does. Anything else: the log line only.
  void* current_host_menu()
  {
   using getter=void*(__cdecl*)();
   __try {
    auto* manager=static_cast<std::uint8_t*>(reinterpret_cast<getter>(native_menu)());
    auto* menu=manager ? *reinterpret_cast<std::uint8_t**>(manager+menu_current) : nullptr;
    return menu && *reinterpret_cast<const std::uint32_t*>(menu)==host_menu_vtable ? menu : nullptr;
   }__except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
  }
  bool hud_text(const char* text)
  {
   using getter=void*(__cdecl*)();using message=int(__thiscall*)(void*,const char*,float,float,float,float,float,int);
   __try {
    void* hud=reinterpret_cast<getter>(native_hud)();if(!hud)return false;
    auto** vt=*reinterpret_cast<void***>(hud);if(vt[hud_message_slot/4]!=reinterpret_cast<void*>(native_hud_message))return false;
    return reinterpret_cast<message>(vt[hud_message_slot/4])(hud,text,hud_notice_seconds,1.0f,1.0f,1.0f,1.0f,3)!=0;
   }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
  void host_notice(refusal why,bool remote)
  {
   logger::write("online: host notice: %s (reason %u, %s)",host_notice_text(why,remote),unsigned(why),remote ? "from the joining player" : "found here");
   if(!notice_ready){logger::write("online: host notice not drawn (its guards failed at startup); the line above is the record");return;}
   if(void* menu=current_host_menu()){
    noticed_menu=menu;notice_line=host_notice_detail(why,remote);notice_since=GetTickCount64();
    logger::write("online: host notice in the lobby for %llu s",notice_ms/1000);return;
   }
   logger::write(hud_text(host_notice_text(why,remote)) ? "online: host notice as a HUD message" : "online: host notice not shown (no lobby or HUD on screen)");
  }
  void clear_host_notice(){noticed_menu=nullptr;notice_line=nullptr;}
  // The lobby draw's advice lookup: the notice while it is current, else the advice as before.
  const char* __cdecl host_advice(const char* key)
  {
   if(notice_line && GetTickCount64()-notice_since<notice_ms && current_host_menu()==noticed_menu)return notice_line;
   return reinterpret_cast<const char*(__cdecl*)(const char*)>(native_text_lookup)(key);
  }
  bool event_fields(void* raw,event& e)
  {
   __try { if(!raw)return false;std::memcpy(&e,raw,sizeof(e));return e.context!=nullptr; }
   __except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
  void disconnect_peer(void* sender)
  {
   if(failure_ready && sender){using disconnect=void(__thiscall*)(void*,bool);reinterpret_cast<disconnect>(native_peer_disconnect)(sender,false);}
  }
  int fail_local(event e,bool joining,refusal why)
  {
   disconnect_peer(e.sender);
   if(joining && failure_ready && e.context){e.type=rejection_opcode;return reinterpret_cast<callback>(native_failure)(&e);}
   if(joining){dialog(false);return 0;}
   host_notice(why,false);return 0; // a host has not admitted this peer; preserve its session, menus and players
  }
  void notify_remote(const event& e,refusal why)
  {
   if(!e.context || !e.sender)return;
   using address=const void*(__thiscall*)(void*);using port=std::uint16_t(__thiscall*)(void*);
   const auto body=refusal_body(why);const auto* data=body.data();const auto size=std::uint16_t(body.size());
   std::array<std::uint8_t,0x14> header{};std::memcpy(header.data(),&data,4);std::memcpy(header.data()+8,&size,2);header[0x10]=rejection_opcode;header[0x11]=1;
   auto* transport=*reinterpret_cast<void**>(static_cast<std::uint8_t*>(e.context)+4);
   if(transport)reinterpret_cast<direct>(native_direct)(transport,reinterpret_cast<address>(native_address)(e.sender),reinterpret_cast<port>(native_port)(e.sender),header.data());
  }
  bool extract(void* raw,std::size_t base,std::array<std::uint8_t,gameplay_capabilities::max_size>& bytes,std::size_t& size)
  {
   __try {
    const auto* e=static_cast<const event*>(raw);if(!e || !e->body || e->length<base+gameplay_capabilities::header_size || e->length>base+bytes.size())return false;
    size=e->length-base;std::memcpy(bytes.data(),static_cast<const std::uint8_t*>(e->body)+base,size);return true;
   }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
  // host: admits a joining player (0x1a). joiner: admitted by a host (0x1b, 0x1f). browser: a host's game info
  // (0x18) answering this PC's direct or LAN query (Connect by IP, the games list); 0x6090d0 files it in the
  // games table (0x608c20/0x608d60, status 6). An incompatible host is not filed: this PC sees the native
  // dialog, as before, and the host is told why with 0x1d (it takes that from a queried peer, see
  // host_rejection_flags). The browser has no pending join or session to clean up.
  enum class endpoint { host,joiner,browser };
  int receive(void* raw,std::size_t base,std::uint32_t native,endpoint role,std::uint8_t opcode)
  {
   std::array<std::uint8_t,gameplay_capabilities::max_size> bytes{};std::size_t size=0;
   const auto match=extract(raw,base,bytes,size) ? capabilities.compare({bytes.data(),size}) : gameplay_capabilities::result::malformed;
   if(role==endpoint::browser && (!transport_ready || match!=gameplay_capabilities::result::match)){
    logger::write("online: game info 0x%02x from an incompatible host refused (reason=%u); not listed or joined",unsigned(opcode),unsigned(match));
    const auto why=classify(transport_ready,match);
    event e{};if(event_fields(raw,e)){notify_remote(e,why);disconnect_peer(e.sender);}
    dialog(false);return 0;
   }
   if(!transport_ready || match!=gameplay_capabilities::result::match){
    logger::write("online: gameplay capabilities 0x%02x refused (reason=%u); no native admission",unsigned(opcode),unsigned(match));
    const auto why=classify(transport_ready,match);const bool joining=role==endpoint::joiner;
    event e{};if(event_fields(raw,e)){notify_remote(e,why);return fail_local(e,joining,why);}
    if(joining)dialog(false);else host_notice(why,false);return 0;
   }
   // A matching record is not yet an admission: retail 0x609970 can still refuse a full session (0x615910 ->
   // 0x609a95, sends 0x1c) or fail to take a participant (0x6099b2). The notice clears when the 0x1b follow-up
   // leaves (send_single), which retail sends only once the participant is taken.
   return reinterpret_cast<callback>(native)(raw);
  }
  struct packet
  {
   std::array<std::uint8_t,0x14> header{};
   std::array<std::uint8_t,request_size+gameplay_capabilities::max_size> body{};
   bool build(const std::uint8_t* original)
   {
    if(!transport_ready || !capabilities.ready())return false;
    std::memcpy(header.data(),original,header.size());std::uint16_t size=0;std::memcpy(&size,original+8,2);
    const auto opcode=original[0x10];const auto expected=opcode==0x18 ? request_size : opcode==0x1f ? reply_size : opcode==0x1a ? info_size : opcode==0x1b ? ack_size : 0;
    if(!expected || size!=expected)return false;
    std::memcpy(body.data(),*reinterpret_cast<const void*const*>(original),size);
    const auto record=capabilities.bytes();std::memcpy(body.data()+size,record.data(),record.size());
    size=static_cast<std::uint16_t>(size+record.size());const auto* data=body.data();std::memcpy(header.data(),&data,4);std::memcpy(header.data()+8,&size,2);return true;
   }
  };
  int __fastcall send_direct(void* transport,void*,const void* address,int port,const std::uint8_t* header)
  { packet p;if(!p.build(header)){show_install_failure();return 0;}return reinterpret_cast<direct>(native_direct)(transport,address,port,p.header.data()); }
  void __fastcall send_single(void* session,void*,void* transport,const std::uint8_t* header,int flags)
  {
   packet p;if(!p.build(header)){show_install_failure();return;}reinterpret_cast<send>(native_send)(session,transport,p.header.data(),flags);
   if(header[0x10]==0x1b)clear_host_notice(); // the host-info follow-up: this participant is in; a stale refusal notice ends
  }
  int __fastcall send_broadcast(void* session,void*,const std::uint8_t* header,int exclude,int participants,int mode)
  { packet p;if(!p.build(header)){show_install_failure();return 0;}return reinterpret_cast<broadcast>(native_broadcast)(session,p.header.data(),exclude,participants,mode); }
  bool enlist()
  {
   if(game_version::needs_gameplay_agreement())return true;
   const auto game=GetModuleHandleW(nullptr);
   return game_version::require_gameplay_agreement(game) && prepare(game);
  }
  const std::uint32_t join_next=join_resume;
  __declspec(naked) void original_join()
  {
   __asm { sub esp,0x1c } __asm { push ebx } __asm { push ebp } __asm { jmp dword ptr [join_next] }
  }
  int receive_rejection(void* raw)
  {
   event e{};if(!event_fields(raw,e)){dialog(false);return 0;}
   return fail_local(e,true,refusal::unspecified); // opcode 0x1d is registered by joiner setup; no response loop
  }
  bool joiner_owns_rejection(void* context)
  {
   __try { return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(context)+joiner_rejection_slot)!=0; }
   __except(EXCEPTION_EXECUTE_HANDLER){return true;}
  }
  refusal remote_refusal(void* raw)
  {
   __try { const auto* e=static_cast<const event*>(raw);return read_refusal(static_cast<const std::uint8_t*>(e->body),e->length); }
   __except(EXCEPTION_EXECUTE_HANDLER){return refusal::unspecified;}
  }
  // A joining player refused this host (its own check failed first) and said so with 0x1d. The host keeps its
  // session: only that pending peer's connected flag clears, as native refusals do; no echo, no cleanup.
  int receive_host_rejection(void* raw)
  {
   event e{};if(!event_fields(raw,e) || joiner_owns_rejection(e.context))return 0;
   const auto why=remote_refusal(raw);
   logger::write("online: a joining player refused this game (reason=%u); host session kept",unsigned(why));
   disconnect_peer(e.sender);host_notice(why,true);return 0;
  }
  struct host_registration { void* dispatcher;std::uint32_t info,rejection; };
  std::array<host_registration,4> host_registrations{};
  using register_message=std::uint32_t(__thiscall*)(void*,std::uint32_t,void*,void*,std::uint32_t,std::uint32_t,std::uint32_t);
  using release_message=void(__thiscall*)(void*,std::uint32_t,std::uint32_t);
  std::uint32_t __fastcall register_host_info(void* dispatcher,void*,std::uint32_t opcode,void* handler,void* context,std::uint32_t flags,std::uint32_t limit,std::uint32_t extra)
  {
   const auto info=reinterpret_cast<register_message>(native_register)(dispatcher,opcode,handler,context,flags,limit,extra);
   if(!info)return info;
   const auto rejection=reinterpret_cast<register_message>(native_register)(dispatcher,rejection_opcode,reinterpret_cast<void*>(&receive_host_rejection),context,host_rejection_flags,limit,extra);
   if(!rejection)return info;
   for(auto& r:host_registrations)if(!r.info){r={dispatcher,info,rejection};return info;}
   reinterpret_cast<release_message>(native_unregister)(dispatcher,rejection_opcode,rejection);return info; // no slot: never leak
  }
  void __fastcall release_host_info(void* dispatcher,void*,std::uint32_t opcode,std::uint32_t handle)
  {
   reinterpret_cast<release_message>(native_unregister)(dispatcher,opcode,handle);
   for(auto& r:host_registrations)if(r.info==handle && r.dispatcher==dispatcher){reinterpret_cast<release_message>(native_unregister)(dispatcher,rejection_opcode,r.rejection);r={};}
   clear_host_notice();
  }
  int __fastcall begin_join(void* context,void*,const void* address,int port)
  {
   if(!transport_ready || !capabilities.ready() || !game_version::gameplay_online_allowed()){
    event e{rejection_opcode,nullptr,0,0,context,nullptr};fail_local(e,true,refusal::sender_install);return 0;
   }
   using join=int(__thiscall*)(void*,const void*,int);return reinterpret_cast<join>(&original_join)(context,address,port);
  }
 }
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> effective,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.add(id,revision,effective,verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 bool register_entity_layout(std::uint16_t revision,std::uint32_t actors,std::uint32_t generic,std::uint32_t world,std::uint32_t token_bits,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.layout(revision,{actors,generic,world,token_bits},verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 void show_install_failure(){dialog(true);}
 // Endpoint roles follow native registration and packet direction (live wire order), never timer presence.
 int receive_request(void* e){return receive(e,request_size,native_request,endpoint::browser,0x18);}
 int receive_reply(void* e){return receive(e,reply_size,native_reply,endpoint::joiner,0x1f);}
 int receive_info(void* e){return receive(e,info_size,native_info,endpoint::host,0x1a);}
 int receive_ack(void* e){return receive(e,ack_size,native_ack,endpoint::joiner,0x1b);}
 bool prepare(HMODULE game)
 {
  if(!failure_ready){
   if(!retail(game,failure_guards)){logger::write("online: native refusal path guard failed; agreement unavailable");return false;}
   const std::array patches{branch(native_join,&begin_join,0xe9),pointer(failure_text+1,incompatibility),pointer(rejection_registration+1,reinterpret_cast<void*>(&receive_rejection))};
   failure_ready=apply(patches);if(!failure_ready)return false;
   game_version::bind_gameplay_refusal(&show_install_failure);
  }
  // Both parts of the host's notice are optional and independent: without the receiver a joiner's own refusal
  // goes unheard (the host's refusals are still logged); without the presentation every refusal is logged only.
  if(!receiver_ready){
   if(!retail(game,receiver_guards))logger::write("online: host refusal receiver guard failed; a joining player's refusal is not received here");
   else {
    const std::array receiver{branch(host_info_register_call,&register_host_info,0xe8),branch(host_info_unregister_call,&release_host_info,0xe8)};
    receiver_ready=apply(receiver);if(!receiver_ready)logger::write("online: host refusal receiver could not be installed; a joining player's refusal is not received here");
   }
  }
  if(!notice_ready){
   if(!retail(game,presentation_guards))logger::write("online: host refusal notice guard failed; refusals are logged only");
   else {
    const std::array notice{branch(host_advice_call,&host_advice,0xe8)};
    notice_ready=apply(notice);if(!notice_ready)logger::write("online: host refusal notice could not be installed; refusals are logged only");
   }
  }
  return true;
 }
 bool finalize(HMODULE game)
 {
  if(transport_ready)return capabilities.ready();
  if(!failure_ready || !capabilities.finalize() || !retail(game,transport_guards)){logger::write("online: capabilities/failure/transport installation unverified; online remains blocked");return false;}
  const std::array patches{
   branch(request_send,&send_direct,0xe8),branch(reply_broadcast,&send_broadcast,0xe8),branch(reply_send,&send_single,0xe8),branch(info_send,&send_single,0xe8),branch(ack_send,&send_single,0xe8),
   pointer(request_registration+1,reinterpret_cast<void*>(&receive_request)),pointer(reply_registration+1,reinterpret_cast<void*>(&receive_reply)),pointer(info_registration+1,reinterpret_cast<void*>(&receive_info)),pointer(ack_registration+1,reinterpret_cast<void*>(&receive_ack))};
  transport_ready=apply(patches);logger::write("online: shared gameplay capability record %s (%u bytes); release identity unchanged",transport_ready ? "verified" : "failed",unsigned(capabilities.bytes().size()));return transport_ready;
 }
}
