#pragma once

// Included inside npc_semi_auto's private namespace. The supported contract covers
// every source gun primary: semi-auto (discrete, warm-up before every shot, or a
// continuous beam warmed up once) and the default-handler pistol. The native ranged brain retains
// its target validation, navigation, near-target selection and close-combat paths; this restores
// the missing weapon branch and the separate actor-input gate.
struct point { float x, y, z; };
using resolve_name_t = void*(__thiscall*)(void*, const u32*);
using intern_move_t = void*(__thiscall*)(u32*, const char*);
using request_move_t = void(__thiscall*)(void*, void*, int, float, u32, void*, void*);
using chain_t = bool(__thiscall*)(void*, void*, void*);
using ctor_t = void*(__thiscall*)(void*);
using set_level_t = void(__thiscall*)(void*, int);
using resolve_target_t = bool(__thiscall*)(void*, u32, void**, void**, void**);
using rotate_t = void(__thiscall*)(void*, const point*);
using ai_get_t = void*(__thiscall*)(void*, u32);
using delay_t = void(__thiscall*)(void*, float);
using alert_t = void(__thiscall*)(void*, float, float, u32);
struct attack_notice { u32 words[9]{}; };
using notice_t = void(__thiscall*)(void*, float, u32, u32, attack_notice, u32, u32);
auto name_function = reinterpret_cast<resolve_name_t>(0x427ca0);
auto intern_function = reinterpret_cast<intern_move_t>(0x41b120);
auto request_function = reinterpret_cast<request_move_t>(0x432030);
auto chain_function = reinterpret_cast<chain_t>(0x4fce50);
auto ctor_function = reinterpret_cast<ctor_t>(0x518090);
auto level_function = reinterpret_cast<set_level_t>(0x517c30);
auto target_function = reinterpret_cast<resolve_target_t>(0x518660);
auto rotate_function = reinterpret_cast<rotate_t>(0x5031b0);
auto ai_function = reinterpret_cast<ai_get_t>(0x525160);
auto delay_function = reinterpret_cast<delay_t>(0x502580);
auto alert_function = reinterpret_cast<alert_t>(0x5078b0);
auto notice_function = reinterpret_cast<notice_t>(0x505a40);
auto random_function = reinterpret_cast<float(__cdecl*)(bool)>(0x560190);
using nav_t = bool(__thiscall*)(void*, point*);
using angles_t = point*(__cdecl*)(point*, const point*);
using face_t = void(__thiscall*)(void*, point*, point*);
using aim_t = void(__thiscall*)(void*, const point*);
const point* __fastcall entity_position(void* entity, void*, point* out)
{ return reinterpret_cast<const point*(__thiscall*)(void*, point*)>((*static_cast<void***>(entity))[0x24 / 4])(entity, out); }
auto nav_function = reinterpret_cast<nav_t>(0x502c50);
auto angles_function = reinterpret_cast<angles_t>(0x5607b0);
auto face_function = reinterpret_cast<face_t>(0x503810);
auto aim_function = reinterpret_cast<aim_t>(0x517cf0);
auto position_function = reinterpret_cast<const point*(__fastcall*)(void*, void*, point*)>(&entity_position);
using see_t = bool(__thiscall*)(void*, bool*);
using state_t = void(__thiscall*)(void*, int);
using turn_t = void(__thiscall*)(void*, const point*, const point*);
auto see_function = reinterpret_cast<see_t>(0x505250);     // XML2's AI target visibility (original 0xfe550)
auto state_function = reinterpret_cast<state_t>(0x505030); // AI state setter (original 0xfe330)
auto turn_function = reinterpret_cast<turn_t>(0x503180);   // AI turn toward angles (original 0xfc5d0)
// Current move flag (original move vfunc +0x54 = flags +0x12c bit 2; PC +0x5c = +0x144 bit 2).
bool __cdecl move_busy(void* move)
{ return reinterpret_cast<bool(__thiscall*)(void*)>((*static_cast<void***>(move))[0x5c / 4])(move); }
auto busy_function = &move_busy;
float native_delta() { return field<float>(*reinterpret_cast<void**>(0x718434), 0x3ec); }
auto delta_function = &native_delta;
void* named_move(void* actor, const char* name)
{
 u32 key = 0;
 intern_function(&key, name);
 return name_function(actor, &key);
}
void* native_ai(void* actor)
{ return actor ? ai_function(reinterpret_cast<void*>(0x78ba10), field<u32>(actor, 0x7dc)) : nullptr; }
const weapon* npc_spec(void* actor)
{ return actor && field<int>(actor, 0x3d4) == -1 ? spec(actor) : nullptr; }
// The converted primary: a ch_weapon_semi_auto move resolves to this handler; the pistol's
// source primary has no handler, so its converted power_attack keeps XML2's default one.
bool gun_move(void* actor, void* move, const weapon& w)
{
 if (!move) return false;
 void* h = field<void*>(move, 0xc8);
 if (!default_handler(w)) return h == &handler;
 return h && *static_cast<const u32*>(h) == default_vtable && move == named_move(actor, "power_attack");
}

