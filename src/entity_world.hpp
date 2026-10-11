#pragma once

// Phase A entity capacity: the CWorld owner (native vtable 0x68f80c, 26 slots 0x0..0x64; static 0x778b70 of 0x9370
// bytes; code 0x4c6120-0x4c7f7c; getter 0x4c7f20, which the integrator replaces) ported into the DLL as a replacement
// object sized by the installed descriptor. One code path serves both capacities: with a world of 256 the object is
// the retail layout (so the differential test runs the retail code and this port side by side), with 1024 it is the
// wide layout of round6/layouts.json "world" (0x24b50 bytes): the handle core comes from
// entity_capacity_layout::world_core_for(n), the single source of truth for the core, and every other embedded
// container follows the native formula scaled to n (the native object is built from one array/pool/map template:
// an array is n elements plus a trailing dword, a pool is an array, a FIFO ring array with enqueue/dequeue/count,
// a bitmap and an active count, a generation pool adds n packed tokens plus mask and shift, and an ordered map is
// a vptr, root, last-index, a pool of 16-byte nodes and a value array; scaling that formula to 1024 reproduces the
// plan's offsets exactly).
//
// What the World is (as decompiled): the handle table (dense index <-> packed token <-> CEntity*), a name registry
// (ordered map keyed by the interned, case-folded name -> list of handles), a hierarchy name map (name -> a record of
// a parent name and up to four child names, applied when an entity registers), ONE relationship table (per entity
// a related token, and per token the list of entities related to it, with an enumeration cursor), a 16-byte property
// record per entity and 112 reciprocal property-to-entity membership sets.
//
// Lifecycle (plan section 3, binding): tokens are (generation << index_bits) | index; the constructor seeds every
// slot at generation 1; only generations 1..0x3fff are minted; a release advances the slot's generation, or retires
// the slot at 0x3fff, BEFORE it publishes a free-ring entry (never the native enqueue-before-advance order); a reset
// is a quiescent state in which allocation is refused and counted, occupied slots advance through destruction and
// every non-retired slot advances once more, then an ascending FIFO of every reusable slot is rebuilt. The 272-byte
// lifecycle sidecar (retired bitmap, destroying bitmap, counts, reset depth/flags) sits in the private header that
// precedes the object. At every observable boundary live + free + retired == capacity.
//
// Methods are `__fastcall` with an unused EDX parameter (see entity_capacity_layout.hpp): the frame is exactly the
// native __thiscall one, so the integrator registers `vtable.slots` as the object's vptr. Every native side effect,
// ordering, class filtering, early return and refusal is preserved (including the native quirks named in the .cpp).

#include "entity_capacity_layout.hpp"
#include <cstddef>
#include <cstdint>

namespace entity_world
{
	using entity_capacity_layout::descriptor;
	using entity_capacity_layout::DenseIndex;
	using entity_capacity_layout::EntitySet;
	using entity_capacity_layout::WorldToken;

	constexpr std::uint32_t property_count = 112;    // 0x70: the membership-set loop bound of 0x4c6990 (unchanged)
	constexpr std::uint32_t property_record_bytes = 16; // one 128-bit property record per entity (unchanged)
	constexpr std::uint32_t hierarchy_record_words = 5; // {parent name id, child name ids[4]} (20 bytes)
	constexpr std::uint32_t map_null = 0x3fffffff;      // the ratl ordered map's "no node" index
	constexpr std::uint32_t pool_none = 0xffffffffu;    // this port's "pool exhausted" answer (native reads a stale ring entry)

	// ---- object layout -----------------------------------------------------------------------------------------
	struct pool_layout
	{
		std::uint32_t elems = 0, elem_size = 0, ring = 0, enqueue = 0, dequeue = 0, count = 0, bitmap = 0, active = 0, size = 0;
	};
	struct genpool_layout
	{
		pool_layout pool;
		std::uint32_t gens = 0, mask = 0, shift = 0, size = 0;
	};
	struct map_layout
	{
		std::uint32_t base = 0, root = 0, last = 0;
		pool_layout nodes; // 16-byte nodes {parent, left, right, key}
		std::uint32_t values = 0, value_size = 0, size = 0;
	};
	struct relation_layout
	{
		std::uint32_t base = 0, heads = 0;
		genpool_layout nodes; // 8-byte nodes {value, next}
		std::uint32_t values = 0, current = 0, cursor = 0, dirty = 0, size = 0;
	};
	struct layout
	{
		std::uint32_t world = 0;      // 256 or 1024 (0: unsupported)
		std::uint32_t index_bits = 0; // 8 / 10
		std::uint32_t index_mask = 0; // 0xff / 0x3ff
		std::uint32_t set_words = 0;  // world / 32
		std::uint32_t set_bytes = 0;  // world / 8: one entity set (32 retail, 128 raised)
		descriptor::world_fields core{};  // the handle core (entity_capacity_layout::world_core_for)
		std::uint32_t core_live_bits_words = 0;
		map_layout names;             // retail +0xc44: name id -> head of a name-node list
		genpool_layout name_nodes;    // retail +0x248c: {handle, next}
		map_layout hierarchy;         // retail +0x34cc: name id -> 20-byte record
		std::uint32_t hierarchy_version = 0; // retail +0x5d14
		relation_layout relations;    // retail +0x5d18 (0x1854)
		std::uint32_t properties = 0; // retail +0x756c: 16 bytes x world
		std::uint32_t reserved = 0;   // retail +0x856c
		std::uint32_t sets = 0;       // retail +0x8570: 112 x set_bytes
		std::uint32_t size = 0;       // object bytes (0x9370 retail, 0x24b50 raised)
		constexpr bool valid() const { return world != 0; }
	};
	layout layout_for(std::uint32_t world);       // invalid() when the capacity is not 256 or 1024
	std::size_t object_size(const descriptor& d); // bytes of the object proper (0 when unsupported)

