#pragma once
// Included in npc_semi_auto's private namespace. Part A owns presentation and
// trace admission; part B supplies the character muzzle, direction and contact.
#include "npc_bullet_ray.hpp"
constexpr u32 bullet_marker = 0x7ffe7ffe; // split_damage="32766 32766", split_count=0
std::array<trace_state, timer_slots> trace_states{};
struct bullet_scope {
 void* actor = nullptr;
 const weapon* row = nullptr;
 bool pending = true, skip = false;
 bullet_scope* previous = nullptr;
 bullet_frame ray_frame{};
 point direction{};
 bool collision_done = false;
};
thread_local bullet_scope* current_bullet = nullptr;
using event_t = bool(__thiscall*)(void*, void*);
u32 event_resume = 0x4dcfe6;
__declspec(naked) bool event_native()
{
 __asm {
  sub esp, 0x10
  push esi
  mov esi, ecx
  jmp dword ptr [event_resume]
 }
}
auto event_function = reinterpret_cast<event_t>(&event_native);
bool __fastcall bullet_event(void* event, void*, void* context) noexcept
{
 // split damage is inert in retail when split_count is zero. The pair marks only
 // builder-converted bullets, including inherited weapon events, never general beams.
 void* actor = context ? field<void*>(context, 0) : nullptr;
 const auto* w = applied && event && field<void*>(event, 0x14) &&
                  field<u32>(event, 0x3c) == bullet_marker && field<unsigned char>(event, 0x45) == 0 ? spec(actor) : nullptr;
 bullet_scope scope{actor, w && bullet(*w) ? w : nullptr, true, false, current_bullet};
 current_bullet = &scope; // also masks an outer scope during an unrelated nested event
 const bool result = event_function(event, context);
 current_bullet = scope.previous;
 return result;
}
using tracer_t = void(__cdecl*)(u32, const point*, const point*, u32, float, u32);
void __cdecl native_tracer(u32 effect, const point* origin, const point* ray, u32 token, float parameter, u32 flags)
{
 void* manager = reinterpret_cast<void*(__cdecl*)()>(0x407750)();
 using play_t = void(__thiscall*)(void*, u32, const point*, const point*, u32, float, u32, float);
 reinterpret_cast<play_t>((*static_cast<void***>(manager))[0x34 / 4])(manager, effect, origin, ray, token, parameter, flags, -1.f);
}
auto tracer_function = &native_tracer;
double bullet_spread_draw()
{
 det_math::fp_env fp;
 double result = 0;
 // Native float and double returns share x87 ST0 on this engine's x86 ABI.
 // Store ST0 directly: a C++ float return temporary would round away seed bits.
 __asm {
  push 1
  call dword ptr [random_function]
  add esp, 4
  fstp qword ptr [result]
 }
 return result;
}
// At 0x452225: native muzzle/aim calculation is complete; no collision query
// has run. frame is ESP before the overwritten cmp. The tracer's ordinary tail
// arguments are known here: origin +0x110, unit direction +0x50, length +0x214.
bool __cdecl begin_bullet_trace(void* actor, void* frame) noexcept
{
 auto* scope = current_bullet;
 if (!scope || !scope->row || scope->actor != actor || !scope->pending) return false;
 scope->pending = false;
 det_math::fp_env fp;
 bullet_frame_function(actor, frame, &scope->ray_frame);
 // These are part A's three draws, now consumed by the original spread math.
 // Do not draw again in the geometry or collision adapter (even for zero).
 std::array<double, 3> spread{};
 // Both owned RNGs return the unrounded x87 ST0 result (integer * 2^-31).
 // Read that ABI result as double, as XML1's inline range expression does;
 // a C++ float temporary would discard seed bits for nonzero accuracy.
 for (auto& r : spread) r = bullet_spread_draw();
 scope->direction = bullet_direction(field<point>(actor, 0x3ac), scope->ray_frame,
                                     scope->row->accuracy_bits, spread);
 field<point>(frame, 0x44) = field<point>(frame, 0x110) = scope->ray_frame.muzzle;
 field<point>(frame, 0x50) = scope->direction;
 field<u32>(frame, 0x214) = scope->row->range_bits;
 const auto effect = field<u32>(frame, 0x228);
 field<u32>(frame, 0x228) = 0; // this call's stack argument only; suppress duplicate tail tracer
 if (effect) {
  const auto d = field<point>(frame, 0x50);
  const float range = field<float>(frame, 0x214);
  const point ray{d.x * range, d.y * range, d.z * range};
  tracer_function(effect, &field<point>(frame, 0x110), &ray, field<u32>(actor, 0x1c),
                  field<float>(frame, 0x234), field<u32>(frame, 0x7c));
 }
 const auto token = field<u32>(actor, 0x1c);
 scope->skip = !trace_gate(trace_states[timer_slot(token)], reinterpret_cast<std::uintptr_t>(actor), token,
                           now(), scope->row->tracetime_bits);
 // The stock beam's additional muzzle-obstruction adjustment also uses its
 // own geometry. All marked shots bypass that query, admitted or rejected.
 return true;
}
bool __cdecl skip_bullet_collision(void* actor, void* frame) noexcept
{
 auto* scope = current_bullet;
 if (!scope || !scope->row || scope->actor != actor || scope->pending) return false;
 if (scope->skip || scope->collision_done) return true;
 scope->collision_done = true;
 det_math::fp_env fp;
 const auto segment = bullet_endpoints(scope->ray_frame, scope->direction, scope->row->range_bits);
 const auto hit = bullet_contact_function(actor, segment);
 if (hit.found) {
  field<point>(frame, 0xb8) = hit.position;
  field<point>(field<void*>(frame, 0x20c), 0x40) = hit.position;
  field<void*>(frame, 0x78) = hit.entity;
  field<unsigned char>(frame, 0x17) = 1;
  // XML1 0xba725..0xba79f admits physents (characters and destructible props),
  // using the event's existing attack. Keep all damage/type/modifier values.
  if (hit.entity && bullet_class_function(hit.entity, 0x753f64)) {
   std::array<std::byte, 0x6c> attack{};
   std::memcpy(attack.data(), field<void*>(frame, 0x20c), attack.size());
   field<point>(attack.data(), 0x40) = hit.position;
   field<point>(attack.data(), 0x4c) = scope->direction;
   field<point>(attack.data(), 0x58) = hit.normal;
   bullet_take_hit_function(hit.entity, attack.data());
  }
  const auto impact = field<u32>(frame, 0x22c);
  if (impact) tracer_function(impact, &hit.position, &hit.normal, field<u32>(actor, 0x1c),
                             field<float>(frame, 0x234), 0);
 }
 // No retail query scope was constructed. Its disposal and every stock
 // collision/to-hit path are bypassed; the ordinary presentation tail remains.
 return true;
}
bool(__cdecl* begin_trace_function)(void*, void*) = &begin_bullet_trace;
bool(__cdecl* skip_trace_function)(void*, void*) = &skip_bullet_collision;
u32 muzzle_resume = 0x45222c, muzzle_skipped = 0x45230b;
u32 collision_resume = 0x452355, collision_skipped = 0x4528a9;
// Preserve the whole native register/flags and FP/SSE state, including ST0. The
// helper's fp_env loads the game's 0x027f word before every new float operation.
__declspec(naked) void bullet_muzzle_hook()
{
 __asm {
  pushfd
  pushad
  mov ebx, esp
  sub esp, 528
  and esp, -16
  mov [esp + 512], ebx
  fxsave [esp]
  lea eax, [ebx + 36]
  push eax
  push edi
  call dword ptr [begin_trace_function]
  add esp, 8
  fxrstor [esp]
  mov esp, [esp + 512]
  test al, al
  jnz skip_muzzle
  popad
  popfd
  cmp byte ptr [esp + 0x238], bl
  jmp dword ptr [muzzle_resume]
 skip_muzzle:
  popad
  popfd
  jmp dword ptr [muzzle_skipped]
 }
}
__declspec(naked) void bullet_collision_hook()
{
 __asm {
  pushfd
  pushad
  mov ebx, esp
  sub esp, 528
  and esp, -16
  mov [esp + 512], ebx
  fxsave [esp]
  lea eax, [ebx + 36]
  push eax
  push edi
  call dword ptr [skip_trace_function]
  add esp, 8
  fxrstor [esp]
  mov esp, [esp + 512]
  test al, al
  jnz skip_collision
  popad
  popfd
  lea edx, [esp + 0x50]
  push edx
  jmp dword ptr [collision_resume]
 skip_collision:
  popad
  popfd
  // No query scope was constructed: bypass its destruction as well. Native
  // endpoint math and the non-collision presentation tail still execute. The
  // normal stays zero, so the stock ricochet/split query path is not entered.
  jmp dword ptr [collision_skipped]
 }
}
