#include "danger_room_party.hpp"
#include "danger_room_party_rules.hpp"
#include "gameplay_agreement.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"

namespace danger_room_party
{
 namespace
 {
  bool wanted = false, applied = false;
  const char names[4][16] = {"cyclops", "phoenix", "storm", "wolverine"};
 }
 bool requested() { return wanted; }
 bool installed() { return applied; }
 void install(HMODULE game)
 {
  if (applied) return;
  const auto selected = danger_room_party_rules::parse(ini::text(L"Game", L"DangerRoomParty").value_or(""));
  if (selected == danger_room_party_rules::choice::off) return;
  wanted = true;
  // The integration owner assigns gameplay IDs. Do not negotiate this change as stock.
  gameplay_agreement::reject_unregistered_rule("DangerRoomParty");
  if (selected != danger_room_party_rules::choice::xml1 || reinterpret_cast<std::uintptr_t>(game) != 0x400000)
  {
   logger::write("danger room party: invalid option or image base; native fallback retained");
   return;
  }
  std::array<std::uint32_t, 4> pointers{};
  for (std::size_t i = 0; i < pointers.size(); ++i)
   pointers[i] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(names[i]));
  xml1_hook_transaction::memory memory;
  bool safe = true;
  applied = xml1_hook_transaction::apply(memory, danger_room_party_rules::patches(pointers), &safe);
  if (!applied)
  {
   if (!safe) game_version::refuse_startup();
   logger::write(safe ? "danger room party: guarded transaction refused; native fallback retained"
                      : "danger room party: rollback/protection verification failed; startup refused");
   return;
  }
  logger::write("danger room party: XML1 four-hero fallback installed; online blocked pending gameplay rule ID assignment");
 }
}
