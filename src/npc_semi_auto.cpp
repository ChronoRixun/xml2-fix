#include "actor_ai_layout.hpp"
#include "npc_semi_auto.hpp"
#include "npc_semi_auto_rules.hpp"
#include "det_math.hpp"
#include "npc_power.hpp"
#include "npc_power_rules.hpp"
#include "mold_query.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "stun_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <tuple>

namespace npc_semi_auto
{
 void register_ai_layout(){actor_ai::register_consumer(actor_ai::consumer::semi_auto);actor_ai::register_consumer(actor_ai::consumer::weapon);}
 namespace
 {
  using namespace npc_semi_auto_rules;
  bool wanted = false, applied = false;
  std::vector<weapon> weapons;
  std::array<std::uint8_t, 32> digest{};
  std::array<state, timer_slots> states{};
  std::array<void*, 8> callbacks{};
  void** handler = callbacks.data();
  template<class T> T& field(void* p, std::size_t offset)
  { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p) + offset); }
  auto clock_function = reinterpret_cast<float(__cdecl*)()>(npc_semi_auto_rules::clock);
  using action_t = void*(__thiscall*)(void*, u32);
  auto action_function = reinterpret_cast<action_t>(resolve_action);
  using entry_t = void(__thiscall*)(void*, void*, void*, void*);
  auto entry_function = reinterpret_cast<entry_t>(default_entry);
  float now() { return clock_function(); }
  const weapon* spec(void* actor)
  {
   if (!actor) return nullptr;
   auto* stats = field<void*>(actor, 0x35c);
   if (!stats) return nullptr;
   const char* name = static_cast<char*>(stats) + 0x150;
   const auto length = strnlen(name, 32);
   if (length == 32) return nullptr;
   for (const auto& w : weapons)
    if (w.actor.size() == length && _strnicmp(name, w.actor.c_str(), length) == 0) return &w;
   return nullptr;
  }
  state* find(void* actor, u32 token)
  {
   auto& s = states[timer_slot(token)];
   return s.actor == reinterpret_cast<std::uintptr_t>(actor) && s.token == token ? &s : nullptr;
  }
  #include "npc_weapon_brain.hpp"
  #include "npc_bullet_trace.hpp"
  // The semi-auto entry callback (default handler vt+4 replacement): its recoil deadline arithmetic runs inside the
  // same fixed floating-point environment as the hooks (det_math::fp_env); the native entry runs first, unchanged.
  void __fastcall enter(void* self, void*, void* actor, void* move, void* previous) noexcept
  {
   entry_function(self, actor, move, previous);
   det_math::fp_env fp;
   const auto* w = spec(actor);
   if (!w) return;
   const float time = now();
   const auto token = field<u32>(actor, 0x1c);
   rearm(states[timer_slot(token)], reinterpret_cast<std::uintptr_t>(actor), token, time, std::bit_cast<float>(w->recoil_bits));
  }
  void* __cdecl handler_lookup(const char* name) noexcept
  {
   if (name && _stricmp(name, "ch_weapon_semi_auto") == 0) return &handler;
   return reinterpret_cast<void*(__cdecl*)(const char*)>(lookup)(name);
  }
  void(__cdecl* prepare_function)(void*) = &prepare_brain; // (non-noexcept pointer types: the tests swap fixtures in)
  bool(__cdecl* preserve_function)(void*, void* const*) = &preserve_weapon_range;
  bool(__cdecl* input_function)(void*, void*) = &weapon_input;
  bool(__cdecl* decision_function)(void*, void*) = &weapon_decision;
  bool(__cdecl* press_function)(int, void*, void*) = &power_pressed;
  void(__cdecl* sight_function)(void*) = &line_of_sight;
  u32 init_resume = 0x504f61, refresh_resume = 0x50fcee, refresh_done = 0x50fe38;
  u32 native_movement = 0x42aa90, input_done = 0x4341f0;
  u32 decision_resume = 0x51a35f, decision_done = 0x51a51e;
  u32 power_true = 0x519ff1, power_false = 0x51a004;
  u32 sight_resume = 0x519e41;
  // Every hook saves the caller's FPU/SSE state (fxsave), runs the weapon brain under Windows' default MXCSR (round to
  // nearest, no FTZ/DAZ: an overlay or driver may have changed it on one PC only) and restores the state (fxrstor).
  u32 mxcsr_value = det_math::default_mxcsr;
  __declspec(naked) void init_hook()
  {
   __asm {
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    mov [esp + 512], ebx
    fxsave [esp]
    ldmxcsr mxcsr_value
    push esi
    call dword ptr [prepare_function]
    add esp, 4
    fxrstor [esp]
    mov esp, [esp + 512]
    popad
    popfd
    mov ecx, [esi + 0x4c4]
    jmp dword ptr [init_resume]
   }
  }
  __declspec(naked) void refresh_hook()
  {
   __asm {
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    mov [esp + 512], ebx
    fxsave [esp]
    ldmxcsr mxcsr_value
    lea eax, [ebx + 0x38]
    push eax
    push edi
    call dword ptr [preserve_function]
    add esp, 8
    fxrstor [esp]
    mov esp, [esp + 512]
    test al, al
    jnz restored_range
    popad
    popfd
    mov ecx, [edi + 0x10]
    mov eax, [ecx + 4]
    jmp dword ptr [refresh_resume]
   restored_range:
    popad
    popfd
    jmp dword ptr [refresh_done]
   }
  }
  __declspec(naked) void input_hook()
  {
   __asm {
    // The original movement call consumes its duplicate argument. Our call
    // frame remains available so the original input tail can be selected.
    push dword ptr [esp + 4]
    call dword ptr [native_movement]
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    mov [esp + 512], ebx
    fxsave [esp]
    ldmxcsr mxcsr_value
    push edi
    push esi
    call dword ptr [input_function]
    add esp, 8
    fxrstor [esp]
    mov esp, [esp + 512]
    test al, al
    jnz weapon_consumed
    popad
    popfd
    ret 4
   weapon_consumed:
    popad
    popfd
    // Preserve EAX while replacing only our return address.
    push eax
    mov eax, [input_done]
    mov [esp + 4], eax
    pop eax
    ret 4
   }
  }
  // Top of the ranged brain update after its target resolve (0x519e3a): the bullet
  // line-of-sight drop, then the two native loads this site displaced.
  __declspec(naked) void sight_hook()
  {
   __asm {
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    mov [esp + 512], ebx
    fxsave [esp]
    ldmxcsr mxcsr_value
    push ebp
    call dword ptr [sight_function]
    add esp, 4
    fxrstor [esp]
    mov esp, [esp + 512]
    popad
    popfd
    mov ebx, [esp + 0x70]
    mov edx, [ebp]
    jmp dword ptr [sight_resume]
   }
  }
  __declspec(naked) void power_hook()
  {
   __asm {
    pushfd
    pushad
    movzx eax, al
    mov esi, esp
    sub esp, 528
    and esp, -16
    mov [esp + 512], esi
    fxsave [esp]
    ldmxcsr mxcsr_value
    push ebx
    push ebp
    push eax
    call dword ptr [press_function]
    add esp, 12
    fxrstor [esp]
    mov esp, [esp + 512]
    test al, al
    jnz power_done
    popad
    popfd
    jmp dword ptr [power_false]
   power_done:
    popad
    popfd
    cmp dword ptr [ebp + 8], -1
    jne power_continue
    jmp dword ptr [power_true]
   power_continue:
    jmp dword ptr [power_false]
   }
  }
  __declspec(naked) void decision_hook()
  {
   __asm {
    pushfd
    pushad
    mov ebx, esp
    sub esp, 528
    and esp, -16
    mov [esp + 512], ebx
    fxsave [esp]
    ldmxcsr mxcsr_value
    push dword ptr [ebx + 16]
    push dword ptr [ebx + 8]
    call dword ptr [decision_function]
    add esp, 8
    fxrstor [esp]
    mov esp, [esp + 512]
    test al, al
    jnz weapon_decided
    popad
    popfd
    mov ecx, [ebp + 4]
    mov eax, [ecx + 0x10]
    jmp dword ptr [decision_resume]
   weapon_decided:
    popad
    popfd
    jmp dword ptr [decision_done]
   }
  }
 }
 bool requested() { return wanted; }
 bool installed() { return wanted && applied; }
 bool verified() { return !wanted || applied; }
 std::array<u32, 4> values(std::array<u32, 4> v)
 { if (wanted) { v[2] = installed() ? 1u : 0u; v[3] = fingerprint(weapons); } return v; }
 const std::array<std::uint8_t, 32>* detail()
 { if (!wanted) return nullptr; digest = table_digest(weapons); return &digest; }
 void install(HMODULE game)
 {
    actor_ai::register_consumer(actor_ai::consumer::semi_auto);
    actor_ai::register_consumer(actor_ai::consumer::weapon);
  if (applied) return;
  const auto choice = npc_power_rules::parse_rule(ini::text(L"Game", L"NpcSemiAuto").value_or(""));
  wanted = !choice.error.empty() || choice.value == npc_power_rules::rule::xml1;
  if (!wanted) return;
  // All or nothing: the requested rule-16 configuration is the power rule AND the weapon brain, so a weapon brain
  // that cannot install takes the power rule's patches back out too (npc_power::withdraw). Online stays closed
  // (requested, unverified) either way; offline the game then runs XMen2.exe's own enemy AI, not half of rule 16.
  const auto fail = [](const char* reason) {
   const bool had = npc_power::installed();
   const bool withdrawn = npc_power::withdraw();
   logger::write("NPC semi-auto: %s; stock weapon conversion retained, %s, requested agreement unverified", reason,
                 !had ? "NpcPowerRule not installed" : withdrawn ? "NpcPowerRule withdrawn with it (all or nothing)" : "NpcPowerRule could NOT be withdrawn");
  };
  if (!choice.error.empty() || !npc_power::installed()) { fail("requires NpcPowerRule=xml1 and NpcSemiAuto=xml1"); return; }
  const int count = ini::number(L"NpcSemiAuto", L"Count", 0);
  if (count < 1 || count > int(capacity)) { fail("invalid source weapon table count"); return; }
  weapons.clear();
  for (int i = 1; i <= count; ++i) {
   const auto key = L"W" + std::to_wstring(i);
   const auto w = parse(ini::text(L"NpcSemiAuto", key.c_str()).value_or(""));
   if (!w) { fail("invalid source weapon record"); return; }
   weapons.push_back(*w);
  }
  if (!canonicalize(weapons)) { fail("duplicate or invalid source weapon table"); return; }
  if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base) { fail("unsupported image base"); return; }
  xml1_hook_transaction::memory memory;
  for (const auto& g : guards) {
   std::vector<std::uint8_t> bytes(limits_rules::hex_size(g.hex));
   if (!memory.read(g.va, bytes.data(), bytes.size()) || !limits_rules::matches(bytes.data(), actor_ai::guard(g.va,g.hex,actor_ai::installed()))) { fail(g.what); return; }
  }
  std::memcpy(callbacks.data(), reinterpret_cast<const void*>(default_vtable), sizeof(callbacks));
  callbacks[1] = reinterpret_cast<void*>(&enter);
  callbacks[6] = reinterpret_cast<void*>(&semi_chain);
  std::vector<xml1_hook_transaction::patch> patches;
  for (const auto [site, target, call, length] : {
       std::tuple<u32, const void*, bool, std::size_t>{lookup_call, &handler_lookup, true, 5},
       {0x504f5b, &init_hook, false, 6},
       {0x50fce8, &refresh_hook, false, 6},
       {0x4341b6, &input_hook, true, 5},
       {0x51a359, &decision_hook, false, 6},
       {0x519fe7, &power_hook, false, 8},
       {0x519e3a, &sight_hook, false, 7},
       {0x4dcfe0, &bullet_event, false, 6},
       {0x452225, &bullet_muzzle_hook, false, 7},
       {0x452350, &bullet_collision_hook, false, 5}}) {
   const auto* p = reinterpret_cast<const std::uint8_t*>(site);
   patches.push_back({site, {p, p + length}, stun_rules::branch(site, target, call, length)});
  }
  bool safe = true;
  applied = xml1_hook_transaction::apply(memory, patches, &safe);
  if (!applied) { if (!safe) game_version::refuse_startup(); fail("guarded transaction failed"); return; }
  logger::write("NPC semi-auto: guarded weapon brain (held fire, empty-query branches, bullet line of sight) and input gate (recoil, warm-up, continuous), %zu source actors, fingerprint %08X; rule 16 revision %u words 2-3", weapons.size(), fingerprint(weapons), unsigned(npc_power::revision));
 }
}