	// Allocates (VirtualAlloc, zero-filled; a private 0x1000-byte header with the layout, the lifecycle sidecar and
	// the telemetry precedes the object) and constructs the owner as the native constructor chain does (0x4c6730),
	// with every slot at generation 1. Returns the object pointer (first dword = vtable.slots) or nullptr.
	void* create(const descriptor& d);
	// Releases the backing (the native destructor 0x4c7ea0 has no observable work on the DLL object). Accepts nullptr.
	void destroy(void* object);
	const layout* layout_of(const void* object); // nullptr for a pointer `create` did not return

	// ---- lifecycle telemetry (plan section 0 P3.3, section 3) -------------------------------------------------
	struct status
	{
		bool available = false;
		std::uint32_t capacity = 0, live = 0, free_slots = 0, retired = 0;
		std::uint32_t max_generation = 0;            // largest generation present in the handle table (next to be minted)
		std::uint32_t refusals = 0;                  // allocation refusals: full ring + reset-time attempts + allocator null
		std::uint32_t full_refusals = 0;             // free ring empty
		std::uint32_t allocator_null_refusals = 0;   // the allocator returned null (native refusal, counts unchanged)
		std::uint32_t reset_time_attempts = 0;       // allocations attempted while resetting (refused)
		std::uint32_t reentrant_release_refusals = 0;
		std::uint32_t resets = 0;
		std::uint32_t retired_by_release = 0, retired_by_reset = 0, retired_by_test = 0;
		std::uint32_t name_map_full_refusals = 0;    // 0x4c7890's `!= capacity` refusal (the name node stays consumed, as native)
		std::uint32_t name_node_exhaustion_refusals = 0; // this port's refusal where native would read a stale ring entry
		std::uint32_t hierarchy_full_refusals = 0;   // 0x4c7c20's `active + 2 > capacity` refusal
		std::uint32_t relation_table_refusals = 0;   // a relationship slot called with a table index other than 0
		bool resetting = false;
		bool reset_failure_latched = false;          // an allocation was attempted during a reset
		bool delete_flag_seen = false;               // slot 0 was called with the delete flag (ignored: the backing is the DLL's)
	};
	status counters(const void* object);
	// Test-only fixture (plan P3.4): force-retire a FREE slot (not live, not retired) and remove it from the ring.
	bool retire_slot(void* object, std::uint32_t index);
	// The invariant at observable boundaries: live/free/retired disjoint, counts sum to the capacity, every reusable free
	// index in the ring exactly once, the ring words consistent, the sidecar counts equal to the core's.
	bool check_invariants(const void* object, char* why, std::size_t why_size);

	// Refusal logging: the World logs refusals directly through this callback (the integrator formats the line with
	// the allocator's actor/generic occupancy). Null (the default) logs nothing.
	enum class refusal_kind : std::uint8_t
	{
		allocation_full, allocation_during_reset, allocator_null, reentrant_release, name_map_full, name_nodes_exhausted,
		hierarchy_full, relation_table_index, delete_flag
	};
	struct refusal
	{
		refusal_kind kind;
		std::uint32_t size;  // the requested entity size for allocation refusals, else 0
		std::uint32_t token; // the token involved, else 0
		status world;        // the counters at the time of the refusal
	};
	using refusal_log_fn = void (*)(const refusal&);
	void set_refusal_logger(refusal_log_fn fn);
	refusal_log_fn refusal_logger();

	// ---- native dependencies -------------------------------------------------------------------------------------
	// Cross-owner calls go through the other owners' getters exactly as the native code does (Grid 0x465470 slots 0x0,
	// 0x24, 0x28; allocator 0x461910 slots 0xc, 0x10); the capacity-independent name helpers are called by address
	// (0x41aa80 name of an interned id, 0x41ab70 intern/hash a string, 0x5602b0 fold case in place). The native
	// World-getter round trips (0x4654b0 / 0x4654d0: getter -> slot 0xc / 0x14) are self calls through this object's
	// own vtable: the getter returns this object. Tests point these at fixture stand-ins.
	struct hooks
	{
		void*(__cdecl* grid_getter)();
		void*(__cdecl* allocator_getter)();
		const char*(__fastcall* name_of_id)(const std::uint32_t* id, void* edx);           // 0x41aa80, thiscall on the id field
		void(__fastcall* name_hash)(std::uint32_t* out, void* edx, const char* text);     // 0x41ab70, thiscall on the out word
		void(__cdecl* fold_case)(char* text);                                               // 0x5602b0
	};
	extern hooks native_hooks; // defaults: the retail addresses; mutable for tests
	hooks retail_hooks();

