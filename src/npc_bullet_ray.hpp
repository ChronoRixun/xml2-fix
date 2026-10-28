#pragma once
// Included in npc_semi_auto's private namespace. XML1 character bullets, not the
// scanner's orientation-only aim. All entry points that do arithmetic fix both
// floating-point environments, including calls into the engine's matrix code.
struct bullet_frame { point muzzle{}, forward{}; };
struct bullet_segment { point from{}, to{}, extent{}; };
struct bullet_contact { void* entity = nullptr; point position{}, normal{}; bool found = false; };
using bullet_matrix = std::array<float, 16>;
template<class T> T bullet_virtual(void* self, unsigned slot)
{ return reinterpret_cast<T>((*static_cast<void***>(self))[slot / 4]); }
template<class T> T bullet_import(u32 address) { return *reinterpret_cast<T*>(address); }
struct bullet_transform_api {
 void(__thiscall* identity)(void*);
 void(__thiscall* rotation)(void*, float, float, float);
 void(__thiscall* translation)(void*, const point*);
 void(__thiscall* multiply)(void*, const void*, const void*);
 bool(__cdecl* bolt)(void*, int, void*, bool, bool);
};
bullet_transform_api native_bullet_transforms()
{
 return {bullet_import<decltype(bullet_transform_api::identity)>(0x67fa28),
         bullet_import<decltype(bullet_transform_api::rotation)>(0x67f9dc),
         bullet_import<decltype(bullet_transform_api::translation)>(0x67fa04),
         bullet_import<decltype(bullet_transform_api::multiply)>(0x67f8c0),
         reinterpret_cast<decltype(bullet_transform_api::bolt)>(0x5692e0)};
}
auto bullet_transforms_function = &native_bullet_transforms;

// Weapon emitter zero: actor's first bolton (+0x7a8, XBE +0x590), Gun1 in that
// model, carried by the actor's weapon bolt frame. The converted event already
// contains the source actorbolt index. PC 0x4e6e21..0x4e6ebe uses this same slot,
// model resource +0x30 and matrix composition for a named bolton bolt.
void __cdecl native_bullet_frame(void* actor, void* beam_frame, bullet_frame* out)
{
 det_math::fp_env fp;
 const auto api = bullet_transforms_function();
 bullet_matrix world{};
 api.identity(world.data());
 const auto angles = field<point>(actor, 0x2c);
 api.rotation(world.data(), angles.x, angles.y, angles.z);
 api.translation(world.data(), &field<point>(actor, 0x20));
 using model_t = void*(__thiscall*)(void*);
 void* model_ref = bullet_virtual<model_t>(actor, 0x188)(actor);
 void* gun = nullptr;
 if (model_ref && *static_cast<void**>(model_ref) &&
     bullet_virtual<model_t>(*static_cast<void**>(model_ref), 0x14)(*static_cast<void**>(model_ref))) {
  gun = field<void*>(actor, 0x7a8);
  const auto* bolt = field<int*>(beam_frame, 0x224);
  if (!bolt || !api.bolt(model_ref, *bolt, world.data(), false, true))
   api.identity(world.data()); // XBE 0xb9fdc: failed actor bolt falls back to identity
 }
 if (gun) {
  void* resource = bullet_virtual<model_t>(gun, 8)(gun);
  bullet_matrix local{}; api.identity(local.data());
  using named_bolt_t = bool(__thiscall*)(void*, const char*, void*);
  if (resource && bullet_virtual<named_bolt_t>(resource, 0x30)(resource, "Gun1", local.data())) {
   api.multiply(local.data(), local.data(), world.data());
   world = local;
  }
 }
 *out = {{world[12], world[13], world[14]}, {world[0], world[1], world[2]}};
}
auto bullet_frame_function = &native_bullet_frame;

