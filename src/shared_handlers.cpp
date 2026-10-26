#include "shared_handlers.hpp"
#include "shared_handler_rules.hpp"
#include "shared_handler_bridges.hpp"
#include "entity_capacity.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"

namespace shared_handlers {
namespace {
std::uint32_t wanted=0,applied=0;
bool valid=true;
}
bool requested() { return wanted!=0; }
bool installed() { return applied!=0 && applied==wanted; }
bool verified() { return valid && wanted==applied; }
std::array<std::uint32_t,4> values() { return {applied,0,0,0}; }
void install(HMODULE game) {
 using namespace shared_handler_rules;
 if(applied)return;
 wanted=0;valid=true;
 for(unsigned i=0;i<keys.size();++i) {
  const auto choice=parse_rule(ini::text(L"Game",keys[i]).value_or(""));
  if(!choice.set || (choice.error.empty() && choice.value==rule::xml2))continue;
  wanted|=1u<<i;
  if(!choice.error.empty())valid=false;
 }
 // OFF performs no image reads, writes or agreement registration.
 if(!wanted)return;
 if(!valid || reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base) {
  logger::write("shared handlers: invalid key/image; native callbacks retained, online agreement refused");return;
 }
 xml1_hook_transaction::memory memory;
 for(const auto& g:guards) {
  if(!(g.parts&wanted))continue;
  std::vector<unsigned char> b(g.bytes.hex.size()/2);
  bool ok=memory.read(g.bytes.va,b.data(),b.size());
  const auto target=g.capacity_owned?entity_capacity::installed_target(g.bytes.va):0;
  if(ok && target) {
   std::uint32_t rel=0;std::memcpy(&rel,b.data()+1,4);
   ok=b[0]==0xe9 && g.bytes.va+5+rel==target;
  } else if(ok)ok=limits_rules::matches(b.data(),g.bytes.hex);
  if(!ok) {
   logger::write("shared handlers: guard %08X (%s) refused; native callbacks retained, online agreement refused",g.bytes.va,g.bytes.what);return;
  }
 }
 using namespace shared_handler_bridges;
 const targets callbacks{&teleport_land,&popup_decision,&charge_enter,&charge_exit,&charge_input,&charge_update,&charge_event_bridge,&nightcrawler_bridge,&gambit_bridge};
 bool safe=true;
 if(xml1_hook_transaction::apply(memory,patches(wanted,callbacks),&safe)) {
  applied=wanted;
  logger::write("shared handlers: XML1 callback rules installed (mask %02X, revision 1)",applied);
 } else {
  if(!safe)game_version::refuse_startup();
  logger::write(safe?"shared handlers: transaction refused; native callbacks restored, online agreement refused":"shared handlers: unsafe rollback; startup refused");
 }
}
}
