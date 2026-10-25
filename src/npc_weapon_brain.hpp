#pragma once

// Included inside npc_semi_auto's private namespace. The supported contract is
// a discrete non-bullet weapon without warm-up. The native ranged brain retains
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

void __cdecl prepare_brain(void* ai)
{
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
bool __cdecl preserve_weapon_range(void* ai, void* const* chains = nullptr)
{
 const auto* w = npc_spec(field<void*>(ai, 0x10));
 if (!w) return false;
 if (chains) {
  bool ordinary = false;
  u32 capabilities = 0;
  for (unsigned slot = 0; slot != 4; ++slot) {
   void* move = chains[5 + slot]; // native refresh's already resolved action table
   if (move && field<void*>(move, 0xc8) == &handler) {
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

bool __fastcall semi_chain(void* self, void*, void* actor, void* input)
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

bool __cdecl weapon_input(void* actor, void* input)
{
 if (!npc_spec(actor) || (field<u32>(input, 0) & 0x90) != 0x90) return false;
 if (field<unsigned char>(actor, 0x764) & 1) return true;
 void* current = field<void*>(actor, 0x378);
 if (!current) return true;
 const int animation = field<int>(current, 0xbc); // native getter 0x500840
 const u32 token = field<u32>(actor, 0x1c);
 const float time = now();
 // Original enums 0, 0x15, 0x86, 0x8c map to PC idle, crouch,
 // power1 and power7. Resolve the move by name, not a contextual power chain.
 if (animation != 0 && animation != 21 && animation != 115 && animation != 127) {
  request_function(actor, named_move(actor, "idle"), -1, -1.f, 0, nullptr, nullptr);
  states[timer_slot(token)] = {reinterpret_cast<std::uintptr_t>(actor), token, time, time + .05f};
  return true;
 }
 const auto* s = find(actor, token);
 if (!ready(s ? *s : state{}, reinterpret_cast<std::uintptr_t>(actor), token, time)) return true;
 void* move = named_move(actor, "power_attack");
 if (!move || field<void*>(move, 0xc8) != &handler) return true;
 request_function(actor, move, (field<unsigned char>(actor, 0x3d9) & 0x40) ? 127 : 115,
                  -1.f, 9, nullptr, nullptr);
 return true;
}

// XML2's attack-state update returns before the weapon brain while the actor is input
// locked in a flagged move (0x512f01..0x512f14: actor+0x764 bit 0 and actor+0x3d9 bit
// 0x10). The original attack-state update (0x10ad70) has no such exit: its weapon brain
// runs on every update, including during the grenade move, where it keeps power+fire
// held (S2 ring: tag 20 on ~every frame of the full-energy windows, input 0x90).
bool __cdecl attack_state_continues(void* actor) { return npc_spec(actor) != nullptr; }

// Called after the ranged brain's power decision (0x519fe4). The original decision
// (0xee260) returns true after pressing a power, so the weapon brain stops for that update
// (0xef5d2..0xef5ef: input+8 = 0, no fire bits; the owner's S2 ring shows the input word
// 0xa0 once at full energy, then 0x90). XML2's decision (0x518770) presses the same power
// bits but returns false, and the held-fire branch then turns the press into a gun shot
// or a heavy melee attack. Treat such a press as the original's true result.
bool __cdecl power_pressed(int native, void* brain, void* input)
{
 if (native & 0xff) return true;
 void* ai = field<void*>(brain, 4);
 if (!ai || !npc_spec(field<void*>(ai, 0x10))) return false;
 const u32 held = field<u32>(input, 0);
 return (held & 0x80) && (held & 0x170);
}

// Called at the native ranged brain's unresolved near-target continuation, after its
// distance (>75), range, capability, camera and actor-state checks have succeeded.
// Revision 9: the original officer reaches its held-fire branch here (0xef8df..0xef981):
// face the destination, hold power+fire on every update and aim at it. Its burst
// timers stay at the constructor values (0, 0.6, 100) for the whole engagement and fire
// is held on every weapon-brain update (owner's S2 ring: tag 24 0x90/0x90 = tag 20 count),
// so cadence comes only from the recoil gate. Revision 8's burst/pause branch (0xefc4a)
// is never executed by the officer and is no longer used.
bool __cdecl weapon_decision(void* brain, void* input)
{
 void* ai = field<void*>(brain, 4);
 void* actor = field<void*>(ai, 0x10);
 const auto* w = npc_spec(actor);
 if (!w) return false;
 void *self = nullptr, *target = nullptr, *entity = nullptr;
 if (!target_function(brain, field<u32>(actor, 0x1c), &self, &target, &entity)) return true;
 void* destination = target ? target : entity;
 if (!destination || self != actor) return true;
 point origin = field<point>(actor, 0x20), position = field<point>(destination, 0x20);
 point direction{position.x - origin.x, position.y - origin.y, 0.f};
 field<u32>(input, 8) = 0;
 if (!nav_function(ai, &direction)) return true;
 point angles{};
 angles = *angles_function(&angles, &direction);
 face_function(ai, &field<point>(actor, 0x20), &angles);
 field<u32>(input, 0) |= 0x90;
 field<u32>(input, 4) |= 0x90;
 point aim{};
 aim_function(brain, position_function(destination, nullptr, &aim));
 return true;
}
