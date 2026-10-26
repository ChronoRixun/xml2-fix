#include "power_pools.hpp"
#include "power_pool_plan.hpp"
#include "pool_counter_sites.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <cstring>

namespace power_pools {
namespace {
using namespace power_pool_layout;
std::array<layout,2> layouts{stock_attached,stock_vf};
std::array<bool,2> wanted{},raised{};
bool retail_base=false;
constexpr unsigned index(pool p) {return p==pool::attached?0:1;}
const char* name(pool p) {return p==pool::attached?"AttachedPowerups":"VFNodes";}
bool raise(pool which) {
 auto* storage=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,power_pool_plan::code_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 if(!storage)return false;
 auto plan=power_pool_plan::make(which,static_cast<u32>(reinterpret_cast<std::uintptr_t>(storage)));
 for(unsigned i=0;i<plan.code.size();++i) {
  if(plan.code[i].size()>power_pool_plan::stride) {VirtualFree(storage,0,MEM_RELEASE);return false;}
  if(!plan.code[i].empty())std::memcpy(storage+i*power_pool_plan::stride,plan.code[i].data(),plan.code[i].size());
 }
 DWORD previous=0;
 if(!VirtualProtect(storage,power_pool_plan::code_size,PAGE_EXECUTE_READ,&previous) ||
    !FlushInstructionCache(GetCurrentProcess(),storage,power_pool_plan::code_size)) {
  VirtualFree(storage,0,MEM_RELEASE);return false;
 }
 auto& c=plan.transaction;
 c.publish=[which] {
  layouts[index(which)]=layout_for(which,256);raised[index(which)]=true;
  // D0 reads the published layout; D1 observes only the unchanged refusal
  // branch, after the relocated comparison, so it composes without overlap.
  pool_counter_sites::install_power_layout(owner(which),layouts[index(which)].live,256);
 };
 c.abandon=[storage] {VirtualFree(storage,0,MEM_RELEASE);};
 c.retain=[] {game_version::refuse_startup();};
 patch_transaction::transaction transaction;transaction.add(std::move(c));
 const auto result=transaction.commit();
 logger::write("limits: %s 256 (allocation 0x%X): %s; %s",name(which),layout_for(which,256).size,
  patch_transaction::name(result),transaction.report().c_str());
 return result==patch_transaction::outcome::committed;
}
}
void install(HMODULE game) {
 retail_base=reinterpret_cast<std::uintptr_t>(game)==0x400000;
 for(const auto which:{pool::attached,pool::vf}) {
  const auto text=ini::text(L"Limits",which==pool::attached?L"AttachedPowerups":L"VFNodes");
  const auto request=power_pool_plan::choose(text?std::optional<std::string_view>(*text):std::nullopt);
  wanted[index(which)]=request.requested;
  if(!request.requested)continue;
  if(!request.valid) {logger::write("limits: %s accepts only 256; request refused",name(which));continue;}
  if(!retail_base || !raise(which))logger::write("limits: %s unverified; capacity remains %u",name(which),stock_capacity(which));
 }
}
void register_agreement() {
 if(!retail_base)return;
 for(const auto which:{pool::attached,pool::vf})if(wanted[index(which)])
  gameplay_agreement::register_rule(which==pool::attached?gameplay_capabilities::attached_powerups_id:gameplay_capabilities::vf_nodes_id,
   1,{layouts[index(which)].capacity,0,0,0},raised[index(which)]);
}
const power_pool_layout::layout& descriptor(pool which) {return layouts[index(which)];}
bool installed(pool which) {return raised[index(which)];}
std::string status() {
 std::string out;
 for(const auto which:{pool::attached,pool::vf}) {
  u32 p=0,live=0;const auto& l=descriptor(which);
  const bool readable=retail_base && patch_transaction::read_memory(owner(which),&p,4) && p &&
   patch_transaction::read_memory(p+l.live,&live,4);
  out+="; ";out+=name(which);out+=" ";out+=readable?std::to_string(live):"-";out+="/"+std::to_string(l.capacity);
 }
 return out;
}
}