// Every entry the hooks call runs inside det_math::fp_env (x87 control word and MXCSR fixed, restored on return).
// None of them can throw (no allocation, no throwing call; noexcept would terminate instead of unwinding through a
// naked hook frame); a fault inside game code is an SEH crash of the game whatever the state.
void __cdecl prepare_brain(void* ai) noexcept
{
 det_math::fp_env fp;
 void* actor = field<void*>(ai, 0x10);
 const auto* w = npc_spec(actor);
 if (!w) return;
 void* brain = field<void*>(ai, 0x4c4);
 // These two stock constructors have no resources beyond the embedded object.
 // Do not replace special-purpose brains even with a manually edited contract.
 if (!brain || (field<u32>(brain, 0) != 0x695084 && field<u32>(brain, 0) != 0x6950f4)) return;
 ctor_function(brain);
 // NpcPowerRule maps PC row 11 to the original top level (six combos).
 // Original weapon init 0xfd435..0xfd466 distinguishes leaders from others.
 level_function(brain, w->leader ? 11 : 2);
 field<u32>(ai, 0x280) = w->range_bits;
 if (!w->leader) field<u32>(ai, 0x2ac) = 1;
}
bool __cdecl preserve_weapon_range(void* ai, void* const* chains = nullptr) noexcept
{
 det_math::fp_env fp;
 const auto* w = npc_spec(field<void*>(ai, 0x10));
 if (!w) return false;
 if (chains) {
  bool ordinary = false;
  u32 capabilities = 0;
  for (unsigned slot = 0; slot != 4; ++slot) {
   void* move = chains[5 + slot]; // native refresh's already resolved action table
   if (gun_move(field<void*>(ai, 0x10), move, *w)) {
    // These attributes were invented by the weapon conversion. Original gun
    // moves do not advertise generic AI powers; restore the empty cached slot.
    field<u32>(ai, 0x41c + 4*slot) = 0;
    field<u32>(ai, 0x430 + 4*slot) = 0;
    field<float>(ai, 0x444 + 4*slot) = 5.f;
    field<float>(ai, 0x458 + 4*slot) = 0;
   } else if (move && field<float>(move, 0x140) < 500.f) ordinary = true;
   capabilities |= field<u32>(ai, 0x430 + 4*slot);
  }
  field<u32>(ai, 0x418) = capabilities;
  auto& flags = field<unsigned char>(ai, 0x2b9);
  flags = ordinary ? flags & ~0x40 : flags | 0x40;
 }
 const void* brain = field<void*>(ai, 0x4c4);
 if (!brain || *static_cast<const u32*>(brain) != 0x6950f4) return false;
 field<u32>(ai, 0x280) = w->range_bits;
 return true; // skip XML2's power-derived role/range replacement
}

bool __fastcall semi_chain(void* self, void*, void* actor, void* input) noexcept
{
 if (!npc_spec(actor)) return chain_function(self, actor, input);
 const u32 held = field<u32>(input, 0);
 if ((held & 0x90) == 0x90) {
  field<void*>(actor, 0x3bc) = field<void*>(actor, 0x378);
  field<u32>(input, 0x24) |= 8;
 } else if (held & 0x100) {
  field<void*>(actor, 0x3bc) = action_function(actor, 3);
 } else {
  field<u32>(input, 0x24) |= 4;
  field<void*>(actor, 0x3bc) = nullptr;
 }
 return true;
}

