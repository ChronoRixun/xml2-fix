#pragma once

// Phase A entity capacity: the native caller frames that keep a 256-bit entity set on their stack (or in a
// non-owner object) and pass it into the World/Grid/Query set-taking slots, replaced whole so the entity domain can
// grow from 256 to 1024 World handles (docs/entity-capacity-phase-a-plan.md section 2, patch-spec "physics" and
// "callers" sites). Ported from the retail XMen2.exe (sha1 7d95cdb4a9a599b982147a3389ea7aff211cb82b, image base
// 0x400000) listings of
//   CCollide (vtable 0x685660, getter 0x44cd20, static 0x717988): 0x44cd70 0x44ce70 0x44d170 0x44d360 and the
//   per-shape trace callbacks 0x44dca0 0x44e140 0x44e210 0x44e440 0x44e4d0 0x44e590 0x44e6a0, the swept-box
//   candidate query 0x44e7d0, and the fourth vtable slot 0x44ea40 (see the note below);
//   navigation (CPathGrid, vtable 0x689c2c, static 0x72c9e8): 0x491210 0x493330;
//   the named-entity query caller 0x4cd350.
// Each function keeps the native calling convention (a __thiscall is spelled `__fastcall(self, void* edx, ...)`
// as the contract header says), argument list, stack cleanup (the observed RET n) and return register. Every
// eight-dword set becomes a 128-byte EntitySet, every 0x100 loop bound becomes descriptor.world and every
// `token & 0xff` becomes descriptor.tokens.index(); everything else - geometry, x87 compare semantics, the order
// of every call into the Query/World/Grid vtables (through the getters), the borrowed CCollide+0xd4 set pointer,
// the 50-entry trace pool reached through the CCollisions filter, the 50-entry named-query output - is the
// native behaviour. Helpers that are capacity-independent (igVec3f/igMatrix44f imports, the box/sphere tests,
// the trace record ctor/fill/commit, the context ctor 0x44d940 whose eight-dword clears are widened after the
// call, the global name table, strncpy) are called by address, never re-implemented.
//
// Nothing here is game content: retail addresses, field offsets and decompiled control flow only.

#include "entity_capacity_layout.hpp"
#include <cstddef>
#include <cstdint>

namespace entity_frames
{
	using entity_capacity_layout::descriptor;
	using entity_capacity_layout::EntitySet;

	// ---- the layout the frames run against ------------------------------------------------------------------
	// Stock until the integrator publishes the installed descriptor (entity_capacity::after_commit); the tests set
	// it per scenario. Never read from retail constants.
	void set_descriptor(const descriptor* installed); // nullptr restores the stock descriptor
	const descriptor& current_descriptor();

