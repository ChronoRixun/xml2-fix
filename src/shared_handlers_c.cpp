#include "shared_handlers_c.hpp"
#include "shared_handler_c_rules.hpp"
#include "shared_handler_c_bridges.hpp"
#include "tk_guard_bridges.hpp"
#include "xml1_grab_carry.hpp"
#include "game_version.hpp"
#include "entity_capacity.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
namespace shared_handlers_c {
namespace {std::uint32_t wanted=0,applied=0;bool valid=true;}
bool requested(){return wanted!=0;}
bool installed(){return applied!=0&&applied==wanted;}
bool verified(){return valid&&wanted==applied;}
std::array<std::uint32_t,4> values(){return {applied,0,0,0};}
void install(HMODULE game) {
 using namespace shared_handler_c_rules;
 if(applied)return;
 wanted=0;valid=true;
 for(unsigned i=0;i<keys.size();++i) {
  auto choice=parse_rule(ini::text(L"Game",keys[i]).value_or(""));
  if(!choice.set||(choice.error.empty()&&choice.value==rule::xml2))continue;
  wanted|=1u<<i;if(!choice.error.empty())valid=false;
 }
 if(!wanted)return; // OFF performs no image access and adds no agreement entry.
 if(!valid||reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base) {
  logger::write("shared handlers C: invalid selection/image; online agreement refused");return;
 }
 if((wanted&(telekinesis|guard))&&!xml1_grab_carry::installed()){
  logger::write("shared handlers C: TK/guard require installed rule45 grab lifecycle; online agreement refused");return;
 }
 xml1_hook_transaction::memory memory;
 for(const auto& g:guards)if(g.parts&wanted) {
  std::vector<unsigned char> b(g.bytes.hex.size()/2);
  bool ok=memory.read(g.bytes.va,b.data(),b.size());
  const auto owner=g.capacity_owned?entity_capacity::installed_target(g.bytes.va):0;
  if(ok&&owner) {
   std::uint32_t relative=0;std::memcpy(&relative,b.data()+1,4);
   ok=b[0]==0xe9&&g.bytes.va+5+relative==owner;
  } else if(ok)ok=limits_rules::matches(b.data(),g.bytes.hex);
  if(!ok) {
   logger::write("shared handlers C: guard %08X (%s) refused; online agreement refused",g.bytes.va,g.bytes.what);return;
  }
 }
 using namespace shared_handler_c_bridges;
 const targets callbacks{&jump_damage_bridge,&fly_damage_bridge,&colossus_input,&colossus_contact,&bounce_contact,&fastball_bridge,
  &tk_guard_bridges::tk_entry,&tk_guard_bridges::tk_update,&tk_guard_bridges::tk_cancel,&tk_guard_bridges::tk_input,&tk_guard_bridges::tk_filter,&tk_guard_bridges::guard_entry,&tk_guard_bridges::tk_parse,&tk_guard_bridges::guard_input,&tk_guard_bridges::release_arming_gate};
 bool safe=true;
 if(xml1_hook_transaction::apply(memory,patches(wanted,callbacks),&safe)) {
  if(wanted&telekinesis)xml1_grab_carry::set_telekinesis_pair_test(&tk_guard_bridges::owns_tk_pair);
  tk_guard_bridges::trace_acquisition=(wanted&telekinesis)&&ini::text(L"Test",L"TKTrace").value_or("")=="1";
  tk_guard_bridges::trace_guard=(wanted&guard)&&(tk_guard_bridges::trace_acquisition||ini::text(L"Test",L"GuardTrace").value_or("")=="1");
  applied=wanted;logger::write("shared handlers C: installed mask %03X revision 1",applied);
  // The builder's DLL gate reads this line (beta 8 080e: Phoenix power1a/1b resolve through PC audio slot 0x38).
  if(applied&telekinesis)logger::write("shared handlers C: telekinesis sounds resolve through PC audio slot 38");
  // Beta 8 080g (fix3): the builder's DLL gate reads these lines too (still TK throw, guard tap exit, Colossus talent owner).
  if(applied&telekinesis)logger::write("shared handlers C: a still telekinesis throw keeps the victim's facing (no zero divide)");
  if(applied&guard)logger::write("shared handlers C: guard decide runs at input and exits after a tap");
  if(applied&colossus)logger::write("shared handlers C: Colossus charge evaluates its talents on the caster");
 } else {
  if(!safe)game_version::refuse_startup();
  logger::write("shared handlers C: transaction refused; online agreement refused");
 }
}
}
