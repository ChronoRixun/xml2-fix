#include "danger_room_healthbars.hpp"
#include "danger_room_healthbars_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"

namespace danger_room_healthbars
{
 namespace { bool applied = false; }
 bool installed() { return applied; }
 void install(HMODULE game)
 {
  if (applied) return;
  const auto selected = danger_room_healthbars_rules::parse(ini::text(L"Game", L"DangerRoomHealthBars").value_or(""));
  if (selected == danger_room_healthbars_rules::choice::off) return;
  if (selected != danger_room_healthbars_rules::choice::xml1 || reinterpret_cast<std::uintptr_t>(game) != 0x400000)
  {
   logger::write("danger room health bars: invalid option or image base; native HUD retained");
   return;
  }
  xml1_hook_transaction::memory memory;
  bool safe = true;
  applied = xml1_hook_transaction::apply(memory, danger_room_healthbars_rules::patches(), &safe);
  if (!applied)
  {
   if (!safe) game_version::refuse_startup();
   logger::write(safe ? "danger room health bars: guarded transaction refused; native HUD retained"
                      : "danger room health bars: rollback/protection verification failed; startup refused");
   return;
  }
  // Presentation only: no actor health, protection, target selection, course
  // state, or peer-agreed gameplay changes; no gameplay-agreement rule.
  logger::write("danger room health bars: protected course targets can display their gauge");
 }
}
