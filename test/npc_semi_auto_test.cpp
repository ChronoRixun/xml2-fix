// Executes the production admission bridge against invented objects, with native
// engine calls replaced by fixture functions. Does not launch the game.
#include "../src/npc_semi_auto.cpp"
#include "gameplay_capabilities.hpp"
#include <cstdio>
#include <fstream>
#include <limits>

std::optional<std::string> fixture_option;
namespace ini {
 std::optional<std::string> text(const wchar_t*, const wchar_t*) { return fixture_option; }
 int number(const wchar_t*, const wchar_t*, int n) { return n; }
}
namespace npc_power { bool installed() { return true; } }
namespace logger { void write(const char*, ...) {} }
namespace game_version { void refuse_startup() {} }

namespace {
 using u32 = std::uint32_t;
 int failures = 0, checks = 0, chosen_path = -1, forced_result = -1;
 unsigned fixture_slot = 0;
 void* fixture_ai = nullptr;
 void* fixture_chains_expected = nullptr;
 std::array<std::uint32_t, 9> before{}, after{};
 alignas(16) std::array<unsigned char, 512> fp_before{}, fp_after{};
 void check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
 __declspec(naked) void path_native() { __asm { mov chosen_path, -1 } __asm { ret } }
 __declspec(naked) void path_allow() { __asm { mov chosen_path, 1 } __asm { ret } }
 __declspec(naked) void path_deny() { __asm { mov chosen_path, 0 } __asm { ret } }
 void* bridge = nullptr;
 __declspec(naked) void invoke(void*, unsigned, void*)
 {
  __asm {
   push ebp
   push ebx
   push esi
   push edi
   mov esi, [esp + 20]
   mov ebp, [esp + 24]
   mov edi, [esp + 28]
   mov eax, 12345678h
   mov ebx, 22334455h
   mov ecx, 34567890h
   mov edx, 45678901h
   cld
   stc
   pushfd
   pushad
   mov esi, esp
   lea edi, before
   mov ecx, 9
   rep movsd
   popad
   popfd
   fxsave fp_before
   push eax
   lea eax, [esp + 0x14]
   mov fixture_chains_expected, eax
   pop eax
   call dword ptr [bridge]
   fxsave fp_after
   pushfd
   pushad
   mov esi, esp
   lea edi, after
   mov ecx, 9
   rep movsd
   popad
   popfd
   pop edi
   pop esi
   pop ebx
   pop ebp
   ret
  }
 }
 float fixture_time = 0;
 float __cdecl fixture_clock() { return fixture_time; }
 std::array<std::byte, 0x148> move{};
 void* __fastcall fixture_action(void*, void*, std::uint32_t) { return move.data(); }
 int entries = 0;
 void __fastcall fixture_entry(void*, void*, void*, void*, void*) { ++entries; }
 int constructed = 0;
 void* __fastcall fixture_constructor(void* brain, void*)
 { ++constructed; npc_semi_auto::field<u32>(brain,0)=0x6950f4; return brain; }
 void __fastcall fixture_level(void* brain, void*, int level)
 { npc_semi_auto::field<int>(brain,0xc)=level; }
 void prepare_cases(void* actor, void* ai)
 {
  using namespace npc_semi_auto;
  std::array<std::byte,0x60> brain{};
  field<void*>(ai,0x4c4)=brain.data();field<void*>(brain.data(),4)=ai;
  ctor_function=reinterpret_cast<ctor_t>(&fixture_constructor);
  level_function=reinterpret_cast<set_level_t>(&fixture_level);
  for(unsigned leader:{0u,1u}) {
   weapons[0].leader=leader;field<u32>(brain.data(),0)=0x695084;
   field<u32>(ai,0x2ac)=77;field<u32>(ai,0x14)=0;
   prepare_brain(ai);
   check(field<int>(brain.data(),0xc)==(leader?11:2),"source leader selects the corresponding restored combo row");
   check(field<u32>(ai,0x14)==0 && field<float>(ai,0x280)==400,"weapon brain preserves AI class and restores prototype range");
   check(field<u32>(ai,0x2ac)==(leader?77u:1u),"source non-leader-only initializer stays conditional");
   field<float>(ai,0x280)=90;
   check(preserve_weapon_range(ai) && field<float>(ai,0x280)==400,"power refresh cannot replace weapon range");
  }
  weapons[0].leader=0;
  std::array<void*,32> chains{};
  std::array<std::byte,0x148> ordinary_move{};
  chains[5]=move.data();chains[6]=ordinary_move.data();chains[7]=move.data();
  field<float>(ordinary_move.data(),0x140)=7.f;
  field<u32>(ai,0x430)=0x40;field<u32>(ai,0x434)=4;field<u32>(ai,0x438)=0x40;
  field<u32>(ai,0x418)=0x44;field<unsigned char>(ai,0x2b9)=0x40;
  check(preserve_weapon_range(ai,chains.data()),"cached source capabilities retain weapon range");
  check(field<u32>(ai,0x430)==0 && field<u32>(ai,0x438)==0 && field<u32>(ai,0x434)==4 && field<u32>(ai,0x418)==4,
        "only synthesized gun capabilities are removed, grenade capability survives");
  check(!(field<unsigned char>(ai,0x2b9)&0x40),"ordinary authored power keeps cache valid");
  chains[6]=nullptr;field<u32>(ai,0x434)=0;
  preserve_weapon_range(ai,chains.data());
  check(field<u32>(ai,0x418)==0 && (field<unsigned char>(ai,0x2b9)&0x40),"weapon-only style matches original empty generic-power cache");
  const int prior=constructed;field<u32>(brain.data(),0)=0x12345678;
  prepare_brain(ai);check(constructed==prior,"special-purpose brain is not overwritten");
  const auto saved=weapons;weapons.clear();
  const auto ai_before=std::vector<std::byte>(static_cast<std::byte*>(ai),static_cast<std::byte*>(ai)+0x800);
  const auto actor_before=std::vector<std::byte>(static_cast<std::byte*>(actor),static_cast<std::byte*>(actor)+0x800);
  std::array<std::byte,0x28> input{};field<u32>(input.data(),0)=0x90;const auto input_before=input;
  prepare_brain(ai);
  check(!preserve_weapon_range(ai) && !weapon_input(actor,input.data()) && !weapon_decision(brain.data(),input.data()) &&
        !power_pressed(0,brain.data(),input.data()) && !attack_state_continues(actor),
        "unlisted control retains every native path");
  check(!std::memcmp(ai,ai_before.data(),ai_before.size()) && !std::memcmp(actor,actor_before.data(),actor_before.size()) && input==input_before,
        "unlisted control actor, AI and input bytes are unchanged");
  weapons=saved;
 }
 #include "npc_weapon_bridges_test.hpp"
 std::string requested_name;
 std::array<std::byte, 0x148> idle_move{};
 unsigned requests = 0;
 int requested_animation = 0;
 u32 requested_flags = 0;
 void* __fastcall fixture_intern(u32* key, void*, const char* name)
 { requested_name = name; *key = 1; return key; }
 void* __fastcall fixture_named(void*, void*, const u32*)
 { return requested_name == "idle" ? idle_move.data() : move.data(); }
 void __fastcall fixture_request(void* actor, void*, void* selected, int animation, float duration, u32 flags, void*, void*)
 {
  ++requests; requested_animation = animation; requested_flags = flags;
  check(duration == -1, "weapon requests native duration");
  npc_semi_auto::field<void*>(actor, 0x378) = selected;
  if (selected == move.data()) npc_semi_auto::enter(nullptr, nullptr, actor, selected, nullptr);
 }
 void input_gate_cases(void* actor)
 {
  using namespace npc_semi_auto;
  name_function = reinterpret_cast<resolve_name_t>(&fixture_named);
  intern_function = reinterpret_cast<intern_move_t>(&fixture_intern);
  request_function = reinterpret_cast<request_move_t>(&fixture_request);
  std::array<std::byte, 0x28> input{};
  field<void*>(actor, 0x378) = idle_move.data();
  field<int>(move.data(), 0xbc) = 115;
  field<u32>(input.data(), 0) = 0x90;
  field<int>(actor, 0x3d4) = -1;
  field<void*>(move.data(), 0xc8) = &handler;
  const u32 token = field<u32>(actor, 0x1c);
  states[timer_slot(token)] = {};
  requests = 0; fixture_time = 100;
  check(weapon_input(actor, input.data()) && requests == 1, "original input path requests first shot");
  check(requested_animation == 115 && requested_flags == 9, "mapped power1 and original restart flags");
  fixture_time = 100.624f;
  check(weapon_input(actor, input.data()) && requests == 1, "input consumes held fire before recoil expires");
  fixture_time = 100.625f;
  check(weapon_input(actor, input.data()) && requests == 2, "input requests repeat at shared deadline");
  field<unsigned char>(actor, 0x3d9) = 0x40;
  fixture_time = 101.25f;
  check(weapon_input(actor, input.data()) && requested_animation == 127, "source crouch flag uses mapped power7");
  field<unsigned char>(actor, 0x3d9) = 0;
  field<int>(move.data(), 0xbc) = 9; // running is not a firing animation
  fixture_time = 102;
  check(weapon_input(actor, input.data()) && requested_name == "idle" && requested_flags == 0,
        "non-firing animation first requests idle without forcing a shot");
  check(find(actor, token)->next == 102.05f, "transition uses original 0.05 second deadline");
  field<int>(move.data(), 0xbc) = 115;
  field<unsigned char>(actor, 0x764) = 1;
  const unsigned prior = requests;
  fixture_time = 200;
  check(weapon_input(actor, input.data()) && requests == prior, "actor input lock blocks fire");
  field<unsigned char>(actor, 0x764) = 0;
  field<u32>(input.data(), 0) = 0x10;
  check(!weapon_input(actor, input.data()), "one button alone retains native input");
  field<u32>(input.data(), 0) = 0x90;
  field<int>(actor, 0x3d4) = 0;
  check(!weapon_input(actor, input.data()), "controlled characters retain native input");
  field<int>(actor, 0x3d4) = -1;
  for (const unsigned mask : {0u, 0x10u, 0x80u, 0x90u, 0x100u, 0x190u}) {
   field<u32>(input.data(), 0) = mask; field<u32>(input.data(), 0x24) = 0;
   check(semi_chain(nullptr, nullptr, actor, input.data()), "semi chain consumes its source cases");
   if ((mask & 0x90) == 0x90) check(field<u32>(input.data(), 0x24) == 8, "held weapon keeps current move");
   else if (!(mask & 0x100)) check(field<void*>(actor, 0x3bc) == nullptr && field<u32>(input.data(), 0x24) == 4,
                                 "released weapon requests normal chain completion");
  }
 }
 // Revision 9 decision: held fire every update, no burst timer, native helpers in order.
 void* decision_self = nullptr; void* decision_target = nullptr; bool decision_nav = true;
 std::string decision_calls;
 bool __fastcall fixture_target(void*, void*, u32, void** self, void** target, void** entity)
 { *self = decision_self; *target = decision_target; *entity = nullptr; return true; }
 bool __fastcall fixture_nav(void*, void*, npc_semi_auto::point* d)
 { decision_calls += "n"; check(d->z == 0 && d->x == 30 && d->y == -40, "navigation check receives the flat target direction"); return decision_nav; }
 npc_semi_auto::point* __cdecl fixture_angles(npc_semi_auto::point* out, const npc_semi_auto::point*)
 { decision_calls += "a"; *out = {1, 2, 3}; return out; }
 void __fastcall fixture_face(void*, void*, npc_semi_auto::point*, npc_semi_auto::point* a)
 { decision_calls += "f"; check(a->x == 1 && a->y == 2 && a->z == 3, "facing receives converted angles"); }
 void __fastcall fixture_aim(void*, void*, const npc_semi_auto::point* p)
 { decision_calls += "m"; check(p->x == 40 && p->y == -30 && p->z == 7, "aim point is the destination position"); }
 const npc_semi_auto::point* __fastcall fixture_position(void* e, void*, npc_semi_auto::point* out)
 { decision_calls += "p"; *out = npc_semi_auto::field<npc_semi_auto::point>(e, 0x20); return out; }
 void decision_cases(void* actor, void* ai)
 {
  using namespace npc_semi_auto;
  target_function = reinterpret_cast<resolve_target_t>(&fixture_target);
  nav_function = reinterpret_cast<nav_t>(&fixture_nav);
  angles_function = &fixture_angles;
  face_function = reinterpret_cast<face_t>(&fixture_face);
  aim_function = reinterpret_cast<aim_t>(&fixture_aim);
  position_function = &fixture_position;
  std::array<std::byte, 0x60> brain{}; std::array<std::byte, 0x800> hero{};
  field<void*>(brain.data(), 4) = ai;
  field<float>(brain.data(), 0x1c) = 0; field<float>(brain.data(), 0x20) = .6f; field<float>(brain.data(), 0x24) = 100;
  field<point>(actor, 0x20) = {10, 10, 7}; field<point>(hero.data(), 0x20) = {40, -30, 7};
  decision_self = actor; decision_target = hero.data();
  for (int update = 0; update != 120; ++update) {
   std::array<std::byte, 0x28> input{}; field<u32>(input.data(), 8) = 5; decision_calls.clear();
   check(weapon_decision(brain.data(), input.data()), "listed weapon actor consumes the continuation");
   check(field<u32>(input.data(), 0) == 0x90 && field<u32>(input.data(), 4) == 0x90 && field<u32>(input.data(), 8) == 0,
         "power and fire are held on every update with movement cleared");
   check(decision_calls == "nafpm", "native navigation, facing and aim order");
  }
  check(field<float>(brain.data(), 0x1c) == 0 && field<float>(brain.data(), 0x20) == .6f && field<float>(brain.data(), 0x24) == 100,
        "constructor burst timers are never written (original officer ring)");
  for (const u32 press : {0x90u, 0xa0u, 0xc0u, 0x180u}) {
   std::array<std::byte, 0x28> pressed{}; field<u32>(pressed.data(), 0) = press | 0x2000;
   check(power_pressed(0, brain.data(), pressed.data()) && power_pressed(0x101, brain.data(), pressed.data()),
         "a power pressed by the decision counts as the original's true result");
  }
  for (const u32 none : {0u, 0x2000u, 0x20u, 0x80u}) {
   std::array<std::byte, 0x28> plain{}; field<u32>(plain.data(), 0) = none;
   check(!power_pressed(0, brain.data(), plain.data()) && power_pressed(1, brain.data(), plain.data()),
         "no press keeps the native result");
  }
  check(attack_state_continues(actor), "listed weapon actor keeps the original attack-state update during locked moves");
  decision_nav = false; std::array<std::byte, 0x28> input{}; decision_calls.clear();
  check(weapon_decision(brain.data(), input.data()) && field<u32>(input.data(), 0) == 0 && decision_calls == "n",
        "blocked navigation holds nothing");
  decision_nav = true; decision_self = hero.data(); decision_calls.clear();
  check(weapon_decision(brain.data(), input.data()) && field<u32>(input.data(), 0) == 0 && decision_calls.empty(),
        "foreign self resolution holds nothing");
 }
 float source_recoil = 0;
 struct source_object { void** vtable; };
 float __fastcall source_get_recoil(void*, void*) { return source_recoil; }
 void* source_prototype_vtable[15]{nullptr, reinterpret_cast<void*>(&source_get_recoil)};
 source_object source_prototype{source_prototype_vtable};
 void* __fastcall source_get_prototype(void*, void*, unsigned slot)
 { check(slot == 0, "source requests weapon slot zero"); return &source_prototype; }
 void* source_weapon_vtable[13]{reinterpret_cast<void*>(&source_get_prototype)};
 source_object source_weapon{source_weapon_vtable};
 void* __fastcall source_get_weapon(void*, void*, unsigned handle)
 { check(handle == 123, "source resolves the actor weapon handle"); return &source_weapon; }
 void* source_manager_vtable[3]{nullptr, nullptr, reinterpret_cast<void*>(&source_get_weapon)};
 source_object source_manager{source_manager_vtable};
 void* __cdecl source_get_manager() { return &source_manager; }
 #include "npc_weapon_input_test.hpp"
 void original_entry_comparison(const char* path)
 {
  // Read the owner's original callback, never embed or redistribute it. Only
  // its two external direct calls are redirected to synthetic dependencies.
  std::ifstream file(path, std::ios::binary);
  std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)), {});
  const auto word = [&raw](std::size_t offset) { u32 value = 0; if (offset + 4 <= raw.size()) std::memcpy(&value, raw.data() + offset, 4); return value; };
  if (raw.size() < 0x124 || std::memcmp(raw.data(), "XBEH", 4)) { check(false, "original XBE readable"); return; }
  const u32 image = word(0x104), count = word(0x11c), section_table = word(0x120);
  if (section_table < image || count > 64) { check(false, "original XBE sections valid"); return; }
  std::vector<unsigned char> body;
  constexpr u32 address = 0xe14d0, size = 0x52;
  for (u32 i = 0; i < count; ++i) {
   const std::size_t section = std::size_t(section_table - image) + i * 0x38;
   if (section + 0x38 > raw.size()) break;
   const u32 va = word(section + 4), offset = word(section + 12), bytes = word(section + 16);
   if (address < va || address - va + size > bytes || std::uint64_t(offset) + address - va + size > raw.size()) continue;
   body.assign(raw.begin() + offset + address - va, raw.begin() + offset + address - va + size); break;
  }
  if (body.size() != size || body[0] != 0x56 || body[1] != 0x57 || body[0x4f] != 0xc2 || body[0x50] != 12 || body[0x51] != 0) {
   check(false, "original callback boundaries"); return;
  }
  constexpr std::array<std::uint8_t, 32> expected_hash{
   0x4f,0x3c,0xd9,0x87,0x2d,0x28,0xe1,0x56,0x4a,0x02,0x23,0x6d,0x39,0x73,0xca,0xd6,
   0xb7,0xe0,0x7f,0xf1,0xa1,0xd5,0x9e,0x9d,0xc1,0xfa,0x3b,0x83,0xef,0xa5,0xbb,0x36};
  const bool supported = sha256_fips::digest(body.data(), body.size()) == expected_hash;
  check(supported, "original callback full identity before executing it");
  if (!supported) return;
  auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  if (!code) { check(false, "synthetic original callback allocation"); return; }
  bool valid = true;
  for (const auto [offset, expected, replacement] : {
       std::tuple<u32, u32, const void*>{2, 0xbd520, &source_get_manager},
       {0x3e, 0x66ab0, &fixture_clock}}) {
   u32 relative = 0; std::memcpy(&relative, body.data() + offset + 1, 4);
   valid &= body[offset] == 0xe8 && address + offset + 5 + relative == expected;
   relative = u32(reinterpret_cast<std::uintptr_t>(replacement)) - u32(reinterpret_cast<std::uintptr_t>(code)) - offset - 5;
   std::memcpy(body.data() + offset + 1, &relative, 4);
  }
  check(valid, "original callback dependency targets");
  if (valid) {
   std::memcpy(code, body.data(), body.size()); DWORD old = 0;
   valid = VirtualProtect(code, size, PAGE_EXECUTE_READ, &old) && FlushInstructionCache(GetCurrentProcess(), code, size);
   check(valid, "synthetic original callback executable");
  }
  if (valid) {
   std::array<std::byte, 0x500> actor{};
   npc_semi_auto::field<u32>(actor.data(), 0x3e0) = 123;
   for (const float time : {0.f, .125f, 13.25f, 1000.f, 1000000.f})
    for (const float recoil : {.03125f, .17f, .625f, .9f, 5.f}) {
     fixture_time = time; source_recoil = recoil;
     reinterpret_cast<npc_semi_auto::entry_t>(code)(nullptr, actor.data(), nullptr, nullptr);
     const auto expected = npc_semi_auto_rules::entered(1, 1, time, std::bit_cast<u32>(recoil));
     check(npc_semi_auto::field<u32>(actor.data(), 0x498) == std::bit_cast<u32>(expected.next), "candidate deadline matches executed original callback bit-for-bit");
    }
  }
  VirtualFree(code, 0, MEM_RELEASE);
 }
 void retail_guards(const char* path)
 {
  std::ifstream file(path, std::ios::binary);
  std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)), {});
  check(raw.size() > 0x1000, "retail guard input readable");
  if (raw.size() <= 0x1000) return;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
  const auto* sections = IMAGE_FIRST_SECTION(nt);
  for (const auto& g : npc_semi_auto_rules::guards) {
   bool found = false;
   for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const auto rva = g.va - nt->OptionalHeader.ImageBase;
    const auto& s = sections[i];
    if (rva < s.VirtualAddress || rva - s.VirtualAddress + limits_rules::hex_size(g.hex) > s.SizeOfRawData) continue;
    const auto off = s.PointerToRawData + rva - s.VirtualAddress;
    found = off + limits_rules::hex_size(g.hex) <= raw.size() && limits_rules::matches(raw.data() + off, g.hex);
    break;
   }
   check(found, g.what);
  }
 }
}
int main(int argc, char** argv)
{
 using namespace npc_semi_auto_rules;
 npc_semi_auto::install(reinterpret_cast<HMODULE>(1));
 check(!npc_semi_auto::requested() && !npc_semi_auto::installed() && npc_semi_auto::verified(),
       "absent option returns before inspecting or writing even an invalid image");
 check(npc_semi_auto::values({1,1,0,0}) == std::array<u32,4>{1,1,0,0}, "OFF preserves released rule values");
 fixture_option = "xml2";
 npc_semi_auto::install(reinterpret_cast<HMODULE>(1));
 check(!npc_semi_auto::requested() && npc_semi_auto::verified(), "explicit OFF also avoids all image access");
 fixture_option = "invalid";
 npc_semi_auto::install(reinterpret_cast<HMODULE>(1));
 check(npc_semi_auto::requested() && !npc_semi_auto::verified(), "bad opt-in fails closed before image access");
 fixture_option.reset();
 for (const char* bad : {"", "a|00000000", "a|80000000", "a|7fc00000", "a|7f800000", "a|bf000000",
                         "a|42800000", "a|3f00000", "bad name|3f000000", "a|3f000000junk"})
  check(!parse(bad), "invalid contract rejected");
 const auto w = parse("Test_Guard|3f200000|43c80000|3f800000|00000000");
 check(w && w->actor == "test_guard" && std::bit_cast<float>(w->recoil_bits) == .625f, "case and exact source bits");
 for (const char* bad : {"a|3f000000|7f800000|3f800000|00000000", "a|3f000000|42960000|3f800000|00000000",
                         "a|3f000000|43c84000|3f800000|00000000", "a|3f000000|43c80000|00000000|00000000",
                         "a|3f000000|43c80000|7fc00000|00000000", "a|3f000000|43c80000|3f800000|00000002"})
  check(!parse(bad), "invalid range or burst rejected");
 std::vector<weapon> a{{"b", 0x3f200000}, {"a", 0x3f000000}}, b(a.rbegin(), a.rend());
 check(canonicalize(a) && canonicalize(b) && fingerprint(a) == fingerprint(b), "canonical order");
 b[0].recoil_bits ^= 1;
 check(fingerprint(a) != fingerprint(b), "recoil bit participates in agreement");
 b=a;b[0].range_bits^=1;check(fingerprint(a)!=fingerprint(b), "range participates in agreement");
 b=a;b[0].burst_bits^=1;check(fingerprint(a)!=fingerprint(b), "burst participates in agreement");
 b=a;b[0].leader^=1;check(fingerprint(a)!=fingerprint(b), "leader flag participates in agreement");
 b = {a[0], a[0]}; check(!canonicalize(b), "duplicate actor rejected");
 const auto s = entered(100, 2, 10.f, 0x3f200000);
 check(!ready(s, 100, 2, 10.624f) && ready(s, 100, 2, 10.625f), "absolute boundary");
 check(ready(s, 100, 3, 10.f) && ready(s, 100, 2, 1.f), "incarnation and clock reset");
 check(!ready(s, 100, 2, std::numeric_limits<float>::quiet_NaN()), "invalid clock fails closed");
 for (unsigned bits : {8u, 10u}) {
  std::array<bool, timer_slots> occupied{};
  for (unsigned index = 0; index != (1u << bits); ++index) {
   const u32 token = ((1 + (index % 15)) << bits) | index;
   const auto slot = timer_slot(token);
   check(!occupied[slot], "live World indices cannot collide in timer table"); occupied[slot] = true;
  }
 }
 for (int fps : {30, 49, 60}) {
  state timer{}; int shots = 0; float previous = -1;
  for (int tick = 0; tick <= fps * 20; ++tick) {
   const float time = float(tick) / fps;
   if (ready(timer, 100, 2, time)) {
    if (previous >= 0) check(time - previous + 0.00001f >= .625f, "cadence uses seconds at every frame rate");
    timer = entered(100, 2, time, 0x3f200000); previous = time; ++shots;
   }
  }
  check(shots >= 30 && shots <= 33, "frame rate does not double recoil admission");
 }
 // Exercise the real callback and resolver-based candidate filter.
 std::array<std::byte, 0x800> actor{}, ai{};
 std::array<std::byte, 0x500> stats{};
 std::memcpy(stats.data() + 0x150, "test_guard", 11);
 npc_semi_auto::field<void*>(actor.data(), 0x35c) = stats.data();
 npc_semi_auto::field<u32>(actor.data(), 0x1c) = 123;
 npc_semi_auto::field<int>(actor.data(), 0x3d4) = -1;
 npc_semi_auto::field<void*>(ai.data(), 0x10) = actor.data();
 npc_semi_auto::field<void*>(move.data(), 0xc8) = &npc_semi_auto::handler;
 npc_semi_auto::weapons = {*w}; npc_semi_auto::applied = true; npc_semi_auto::wanted = true;
 npc_semi_auto::clock_function = &fixture_clock;
 npc_semi_auto::entry_function = reinterpret_cast<npc_semi_auto::entry_t>(&fixture_entry);
 npc_semi_auto::action_function = reinterpret_cast<npc_semi_auto::action_t>(&fixture_action);
 prepare_cases(actor.data(),ai.data());
 fixture_time = 20;
 npc_semi_auto::enter(nullptr, nullptr, actor.data(), move.data(), nullptr);
 check(entries == 1 && npc_semi_auto::find(actor.data(),123)->next == 20.625f, "entry arms actual callback deadline");
 fixture_time = 20.625f;
 check(ready(*npc_semi_auto::find(actor.data(),123),reinterpret_cast<std::uintptr_t>(actor.data()),123,fixture_time), "shared deadline expires at its boundary");
 input_gate_cases(actor.data());
 decision_cases(actor.data(), ai.data());
 if (argc >= 3) original_input_comparison(argv[2], actor.data());
 // Check GCAP rejection: different mode, metadata, revision or failed install.
 const auto vals = npc_semi_auto::values({1, 1, 0, 0});
 gameplay_capabilities::record local, equal, off, altered, old, failed;
 gameplay_capabilities::record power_only, candidate9, released4;
 local.add(16, npc_power::revision, vals, true); equal.add(16, npc_power::revision, vals, true);
 off.add(16, 3, {0, 0, 0, 0}, true); released4.add(16, 4, {1, 1, 0, 0}, true);
 power_only.add(16, npc_power::revision, {1, 1, 0, 0}, true);
 auto changed = vals; changed[3] ^= 1; altered.add(16, npc_power::revision, changed, true);
 old.add(16, 8, vals, true); candidate9.add(16, 9, vals, true); failed.add(16, npc_power::revision, vals, false);
 for (auto* r : {&local, &equal, &off, &altered, &old, &failed, &power_only, &candidate9, &released4}) r->finalize();
 check(npc_power::revision == 10, "officer weapon brain ships under rule 16 revision 10");
 check(local.compare(equal.bytes()) == gameplay_capabilities::result::match, "equal peers agree");
 for (const auto* peer : {&off, &altered, &old, &power_only, &candidate9, &released4})
  check(local.compare(peer->bytes()) != gameplay_capabilities::result::match, "incompatible peer refused");
 check(!failed.ready(), "requested failure poisons agreement");
 weapon_bridge_cases();
 if (argc >= 2) retail_guards(argv[1]); else check(false, "retail executable path required");
 if (argc >= 3) original_entry_comparison(argv[2]);
 else std::printf("Original callback comparison not requested (supply owned XBE as second path)\n");
 std::printf("NPC semi-auto: %d checks, %d failures\n", checks, failures);
 return failures ? 1 : 0;
}
