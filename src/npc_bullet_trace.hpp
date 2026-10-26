#pragma once
// Included in npc_semi_auto's private namespace. A only: retain the PC beam's
// aim/width/hit handling for admitted traces. B's character ray is separate work.
constexpr u32 bullet_marker = 0x7ffe7ffe; // split_damage="32766 32766", split_count=0
std::array<trace_state, timer_slots> trace_states{};
struct bullet_scope {
 void* actor = nullptr;
 const weapon* row = nullptr;
 bool pending = true, skip = false;
 bullet_scope* previous = nullptr;
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
// At 0x452225: native muzzle/aim calculation is complete; no collision query
// has run. frame is ESP before the overwritten cmp. The tracer's ordinary tail
// arguments are known here: origin +0x110, unit direction +0x50, length +0x214.
bool __cdecl begin_bullet_trace(void* actor, void* frame) noexcept
{
 auto* scope = current_bullet;
 if (!scope || !scope->row || scope->actor != actor || !scope->pending) return false;
 scope->pending = false;
 det_math::fp_env fp;
 // XML1's character-aim path consumes three simulation spread draws even for
 // accuracy zero. Stock PC beam aiming consumes none. A retains stock geometry.
 for (unsigned i = 0; i != 3; ++i) random_function(true);
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
 return scope->skip;
}
bool __cdecl skip_bullet_collision(void* actor) noexcept
{
 const auto* scope = current_bullet;
 return scope && scope->row && scope->actor == actor && !scope->pending && scope->skip;
}
bool(__cdecl* begin_trace_function)(void*, void*) = &begin_bullet_trace;
bool(__cdecl* skip_trace_function)(void*) = &skip_bullet_collision;
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
  push edi
  call dword ptr [skip_trace_function]
  add esp, 4
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
