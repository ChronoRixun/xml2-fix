#include "gameplay_agreement.hpp"
#include "gameplay_agreement_rules.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"
#include <cstdio>
#include <cstring>

namespace gameplay_agreement
{
 namespace
 {
  using namespace gameplay_agreement_rules;
  gameplay_capabilities::record capabilities;
  // Published only by the transaction's verified commit: the native refusal sites and the transport sites are
  // written together (plan section 7); before publication the adapters are not reachable through native code.
  // The host notice's receiver and presentation are optional contributions of their own in the same transaction.
  bool failure_ready=false,transport_ready=false,contributed_=false,published=false,frozen=false,receiver_ready=false,notice_ready=false,fragment_ready=false;
  void* noticed_menu=nullptr;        // the host lobby that shows our notice in place of its advice,
  const char* notice_line=nullptr;   // what it says, until a join is admitted, the host closes
  unsigned long long notice_since=0; // or notice_ms pass
  // The native failure dialog's text (failure_text points here for good): the general explanation, or - when this PC
  // refused a peer's different settings, or the host said which settings differ - the same advice naming them.
  #define XML2_FIX_INCOMPATIBILITY_ADVICE "Both players must use the same build and effective gameplay settings. Restart after fixing the installation."
  constexpr char incompatibility_general[]="Incompatible gameplay or an unverified installation. " XML2_FIX_INCOMPATIBILITY_ADVICE;
  char incompatibility[512]="Incompatible gameplay or an unverified installation. " XML2_FIX_INCOMPATIBILITY_ADVICE;
  void dialog_text(const char* differing)
  {
   if(differing && *differing)std::snprintf(incompatibility,sizeof(incompatibility),"Incompatible gameplay settings: %s. " XML2_FIX_INCOMPATIBILITY_ADVICE,differing);
   else std::memcpy(incompatibility,incompatibility_general,sizeof(incompatibility_general));
  }
  // The rules behind a settings refusal, from the rule-summary buckets that differ: "9 DeathRule, 15 DeathSwitchClear"
  // for logs, "DeathRule, DeathSwitchClear" for the dialog. Empty when none is known.
  struct differing_rules { char log[384]{},dialog[384]{}; };
  differing_rules name_rules(std::uint64_t buckets,std::uint64_t local_only)
  {
   differing_rules d;if(!buckets || !capabilities.ready())return d;
   capabilities.describe(buckets,local_only,true,d.log,sizeof(d.log));capabilities.describe(buckets,local_only,false,d.dialog,sizeof(d.dialog));return d;
  }
  using callback=int(__cdecl*)(void*);
  using direct=int(__thiscall*)(void*,const void*,int,const std::uint8_t*);
  using send=void(__thiscall*)(void*,void*,const std::uint8_t*,int);
  using broadcast=int(__thiscall*)(void*,const std::uint8_t*,int,int,int);
  struct event { std::uint32_t type;void* body;std::uint16_t length,reserved;void* context;void* sender; };
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
  void notify_remote(const event& e,refusal why,std::uint64_t differing)
  {
   if(!e.context || !e.sender)return;
   using address=const void*(__thiscall*)(void*);using port=std::uint16_t(__thiscall*)(void*);
   const auto body=refusal_body(why,why==refusal::settings ? differing : 0);const auto* data=body.data();const auto size=std::uint16_t(body.size());
   std::array<std::uint8_t,0x14> header{};std::memcpy(header.data(),&data,4);std::memcpy(header.data()+8,&size,2);header[0x10]=rejection_opcode;header[0x11]=1;
   auto* transport=*reinterpret_cast<void**>(static_cast<std::uint8_t*>(e.context)+4);
   if(transport)reinterpret_cast<direct>(native_direct)(transport,reinterpret_cast<address>(native_address)(e.sender),reinterpret_cast<port>(native_port)(e.sender),header.data());
  }
  // Room for an older protocol's longer record (revision 2: up to 332 bytes), so its header is read and it is refused
  // as another protocol; anything longer is malformed. Neither reaches the native handler.
  constexpr std::size_t receive_room=512;
  bool extract(void* raw,std::size_t base,std::array<std::uint8_t,receive_room>& bytes,std::size_t& size)
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
   std::array<std::uint8_t,receive_room> bytes{};std::size_t size=0;gameplay_capabilities::difference diff{};
   const auto match=extract(raw,base,bytes,size) ? capabilities.compare({bytes.data(),size},&diff) : gameplay_capabilities::result::malformed;
   if(transport_ready && match==gameplay_capabilities::result::match){
    // A matching record is not yet an admission: retail 0x609970 can still refuse a full session (0x615910 ->
    // 0x609a95, sends 0x1c) or fail to take a participant (0x6099b2). The notice clears when the 0x1b follow-up
    // leaves (send_single), which retail sends only once the participant is taken.
    return reinterpret_cast<callback>(native)(raw);
   }
   // Nothing of a refused message reaches the native handler: a wrong protocol, a short, long or malformed body and
   // a different record all end here.
   const auto why=classify(transport_ready,match);
   const auto rules=why==refusal::settings ? name_rules(diff.buckets,diff.local_only) : differing_rules{};
   dialog_text(rules.dialog);
   if(role==endpoint::browser){
    logger::write("online: game info 0x%02x from an incompatible host refused (reason=%u%s%s); not listed or joined",unsigned(opcode),unsigned(match),*rules.log ? "; differs: " : "",rules.log);
    event e{};if(event_fields(raw,e)){notify_remote(e,why,diff.buckets);disconnect_peer(e.sender);}
    dialog(false);return 0;
   }
   logger::write("online: gameplay capabilities 0x%02x refused (reason=%u%s%s); no native admission",unsigned(opcode),unsigned(match),*rules.log ? "; differs: " : "",rules.log);
   const bool joining=role==endpoint::joiner;
   event e{};if(event_fields(raw,e)){notify_remote(e,why,diff.buckets);return fail_local(e,joining,why);}
   if(joining)dialog(false);else host_notice(why,false);return 0;
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
  refusal remote_refusal(void* raw,std::uint64_t& buckets)
  {
   buckets=0;
   __try { const auto* e=static_cast<const event*>(raw);const auto* body=static_cast<const std::uint8_t*>(e->body);buckets=read_refusal_buckets(body,e->length);return read_refusal(body,e->length); }
   __except(EXCEPTION_EXECUTE_HANDLER){buckets=0;return refusal::unspecified;}
  }
  int receive_rejection(void* raw)
  {
   std::uint64_t buckets=0;const auto why=remote_refusal(raw,buckets);const auto rules=name_rules(buckets,0);
   logger::write("online: the other PC refused this join (reason=%u%s%s)",unsigned(why),*rules.log ? "; differs: " : "",rules.log);
   dialog_text(rules.dialog);
   event e{};if(!event_fields(raw,e)){dialog(false);return 0;}
   return fail_local(e,true,refusal::unspecified); // opcode 0x1d is registered by joiner setup; no response loop
  }
  bool joiner_owns_rejection(void* context)
  {
   __try { return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(context)+joiner_rejection_slot)!=0; }
   __except(EXCEPTION_EXECUTE_HANDLER){return true;}
  }
  // A joining player refused this host (its own check failed first) and said so with 0x1d. The host keeps its
  // session: only that pending peer's connected flag clears, as native refusals do; no echo, no cleanup.
  int receive_host_rejection(void* raw)
  {
   event e{};if(!event_fields(raw,e) || joiner_owns_rejection(e.context))return 0;
   std::uint64_t buckets=0;const auto why=remote_refusal(raw,buckets);const auto rules=name_rules(buckets,0);
   logger::write("online: a joining player refused this game (reason=%u%s%s); host session kept",unsigned(why),*rules.log ? "; differs: " : "",rules.log);
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
    dialog_text(nullptr);event e{rejection_opcode,nullptr,0,0,context,nullptr};fail_local(e,true,refusal::sender_install);return 0;
   }
   using join=int(__thiscall*)(void*,const void*,int);return reinterpret_cast<join>(&original_join)(context,address,port);
  }
  template<std::size_t N> void expect(patch_transaction::contribution& c,const std::array<limits_rules::guard,N>& guards)
  { for(const auto& g:guards)c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),g.what}); }
  std::uint32_t address_of(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
  // An optional part must never be the reason the transaction refuses for something it can know alone: a write
  // outside its own guards would refuse the whole startup transaction (refused_unexpected), so it stays out instead.
  bool writes_inside_own_guards(const patch_transaction::contribution& c)
  {
   for(const auto& w:c.writes)for(std::size_t i=0;i<w.bytes.size();++i){
    const auto a=w.va+static_cast<std::uint32_t>(i);bool covered=false;
    for(const auto& g:c.guards)covered=covered || (a>=g.va && a-g.va<g.bytes.size());
    if(!covered){logger::write("online: %s writes %s outside its own guards; not installed",c.owner.c_str(),patch_transaction::hex(a).c_str());return false;}
   }
   return true;
  }
  void contribute_receiver(patch_transaction::transaction& transaction,HMODULE game)
  {
   if(!retail(game,receiver_guards)){logger::write("online: host refusal receiver guard failed; a joining player's refusal is not received here");return;}
   patch_transaction::contribution c;c.owner="gameplay_agreement.host_receiver";expect(c,receiver_guards);
   using patch_transaction::relocation;
   c.writes.push_back(relocation(host_info_register_call,"e8",true,address_of(reinterpret_cast<const void*>(&register_host_info)),0,"host-info registration"));
   c.writes.push_back(relocation(host_info_unregister_call,"e8",true,address_of(reinterpret_cast<const void*>(&release_host_info)),0,"host-info release"));
   c.publish=[]{receiver_ready=true;};
   c.abandon=[]{logger::write("online: host refusal receiver could not be installed; a joining player's refusal is not received here");};
   c.retain=c.abandon;
   if(!writes_inside_own_guards(c))return;
   if(!transaction.add(std::move(c)))logger::write("online: host refusal receiver could not join the startup transaction");
  }
  // The native fragment receiver 0x6101b0 (opcodes 0x12/0x13) dereferences null at 0x61020c when a 0x13 fragment
  // names a stream it never opened - what a peer's queued body over 300 bytes does (a 1.5 pre-release, protocol 2,
  // 360-byte 0x1a). The miss at 0x610200 leaves through the handler's own drop exit instead. Optional and independent.
  int fragment_drops=0;
  void __cdecl fragment_dropped()
  {
   if(fragment_drops>=8)return;
   ++fragment_drops;
   logger::write("online: a large-message fragment for a stream this PC never opened was dropped (the game would have crashed; a peer sent a body over 300 bytes - an older or modified build)%s",fragment_drops==8 ? "; further drops are not logged" : "");
  }
  const std::uint32_t fragment_exit=fragment_drop;
  __declspec(naked) void fragment_miss()
  {
   __asm { pushad } __asm { call fragment_dropped } __asm { popad } __asm { jmp dword ptr [fragment_exit] }
  }
  void contribute_fragment_guard(patch_transaction::transaction& transaction,HMODULE game)
  {
   if(!retail(game,fragment_guards)){logger::write("online: fragment guard check failed; a peer's oversized message is left to the game");return;}
   patch_transaction::contribution c;c.owner="gameplay_agreement.fragment_guard";expect(c,fragment_guards);
   c.writes.push_back(patch_transaction::relocation(fragment_stream_miss,"e9",true,address_of(reinterpret_cast<const void*>(&fragment_miss)),3,"fragment stream miss drops"));
   c.publish=[]{fragment_ready=true;};
   c.abandon=[]{logger::write("online: fragment guard could not be installed; a peer's oversized message is left to the game");};
   c.retain=c.abandon;
   if(!writes_inside_own_guards(c))return;
   if(!transaction.add(std::move(c)))logger::write("online: fragment guard could not join the startup transaction");
  }
  void contribute_notice(patch_transaction::transaction& transaction,HMODULE game)
  {
   if(!retail(game,presentation_guards)){logger::write("online: host refusal notice guard failed; refusals are logged only");return;}
   patch_transaction::contribution c;c.owner="gameplay_agreement.host_notice";expect(c,presentation_guards);
   c.writes.push_back(patch_transaction::relocation(host_advice_call,"e8",true,address_of(reinterpret_cast<const void*>(&host_advice)),0,"lobby advice lookup"));
   c.publish=[]{notice_ready=true;};
   c.abandon=[]{logger::write("online: host refusal notice could not be installed; refusals are logged only");};
   c.retain=c.abandon;
   if(!writes_inside_own_guards(c))return;
   if(!transaction.add(std::move(c)))logger::write("online: host refusal notice could not join the startup transaction");
  }
 }
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> effective,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.add(id,revision,effective,verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 bool register_entity_layout(std::uint16_t revision,std::uint32_t actors,std::uint32_t generic,std::uint32_t world,std::uint32_t token_bits,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.layout(revision,{actors,generic,world,token_bits},verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 void show_install_failure(){dialog_text(nullptr);dialog(true);}
 // Endpoint roles follow native registration and packet direction (live wire order), never timer presence.
 int receive_request(void* e){return receive(e,request_size,native_request,endpoint::browser,0x18);}
 int receive_reply(void* e){return receive(e,reply_size,native_reply,endpoint::joiner,0x1f);}
 int receive_info(void* e){return receive(e,info_size,native_info,endpoint::host,0x1a);}
 int receive_ack(void* e){return receive(e,ack_size,native_ack,endpoint::joiner,0x1b);}
 bool contributed(){return contributed_;}
 bool contribute(patch_transaction::transaction& transaction,HMODULE game)
 {
  if(contributed_)return published;
  if(!retail(game,failure_guards) || !retail(game,transport_guards)){logger::write("online: native refusal/transport guards failed; agreement unavailable");return false;}
  patch_transaction::contribution c;c.owner="gameplay_agreement";
  expect(c,failure_guards);expect(c,transport_guards);
  using patch_transaction::relocation;
  c.writes.push_back(relocation(native_join,"e9",true,address_of(reinterpret_cast<const void*>(&begin_join)),0,"begin join"));
  c.writes.push_back(relocation(failure_text+1,"",false,address_of(incompatibility),0,"failure text"));
  c.writes.push_back(relocation(rejection_registration+1,"",false,address_of(reinterpret_cast<const void*>(&receive_rejection)),0,"rejection callback"));
  c.writes.push_back(relocation(request_send,"e8",true,address_of(reinterpret_cast<const void*>(&send_direct)),0,"request send"));
  c.writes.push_back(relocation(reply_broadcast,"e8",true,address_of(reinterpret_cast<const void*>(&send_broadcast)),0,"reply broadcast"));
  c.writes.push_back(relocation(reply_send,"e8",true,address_of(reinterpret_cast<const void*>(&send_single)),0,"reply send"));
  c.writes.push_back(relocation(info_send,"e8",true,address_of(reinterpret_cast<const void*>(&send_single)),0,"info send"));
  c.writes.push_back(relocation(ack_send,"e8",true,address_of(reinterpret_cast<const void*>(&send_single)),0,"ack send"));
  c.writes.push_back(relocation(request_registration+1,"",false,address_of(reinterpret_cast<const void*>(&receive_request)),0,"request callback"));
  c.writes.push_back(relocation(reply_registration+1,"",false,address_of(reinterpret_cast<const void*>(&receive_reply)),0,"reply callback"));
  c.writes.push_back(relocation(info_registration+1,"",false,address_of(reinterpret_cast<const void*>(&receive_info)),0,"info callback"));
  c.writes.push_back(relocation(ack_registration+1,"",false,address_of(reinterpret_cast<const void*>(&receive_ack)),0,"ack callback"));
  c.publish=[]{failure_ready=true;transport_ready=true;published=true;game_version::bind_gameplay_refusal(&show_install_failure);
   logger::write("online: shared gameplay agreement sites published (refusal and transport, protocol %u)",unsigned(gameplay_capabilities::protocol_revision));};
  c.abandon=[]{logger::write("online: shared gameplay agreement sites not installed; online remains blocked");};
  c.retain=[]{logger::write("online: shared gameplay agreement sites uncertified after a failed rollback; online remains blocked");};
  contributed_=transaction.add(std::move(c));
  // Both parts of the host's notice are optional and independent: without the receiver a joiner's own refusal
  // goes unheard (the host's refusals are still logged); without the presentation every refusal is logged only.
  // Each joins the startup transaction only when its own guards read back as retail and every byte it writes lies
  // inside them, so a mismatch in either part leaves that part out instead of refusing startup. Once joined, a part
  // is an ordinary contributor: a conflict with another contribution's bytes, a page that cannot be made writable or
  // a failed readback refuses or rolls back the WHOLE transaction (and with it the LC startup), as for any other part.
  if(contributed_){contribute_receiver(transaction,game);contribute_notice(transaction,game);contribute_fragment_guard(transaction,game);}
  return contributed_;
 }
 bool commit_verified()
 {
  if(frozen)return capabilities.ready();
  frozen=true;
  if(!published || !capabilities.finalize()){logger::write("online: capabilities/failure/transport installation unverified; online remains blocked");return false;}
  logger::write("online: shared gameplay capability record verified (%u bytes, protocol %u, %u rules); release identity unchanged",unsigned(capabilities.bytes().size()),unsigned(gameplay_capabilities::protocol_revision),unsigned(capabilities.count()));
  // The wire carries a digest only: the canonical list is logged here, so two players' logs show what differs.
  for(std::size_t i=0;i<capabilities.count();++i){const auto& r=capabilities.rule(i);
   logger::write("online: rule %u (%s) revision %u {%u,%u,%u,%u}",unsigned(r.id),gameplay_capabilities::rule_name(r.id),unsigned(r.revision),r.values[0],r.values[1],r.values[2],r.values[3]);}
  return true;
 }
 bool prepare(HMODULE game)
 {
  if(contributed_){
   // A host-notice part whose guards did not read back as retail when the agreement committed may join a later
   // transaction of its own (the late path only; production contributes once, to the startup transaction).
   if(published && (!receiver_ready || !notice_ready || !fragment_ready)){
    patch_transaction::transaction late;
    if(!receiver_ready)contribute_receiver(late,game);
    if(!notice_ready)contribute_notice(late,game);
    if(!fragment_ready)contribute_fragment_guard(late,game);
    if(late.contributions()){const auto outcome=late.commit();
     if(outcome!=patch_transaction::outcome::committed)logger::write("online: late host notice installation %s: %s",patch_transaction::name(outcome),late.report().c_str());}
   }
   return published;
  }
  patch_transaction::transaction late;
  if(!contribute(late,game))return false;
  const auto outcome=late.commit();
  if(outcome!=patch_transaction::outcome::committed){logger::write("online: late agreement installation %s: %s",patch_transaction::name(outcome),late.report().c_str());contributed_=false;return false;}
  return published;
 }
 bool finalize(HMODULE game)
 {
  if(!contributed_ && !prepare(game))return false;
  return commit_verified();
 }
}
