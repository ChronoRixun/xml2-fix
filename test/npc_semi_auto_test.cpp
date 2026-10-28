// Executes the production admission bridge against invented objects, with native
// engine calls replaced by fixture functions. Does not launch the game.
#include "../src/npc_semi_auto.cpp"
#include "gameplay_capabilities.hpp"
#include "npc_power_withdraw.hpp"
#include <map>
#include <cstdio>
#include <fstream>
#include <limits>
#include <float.h>
#include <xmmintrin.h>

std::optional<std::string> fixture_option;
namespace ini {
 std::optional<std::string> text(const wchar_t*, const wchar_t*) { return fixture_option; }
 int number(const wchar_t*, const wchar_t*, int n) { return n; }
}
int power_withdrawn = 0; bool power_installed = true;
namespace npc_power {
 bool installed() { return power_installed; }
 bool withdraw() { if (power_installed) ++power_withdrawn; power_installed = false; return true; }
}
namespace logger { void write(const char*, ...) {} }
namespace game_version { void refuse_startup() {} }
namespace mold_query {
 bool pools_ready() { return false; }
 bool is_a(void*, std::uint32_t) { return false; }
}

namespace {
 using u32 = std::uint32_t;
 std::uint16_t x87_control_word() { std::uint16_t cw = 0; __asm { fnstcw cw } return cw; }
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
        !power_pressed(0,brain.data(),input.data()),
        "unlisted control retains every native path");
  check(!std::memcmp(ai,ai_before.data(),ai_before.size()) && !std::memcmp(actor,actor_before.data(),actor_before.size()) && input==input_before,
        "unlisted control actor, AI and input bytes are unchanged");
  weapons=saved;
 }
 #include "npc_weapon_bridges_test.hpp"
 #include "npc_trace_gate_test.hpp"
 #include "npc_bullet_trace_test.hpp"
 #include "npc_bullet_ray_test.hpp"
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
  check(find(actor, token)->next == 102.25f, "transition uses the original 0.25 second deadline");
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
 // Pistol (default-handler primary): the converted power_attack keeps XML2's default handler,
 // whose entry does not arm the recoil deadline, so the gate requests it on every update in
 // which the actor is neither input locked nor outside the firing animations; only its idle
 // transition delays it. Semi-auto rows never accept that move and the pistol row never
 // accepts the semi-auto handler.
 std::array<std::byte, 0x148> pistol_move{};
 std::array<u32, 1> default_object{npc_semi_auto_rules::default_vtable};
 void* __fastcall fixture_named_pistol(void*, void*, const u32*)
 { return requested_name == "idle" ? idle_move.data() : pistol_move.data(); }
 void pistol_cases(void* actor, void* ai)
 {
  using namespace npc_semi_auto;
  const auto saved = weapons;
  const auto row = parse("test_guard|3e4ccccd|44098000|3fc00000|00000000|00000000|00000040|00000000|00000000");
  check(row.has_value(), "pistol row parses");
  if (!row) return;
  weapons = {*row};
  name_function = reinterpret_cast<resolve_name_t>(&fixture_named_pistol);
  field<void*>(pistol_move.data(), 0xc8) = default_object.data();
  field<int>(pistol_move.data(), 0xbc) = 115;
  field<void*>(actor, 0x378) = pistol_move.data();
  field<unsigned char>(actor, 0x764) = 0; field<unsigned char>(actor, 0x3d9) = 0;
  const u32 token = field<u32>(actor, 0x1c);
  states[timer_slot(token)] = {};
  std::array<std::byte, 0x28> input{}; field<u32>(input.data(), 0) = 0x90;
  requests = 0; const int prior_entries = entries;
  for (int tick = 0; tick != 30; ++tick) {
   fixture_time = 300.f + tick / 60.f;
   check(weapon_input(actor, input.data()), "pistol gate consumes held fire");
  }
  check(requests == 30 && requested_flags == 9 && entries == prior_entries,
        "default-handler primary is requested on every update without a recoil deadline");
  field<unsigned char>(actor, 0x764) = 1; requests = 0;
  check(weapon_input(actor, input.data()) && requests == 0, "input lock (the authored lockangles move) holds the pistol");
  field<unsigned char>(actor, 0x764) = 0;
  field<void*>(actor, 0x378) = move.data(); field<int>(move.data(), 0xbc) = 9; requests = 0; fixture_time = 301;
  check(weapon_input(actor, input.data()) && requests == 1 && requested_name == "idle", "pistol outside firing animations requests idle");
  field<int>(move.data(), 0xbc) = 115; field<void*>(actor, 0x378) = pistol_move.data(); requests = 0;
  fixture_time = 301.2f;
  check(weapon_input(actor, input.data()) && requests == 0, "pistol waits for the 0.25 s transition deadline");
  fixture_time = 301.25f;
  check(weapon_input(actor, input.data()) && requests == 1, "pistol fires at the transition deadline");
  // The semi-auto handler is not the pistol's primary, and a default-handler move is not a semi-auto row's.
  field<void*>(pistol_move.data(), 0xc8) = &handler; requests = 0; fixture_time = 302;
  check(weapon_input(actor, input.data()) && requests == 0, "pistol row refuses a semi-auto handler primary");
  field<void*>(pistol_move.data(), 0xc8) = default_object.data();
  default_object[0] = 0x692f50; requests = 0;
  check(weapon_input(actor, input.data()) && requests == 0, "pistol row needs XML2's default handler");
  default_object[0] = npc_semi_auto_rules::default_vtable;
  weapons = saved;
  requests = 0;
  check(weapon_input(actor, input.data()) && requests == 0, "semi-auto row refuses a default-handler primary");
  // Refresh strips the converter's AI capability from the pistol primary only.
  weapons = {*row};
  std::array<void*, 32> chains{}; std::array<std::byte, 0x148> other{};
  field<void*>(other.data(), 0xc8) = default_object.data(); field<float>(other.data(), 0x140) = 7.f;
  chains[5] = pistol_move.data(); chains[6] = other.data();
  std::array<std::byte, 0x60> brain{}; field<u32>(brain.data(), 0) = 0x6950f4;
  field<void*>(ai, 0x4c4) = brain.data(); field<void*>(ai, 0x10) = actor;
  field<u32>(ai, 0x430) = 0x40; field<u32>(ai, 0x434) = 0x40;
  check(preserve_weapon_range(ai, chains.data()) && field<u32>(ai, 0x430) == 0 && field<u32>(ai, 0x434) == 0x40,
        "only the pistol's converted primary loses its synthesized capability");
  field<void*>(ai, 0x4c4) = nullptr;
  weapons = saved;
  name_function = reinterpret_cast<resolve_name_t>(&fixture_named);
  field<void*>(actor, 0x378) = idle_move.data();
 }
 // Empty near-target query branches (0xef9bc bullet, 0xefc4a other) and the bullet
 // line-of-sight drop, with native helpers replaced by recording fixtures.
 void* decision_self = nullptr; void* decision_target = nullptr; void* decision_entity = nullptr;
 std::string decision_calls;
 float fixture_dt = 1.f / 60, fixture_random = .5f;
 bool fixture_busy = false, fixture_seen = true, fixture_unknown = false;
 int fixture_state = -1; unsigned delays = 0, rotations = 0, turns = 0, notices = 0, alerts = 0, aims = 0;
 npc_semi_auto::point last_rotation{}, last_turn{};
 bool __fastcall fixture_target(void*, void*, u32, void** self, void** target, void** entity)
 { *self = decision_self; *target = decision_target; *entity = decision_entity; return true; }
 npc_semi_auto::point* __cdecl fixture_angles(npc_semi_auto::point* out, const npc_semi_auto::point* d)
 { decision_calls += "a"; *out = {d->x, d->y, 99}; return out; }
 void __fastcall fixture_aim(void* brain, void*, const npc_semi_auto::point* p)
 {
  using namespace npc_semi_auto;
  ++aims; decision_calls += "m";
  field<point>(field<void*>(field<void*>(brain, 4), 0x10), 0x3ac) = *p; // PC 0x517cf0
 }
 const npc_semi_auto::point* __fastcall fixture_position(void* e, void*, npc_semi_auto::point* out)
 { *out = npc_semi_auto::field<npc_semi_auto::point>(e, 0x20); return out; }
 float __cdecl fixture_delta() { return fixture_dt; }
 float __cdecl fixture_rand(bool) { return fixture_random; }
 bool __cdecl fixture_move_busy(void*) { return fixture_busy; }
 void __fastcall fixture_turn(void*, void*, const npc_semi_auto::point*, const npc_semi_auto::point* a)
 { ++turns; decision_calls += "t"; last_turn = *a; }
 void __fastcall fixture_rotate(void*, void*, const npc_semi_auto::point* a) { ++rotations; last_rotation = *a; }
 void __fastcall fixture_delay(void*, void*, float d) { ++delays; check(d == .3f, "pause starts with the original 0.3 s defer"); }
 void __fastcall fixture_alert(void*, void*, float a, float b, u32) { ++alerts; check(a == 1 && b == 300, "alert radius"); }
 void __fastcall fixture_notice(void*, void*, float, u32 kind, u32, npc_semi_auto::attack_notice, u32, u32)
 { ++notices; check(kind == 3, "victim notice kind"); }
 void* __fastcall fixture_ai_of(void*, void*, u32 token) { return token == 0x777 ? decision_entity : nullptr; }
 bool __fastcall fixture_see(void*, void*, bool* unknown) { decision_calls += "s"; *unknown = fixture_unknown; return fixture_seen; }
 void __fastcall fixture_set_state(void*, void*, int state) { fixture_state = state; }
 void decision_cases(void* actor, void* ai)
 {
  using namespace npc_semi_auto;
  const auto saved = weapons;
  target_function = reinterpret_cast<resolve_target_t>(&fixture_target);
  angles_function = &fixture_angles;
  aim_function = reinterpret_cast<aim_t>(&fixture_aim);
  position_function = &fixture_position;
  delta_function = &fixture_delta;
  random_function = &fixture_rand;
  busy_function = &fixture_move_busy;
  turn_function = reinterpret_cast<turn_t>(&fixture_turn);
  rotate_function = reinterpret_cast<rotate_t>(&fixture_rotate);
  delay_function = reinterpret_cast<delay_t>(&fixture_delay);
  alert_function = reinterpret_cast<alert_t>(&fixture_alert);
  notice_function = reinterpret_cast<notice_t>(&fixture_notice);
  ai_function = reinterpret_cast<ai_get_t>(&fixture_ai_of);
  see_function = reinterpret_cast<see_t>(&fixture_see);
  state_function = reinterpret_cast<state_t>(&fixture_set_state);
  std::array<std::byte, 0x60> brain{}; std::array<std::byte, 0x800> hero{}, hero_ai{}, current{};
  field<void*>(brain.data(), 4) = ai;
  const auto reset_timers = [&brain] {
   field<float>(brain.data(), 0x1c) = 0; field<float>(brain.data(), 0x20) = .6f; field<float>(brain.data(), 0x24) = 100;
   field<int>(brain.data(), 0x28) = 1;
  };
  field<point>(actor, 0x20) = {10, 10, 7}; field<point>(actor, 0x2c) = {0, 0, 0};
  field<point>(hero.data(), 0x20) = {40, -30, 7};
  field<u32>(hero.data(), 0x7dc) = 0x777; field<u32>(hero.data(), 0x3d8) = 1;
  field<void*>(actor, 0x378) = current.data();
  decision_self = actor; decision_target = hero.data(); decision_entity = hero_ai.data();
  const auto row = [](const char* text) { const auto r = parse(text); check(r.has_value(), "decision row parses"); return *r; };
  const weapon mp5 = row("test_guard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|00000000|00000000");
  const weapon laser = row("test_guard|3e2e147b|44098000|3fc00000|00000000|00000000|00000000|00000000|00000000");
  const weapon officer = row("test_guard|3f666666|44098000|3f800000|00000001|00000000|00000002|00000000|00000000");
  const weapon lightning = row("test_guard|00000000|44098000|40200000|00000001|3f19999a|00000013|00000000|00000000");
  const weapon nullifier = row("test_guard|3f333333|44098000|3f800000|00000001|3f000000|00000003|00000000|00000000");

  // Bullet branch at 60 fps: a one-update burst from the constructor timers, then 1.0 s
  // pauses and rand + 2.0 = 2.5 s bursts; fire held only while the burst timer is >= 0.
  for (const auto& w : {mp5, laser}) {
   weapons = {w}; reset_timers(); field<point>(actor, 0x3ac) = {10, 10, 7};
   field<unsigned char>(actor, 0x764) = 4;
   std::vector<int> fired;
   for (int tick = 0; tick <= 60 * 20; ++tick) {
    fixture_time = 100.f + tick / 60.f;
    std::array<std::byte, 0x28> input{}; field<u32>(input.data(), 0) = 0x2000; field<u32>(input.data(), 8) = 5;
    field<point>(input.data(), 0x14) = {1, 2, 3};
    check(weapon_decision(brain.data(), input.data()), "listed bullet actor consumes the empty-query continuation");
    const bool held = field<u32>(input.data(), 0) == 0x90 && field<u32>(input.data(), 4) == 0x90;
    if (held) {
     fired.push_back(tick);
     check(field<u32>(input.data(), 8) == 0 && field<point>(input.data(), 0x14).x == 0 && field<point>(input.data(), 0x14).z == 0,
           "firing clears movement and the strafe flag");
    } else check(field<u32>(input.data(), 0) == 0x2000 && field<u32>(input.data(), 8) == 5, "pause leaves the input alone");
   }
   check((field<unsigned char>(actor, 0x764) & 4) == 0, "empty query clears the actor's bit 2 like 0xef98d");
   // Burst lengths and pauses in frames.
   std::vector<int> runs, gaps; int start = fired.empty() ? 0 : fired[0], prev = start;
   for (std::size_t i = 1; i <= fired.size(); ++i) {
    if (i == fired.size() || fired[i] != prev + 1) { runs.push_back(prev - start + 1); if (i != fired.size()) { gaps.push_back(fired[i] - prev - 1); start = fired[i]; } }
    if (i != fired.size()) prev = fired[i];
   }
   check(runs.size() >= 5 && runs[0] == 1, "first update fires once and starts the pause");
   bool cadence = true;
   for (std::size_t i = 1; i + 1 < runs.size(); ++i) cadence &= runs[i] >= 150 && runs[i] <= 152;
   for (const int g : gaps) cadence &= g >= 59 && g <= 61;
   check(cadence, "bursts last rand + 2 s and pauses 1 s at 60 fps");
   std::printf("  bullet branch %s: %zu bursts, burst %d frames, pause %d frames\n",
               w.flags & flag_rapidfire ? "rapid" : "semi", runs.size(), runs.size() > 1 ? runs[1] : 0, gaps.empty() ? 0 : gaps[0]);
  }
  // Aim smoothing: 1 - 0.15^dt while firing, 1 - 0.25^dt otherwise; rapid fire pulls the
  // pause frame's aim 75 % back toward the actor.
  for (const auto& w : {mp5, laser}) {
   weapons = {w};
   reset_timers(); field<float>(brain.data(), 0x1c) = 1.f; field<float>(brain.data(), 0x20) = 0;
   field<point>(actor, 0x3ac) = {10, 10, 7}; fixture_time = 200;
   std::array<std::byte, 0x28> input{};
   weapon_decision(brain.data(), input.data());
   const float k = float(1.0 - std::pow(.15, double(fixture_dt)));
   const point fire_aim{float(30.f * k) + 10.f, float(-40.f * k) + 10.f, float(0.f * k) + 7.f};
   check(field<point>(actor, 0x3ac).x == fire_aim.x && field<point>(actor, 0x3ac).y == fire_aim.y, "firing aim smoothing 1 - 0.15^dt");
   field<float>(brain.data(), 0x1c) = .001f; field<point>(actor, 0x3ac) = {10, 10, 7}; fixture_time = 201;
   weapon_decision(brain.data(), input.data()); // fires, burst ends, pause frame
   point expected = fire_aim;
   if (w.flags & flag_rapidfire) expected = {(fire_aim.x - 10.f) * .25f + 10.f, (fire_aim.y - 10.f) * .25f + 10.f, (fire_aim.z - 7.f) * .25f + 7.f};
   check(field<point>(actor, 0x3ac).x == expected.x && field<point>(actor, 0x3ac).y == expected.y,
         "rapid fire pulls the pause frame's aim toward the actor, semi-auto does not");
   field<point>(actor, 0x3ac) = {10, 10, 7}; fixture_time = 201.5f; // paused: no fire
   weapon_decision(brain.data(), input.data());
   const float kp = float(1.0 - std::pow(.25, double(fixture_dt)));
   check(field<point>(actor, 0x3ac).x == float(30.f * kp) + 10.f, "paused aim smoothing 1 - 0.25^dt");
  }
  // A flagged current move ends the update before aiming; the turn needs more than 10 units.
  weapons = {mp5}; reset_timers(); field<float>(brain.data(), 0x20) = 0; fixture_time = 300;
  {
   std::array<std::byte, 0x28> input{};
   fixture_busy = true; aims = turns = 0;
   weapon_decision(brain.data(), input.data());
   check(aims == 0 && turns == 0 && field<u32>(input.data(), 0) == 0x90, "flagged move: fire held, no aim or turn");
   fixture_busy = false; field<point>(actor, 0x3ac) = {40, -30, 7}; decision_calls.clear();
   weapon_decision(brain.data(), input.data());
   check(aims == 1 && turns == 1 && decision_calls == "mat" && last_turn.z == 99 && last_turn.x > 0,
         "aim, then angles of the normalised ground direction, then turn");
   field<point>(hero.data(), 0x20) = {10, 10.5f, 500}; field<point>(actor, 0x3ac) = {10, 10, 7}; turns = 0;
   weapon_decision(brain.data(), input.data());
   check(turns == 0, "no turn when the aim point is within 10 units on the ground plane");
   field<point>(hero.data(), 0x20) = {40, -30, 7};
  }

  // Other types (0xefc4a): burst/pause with bursttime, strafe flag, notices, turning.
  for (const auto& w : {officer, nullifier}) {
   weapons = {w}; reset_timers(); field<point>(actor, 0x2c) = {0, 0, 0};
   field<point>(hero.data(), 0x20) = {1000, 10, 7}; // straight ahead (+x)
   states[timer_slot(field<u32>(actor, 0x1c))] = {};
   rotations = delays = notices = 0;
   int held = 0, last = -1; std::vector<int> gaps;
   for (int tick = 0; tick <= 60 * 20; ++tick) {
    fixture_time = 400.f + tick / 60.f;
    std::array<std::byte, 0x28> input{};
    check(weapon_decision(brain.data(), input.data()), "listed actor consumes the continuation");
    if (field<u32>(input.data(), 0) & 0x90) { if (last >= 0 && tick - last > 1) gaps.push_back(tick - last - 1); last = tick; ++held; }
   }
   check(rotations == 0, "on-axis discrete weapon does not sweep");
   check(delays >= 3 && delays >= gaps.size() && delays <= gaps.size() + 1, "every pause starts with the 0.3 s defer");
   check(notices == unsigned(held), "every fire update notices the victim");
   // bursttime 1.0 + rand 0.5 * 0.5 = 1.25 s = 75 updates (fires while >= 0 before the decrement);
   // pause (1.2 + 1.5 * 0.5) = 1.95 s against a player target (bit set), 0.3 s of it deferred.
   bool cadence = !gaps.empty();
   for (std::size_t i = 1; i < gaps.size(); ++i) cadence &= gaps[i] >= 98 && gaps[i] <= 101;
   check(cadence, "pause length 1.95 s including the 0.3 s defer");
   std::printf("  other branch %s: %d fire updates, %zu pauses of %d frames\n", w.warmup_bits ? "warm-up" : "discrete", held, gaps.size(), gaps.size() > 1 ? gaps[1] : 0);
  }
  {
   // Turning: behind ends the burst; off-axis turns 0.02 toward; continuous sweeps 0.015.
   weapons = {officer}; reset_timers(); std::array<std::byte, 0x28> input{}; fixture_time = 500;
   field<point>(hero.data(), 0x20) = {-1000, 10, 7}; field<float>(brain.data(), 0x1c) = 5; rotations = 0;
   weapon_decision(brain.data(), input.data());
   check(rotations == 0 && field<float>(brain.data(), 0x1c) < 0, "target behind ends the burst");
   reset_timers(); field<float>(brain.data(), 0x1c) = 5;
   field<point>(hero.data(), 0x20) = {1000, 400, 7}; rotations = 0;
   weapon_decision(brain.data(), input.data());
   check(rotations == 1 && std::fabs(std::fabs(last_rotation.z) - .02f) < 1e-6f && std::abs(field<int>(brain.data(), 0x28)) == 1,
         "off-axis target turns 0.02 rad and records the direction");
   weapons = {lightning}; reset_timers(); field<float>(brain.data(), 0x1c) = 5; field<int>(brain.data(), 0x28) = -1;
   field<point>(hero.data(), 0x20) = {1000, 10, 7}; rotations = 0;
   states[timer_slot(field<u32>(actor, 0x1c))] = {};
   weapon_decision(brain.data(), input.data());
   check(rotations == 1 && last_rotation.z == -.015f, "continuous beam sweeps 0.015 rad in the last turn direction");
   // Warm-up running: no turning, no pause before the deadline is 0.4 s old, and within
   // 0.4 s of the deadline the strafe and movement stop.
   weapons = {nullifier}; reset_timers(); field<float>(brain.data(), 0x1c) = -1; rotations = 0; delays = 0;
   field<point>(hero.data(), 0x20) = {1000, 400, 7};
   auto& s = states[timer_slot(field<u32>(actor, 0x1c))];
   s = {reinterpret_cast<std::uintptr_t>(actor), field<u32>(actor, 0x1c), 600.f, 600.f, 600.3f};
   fixture_time = 600; std::array<std::byte, 0x28> warm_input{}; field<u32>(warm_input.data(), 8) = 5;
   field<u32>(ai, 0x270) = 7;
   weapon_decision(brain.data(), warm_input.data());
   check(rotations == 0 && delays == 0, "a running warm-up suppresses turning and the pause");
   check(field<u32>(warm_input.data(), 0) == 0x90 && field<u32>(warm_input.data(), 8) == 0 && field<u32>(ai, 0x270) == 0xffffffffu,
         "warm-up ending within 0.4 s stops strafe and movement and holds fire");
   s.warm = 0; field<point>(hero.data(), 0x20) = {40, -30, 7};
  }
  // Line-of-sight drop (bullet only): blocked within range resets the burst timers and
  // drops the AI to state 0; visible, unknown or non-bullet weapons change nothing.
  for (const auto& [w, seen, unknown, drop] : {std::tuple<weapon, bool, bool, bool>{mp5, false, false, true},
       {mp5, true, false, false}, {mp5, false, true, false}, {officer, false, false, false}, {nullifier, false, false, false}}) {
   weapons = {w}; reset_timers(); fixture_seen = seen; fixture_unknown = unknown; fixture_state = -1; decision_calls.clear();
   field<float>(brain.data(), 0x1c) = 2; field<float>(brain.data(), 0x20) = 3;
   line_of_sight(brain.data());
   check(drop == (fixture_state == 0 && field<float>(brain.data(), 0x20) == 0 && field<float>(brain.data(), 0x1c) == -1.f),
         "bullet line-of-sight drop resets timers and AI state only when blocked");
   check(!drop || decision_calls == "s", "visibility tested once");
   if (!bullet(w)) check(decision_calls.empty(), "non-bullet weapons never test visibility");
  }
  // Review round 2: the brain's results must not depend on the caller's x87 precision or rounding mode, nor on its
  // MXCSR (a Direct3D 8 device sets 24-bit x87 precision; overlays may change either). Run the bullet aim smoothing
  // (firing and paused), the other-type turn and the warm-up gate under 24/53/64-bit x87 precision x four rounding
  // modes x two MXCSR rounding modes and require identical bits; the caller's environment is restored afterwards.
  {
   const auto snapshot = [&](unsigned pc, unsigned rc, unsigned csr) {
    unsigned old_cw = 0; _controlfp_s(&old_cw, 0, 0);
    const unsigned old_csr = _mm_getcsr();
    unsigned ignored = 0;
    _controlfp_s(&ignored, pc | rc, _MCW_PC | _MCW_RC);
    _mm_setcsr(csr);
    // The raw x87 control word as set (_controlfp reports x87 and SSE2 merged, ambiguous when they differ).
    std::uint16_t x87_before = 0, x87_after = 0;
    x87_before = x87_control_word();
    std::array<u32, 12> bits{};
    weapons = {mp5}; reset_timers(); field<float>(brain.data(), 0x1c) = 1.f; field<float>(brain.data(), 0x20) = 0;
    field<point>(actor, 0x3ac) = {10, 10, 7}; fixture_time = 200; field<point>(hero.data(), 0x20) = {40, -30, 7};
    std::array<std::byte, 0x28> in1{};
    weapon_decision(brain.data(), in1.data());
    bits[0] = std::bit_cast<u32>(field<point>(actor, 0x3ac).x); bits[1] = std::bit_cast<u32>(field<point>(actor, 0x3ac).y);
    field<float>(brain.data(), 0x20) = 1e9f; field<point>(actor, 0x3ac) = {10, 10, 7};   // paused
    weapon_decision(brain.data(), in1.data());
    bits[2] = std::bit_cast<u32>(field<point>(actor, 0x3ac).x); bits[3] = std::bit_cast<u32>(field<point>(actor, 0x3ac).y);
    bits[4] = std::bit_cast<u32>(last_turn.x); bits[5] = std::bit_cast<u32>(last_turn.z);
    weapons = {officer}; reset_timers(); field<point>(actor, 0x2c) = {0, .3f, 1.1f};
    field<point>(hero.data(), 0x20) = {13, 1000, 7}; states[timer_slot(field<u32>(actor, 0x1c))] = {};
    std::array<std::byte, 0x28> in2{};
    weapon_decision(brain.data(), in2.data());
    bits[6] = std::bit_cast<u32>(last_rotation.y); bits[7] = std::bit_cast<u32>(last_rotation.z);
    bits[8] = std::bit_cast<u32>(field<float>(brain.data(), 0x1c)); bits[9] = std::bit_cast<u32>(field<float>(brain.data(), 0x20));
    // The semi-auto entry callback's recoil deadline (entry time + recoil), through the production callback.
    weapons = {nullifier}; fixture_time = 7.1f;
    npc_semi_auto::enter(nullptr, nullptr, actor, move.data(), nullptr);
    bits[10] = std::bit_cast<u32>(states[timer_slot(field<u32>(actor, 0x1c))].next);
    bits[11] = std::bit_cast<u32>(states[timer_slot(field<u32>(actor, 0x1c))].entered);
    x87_after = x87_control_word();
    const bool restored = x87_after == x87_before && _mm_getcsr() == csr;
    _controlfp_s(&ignored, old_cw, _MCW_PC | _MCW_RC);
    _mm_setcsr(old_csr);
    check(restored, "the brain restores the caller's x87 control word and MXCSR");
    return bits;
   };
   const auto reference = snapshot(_PC_53, _RC_NEAR, 0x1f80);
   bool same = true;
   for (const unsigned pc : {unsigned(_PC_24), unsigned(_PC_53), unsigned(_PC_64)})
    for (const unsigned rc : {unsigned(_RC_NEAR), unsigned(_RC_DOWN), unsigned(_RC_UP), unsigned(_RC_CHOP)})
     for (const unsigned csr : {0x1f80u, 0x7f80u, 0x9fc0u}) // nearest; toward zero; nearest with FTZ+DAZ
      same &= snapshot(pc, rc, csr) == reference;
   check(same, "aim smoothing, turn, burst timers and entry deadline: identical bits under every x87 precision / rounding and MXCSR");
   // The pinned det_math cases inside an fp_env under a 24-bit, round-down caller.
   unsigned ignored = 0, old_cw = 0; _controlfp_s(&old_cw, 0, 0);
   _controlfp_s(&ignored, _PC_24 | _RC_DOWN, _MCW_PC | _MCW_RC);
   std::uint64_t p15 = 0; u32 k15 = 0;
   { det_math::fp_env fp; const double keep = det_math::pow_0_15(double(1.f / 60)); p15 = std::bit_cast<std::uint64_t>(keep); k15 = std::bit_cast<u32>(float(1.0 - keep)); }
   _controlfp_s(&ignored, old_cw, _MCW_PC | _MCW_RC);
   check(p15 == 0x3fef0108304f8cf4ull && k15 == 0x3cfef7d0u, "pinned 0.15^(1/60) and its aim coefficient under a 24-bit, round-down caller (fp_env)");
   weapons = {mp5}; reset_timers(); field<point>(hero.data(), 0x20) = {40, -30, 7};
  }
  fixture_seen = true; fixture_unknown = false;
  weapons.clear(); fixture_state = -1; decision_calls.clear();
  line_of_sight(brain.data());
  check(fixture_state == -1 && decision_calls.empty(), "unlisted actor keeps the native brain update");
  weapons = saved;
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
  decision_self = hero.data();
  std::array<std::byte, 0x28> input{};
  check(weapon_decision(brain.data(), input.data()) && field<u32>(input.data(), 0) == 0, "foreign self resolution holds nothing");
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
 // NpcSemiAuto installs only after rule 16, so none of its guards may read bytes rule 16 has already rewritten:
 // such a guard fails in the game (the weapon brain stays off and host/join close) although it matches the retail exe.
 void guards_clear_of_rule16()
 {
  using namespace npc_power_rules;
  std::vector<std::pair<u32, std::size_t>> written;
  for (const auto* s : sites) if (s->patched) written.emplace_back(s->va, s->patched);
  for (const auto [va, size] : std::initializer_list<std::pair<u32, std::size_t>>{
       {picker_call_va, 5}, {combolevel_call_va, 3}, {combo_exit_va, 6}, {combo_exit2_va, 6},
       {combo_buttons_va, 24}, {combo_lengths_va, 24}, {combo_row2_site.va, combo_row_size},
       {combo_row3_site.va, combo_row_size}, {combo_row11_site.va, combo_row_size},
       {lock_exit_va, 2}, {power_lock_va, 6}})
   written.emplace_back(va, size);
  check(written.size() >= 22, "rule 16 written ranges collected");
  for (const auto& g : npc_semi_auto_rules::guards) {
   const auto size = limits_rules::hex_size(g.hex);
   bool clear = true;
   for (const auto [va, n] : written) if (g.va < va + n && va < g.va + size) clear = false;
   check(clear, g.what);
  }
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
 std::setvbuf(stdout, nullptr, _IONBF, 0);
 SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
 using namespace npc_semi_auto_rules;
 if (argc >= 4) source_trace_rows(argv[3]);
 trace_gate_cases();
 bullet_trace_cases();
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
 // All or nothing (review finding 6): a requested weapon brain that cannot install withdraws the power rule.
 check(power_withdrawn == 1 && !power_installed, "failed opt-in withdraws NpcPowerRule's patches");
 power_installed = true; npc_semi_auto::wanted = false;
 fixture_option = "xml1"; // the power rule installed, but no [NpcSemiAuto] Count: an invalid table
 npc_semi_auto::install(reinterpret_cast<HMODULE>(1));
 check(npc_semi_auto::requested() && !npc_semi_auto::verified() && power_withdrawn == 2 && !power_installed,
       "an invalid weapon table withdraws NpcPowerRule's patches too");
 check(npc_semi_auto::detail() != nullptr, "a requested weapon brain always registers its rule 16 detail");
 power_installed = true; npc_semi_auto::wanted = false;
 fixture_option.reset();
 for (const char* bad : {"", "a|00000000", "a|80000000", "a|7fc00000", "a|7f800000", "a|bf000000",
                         "a|42800000", "a|3f00000", "bad name|3f000000", "a|3f000000junk"})
  check(!parse(bad), "invalid contract rejected");
 // Schema 2 rows (no warm-up and flags words) are refused: the brain now needs the type.
 check(!parse("Test_Guard|3f200000|43c80000|3f800000|00000000"), "schema 2 row refused");
 const auto w = parse("Test_Guard|3f200000|43c80000|3f800000|00000000|00000000|00000002|00000000|00000000");
 check(w && w->actor == "test_guard" && std::bit_cast<float>(w->recoil_bits) == .625f, "case and exact source bits");
 for (const char* bad : {"a|3f000000|7f800000|3f800000|00000000", "a|3f000000|42960000|3f800000|00000000",
                         "a|3f000000|43c84000|3f800000|00000000", "a|3f000000|43c80000|00000000|00000000",
                         "a|3f000000|43c80000|7fc00000|00000000", "a|3f000000|43c80000|3f800000|00000002"})
  check(!parse((std::string(bad) + "|00000000|00000002|00000000|00000000").c_str()), "invalid range or burst rejected");
 // Warm-up and flags words (schema 3).
 for (const char* bad : {"a|3f000000|43c80000|3f800000|00000000|7fc00000|00000002|00000000|00000000",   // NaN warm-up
                         "a|3f000000|43c80000|3f800000|00000000|bf000000|00000002|00000000|00000000",   // negative warm-up
                         "a|3f000000|43c80000|3f800000|00000000|80000000|00000002|00000000|00000000",   // negative zero
                         "a|3f000000|43c80000|3f800000|00000000|42800000|00000002|00000000|00000000",   // 64 s
                         "a|3f000000|43c80000|3f800000|00000000|00000000|00000080|00000000|00000000",   // unknown flag
                         "a|3f000000|43c80000|3f800000|00000000|00000000|00000004|00000000|00000000",   // unknown type bit
                         "a|00000000|43c80000|3f800000|00000000|3f19999a|00000003|00000000|00000000",   // zero recoil, discrete
                         "a|80000000|43c80000|3f800000|00000000|3f19999a|00000013|00000000|00000000",   // negative zero recoil
                         "a|3e4ccccd|43c80000|3fc00000|00000000|3f000000|00000040|00000000|00000000",   // pistol with warm-up
                         "a|3e4ccccd|43c80000|3fc00000|00000000|00000000|00000050|00000000|00000000",   // continuous pistol
                         "a|3f000000|43c80000|3f800000|00000000|00000000|00000002|00000000"})
  check(!parse(bad), "invalid warm-up or flags rejected");
 {
  const auto zero = parse("Lightning|00000000|44098000|40200000|00000001|3f19999a|00000013|00000000|00000000");
  check(zero && zero->recoil_bits == 0 && continuous(*zero) && !bullet(*zero) && play_flags(*zero) == 1,
        "zero recoil admitted only with the continuous beam");
  const auto flame = parse("flame|3cf5c28f|43898000|40600000|00000000|3f800000|00000011|00000000|00000000");
  check(flame && std::bit_cast<float>(flame->range_bits) == 275 && (flame->flags & type_mask) == type_flame,
        "flame row keeps its 275 range and type");
  const auto pistol = parse("cop|3e4ccccd|44098000|3fc00000|00000000|00000000|00000040|00000000|00000000");
  check(pistol && default_handler(*pistol) && bullet(*pistol) && play_flags(*pistol) == 9, "default-handler pistol row");
 }
 std::vector<weapon> a{{"b", 0x3f200000}, {"a", 0x3f000000}}, b(a.rbegin(), a.rend());
 check(canonicalize(a) && canonicalize(b) && fingerprint(a) == fingerprint(b), "canonical order");
 b[0].recoil_bits ^= 1;
 check(fingerprint(a) != fingerprint(b), "recoil bit participates in agreement");
 b=a;b[0].range_bits^=1;check(fingerprint(a)!=fingerprint(b), "range participates in agreement");
 b=a;b[0].burst_bits^=1;check(fingerprint(a)!=fingerprint(b), "burst participates in agreement");
 b=a;b[0].leader^=1;check(fingerprint(a)!=fingerprint(b), "leader flag participates in agreement");
 b=a;b[0].warmup_bits^=1;check(fingerprint(a)!=fingerprint(b), "warm-up participates in agreement");
 b=a;b[0].flags^=flag_continuous;check(fingerprint(a)!=fingerprint(b), "weapon flags participate in agreement");
 b=a;b[0].tracetime_bits^=1;check(table_digest(a)!=table_digest(b), "trace time participates in full agreement");
 b=a;b[0].accuracy_bits^=1;check(table_digest(a)!=table_digest(b), "accuracy participates in full agreement");
 for (const auto badbits : {"7fc00000", "7f800000", "bf800000", "80000000", "43b48000"}) {
  check(!parse(std::string("guard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|") + badbits + "|00000000"), "invalid trace time refused");
  check(!parse(std::string("guard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|00000000|") + badbits), "invalid accuracy refused");
 }
 check(canonical_bytes(a)[3] == '4', "schema 4 digest has a new domain");
 check(!parse("guard|3e23d70a|44098000|3fc00000|00000000|00000000|00000020"), "schema 3 is refused");
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
 // Round 7: discrete bullet rows (0.16 s and 0.17 s recoil, burst 1.5) share the gate with the
 // projectile row; the deadline lands on whole frames like the original's 0.9 s gate.
 for (const auto [bits, fps, frames] : {std::tuple<u32, int, long>{0x3e23d70a, 60, 10}, {0x3e23d70a, 49, 8},
                                       {0x3e23d70a, 30, 5}, {0x3e2e147b, 60, 11}, {0x3e2e147b, 49, 9}}) {
  state timer{}; float previous = -1; bool exact = true; int shots = 0;
  for (int tick = 0; tick <= fps * 30; ++tick) {
   const float time = float(tick) / fps;
   if (!ready(timer, 100, 2, time)) continue;
   if (previous >= 0) exact &= std::lround((time - previous) * fps) == frames;
   timer = entered(100, 2, time, bits); previous = time; ++shots;
  }
  check(exact && shots == 1 + fps * 30 / frames, "bullet recoil admits one shot per frame-quantised gap");
 }
 {
  std::vector<weapon> table, single;
  for (const char* row : {"guard_p|3f666666|44098000|3f800000|00000001|00000000|00000002|00000000|00000000",
                          "guard_m|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|00000000|00000000",
                          "guard_l|3e2e147b|44098000|3fc00000|00000000|00000000|00000000|00000000|00000000",
                          "guard_mb|3e23d70a|44098000|3fc00000|00000000|00000000|00000020|00000000|00000000"}) {
   const auto r = parse(row);
   check(r.has_value(), "discrete bullet and projectile rows parse");
   if (r) table.push_back(*r);
  }
  single = {table[0]};
  check(canonicalize(table) && canonicalize(single) && table.size() == 4, "mixed weapon table canonicalizes");
  check(fingerprint(table) != fingerprint(single), "added bullet rows change the agreement value");
  gameplay_capabilities::record one, all;
  const auto d1 = table_digest(single), dall = table_digest(table);
  one.add(16, npc_power::revision, {1, 1, 1, fingerprint(single)}, true, &d1);
  all.add(16, npc_power::revision, {1, 1, 1, fingerprint(table)}, true, &dall);
  one.finalize(); all.finalize();
  check(one.compare(all.bytes()) != gameplay_capabilities::result::match && all.compare(one.bytes()) != gameplay_capabilities::result::match,
        "peers with different weapon tables refuse each other");
 }
 // Review finding 2: the weapon brain's aiming is lockstep simulation, so its transcendental math is det_math's
 // (only correctly rounded double operations in a fixed order), never the CRT's CPU-dispatched pow / sin / cos. The
 // bits below come from an independent IEEE-double re-implementation (beta7-fixes scripts/det_math_ref.py, Python
 // floats); every sin/cos result there is the correctly rounded float.
 {
  struct pow_case { u32 dt; std::uint64_t keep15; u32 k15; std::uint64_t keep25; u32 k25; };
  static constexpr pow_case pows[] = {
   {0x00000000, 0x3ff0000000000000ull, 0x00000000, 0x3ff0000000000000ull, 0x00000000},
   {0x3c888889, 0x3fef0108304f8cf4ull, 0x3cfef7d0, 0x3fef44e4faca8085ull, 0x3cbb1b05},
   {0x3d088889, 0x3fee09ffe61f3e83ull, 0x3d7b000d, 0x3fee8e0ff978b317ull, 0x3d38f803},
   {0x3ca72f05, 0x3feec8e4d5b1179aull, 0x3d1b8d95, 0x3fef1b7b67649bbfull, 0x3ce48499},
   {0x3dcccccd, 0x3fea786689b457b7ull, 0x3e30f32f, 0x3febdb8cd9e4b7fdull, 0x3e048e65},
   {0x3f000000, 0x3fd8c97ef43f7248ull, 0x3f1cda04, 0x3fe0000000000000ull, 0x3f000000},
   {0x3f800000, 0x3fc3333333333333ull, 0x3f59999a, 0x3fd0000000000000ull, 0x3f400000},
   {0x40200000, 0x3f81d8c1ce9414d8ull, 0x3f7dc4e8, 0x3fa0000000000000ull, 0x3f780000}};
  bool exact = true;
  for (const auto& c : pows) {
   const double dt = double(std::bit_cast<float>(c.dt));
   const double p15 = det_math::pow_0_15(dt), p25 = det_math::pow_0_25(dt);
   exact &= std::bit_cast<std::uint64_t>(p15) == c.keep15 && std::bit_cast<u32>(float(1.0 - p15)) == c.k15 &&
            std::bit_cast<std::uint64_t>(p25) == c.keep25 && std::bit_cast<u32>(float(1.0 - p25)) == c.k25;
  }
  check(exact, "aim smoothing 0.15^dt / 0.25^dt: pinned bits");
  struct trig_case { u32 angle, s, c; };
  static constexpr trig_case trig[] = {
   {0x00000000, 0x00000000, 0x3f800000}, {0x3f000000, 0x3ef57744, 0x3f60a940}, {0xbf000000, 0xbef57744, 0x3f60a940},
   {0x3fc90fdb, 0x3f800000, 0xb33bbd2e}, {0x40490fdb, 0xb3bbbd2e, 0xbf800000}, {0xc0490fdb, 0x33bbbd2e, 0xbf800000},
   {0x40000000, 0x3f68c7b7, 0xbed51133}, {0xc02ccccd, 0xbedad188, 0xbf677146}, {0x40c90fdb, 0x343bbd2e, 0x3f800000},
   {0x41200000, 0xbf0b44f8, 0xbf56cd64}, {0x42c88000, 0xbe8df805, 0x3f75f639}, {0x3ca3d70a, 0x3ca3d43e, 0x3f7ff2e5},
   {0xbc75c28f, 0xbc75c033, 0x3f7ff8a1}, {0x3f490fdb, 0x3f3504f3, 0x3f3504f3}, {0xbf490fdb, 0xbf3504f3, 0x3f3504f3},
   {0x447a2000, 0x3f7eca46, 0x3dc6dffc}};
  exact = true;
  for (const auto& c : trig) {
   const auto r = det_math::sincos(std::bit_cast<float>(c.angle));
   exact &= std::bit_cast<u32>(r.s) == c.s && std::bit_cast<u32>(r.c) == c.c;
  }
  check(exact, "weapon brain sin/cos: pinned bits");
  // Accuracy against this machine's CRT (not a determinism check: the CRT is what det_math replaces).
  const auto ulps = [](float a, float b) { const auto x = std::bit_cast<std::int32_t>(a), y = std::bit_cast<std::int32_t>(b);
   return (x < 0) != (y < 0) ? (a == b ? 0 : 99) : std::abs(x - y); };
  int worst = 0; double worst_pow = 0;
  for (int i = -200000; i <= 200000; ++i) {
   const float angle = float(i) * (8.f / 200000.f);
   const auto r = det_math::sincos(angle);
   worst = std::max({worst, ulps(r.s, float(std::sin(double(angle)))), ulps(r.c, float(std::cos(double(angle))))});
  }
  for (int i = 0; i <= 20000; ++i) {
   const double dt = double(float(i) * (2.f / 20000.f));
   worst_pow = std::max({worst_pow, std::abs(det_math::pow_0_15(dt) - std::pow(.15, dt)) / std::pow(.15, dt),
                         std::abs(det_math::pow_0_25(dt) - std::pow(.25, dt)) / std::pow(.25, dt)});
  }
  std::printf("det_math vs CRT: sin/cos worst %d float ulp over [-8, 8]; pow worst relative %.3g over [0, 2] s\n", worst, worst_pow);
  check(worst <= 1, "det_math sin/cos within one float ulp of the CRT");
  check(worst_pow < 1e-15, "det_math pow within 1e-15 of the CRT");
  check(std::isnan(det_math::sincos(std::numeric_limits<float>::infinity()).s) && det_math::pow_0_15(std::numeric_limits<double>::quiet_NaN()) != det_math::pow_0_15(std::numeric_limits<double>::quiet_NaN()),
        "non-finite inputs stay non-finite");
  // Every hook runs the brain under the default MXCSR: fxsave [esp] is followed by ldmxcsr [mxcsr_value].
  const u32 where = static_cast<u32>(reinterpret_cast<std::uintptr_t>(&npc_semi_auto::mxcsr_value));
  bool pinned = npc_semi_auto::mxcsr_value == 0x1f80;
  for (const void* hook : {reinterpret_cast<const void*>(&npc_semi_auto::init_hook), reinterpret_cast<const void*>(&npc_semi_auto::refresh_hook),
                           reinterpret_cast<const void*>(&npc_semi_auto::input_hook), reinterpret_cast<const void*>(&npc_semi_auto::sight_hook),
                           reinterpret_cast<const void*>(&npc_semi_auto::power_hook), reinterpret_cast<const void*>(&npc_semi_auto::decision_hook)}) {
   const auto* code = static_cast<const unsigned char*>(hook);
   bool found = false;
   for (int i = 0; i + 10 < 96 && !found; ++i)
    if (code[i] == 0x0f && code[i + 1] == 0xae && code[i + 2] == 0x04 && code[i + 3] == 0x24 &&
        code[i + 4] == 0x0f && code[i + 5] == 0xae && code[i + 6] == 0x15 && std::memcmp(code + i + 7, &where, 4) == 0) found = true;
   pinned &= found;
  }
  check(pinned, "every weapon-brain hook loads the default MXCSR after saving the caller's state");
 }
 // Review round 2: npc_power::withdraw refuses startup on every outcome but a verified withdrawal; the reverse
 // transaction it runs (npc_power_withdraw::run) against a memory that fails at each step.
 {
  struct memory {
   std::map<u32, unsigned char> bytes; bool fail_acquire = false, fail_write = false, fail_restore_write = false; int writes = 0;
   bool read(u32 a, void* p, std::size_t n) { for (std::size_t i = 0; i != n; ++i) static_cast<unsigned char*>(p)[i] = bytes[a + u32(i)]; return true; }
   bool write(u32 a, const void* p, std::size_t n) {
    ++writes;
    if (fail_write && writes == 1) return false;            // the withdrawal's own write
    if (fail_restore_write && writes > 1) return false;     // and then its rollback
    for (std::size_t i = 0; i != n; ++i) bytes[a + u32(i)] = static_cast<const unsigned char*>(p)[i];
    return true;
   }
   bool acquire(u32, u32& old) { old = 0x20; return !fail_acquire; }
   bool restore(u32, u32) { return true; }
   bool flush() { return true; }
  };
  const std::vector<xml1_hook_transaction::patch> written{{0x512f0b, {0x74}, {0xeb}}, {0x5187c4, {0x0f, 0x85}, {0xe9, 0x10}}};
  const auto installed = [] { memory m; m.bytes[0x512f0b] = 0xeb; m.bytes[0x5187c4] = 0xe9; m.bytes[0x5187c5] = 0x10; return m; };
  using npc_power_withdraw::result;
  {
   auto m = installed();
   check(npc_power_withdraw::run(m, written) == result::withdrawn && m.bytes[0x512f0b] == 0x74 && m.bytes[0x5187c4] == 0x0f && m.bytes[0x5187c5] == 0x85,
         "withdrawal restores every site's retail bytes");
   check(npc_power_withdraw::run(m, {}) == result::nothing, "nothing installed: nothing to withdraw");
  }
  {
   auto m = installed(); m.fail_acquire = true;
   check(npc_power_withdraw::run(m, written) == result::failed && m.bytes[0x512f0b] == 0xeb,
         "a page that cannot be made writable fails (the rule stays installed: the caller refuses startup)");
  }
  {
   auto m = installed(); m.fail_write = true;
   check(npc_power_withdraw::run(m, written) == result::failed, "a failed write fails (rolled back to the rule)");
  }
  {
   auto m = installed(); m.fail_write = true; m.fail_restore_write = true;
   check(npc_power_withdraw::run(m, written) == result::failed, "a failed write whose rollback fails too fails");
  }
  {
   auto m = installed(); m.bytes[0x512f0b] = 0x90;
   check(npc_power_withdraw::run(m, written) == result::failed && m.bytes[0x5187c4] == 0xe9, "a site that is no longer the rule's is not touched and fails");
  }
 }
 // Review finding 1 (GPT Astra 2026-10-25): two valid one-row tables whose 32-bit FNV fold collided (fc72604b),
 // so beta 7's first agreement admitted them; their warm-ups are ~0.1008 s and ~0.0332 s. The detail (SHA-256 of the
 // complete canonical rows) now refuses them both ways.
 {
  const auto legacy_fnv = [](const std::vector<weapon>& t) {
   u32 h = 2166136261u;
   const auto add = [&h](unsigned char c) { h = (h ^ c) * 16777619u; };
   for (const auto& w : t) {
    for (const unsigned char c : w.actor) add(c);
    add(0);
    for (const u32 value : {w.recoil_bits, w.range_bits, w.burst_bits, w.leader, w.warmup_bits, w.flags})
     for (unsigned i = 0; i != 4; ++i) add(static_cast<unsigned char>(value >> (8 * i)));
   }
   return h;
  };
  const auto r1 = parse("haarpsoldier|3f20094f|44098000|3fc00000|00000000|3dce7218|00000020|00000000|00000000");
  const auto r2 = parse("haarpsoldier|3e0e857e|44098000|3fc00000|00000000|3d07fa5d|00000020|00000000|00000000");
  check(r1 && r2, "reviewer's colliding rows parse");
  if (r1 && r2) {
   std::vector<weapon> t1{*r1}, t2{*r2};
   check(canonicalize(t1) && canonicalize(t2), "colliding tables canonicalize");
   check(legacy_fnv(t1) == 0xfc72604bu && legacy_fnv(t2) == 0xfc72604bu, "the two tables collide in the old 32-bit fold");
   const auto d1 = table_digest(t1), d2 = table_digest(t2);
   check(d1 != d2, "the full digest of the canonical rows differs");
   gameplay_capabilities::record ra, rb, a2, nodetail;
   ra.add(16, npc_power::revision, {1, 1, 1, fingerprint(t1)}, true, &d1);
   rb.add(16, npc_power::revision, {1, 1, 1, fingerprint(t2)}, true, &d2);
   a2.add(16, npc_power::revision, {1, 1, 1, fingerprint(t1)}, true, &d1);
   // Even with equal words (the old fingerprint in word 3), the detail alone refuses.
   gameplay_capabilities::record sa, sb;
   sa.add(16, npc_power::revision, {1, 1, 1, 0xfc72604bu}, true, &d1);
   sb.add(16, npc_power::revision, {1, 1, 1, 0xfc72604bu}, true, &d2);
   nodetail.add(16, npc_power::revision, {1, 1, 1, fingerprint(t1)}, true);
   for (auto* r : {&ra, &rb, &a2, &sa, &sb, &nodetail}) r->finalize();
   check(ra.compare(rb.bytes()) != gameplay_capabilities::result::match && rb.compare(ra.bytes()) != gameplay_capabilities::result::match,
         "peers with the colliding rows refuse each other");
   gameplay_capabilities::difference diff{};
   check(sa.compare(sb.bytes(), &diff) == gameplay_capabilities::result::value && sb.compare(sa.bytes()) == gameplay_capabilities::result::value,
         "equal words, different rows: refused as a value difference");
   char names[128]{}; sa.describe(diff.buckets, diff.local_only, true, names, sizeof names);
   check(std::string_view(names) == "16 NpcPowerRule", "the refusal names rule 16");
   check(ra.compare(a2.bytes()) == gameplay_capabilities::result::match, "equal rows still agree");
   check(ra.compare(nodetail.bytes()) != gameplay_capabilities::result::match, "a peer without the detail is refused");
   check(ra.detail(16) && *ra.detail(16) == d1 && !nodetail.detail(16), "detail is kept per rule");
   check(fingerprint(t1) != fingerprint(t2), "word 3 (digest prefix) differs as well");
  }
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
 pistol_cases(actor.data(), ai.data());
 decision_cases(actor.data(), ai.data());
 if (argc >= 3) original_input_comparison(argv[2], actor.data());
 // Check GCAP rejection: different mode, metadata, revision or failed install.
 const auto vals = npc_semi_auto::values({1, 1, 0, 0});
 gameplay_capabilities::record local, equal, off, altered, old, failed;
 gameplay_capabilities::record power_only, candidate9, released4, released10;
 local.add(16, npc_power::revision, vals, true); equal.add(16, npc_power::revision, vals, true);
 off.add(16, 3, {0, 0, 0, 0}, true); released4.add(16, 4, {1, 1, 0, 0}, true); released10.add(16, 10, vals, true);
 power_only.add(16, npc_power::revision, {1, 1, 0, 0}, true);
 auto changed = vals; changed[3] ^= 1; altered.add(16, npc_power::revision, changed, true);
 old.add(16, 8, vals, true); candidate9.add(16, 9, vals, true); failed.add(16, npc_power::revision, vals, false);
 for (auto* r : {&local, &equal, &off, &altered, &old, &failed, &power_only, &candidate9, &released4, &released10}) r->finalize();
 check(npc_power::revision == 14, "schema 4 weapon brain and the character bullet ray ship under rule 16 revision 14 (10 = 1.8.0-beta.6, 11 = 1.8.0-beta.7, 12/13 unreleased candidates)");
 check(local.compare(equal.bytes()) == gameplay_capabilities::result::match, "equal peers agree");
 for (const auto* peer : {&off, &altered, &old, &power_only, &candidate9, &released4, &released10})
  check(local.compare(peer->bytes()) != gameplay_capabilities::result::match, "incompatible peer refused");
 check(!failed.ready(), "requested failure poisons agreement");
 bullet_bridge_cases();
 weapon_bridge_cases();
 guards_clear_of_rule16();
 if (argc >= 2) { retail_guards(argv[1]); native_rejected_trace(argv[1]); } else check(false, "retail executable path required");
 if (argc >= 3) {
  original_entry_comparison(argv[2]); owned_bullet_direction(argv[2], argv[1]); owned_bullet_endpoints(argv[2]); owned_bullet_filters(argv[2]);
  owned_bullet_first_hit(argv[2]);
  owned_bullet_muzzle(argv[2]);
 }
 else std::printf("Original callback comparison not requested (supply owned XBE as second path)\n");
 std::printf("NPC semi-auto: %d checks, %d failures\n", checks, failures);
 return failures ? 1 : 0;
}