point bullet_normalize(point v)
{
 det_math::fp_env fp;
 // XBE 0x129c0: binary64 intermediate arithmetic, binary32 components.
 const double length = std::sqrt((double(v.x) * v.x + double(v.y) * v.y) + double(v.z) * v.z);
 const double inverse = 1.0 / length;
 return {float(double(v.x) * inverse), float(double(v.y) * inverse), float(double(v.z) * inverse)};
}
point bullet_dot_adjust(point direction, const point& forward)
{
 det_math::fp_env fp;
 const double dot = (double(direction.y) * forward.y + double(forward.z) * direction.z) +
                    double(direction.x) * forward.x;
 const float blend = float(dot);
 if (dot < double(.9f)) {
  direction = {float((double(direction.x) - forward.x) * blend + forward.x),
               float((double(direction.y) - forward.y) * blend + forward.y),
               float((double(direction.z) - forward.z) * blend + forward.z)};
 }
 return direction;
}
point bullet_direction(const point& cached, const bullet_frame& frame, u32 accuracy_bits,
                       const std::array<double, 3>& random)
{
 det_math::fp_env fp;
 // 0xba0f3..0xba10d subtracts 36 when projectile speed is ordered <= 0;
 // converted bullets have no projectile speed. The aim cache is actor +0x3ac.
 const auto aim = bullet_normalize({cached.x - frame.muzzle.x, cached.y - frame.muzzle.y,
                                  float((double(cached.z) - 36.0) - frame.muzzle.z)});
 // XML1 parser 0xbc3e9..0xbc3f4: this exact widened coefficient, then a float.
 const float radians = float(double(std::bit_cast<float>(accuracy_bits)) * 0.01745329238474369);
 const float spread = det_math::sincos(radians).s, low = -spread;
 const auto draw = [=](double r) { return double(low) + r * (double(spread) - low); };
 // The source evaluates the Z, Y, X offsets in that order.
 const float z = float(draw(random[0]));
 const float y = float(draw(random[1]));
 point direction = bullet_normalize({float(double(aim.x) + draw(random[2])),
                                    float(double(aim.y) + y), float(double(aim.z) + z)});
 // XBE 0xba195..0xba1c8 blends only below .9; equality and unordered skip.
 // Lerp toward aim from the muzzle forward, by dot, without normalizing.
 return bullet_dot_adjust(direction, frame.forward);
}
bullet_segment bullet_endpoints(const bullet_frame& frame, const point& direction, u32 range_bits)
{
 det_math::fp_env fp;
 const float range = std::bit_cast<float>(range_bits);
 // Both scale results are stored before addition in XBE 0xba5ba..0xba674.
 const point back{direction.x * -36.f, direction.y * -36.f, direction.z * -36.f};
 const point ahead{direction.x * range, direction.y * range, direction.z * range};
 // Z's backward product stays in ST0 through its addition in the source.
 return {{frame.muzzle.x + back.x, frame.muzzle.y + back.y, float(double(direction.z) * -36.0 + frame.muzzle.z)},
         {frame.muzzle.x + ahead.x, frame.muzzle.y + ahead.y, frame.muzzle.z + ahead.z}, {0, 0, 0}};
}