	// ---- retail addresses this port depends on (not in the shared header; see the report) ----------------------
	constexpr std::uint32_t native_collide_getter = 0x44cd20;    // __cdecl() -> CCollide* (vtable 0x685660)
	constexpr std::uint32_t native_collide_vtable = 0x685660;    // five slots: 44cd70 44ce70 44d170 44ea40 44d360
	constexpr std::uint32_t native_collide_object = 0x717988;
	constexpr std::uint32_t native_entity_of_dense = 0x45feb0;   // __cdecl(uint32 dense) -> CEntity* ([0x718430] World slot 0x10)
	constexpr std::uint32_t native_player_handle = 0x45fe70;     // __cdecl(uint32* out) -> out ([0x718430] World slot 0x24)
	constexpr std::uint32_t native_entity_of_handle = 0x4654b0;  // __thiscall(uint32* handle) -> CEntity* (World slot 0xc)
	constexpr std::uint32_t native_handle_valid = 0x4654d0;      // __thiscall(uint32* handle) -> bool (World slot 0x14)
	constexpr std::uint32_t native_box_overlaps = 0x44d010;      // __thiscall(const float bounds[6]; const float box[6]) -> bool
	constexpr std::uint32_t native_box_inside = 0x44d070;        // __thiscall(const float bounds[6]; const float box[6]) -> bool
	constexpr std::uint32_t native_sphere_hits_box = 0x563160;   // __thiscall(const float sphere[4]; const float bounds[6]) -> bool
	constexpr std::uint32_t native_sphere_hits_box_exact = 0x5631d0;
	constexpr std::uint32_t native_context_ctor = 0x44d940;      // __thiscall(ctx; entity, start, end, extents, set) RET 0x14 -> ctx
	constexpr std::uint32_t native_record_ctor = 0x44d8f0;       // __thiscall(record) -> record
	constexpr std::uint32_t native_record_fill = 0x44d780;       // __thiscall(record; entity) RET 4 -> bool
	constexpr std::uint32_t native_context_max = 0x44d720;       // __thiscall(ctx; float out[3]) RET 4 -> out
	constexpr std::uint32_t native_context_min = 0x44d6c0;       // __thiscall(ctx; float out[3]) RET 4 -> out
	constexpr std::uint32_t native_commit_hit = 0x44de70;        // __thiscall(ctx; record, filter, flag) RET 0xc
	constexpr std::uint32_t native_shared_iterator = 0x44d0d0;   // NOT patched, NOT called: other pools share it (plan section 2)
	constexpr std::uint32_t native_capsule_sweep = 0x600dc0;     // __cdecl(ctx+0xf0)
	constexpr std::uint32_t native_capsule_sweep_ratio = 0x601280; // __cdecl(ctx+0xf0)
	constexpr std::uint32_t native_physics_world = 0x56c7f0;     // __cdecl() -> object whose slot 0x10 sweeps a box
	constexpr std::uint32_t native_nav_component = 0x4240f0;     // __cdecl(entity) -> component or null
	constexpr std::uint32_t native_nav_component_b = 0x41e4d0;   // __cdecl(entity) -> component or null
	constexpr std::uint32_t native_nav_reachable = 0x4c2590;     // __thiscall(component; const float point[3]) RET 4 -> bool
	constexpr std::uint32_t native_pathgrid_box = 0x490a80;      // __thiscall(grid; const uint8* cell, float lo[3], float hi[3]) RET 0xc
	constexpr std::uint32_t native_pathgrid_center = 0x490980;   // __thiscall(grid; const uint8* cell, float out[3]) RET 8
	constexpr std::uint32_t native_pathgrid_cell = 0x492450;     // __thiscall(grid; uint32 x, uint32 y) RET 8 -> uint8* cell
	constexpr std::uint32_t native_pathgrid_cell_b = 0x493290;   // __thiscall(grid; int x, int y) RET 8 -> uint8* cell
	constexpr std::uint32_t native_name_table = 0x602140;        // __cdecl() -> table base
	constexpr std::uint32_t native_snprintf = 0x5604f0;          // __cdecl(char* out, int size, const char* format, ...)
	constexpr std::uint32_t native_game_object = 0x46dce0;       // __cdecl() -> object; slot 0x268 -> difficulty
	constexpr std::uint32_t native_actor_set_state = 0x4749a0;   // __thiscall(actor; int) RET 4
	constexpr std::uint32_t native_actor_timing = 0x4198d0;      // __thiscall(actor; float, float) RET 8
	constexpr std::uint32_t native_actor_order = 0x41d3e0;       // __thiscall(actor; int) RET 4
	constexpr std::uint32_t native_actor_flag = 0x41a1f0;        // __thiscall(actor; int bit) RET 4
	constexpr std::uint32_t iat_vec_set3 = 0x67fa1c;             // libIGMath igVec3f::set(float, float, float)
	constexpr std::uint32_t iat_vec_set = 0x67fa64;              // igVec3f::set(const igVec3f&)
	constexpr std::uint32_t iat_vec_length = 0x67fa50;           // igVec3f::length() -> ST0
	constexpr std::uint32_t iat_vec_length2 = 0x67fa58;          // igVec3f::length2() -> ST0
	constexpr std::uint32_t iat_vec_make_min = 0x67f8cc;         // igVec3f::makeMin(const igVec3f&)
	constexpr std::uint32_t iat_vec_make_max = 0x67f8c8;         // igVec3f::makeMax(const igVec3f&)
	constexpr std::uint32_t iat_vec_scale = 0x67f9f8;            // igVec3f::operator*(const float&) const -> hidden result
	constexpr std::uint32_t iat_vec_sub = 0x67fa0c;              // igVec3f::operator-(const igVec3f&) const -> hidden result
	constexpr std::uint32_t iat_vec_assign = 0x67fa2c;           // igVec3f::operator=(const igVec3f&)
	constexpr std::uint32_t iat_vec_index = 0x67f9c4;            // igVec3f::operator[](int) -> float*
	constexpr std::uint32_t iat_vec_ctor = 0x67fa60;             // igVec3f::igVec3f()
	constexpr std::uint32_t iat_vec_transform = 0x67f908;        // igVec3f::transformVector(const igVec3f&, const igMatrix44f&)
	constexpr std::uint32_t iat_mat_identity = 0x67fa28;         // igMatrix44f::makeIdentity()
	constexpr std::uint32_t iat_mat_pre_scale = 0x67f8c4;        // igMatrix44f::preScale(const igVec3f&)
	constexpr std::uint32_t iat_mat_translation = 0x67fa04;      // igMatrix44f::setTranslation(const igVec3f&)
	constexpr std::uint32_t iat_strncpy = 0x67f148;              // MSVCR71 strncpy
	constexpr std::uint32_t const_zero = 0x680030;               // 0.0f
	constexpr std::uint32_t const_one = 0x685658;                // 1.0f (box inflation / radius floor)
	constexpr std::uint32_t const_ratio_threshold = 0x685678;    // 0.09f
	constexpr std::uint32_t const_epsilon = 0x6819cc;            // 1e-8f
	constexpr std::uint32_t const_half = 0x680488;               // 0.5f
	constexpr std::uint32_t const_quarter = 0x6819a0;            // 0.25f
	constexpr std::uint32_t const_nav_speed2 = 0x687460;         // 5e-7f
	constexpr std::uint32_t const_nav_distance2 = 0x680510;      // 800.0f
	constexpr std::uint32_t const_four = 0x68fb68;               // 4.0f
	constexpr std::uint32_t counter_bad_capsule = 0x7179a0;      // saturating diagnostic counters (0x44dca0 / 0x44d780 / 0x44d940)
	constexpr std::uint32_t class_id_a = 0x72a0ac;               // int cells: class-descriptor flag ids (+0x24 -> descriptor+0x14 bitmap)
	constexpr std::uint32_t class_id_b = 0x70b840;
	constexpr std::uint32_t class_id_c = 0x72bac8;
	constexpr std::uint32_t class_id_d = 0x70b67c;
	constexpr std::uint32_t class_id_e = 0x753f68;
	constexpr std::uint32_t danger_room_flag = 0x782728;         // byte
	constexpr std::uint32_t danger_room_courses = 0x784e00;      // 0x78-byte course records
	constexpr std::uint32_t string_empty = 0x681968;             // ""
	constexpr std::uint32_t format_name_index = 0x68fcc4;        // "%s%d"
	constexpr std::uint32_t format_name_zero_index = 0x68fcbc;   // "%s0%d"
	constexpr std::uint32_t collide_context_bytes = 0x17c, trace_record_bytes = 0xbc;
	constexpr std::uint32_t named_output_capacity = 50, named_output_count = 0xcc; // the 4cd350 output collection stays 50
	constexpr std::uint32_t pathgrid_scratch_set = 0x20b20, pathgrid_scratch_byte = 0x20b40; // retail CPathGrid fields, left as laid out