bool __cdecl weapon_input(void* actor, void* input) noexcept
{
 det_math::fp_env fp;
 const auto* w = npc_spec(actor);
 if (!w) return false;
 const u32 token = field<u32>(actor, 0x1c);
 const auto self = reinterpret_cast<std::uintptr_t>(actor);
 auto& s = states[timer_slot(token)];
 if ((field<u32>(input, 0) & 0x90) != 0x90) { released(s, self, token); return false; }
 if (field<unsigned char>(actor, 0x764) & 1) return true;
 void* current = field<void*>(actor, 0x378);
 if (!current) return true;
 const int animation = field<int>(current, 0xbc); // native getter 0x500840
 const float time = now();
 // Original enums 0, 0x15, 0x86, 0x8c map to PC idle, crouch,
 // power1 and power7. Resolve the move by name, not a contextual power chain.
 if (animation != 0 && animation != 21 && animation != 115 && animation != 127) {
  request_function(actor, named_move(actor, "idle"), -1, -1.f, 0, nullptr, nullptr);
  rearm(s, self, token, time, transition_delay);
  return true;
 }
 if (!ready(s, self, token, time)) return true;
 void* move = named_move(actor, "power_attack");
 if (!gun_move(actor, move, *w)) return true;
 // Warm-up start (0x4309a): the original also plays the weapon's charge effect through
 // actor vfunc +0x84; the PC conversion authors that effect inside the primary at the
 // shot, so only the deadline is restored here.
 if (gate(s, self, token, time, *w) != admit::fire) return true;
 request_function(actor, move, (field<unsigned char>(actor, 0x3d9) & 0x40) ? 127 : 115,
                  -1.f, play_flags(*w), nullptr, nullptr);
 return true;
}

// Called after the ranged brain's power decision (0x519fe4). The original decision
// (0xee260) returns true after pressing a power, so the weapon brain stops for that update
// (0xef5d2..0xef5ef: input+8 = 0, no fire bits; the owner's S2 ring shows the input word
// 0xa0 once at full energy, then 0x90). XML2's decision (0x518770) presses the same power
// bits but returns false, and the held-fire branch then turns the press into a gun shot
// or a heavy melee attack. Treat such a press as the original's true result.
bool __cdecl power_pressed(int native, void* brain, void* input) noexcept
{
 if (native & 0xff) return true;
 void* ai = field<void*>(brain, 4);
 if (!ai || !npc_spec(field<void*>(ai, 0x10))) return false;
 const u32 held = field<u32>(input, 0);
 return (held & 0x80) && (held & 0x170);
}

// Bullet weapons only, at the top of every weapon-brain update (0xef3cd..0xef434, between
// the target resolve 0xee150 and the attack-state call; PC 0x519e3a, after 0x518660):
// when the AI's visibility test (0xfe550; XML2's own version is 0x505250, same contract:
// false plus an untouched flag = blocked within 600 units) fails, the burst timers reset
// (+0x20 = 0, +0x1c = -1) and the AI drops to state 0 (0xfe330 = PC 0x505030).
void __cdecl line_of_sight(void* brain) noexcept
{
 det_math::fp_env fp;
 void* ai = field<void*>(brain, 4);
 if (!ai) return;
 const auto* w = npc_spec(field<void*>(ai, 0x10));
 if (!w || !bullet(*w)) return;
 bool unknown = false;
 if (see_function(ai, &unknown) || unknown) return;
 field<float>(brain, 0x20) = 0.f;
 field<float>(brain, 0x1c) = -1.f;
 state_function(ai, 0);
}

