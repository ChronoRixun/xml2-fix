#include "gameplay_agreement.hpp"
#include "gameplay_agreement_rules.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"
#include <cstring>

namespace gameplay_agreement
{
 namespace
 {
  using namespace gameplay_agreement_rules;
  gameplay_capabilities::record capabilities;
  // Published only by the transaction's verified commit: the native refusal sites and the transport sites are
  // written together (plan section 7); before publication the adapters are not reachable through native code.
  bool failure_ready=false,transport_ready=false,contributed_=false,published=false,frozen=false;
  constexpr char incompatibility[]="Incompatible gameplay or an unverified installation. Both players must use the same build and effective gameplay settings. Restart after fixing the installation.";
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
  bool event_fields(void* raw,event& e)
  {
   __try { if(!raw)return false;std::memcpy(&e,raw,sizeof(e));return e.context!=nullptr; }
   __except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
  int fail_local(event e,bool joining)
  {
   if(failure_ready && e.sender){using disconnect=void(__thiscall*)(void*,bool);reinterpret_cast<disconnect>(native_peer_disconnect)(e.sender,false);}
   if(joining && failure_ready && e.context){e.type=0x1d;return reinterpret_cast<callback>(native_failure)(&e);}
   dialog(false);return 0; // a host has not admitted this peer; preserve other participants
  }
  void notify_remote(const event& e)
  {
   if(!e.context || !e.sender)return;
   using address=const void*(__thiscall*)(void*);using port=std::uint16_t(__thiscall*)(void*);
   std::array<std::uint8_t,0x14> header{};header[0x10]=0x1d;header[0x11]=1;
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
  enum class endpoint { host,joiner };
  int receive(void* raw,std::size_t base,std::uint32_t native,endpoint role)
  {
   std::array<std::uint8_t,gameplay_capabilities::max_size> bytes{};std::size_t size=0;
   const auto match=extract(raw,base,bytes,size) ? capabilities.compare({bytes.data(),size}) : gameplay_capabilities::result::malformed;
   if(!transport_ready || match!=gameplay_capabilities::result::match){
    logger::write("online: gameplay capabilities refused (reason=%u); no native admission",unsigned(match));
    event e{};if(event_fields(raw,e)){notify_remote(e);return fail_local(e,role==endpoint::joiner);}dialog(false);return 0;
   }
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
  { packet p;if(!p.build(header)){show_install_failure();return;}reinterpret_cast<send>(native_send)(session,transport,p.header.data(),flags); }
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
   return fail_local(e,true); // opcode 0x1d is registered by joiner setup; no response loop
  }
  int __fastcall begin_join(void* context,void*,const void* address,int port)
  {
   if(!transport_ready || !capabilities.ready() || !game_version::gameplay_online_allowed()){
    event e{0x1d,nullptr,0,0,context,nullptr};fail_local(e,true);return 0;
   }
   using join=int(__thiscall*)(void*,const void*,int);return reinterpret_cast<join>(&original_join)(context,address,port);
  }
  template<std::size_t N> void expect(patch_transaction::contribution& c,const std::array<limits_rules::guard,N>& guards)
  { for(const auto& g:guards)c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),g.what}); }
  std::uint32_t address_of(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
 }
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> effective,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.add(id,revision,effective,verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 bool register_entity_layout(std::uint16_t revision,std::uint32_t actors,std::uint32_t generic,std::uint32_t world,std::uint32_t token_bits,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.layout(revision,{actors,generic,world,token_bits},verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 void show_install_failure(){dialog(true);}
 // Endpoint roles follow native registration and packet direction, never timer presence.
 int receive_request(void* e){return receive(e,request_size,native_request,endpoint::host);}
 int receive_reply(void* e){return receive(e,reply_size,native_reply,endpoint::joiner);}
 int receive_info(void* e){return receive(e,info_size,native_info,endpoint::host);}
 int receive_ack(void* e){return receive(e,ack_size,native_ack,endpoint::joiner);}
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
  contributed_=transaction.add(std::move(c));return contributed_;
 }
 bool commit_verified()
 {
  if(frozen)return capabilities.ready();
  frozen=true;
  if(!published || !capabilities.finalize()){logger::write("online: capabilities/failure/transport installation unverified; online remains blocked");return false;}
  logger::write("online: shared gameplay capability record verified (%u bytes); release identity unchanged",unsigned(capabilities.bytes().size()));return true;
 }
 bool prepare(HMODULE game)
 {
  if(contributed_)return published;
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