	// ---- owner-keyed navigation scratch (plan section 2) ------------------------------------------------------
	// 0x493330 handed CPathGrid+0x20b20 (a 32-byte set) and the adjacent byte +0x20b40 to 0x491210. The object keeps
	// its retail layout; the wide result set and the output byte live here, keyed by the CPathGrid address. The
	// scratch is transient (filled and consumed inside one 0x493330 call; no other reader of +0x20b20/+0x20b40
	// exists in the retail code), so a full table recycles its oldest slot.
	struct NavigationScratch
	{
		const void* owner = nullptr;
		EntitySet set{};
		std::uint8_t output = 0;
	};
	constexpr std::size_t navigation_scratch_slots = 16;
	NavigationScratch& navigation_scratch(const void* owner);

	// ---- helpers shared with the tests ------------------------------------------------------------------------
	// The exact FPU-compare results the frames branch on (documented per site in entity_frames.cpp).
	bool capsule_ratio_below_threshold(float a, float a_scale, float b, float b_scale, float threshold); // 0x44e2c2..0x44e2e5
	void add_quarter_scaled(float* target, float delta);      // 0x4912ab: *target = delta * 0.25 + *target (x87)
	void subtract_quarter_scaled(float* target, float delta); // 0x4912c4: *target = *target - delta * 0.25 (x87)
}

namespace entity_capacity
{
	using entity_capacity_layout::EntitySet;

