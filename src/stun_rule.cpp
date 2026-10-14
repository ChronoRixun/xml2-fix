#include "stun_rule.hpp"
#include "stun_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <cctype>

namespace stun_rule
{
 namespace
 {
  using u32 = std::uint32_t;
  bool wanted = false, applied = false;
  const u32 set_move_resume = stun_rules::set_move_resume;
  template<class T> T& field(void* p, std::size_t offset)
  { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p) + offset); }

  void __cdecl settle_flag(void* actor, void* move)
  {
   auto& flags = field<std::uint8_t>(actor, 0x765);
   if (!(flags & 1)) return;
   const bool hero = stun_rules::party_hero(field<u32>(actor, 4), field<int>(actor, 0x3d4));
   if (!hero) return;
   // Resolve for this actor, just as native expiry does. A move's +8 is its name, but the
   // resolver can select an alternate move with another name through its applicability test.
   using resolve_t = void* (__thiscall*)(void*, const u32*);
   const bool stunned = move && move == reinterpret_cast<resolve_t>(stun_rules::resolve_move)(
       actor, reinterpret_cast<const u32*>(stun_rules::stun_name));
   flags = stun_rules::reaction_flags(flags, hero, stunned);
  }

  // Wrap only this powerup's request. Native dispatch keeps every priority, duration and refresh decision.
  // If it refuses the move, XML1 never enters ch_stun and therefore never owns a stun flag.
  void __fastcall request_stun(void* actor, void*, void* move, u32 animation, float speed, u32 a, u32 b, u32 c)
  {
   using dispatch_t = void(__fastcall*)(void*, void*, void*, u32, float, u32, u32, u32);
   reinterpret_cast<dispatch_t>(stun_rules::native_dispatch)(actor, nullptr, move, animation, speed, a, b, c);
   settle_flag(actor, field<void*>(actor, 0x378));
  }

  // At the final assignment, EDI is the actor and ESI the resolved non-null move (including idle fallback).
  // Clear on any accepted exit from stun, including knockback, death and scripted interruptions.
  __declspec(naked) void set_move_hook()
  {
   __asm {
    pushfd
    pushad
    push esi
    push edi
    call settle_flag
    add esp, 8
    popad
    popfd
    mov [edi + 0x378], esi
    jmp dword ptr [set_move_resume]
   }
  }
 }

 bool requested() { return wanted; }
 bool installed() { return applied; }
 bool verified() { return !wanted || applied; }
 std::array<u32, 4> values() { return {applied ? 1u : 0u, 0, 0, 0}; }

 void install(HMODULE game)
 {
  if (applied) return;
  const auto raw = ini::text(L"Game", L"StunRule").value_or("");
  std::string key(ini_rules::value_text(raw));
  for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (key.empty() || key == "xml2") return;
  wanted = true;
  if (key != "xml1") {
   logger::write("stun rule: StunRule must be xml1 or xml2; native behavior retained, online agreement refused");
   return;
  }
  xml1_hook_transaction::memory memory;
  if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base) {
   logger::write("stun rule: unsupported image base; native behavior retained, online agreement refused");
   return;
  }
  for (const auto& g : stun_rules::guards) {
   std::vector<std::uint8_t> bytes(limits_rules::hex_size(g.hex));
   if (!memory.read(g.va, bytes.data(), bytes.size()) || !limits_rules::matches(bytes.data(), g.hex)) {
    logger::write("stun rule: guard %08X (%s) refused; native behavior retained, online agreement refused", unsigned(g.va), g.what);
    return;
   }
  }
  const auto& call = stun_rules::guards[0];
  const auto& setter = stun_rules::guards[1];
  const std::vector<xml1_hook_transaction::patch> writes{
   {call.va, {0xe8, 0x0c, 0x7f, 0xee, 0xff}, stun_rules::branch(call.va, &request_stun, true, 5)},
   {setter.va, {0x89, 0xb7, 0x78, 0x03, 0x00, 0x00}, stun_rules::branch(setter.va, &set_move_hook, false, 6)},
  };
  bool safe = true;
  applied = xml1_hook_transaction::apply(memory, writes, &safe);
  if (!applied) {
   if (!safe) game_version::refuse_startup();
   logger::write(safe ? "stun rule: transaction refused; native behavior restored, online agreement refused"
                      : "stun rule: rollback/protection verification failed; process startup refused");
   return;
  }
  logger::write("stun rule: [Game] StunRule=xml1; party heroes discard rejected stun flags and clear them on reaction exit (rule 18 revision 1)");
 }
}
