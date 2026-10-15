#include "danger_room_healthbars.hpp"
#include "danger_room_healthbars_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"

namespace danger_room_healthbars
{
 namespace { bool applied = false, wanted = false; }
 bool installed() { return applied; }
 bool requested() { return wanted; }
 bool verified() { return !wanted || applied; }
 void install(HMODULE game)
 {
  if (applied) return;
  const auto selected = danger_room_healthbars_rules::parse(ini::text(L"Game", L"DangerRoomHealthBars").value_or(""));
  if (selected == danger_room_healthbars_rules::choice::off) return;
  wanted = true;
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
  // The native HUD path can refresh dirty stats and schedule health clamping.
  // Rule 22 covers this timing uncertainty as well as the gauge gate.
  logger::write("danger room health bars: protected course targets can display their gauge");
 }
}