	// The DLL vtable, native slot order; the RTTI locator dword precedes the slots as in the retail image.
	struct vtable_image { const void* rtti_locator; const void* slots[26]; };
	extern const vtable_image vtable;
	constexpr std::uint32_t native_vtable = 0x68f80c, native_object = 0x778b70, native_rtti_locator = 0x6aefd4;
	constexpr std::uint32_t native_ctor = 0x4c6730, native_dtor = 0x4c7ea0, native_atexit_thunk = 0x67ddc0;
	constexpr std::uint32_t native_region_lo = 0x4c6120, native_region_hi = 0x4c7f7c;

	// ---- the 26 slots (native target, stack cleanup) -------------------------------------------------------------
	void* __fastcall scalar_deleting_destructor(void* self, void* edx, std::uint32_t flags);                      // 0x00 0x4c7f00 ret 4
	void __fastcall reset(void* self, void* edx);                                                                   // 0x04 0x4c7730 ret 0
	void* __fastcall entity_by_name(void* self, void* edx, const char* name);                                       // 0x08 0x4c6f20 ret 4
	void* __fastcall entity_by_token(void* self, void* edx, WorldToken token);                                      // 0x0c 0x4c6c20 ret 4
	void* __fastcall entity_by_dense(void* self, void* edx, DenseIndex index);                                      // 0x10 0x4c68d0 ret 4
	bool __fastcall token_valid(void* self, void* edx, WorldToken token);                                           // 0x14 0x4c6bc0 ret 4
	std::uint32_t* __fastcall handle_by_dense(void* self, void* edx, std::uint32_t* out, DenseIndex index);                 // 0x18 0x4c6910 ret 8
	std::uint32_t __fastcall index_of(void* self, void* edx, std::uint32_t token);                                  // 0x1c 0x4c6840 ret 4
	std::uint32_t* __fastcall hierarchy_version_get(void* self, void* edx, std::uint32_t* out);               // 0x24 0x4c6850 ret 4
	const std::uint32_t* __fastcall hierarchy_version_set(void* self, void* edx, const std::uint32_t* in);               // 0x28 0x4c6860 ret 4
	void* __fastcall entity_of(void* self, void* edx, WorldToken token);                                            // 0x20 0x4c6870 ret 4
	void __fastcall register_name(void* self, void* edx, const char* name, std::uint32_t token);                    // 0x2c 0x4c7890 ret 8
	void __fastcall unregister_name(void* self, void* edx, const char* name, std::uint32_t token);                  // 0x30 0x4c70a0 ret 8
	bool __fastcall entities_named(void* self, void* edx, const char* name, std::uint32_t* set);                    // 0x34 0x4c6e10 ret 8
	void __fastcall hierarchy_relate(void* self, void* edx, const char* parent, const char* child);                 // 0x38 0x4c7c20 ret 8
	void __fastcall property_update(void* self, void* edx, std::uint32_t token, const std::uint32_t* record);       // 0x3c 0x4c6990 ret 8
	std::uint32_t* __fastcall property_members(void* self, void* edx, std::uint32_t property);                      // 0x40 0x4c6950 ret 4
	std::uint32_t* __fastcall property_record(void* self, void* edx, std::uint32_t index);                          // 0x44 0x4c6970 ret 4
	void __fastcall grid_update(void* self, void* edx, std::uint32_t token);                                        // 0x48 0x4c6890 ret 4
	void __fastcall relation_set(void* self, void* edx, int table, std::uint32_t token, std::uint32_t value);       // 0x4c 0x4c7240 ret 12
	bool __fastcall relation_members(void* self, void* edx, int table, std::uint32_t token, std::uint32_t* set);    // 0x50 0x4c6c60 ret 12
	std::uint32_t* __fastcall relation_get(void* self, void* edx, std::uint32_t* out, int table, std::uint32_t token);// 0x54 0x4c6b60 ret 12
	std::uint32_t* __fastcall relation_begin(void* self, void* edx, std::uint32_t* out, int table, std::uint32_t token); // 0x58 0x4c6da0 ret 12
	std::uint32_t* __fastcall relation_next(void* self, void* edx, std::uint32_t* out, int table, std::uint32_t unused); // 0x5c 0x4c6dd0 ret 12
	void* __fastcall allocate(void* self, void* edx, std::uint32_t* out_token, std::uint32_t size);                  // 0x60 0x4c6fd0 ret 8
	void __fastcall release(void* self, void* edx, std::uint32_t token);                                            // 0x64 0x4c7460 ret 4
}