// Empty near-target query, bullet weapons (0xef9bc..0xefc3e): fire is held while the
// burst timer (+0x1c) is non-negative, minus the frame delta; when it goes negative the
// brain pauses until now + 1.0 (+0x20) and the next burst lasts rand + 2.0 s. The aim
// point (actor+0x318 = PC +0x3ac) moves toward the destination by 1 - 0.15^dt while firing
// (0.25^dt otherwise); a rapid-fire weapon's pause frame pulls it 75 % back toward the
// actor (0x23fc50 lerp 0.25). Then the AI turns toward the aim point when it is more than
// 10 units away on the ground plane (0xfc5d0 = PC 0x503180). A current move whose
// flag +0x54 (PC +0x5c, flags +0x144 bit 2) is set ends the update before aiming.
bool bullet_branch(void* brain, void* input, void* ai, void* actor, const weapon& w, void* destination)
{
 const float time = now(), dt = delta_function();
 float& burst = field<float>(brain, 0x1c);
 float& resume = field<float>(brain, 0x20);
 bool fired = false, paused = false;
 if (time > resume) {
  if (burst >= 0) {
   field<u32>(input, 0) |= 0x90;
   field<u32>(input, 4) |= 0x90;
   burst -= dt;
   fired = true;
   field<point>(input, 0x14) = {};
   field<u32>(input, 0) &= ~0x2000u;
   field<u32>(input, 8) = 0;
  }
  if (!(burst >= 0)) {
   resume = time + 1.f;
   burst = random_function(true) + 2.f;
   paused = true;
  }
 }
 if (void* current = field<void*>(actor, 0x378); current && busy_function(current)) return true;
 point target{};
 target = *position_function(destination, nullptr, &target);
 const point last = field<point>(actor, 0x3ac);
 // det_math, not std::pow: lockstep peers must round identically on every CPU (docs/npc-semi-auto.md).
 const double keep = fired ? det_math::pow_0_15(double(dt)) : det_math::pow_0_25(double(dt));
 const float k = float(1.0 - keep);
 point aim{float(float(target.x - last.x) * k) + last.x, float(float(target.y - last.y) * k) + last.y,
           float(float(target.z - last.z) * k) + last.z};
 if (paused && (w.flags & flag_rapidfire)) {
  point self{};
  self = *position_function(actor, nullptr, &self);
  aim = {(aim.x - self.x) * .25f + self.x, (aim.y - self.y) * .25f + self.y, (aim.z - self.z) * .25f + self.z};
 }
 aim_function(brain, &aim);
 point self{};
 self = *position_function(actor, nullptr, &self);
 point ground = field<point>(actor, 0x3ac);
 ground = {ground.x - self.x, ground.y - self.y, 0.f};
 const float length = std::sqrt(ground.x * ground.x + ground.y * ground.y);
 if (!(length > 10.f)) return true;
 ground = {ground.x / length, ground.y / length, 0.f};
 point angles{};
 angles = *angles_function(&angles, &ground);
 turn_function(ai, &field<point>(actor, 0x20), &angles);
 return true;
}

