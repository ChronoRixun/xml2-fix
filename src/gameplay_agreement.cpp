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
  bool failure_ready=false,transport_ready=false;
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
  bool event_fields(void* raw,event& e)
  {
   __try { if(!raw)return false;std::memcpy(&e,raw,sizeof(e));return e.context!=nullptr; }
   __except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
  bool pending(void* context)
  {
   __try { return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(context)+0x22bc)!=0; }
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
  int receive(void* raw,std::size_t base,std::uint32_t native,bool joining)
  {
   std::array<std::uint8_t,gameplay_capabilities::max_size> bytes{};std::size_t size=0;
   const auto match=extract(raw,base,bytes,size) ? capabilities.compare({bytes.data(),size}) : gameplay_capabilities::result::malformed;
   if(!transport_ready || match!=gameplay_capabilities::result::match){
    logger::write("online: gameplay capabilities refused (reason=%u); no native admission",unsigned(match));
    event e{};if(event_fields(raw,e)){notify_remote(e);return fail_local(e,joining || pending(e.context));}dialog(false);return 0;
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
   return fail_local(e,pending(e.context)); // no response loop; only the pending endpoint is torn down
  }
  int __fastcall begin_join(void* context,void*,const void* address,int port)
  {
   if(!transport_ready || !capabilities.ready() || !game_version::gameplay_online_allowed()){
    event e{0x1d,nullptr,0,0,context,nullptr};fail_local(e,true);return 0;
   }
   using join=int(__thiscall*)(void*,const void*,int);return reinterpret_cast<join>(&original_join)(context,address,port);
  }
 }
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> effective,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.add(id,revision,effective,verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 bool register_entity_layout(std::uint16_t revision,std::uint32_t actors,std::uint32_t generic,std::uint32_t world,std::uint32_t token_bits,bool verified)
 { const bool enabled=enlist();const bool ok=capabilities.layout(revision,{actors,generic,world,token_bits},verified && enabled);if(!ok)game_version::bind_gameplay_agreement(false);return ok; }
 void show_install_failure(){dialog(true);}
 int receive_request(void* e){return receive(e,request_size,native_request,false);}
 int receive_reply(void* e){return receive(e,reply_size,native_reply,true);}
 int receive_info(void* e){return receive(e,info_size,native_info,true);}
 int receive_ack(void* e){return receive(e,ack_size,native_ack,false);}
 bool prepare(HMODULE game)
 {
  if(failure_ready)return true;
  if(!retail(game,failure_guards)){logger::write("online: native refusal path guard failed; agreement unavailable");return false;}
  const std::array patches{branch(native_join,&begin_join,0xe9),pointer(failure_text+1,incompatibility),pointer(rejection_registration+1,reinterpret_cast<void*>(&receive_rejection))};
  failure_ready=apply(patches);if(failure_ready)game_version::bind_gameplay_refusal(&show_install_failure);return failure_ready;
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