	// CCollide slot 0 (0x44cd70, RET 0xc): set &= grid cells(lo, hi); returns the grid's result (AL), set zeroed on false.
	std::uint32_t __fastcall Physics_0044cd70(void* self, void* edx, const float* lo, const float* hi, EntitySet* set);
	// CCollide slot 1 (0x44ce70, RET 0x10): candidates = cells(lo, hi) & *set; *set = those whose collision bounds
	// overlap (overlap != 0) or lie inside (overlap == 0) the box; returns the count.
	std::uint32_t __fastcall Physics_0044ce70(void* self, void* edx, const float* lo, const float* hi, EntitySet* set, std::uint8_t overlap);
	// CCollide slot 2 (0x44d170, RET 0x10): the sphere query (center, radius) over cells(center-r, center+r) & *set.
	std::uint32_t __fastcall Physics_0044d170(void* self, void* edx, const float* center, float radius, EntitySet* set, std::uint8_t exact);
	// CCollide slot 4 (0x44d360, RET 0x34): the swept trace. Builds the collide context (0x44d940) over *set, tests
	// the player entity first when asked, collects the swept-box candidates (0x44e7d0) and runs the per-shape
	// callbacks below over the surviving set; *result = the best hit token (ctx+0xe0); returns result.
	std::uint32_t* __fastcall Physics_0044d360(void* self, void* edx, std::uint32_t* result, const float* start, const float* end, const float* extents,
	                                           std::uint32_t a5, std::uint32_t a6, std::uint8_t a7, std::uint8_t a8, EntitySet* set, void* filter,
	                                           std::uint8_t force_player_test, std::uint8_t scale_direction, void* entity);
	// CCollide slot 3 (0x44ea40, RET 0x18): NOT one of the plan's enumerated frames (see the report): context over
	// *set, then the swept-box candidate query; returns ctx+0xe0.
	std::uint32_t __fastcall Physics_0044ea40(void* self, void* edx, const float* start, const float* end, const float* extents, EntitySet* set, void* entity, void* unused);
	// Per-shape trace callbacks (RET 8 each): (context; record, filter). On a miss the record's entity is cleared
	// from the borrowed context set (*(ctx+0xd4)) by its token's dense index.
	void __fastcall Physics_0044dca0(void* context, void* edx, void* record, void* filter); // capsule vs sphere
	void __fastcall Physics_0044e140(void* context, void* edx, void* record, void* filter); // scaled box through the physics world
	void __fastcall Physics_0044e210(void* context, void* edx, void* record, void* filter); // capsule vs capsule (ratio-selected sweep)
	void __fastcall Physics_0044e440(void* context, void* edx, void* record, void* filter); // record model, context box
	void __fastcall Physics_0044e4d0(void* context, void* edx, void* record, void* filter); // context model, record box (reversed sweep)
	void __fastcall Physics_0044e590(void* context, void* edx, void* record, void* filter); // record model vs context entity bounds
	void __fastcall Physics_0044e6a0(void* context, void* edx, void* record, void* filter); // context model vs record entity bounds
	// Swept-box candidate query (0x44e7d0, RET 0x10, four stack arguments, ECX unused): *set &= cells(box swept by
	// `sweep`, inflated by one unit) filtered by bounds overlap; returns the count.
	std::uint32_t __fastcall Physics_0044e7d0(void* ecx, void* edx, const float* lo, const float* hi, EntitySet* set, const float* sweep);

	// Navigation (0x491210, RET 0x18): the cell's candidate query into a 128-byte result set, pruned per entity;
	// returns the number of blocking entities. (0x493330, RET 0xc): the cell classification that calls it with the
	// owner-keyed scratch; returns the layer's flag byte (AL).
	std::uint32_t __fastcall EntitySetCaller_00491210(void* grid, void* edx, const std::uint8_t* cell, std::uint32_t x, std::uint32_t y, std::uint32_t layer, EntitySet* set, std::uint8_t* output);
	std::uint32_t __fastcall EntitySetCaller_00493330(void* grid, void* edx, std::int32_t x, std::int32_t y, std::uint8_t height);

	// Named-entity query caller (0x4cd350, RET 0x14): collects up to 50 handles named <name>, <name>0..14 and
	// <name>00..014 through World slots 0x34/0x18 into `output` (count at +0xcc) and applies the order to them.
	void __fastcall EntitySetCaller_004cd350(void* self, void* edx, std::uint32_t* output, const std::uint32_t* name_token, std::uint8_t apply, std::int32_t order, std::uint8_t set_state);
}