// Reuse the turret adapter's QueryRef/OrderRef/TOI-contact protocol through
// virtual methods. Never address a retail bitmap or order slot: EntityCapacity
// replaces these same functions/slots for its 1024-entity layout.
using bullet_query_t = void(__thiscall*)(void**, const void*, const u32*);
using bullet_release_t = void(__thiscall*)(void**);
using bullet_order_t = void*(__cdecl*)(void*, const void*);
using bullet_order_release_t = void(__cdecl*)(void*);
auto bullet_query_function = reinterpret_cast<bullet_query_t>(0x4ab6d0);
auto bullet_release_function = reinterpret_cast<bullet_release_t>(0x4ab720);
auto bullet_order_function = reinterpret_cast<bullet_order_t>(0x4ab770);
auto bullet_order_release_function = reinterpret_cast<bullet_order_release_t>(0x4ab7d0);
auto bullet_pool_function = &mold_query::pools_ready;
auto bullet_class_function = &mold_query::is_a;
struct bullet_query_scope {
 void* query = nullptr; void* order = nullptr;
 ~bullet_query_scope() {
  if (order) bullet_order_release_function(order);
  if (query) bullet_release_function(&query);
 }
};
bool __cdecl native_bullet_friendly()
{
 // The source debug flag 0x485840 is PC's friendlyfire at 0x70b6fd
 // (console registration 0x46b91b..0x46b92f). The game mode also bypasses
 // team filtering (XBE game +0x54, PC game +0x5c, 0x44fe33..0x44fe41).
 if (*reinterpret_cast<const unsigned char*>(0x70b6fd)) return true;
 auto* game = reinterpret_cast<void*(__cdecl*)()>(0x48de40)();
 return bullet_virtual<bool(__thiscall*)(void*)>(game, 0x5c)(game);
}
auto bullet_friendly_function = &native_bullet_friendly;
auto bullet_team_function = reinterpret_cast<int(__thiscall*)(void*)>(0x41a220);
auto bullet_copy_query_function = reinterpret_cast<void(__thiscall*)(void**, void**)>(0x4ab610);
bool bullet_source_filter(void* actor, void* query)
{
 if (bullet_friendly_function()) return true;
 using filter_t = void(__thiscall*)(void*, int, int);
 const int team = bullet_team_function(actor);
 if (team != 0x1c) bullet_virtual<filter_t>(query, 0x40)(query, 1, team);
 // XBE 0x599e4..0x59a2d: remove neutral actors only, not neutral props.
 // PC's 0x44fe10 adds an XML2-only team-23 filter; do not call that helper.
 if (!bullet_pool_function()) return false;
 bullet_query_scope neutral;
 bullet_copy_query_function(&neutral.query, &query);
 if (!neutral.query) return false;
 using class_t = void(__thiscall*)(void*, int, const void*);
 bullet_virtual<class_t>(neutral.query, 0x54)(neutral.query, 2, reinterpret_cast<const void*>(0x70b83c));
 bullet_virtual<filter_t>(neutral.query, 0x40)(neutral.query, 2, 0x1c);
 bullet_virtual<void(__thiscall*)(void*, int, void**)>(query, 0x34)(query, 1, &neutral.query);
 return true;
}
bullet_contact first_bullet_contact(void* actor, const bullet_segment& segment)
{
 det_math::fp_env fp;
 bullet_contact hit;
 if (!bullet_pool_function()) return hit;
 bullet_query_scope q;
 const auto token = field<u32>(actor, 0x1c);
 bullet_query_function(&q.query, reinterpret_cast<const void*>(0x753f64), &token);
 if (!q.query || !bullet_source_filter(actor, q.query)) return hit;
 using filter_t = void(__thiscall*)(void*, int, int);
 bullet_virtual<filter_t>(q.query, 0x40)(q.query, 1, 1);
 bullet_virtual<filter_t>(q.query, 0x40)(q.query, 2, 0xe); // XML1 0xd -> PC 0xe
 using segment_t = void(__thiscall*)(void*, const point*, const point*, const point*);
 bullet_virtual<segment_t>(q.query, 0x74)(q.query, &segment.from, &segment.to, &segment.extent);
 struct { u32 vtable; void* query; } metric{0x68d684, q.query};
 q.order = bullet_order_function(q.query, &metric);
 if (!q.order || bullet_virtual<int(__thiscall*)(void*)>(q.order, 0)(q.order) < 1) return hit;
 hit.entity = bullet_virtual<void*(__thiscall*)(void*, int)>(q.order, 4)(q.order, 0);
 void* record = nullptr;
 bullet_virtual<void*(__thiscall*)(void*, void**, int)>(q.order, 8)(q.order, &record, 0);
 // First contact only, including a world contact with no entity. Never scan
 // past a wall/prop to find an actor, and never add the turret's visibility ray.
 if (record) {
  hit.position = field<point>(record, 0x30); hit.normal = field<point>(record, 0x20); hit.found = true;
 }
 return hit;
}
auto bullet_contact_function = &first_bullet_contact;
void __cdecl native_bullet_take_hit(void* entity, void* attack)
{ bullet_virtual<void(__thiscall*)(void*, void*, u32, u32, u32)>(entity, 0xb0)(entity, attack, 1, 1, 1); }
auto bullet_take_hit_function = &native_bullet_take_hit;
