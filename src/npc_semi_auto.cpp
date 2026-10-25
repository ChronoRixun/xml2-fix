#include "npc_semi_auto.hpp"
#include "npc_semi_auto_rules.hpp"
#include "npc_power.hpp"
#include "npc_power_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "stun_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <tuple>

namespace npc_semi_auto
{
 namespace
 {
  using namespace npc_semi_auto_rules;
  bool wanted = false, applied = false;
  std::vector<weapon> weapons;
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
  void __fastcall enter(void* self, void*, void* actor, void* move, void* previous)
  {
   entry_function(self, actor, move, previous);
   const auto* w = spec(actor);
   if (!w) return;
   const float time = now();
   const auto token = field<u32>(actor, 0x1c);
   states[timer_slot(token)] = entered(reinterpret_cast<std::uintptr_t>(actor), token, time, w->recoil_bits);
  }
  void* __cdecl handler_lookup(const char* name)
  {
   if (name && _stricmp(name, "ch_weapon_semi_auto") == 0) return &handler;
   return reinterpret_cast<void*(__cdecl*)(const char*)>(lookup)(name);
  }
  auto prepare_function = &prepare_brain;
  auto preserve_function = &preserve_weapon_range;
  auto input_function = &weapon_input;
  auto decision_function = &weapon_decision;
  auto press_function = &power_pressed;
  auto lock_function = &attack_state_continues;
  u32 init_resume = 0x504f61, refresh_resume = 0x50fcee, refresh_done = 0x50fe38;
  u32 native_movement = 0x42aa90, input_done = 0x4341f0;
  u32 decision_resume = 0x51a35f, decision_done = 0x51a51e;
  u32 power_true = 0x519ff1, power_false = 0x51a004;
  u32 lock_resume = 0x512f1a, lock_return = 0x51304a;
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
  __declspec(naked) void lock_hook()
  {
   __asm {
    pushfd
    pushad
    mov esi, esp
    sub esp, 528
    and esp, -16
    mov [esp + 512], esi
    fxsave [esp]
    push eax
    call dword ptr [lock_function]
    add esp, 4
    fxrstor [esp]
    mov esp, [esp + 512]
    test al, al
    jnz lock_continue
    popad
    popfd
    test byte ptr [eax + 0x3d9], 0x10
    jne lock_skip
    jmp dword ptr [lock_resume]
   lock_skip:
    jmp dword ptr [lock_return]
   lock_continue:
    popad
    popfd
    jmp dword ptr [lock_resume]
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
 void install(HMODULE game)
 {
  if (applied) return;
  const auto choice = npc_power_rules::parse_rule(ini::text(L"Game", L"NpcSemiAuto").value_or(""));
  wanted = !choice.error.empty() || choice.value == npc_power_rules::rule::xml1;
  if (!wanted) return;
  const auto fail = [](const char* reason) { logger::write("NPC semi-auto: %s; stock weapon conversion retained, requested agreement unverified", reason); };
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
   if (!memory.read(g.va, bytes.data(), bytes.size()) || !limits_rules::matches(bytes.data(), g.hex)) { fail(g.what); return; }
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
       {0x512f0d, &lock_hook, false, 13}}) {
   const auto* p = reinterpret_cast<const std::uint8_t*>(site);
   patches.push_back({site, {p, p + length}, stun_rules::branch(site, target, call, length)});
  }
  bool safe = true;
  applied = xml1_hook_transaction::apply(memory, patches, &safe);
  if (!applied) { if (!safe) game_version::refuse_startup(); fail("guarded transaction failed"); return; }
  logger::write("NPC semi-auto: guarded non-bullet weapon brain and input gate, %zu source actors, fingerprint %08X; rule 16 revision 10 words 2-3", weapons.size(), fingerprint(weapons));
 }
}
