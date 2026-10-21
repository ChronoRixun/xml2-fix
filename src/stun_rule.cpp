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
  unsigned requests = 0; // reaction requests forwarded by the powerup entry (game thread only)
  const u32 set_move_resume = stun_rules::set_move_resume;
  const u32 expire_resume = stun_rules::expire_resume, expire_done = stun_rules::expire_done;
  template<class T> T& field(void* p, std::size_t offset)
  { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p) + offset); }
  bool __cdecl party(void* actor)
  { return stun_rules::party_hero(field<u32>(actor, 4), field<int>(actor, 0x3d4)); }
  void* resolved_stun(void* actor)
  {
   using resolve_t = void* (__thiscall*)(void*, const u32*);
   return reinterpret_cast<resolve_t>(stun_rules::resolve_move)(actor, reinterpret_cast<const u32*>(stun_rules::stun_name));
  }

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

  // Keep native priority/refresh dispatch, with XML1's animation-speed argument for party heroes.
  void __fastcall request_stun(void* actor, void*, void* move, u32 animation, float speed, u32 a, u32 b, u32 c)
  {
   using dispatch_t = void(__fastcall*)(void*, void*, void*, u32, float, u32, u32, u32);
   ++requests;
   if (party(actor)) speed = stun_rules::reaction_speed(speed, field<float>(actor, 0x520));
   reinterpret_cast<dispatch_t>(stun_rules::native_dispatch)(actor, nullptr, move, animation, speed, a, b, c);
   settle_flag(actor, field<void*>(actor, 0x378));
  }

  bool arena()
  {
   using game_t = void* (__cdecl*)();
   void* game = reinterpret_cast<game_t>(stun_rules::game_object)();
   using flag_t = bool(__thiscall*)(void*);
   return reinterpret_cast<flag_t>(field<void**>(game, 0)[0x5c / sizeof(void*)])(game);
  }

  // The powerup entry's own request (0x54a009..0x54a11f) for an already-existing shared_stunned
  // instance: same resolved move, authored speed, animation, susceptibility and living-actor tests.
  void fresh_request(void* actor)
  {
   void* move = resolved_stun(actor);
   if (!move) return;
   void** vtable = field<void**>(move, 0);
   using speed_t = float(__thiscall*)(void*, void*);
   const float speed = reinterpret_cast<speed_t>(vtable[0xf0 / sizeof(void*)])(move, field<void*>(actor, 0x35c));
   if (field<std::uint8_t>(actor, 0x57a) & 1)
    reinterpret_cast<void(__thiscall*)(void*)>(stun_rules::refresh_susceptibility)(static_cast<unsigned char*>(actor) + 0x470);
   using test_t = bool(__thiscall*)(void*);
   if (!reinterpret_cast<test_t>(stun_rules::stunnable)(actor) || !(field<float>(actor, 0x27c) > 0)) return;
   if (!(field<float>(actor, 0x520) > 0)) return;
   field<std::uint8_t>(actor, 0x765) |= 1;
   using animation_t = u32(__thiscall*)(void*);
   const u32 animation = reinterpret_cast<animation_t>(vtable[0xe8 / sizeof(void*)])(move);
   request_stun(actor, nullptr, move, animation, speed, 0, 0, 1);
  }

  // XML1 dispatches every ordinary stun hit (0x44af4..0x44cb0); PC only requests the reaction when the
  // shared_stunned powerup is new. For a party hero whose powerup was refreshed, request it here.
  bool __fastcall apply_stun(void* actor, void*, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g, u32 h)
  {
   using apply_t = bool(__thiscall*)(void*, u32, u32, u32, u32, u32, u32, u32, u32);
   const unsigned before = requests;
   const bool result = reinterpret_cast<apply_t>(stun_rules::apply_powerup)(actor, a, b, c, d, e, f, g, h);
   const bool hero = party(actor);
   if (stun_rules::fresh_dispatch(result, requests != before, hero, hero && arena())) fresh_request(actor);
   return result;
  }

  // Called only after the native input lock and pending-chain gates. XML1's ch_stun chooses the
  // current move's idle action once its animation unlocks; attack/smash press also satisfies its clock test.
  bool __fastcall move_input(void* move, void*, void* actor, void* input)
  {
   if (party(actor) && move == resolved_stun(actor)) {
    using clock_t = float(__cdecl*)();
    if (!stun_rules::release_input(field<float>(actor, 0x27c), field<u32>(input, 4),
        reinterpret_cast<clock_t>(stun_rules::clock)(), field<float>(actor, 0x5e8))) return false;
    using action_t = void*(__thiscall*)(void*, u32);
    void* next = reinterpret_cast<action_t>(stun_rules::resolve_action)(actor, 0);
    field<void*>(actor, 0x3bc) = next;
    // Native playmove enters the new handler before its final current-move assignment. XML1's
    // old ch_stun exits first; without this clear PC's idle entry redirects straight back to stun.
    if (next && next != move) field<std::uint8_t>(actor, 0x765) &= 0xfe;
    return true;
   }
   using input_t = bool(__thiscall*)(void*, void*, void*);
   const auto vtable = field<input_t*>(move, 0);
   return vtable[0x48 / sizeof(void*)](move, actor, input);
  }

  // A PC powerup's expiry cannot end a move-owned stun (or clear a later stun's flag).
  // The surrounding native callback still resolves the victim and retains its missing-victim path.
  __declspec(naked) void expire_hook()
  {
   __asm {
    pushfd
    pushad
    push edi
    call party
    add esp, 4
    test al, al
    jz native_expiry
    popad
    popfd
    jmp dword ptr [expire_done]
   native_expiry:
    popad
    popfd
    and byte ptr [edi + 0x765], 0xfe
    jmp dword ptr [expire_resume]
   }
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
  auto input = stun_rules::branch(stun_rules::input_call + 1, &move_input, true, 5);
  input.insert(input.begin(), 0x56); // push esi: actor, after the existing push edi: input
  const std::vector<xml1_hook_transaction::patch> writes{
   {call.va, {0xe8, 0x0c, 0x7f, 0xee, 0xff}, stun_rules::branch(call.va, &request_stun, true, 5)},
   {setter.va, {0x89, 0xb7, 0x78, 0x03, 0x00, 0x00}, stun_rules::branch(setter.va, &set_move_hook, false, 6)},
   {stun_rules::input_call, {0x8b, 0x01, 0x56, 0xff, 0x50, 0x48}, input},
   {stun_rules::expire_flag, {0x80, 0xa7, 0x65, 0x07, 0x00, 0x00, 0xfe}, stun_rules::branch(stun_rules::expire_flag, &expire_hook, false, 7)},
   {stun_rules::impact_apply, {0xe8, 0xea, 0x8e, 0x11, 0x00}, stun_rules::branch(stun_rules::impact_apply, &apply_stun, true, 5)},
  };
  bool safe = true;
  applied = xml1_hook_transaction::apply(memory, writes, &safe);
  if (!applied) {
   if (!safe) game_version::refuse_startup();
   logger::write(safe ? "stun rule: transaction refused; native behavior restored, online agreement refused"
                      : "stun rule: rollback/protection verification failed; process startup refused");
   return;
  }
  logger::write("stun rule: [Game] StunRule=xml1; party stun uses original animation speed, input recovery, reaction ownership and per-hit stun dispatch (rule 18 revision 3)");
 }
}