// Empty near-target query, other weapon types (0xefc4a..0xf02a6; revision 8 ported it for the
// officer without its warm-up and continuous parts). Strafe flag 0x2000; unless a started
// warm-up is still running, turn toward the destination (behind: end the burst; off-axis:
// 0.02 rad toward it; on-axis: a continuous beam sweeps 0.015 rad per update in the last
// turn direction). A spent burst pauses (first 0.3 s defer, then the pause timer) once the
// warm-up deadline is 0.4 s old, then a new burst of bursttime * (1 + rand/2). Otherwise fire
// is held (a warm-up ending within 0.4 s stops the strafe and movement), the victim is
// noticed and the burst runs down; when it is spent the pause is 1.2 + 0.8 * other attackers
// + 1.5 * rand, halved against a non-player target.
bool other_branch(void* brain, void* input, void* ai, void* actor, const weapon& w, void* target, void* destination,
                  const state* s)
{
 field<u32>(input, 0) |= 0x2000;
 const float time = now();
 const float warm = s ? s->warm : 0.f;
 if (warm == 0 || time > warm) {
  point direction = field<point>(destination, 0x20), origin = field<point>(actor, 0x20);
  direction.x -= origin.x; direction.y -= origin.y; direction.z -= origin.z;
  const float distance = std::sqrt(direction.x*direction.x + direction.y*direction.y + direction.z*direction.z);
  if (distance > 0) {
   direction.x /= distance; direction.y /= distance; direction.z /= distance;
   point angles = field<point>(actor, 0x2c);
   const auto yaw = det_math::sincos(angles.y), roll = det_math::sincos(angles.z); // deterministic, not the CRT's
   const float cy = yaw.c, sy = yaw.s, cz = roll.c, sz = roll.s;
   const float dot = direction.x*cy*cz + direction.y*cy*sz - direction.z*sy;
   if (!(dot > -.707f)) field<float>(brain, 0x1c) = -1.f;
   else if (!(dot > .999f)) {
    const float side = -direction.x*sz + direction.y*cz;
    const int sign = side <= 0 ? -1 : 1;
    // 0x120a30's right vector is opposite the conventional left vector.
    angles.z += sign * .02f;
    rotate_function(ai, &angles);
    field<int>(brain, 0x28) = sign;
   } else if (continuous(w)) {
    angles.z += float(field<int>(brain, 0x28)) * .015f;
    rotate_function(ai, &angles);
   }
  }
 }
 const float dt = delta_function();
 float& burst_left = field<float>(brain, 0x1c);
 float& pause_left = field<float>(brain, 0x20);
 float& pause_initial = field<float>(brain, 0x24);
 // The original compares the unrounded now - 0.4 and warm - now (x87) with float operands.
 if (!(burst_left >= 0) && !(double(time) - double(.4f) < double(warm))) {
  if (pause_left == pause_initial) {
   delay_function(ai, .3f);
   pause_left -= .3f;
   field<u32>(input, 0) &= ~0x2000u;
   return true;
  }
  pause_left -= dt;
  if (!(pause_left <= 0)) return true;
  const float burst = std::bit_cast<float>(w.burst_bits);
  burst_left = burst + random_function(true) * burst * .5f;
  return true;
 }
 if (warm != 0 && double(warm) - double(time) < double(.4f)) {
  field<u32>(input, 0) &= ~0x2000u;
  field<u32>(input, 8) = 0;
  field<u32>(ai, 0x270) = 0xffffffffu; // original ai+0x244
 }
 burst_left -= dt;
 field<u32>(input, 0) |= 0x90;
 field<u32>(input, 4) |= 0x90;
 point offset = field<point>(destination, 0x20), origin = field<point>(actor, 0x20);
 offset.x -= origin.x; offset.y -= origin.y; offset.z -= origin.z;
 const float distance = std::sqrt(offset.x*offset.x + offset.y*offset.y + offset.z*offset.z);
 if (void* victim_ai = native_ai(target)) {
  attack_notice notice{}; notice.words[0] = 1; notice.words[1] = 30;
  notice.words[8] = std::bit_cast<u32>(distance);
  notice_function(victim_ai, 0.f, 3, field<u32>(actor, 0x1c), notice, 0, 0);
  alert_function(ai, 1.f, 300.f, field<u32>(ai, 0x18));
 }
 if (!(burst_left <= 0)) return true;
 if (void* victim_ai = native_ai(target)) {
  unsigned others = 0;
  for (unsigned i = 0; i != 5; ++i) {
   const u32 other = field<u32>(victim_ai, 0x18c + 8*i);
   if (other && other != field<u32>(actor, 0x1c)) ++others;
  }
  pause_left = 1.2f + .8f * others + 1.5f * random_function(true);
  if (!(field<u32>(target, 0x3d8) & 1)) pause_left *= .5f;
 } else pause_left = 1.f;
 pause_initial = pause_left;
 return true;
}

// Called at the native ranged brain's continuation for an empty near-target query
// (0x51a359; the query 0x50a250 at 0x51a26e corresponds to the original 0x103850, and a
// found entity runs the native held-fire branch 0x51a284..0x51a356 = original
// 0xef8df..0xef981). The original runs its empty-query branches here (0xef98d): clear
// the actor's bit 2, then the bullet branch 0xef9bc or the other-type branch 0xefc4a.
// Revision 9/10 had placed the held-fire branch here; no measured window reached it.
bool __cdecl weapon_decision(void* brain, void* input) noexcept
{
 det_math::fp_env fp;
 void* ai = field<void*>(brain, 4);
 void* actor = field<void*>(ai, 0x10);
 const auto* w = npc_spec(actor);
 if (!w) return false;
 field<unsigned char>(actor, 0x764) &= ~4;
 void *self = nullptr, *target = nullptr, *entity = nullptr;
 if (!target_function(brain, field<u32>(actor, 0x1c), &self, &target, &entity)) return true;
 void* destination = target ? target : entity;
 if (!destination || self != actor) return true;
 if (bullet(*w)) return bullet_branch(brain, input, ai, actor, *w, destination);
 return other_branch(brain, input, ai, actor, *w, target, destination, find(actor, field<u32>(actor, 0x1c)));
}
