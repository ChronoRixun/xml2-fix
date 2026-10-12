#pragma once

// Phase A entity capacity: the CEntityGrid owner (native vtable 0x686c54, static 0x718fd0, code 0x463c00-0x4654aa,
// getter 0x465470) ported into the DLL as a replacement object. One code path serves both capacities: with a
// world of 256 the object is the retail layout byte for byte (so the differential test can compare raw memory
// against the native code), with 1024 it is the wide layout of round6/layouts.json "grid".
//
// What the grid is (as decompiled): a 2D cell grid over the zone's XY extent (cells of `cell` size, at most 84 x 84
// cells), stored as 128 blocks of 4x4 cells reached through a 21x21 byte "coarse map" (0xff = no block). A cell is an
// ordered list of entity indices (append at the end, remove by swapping the last entry in). Each entity keeps its
// cell rectangle in four bytes {x0, x1, y0, y1}; {0xff, 0, 0xff, 0} means "not in the grid". Two cached result sets
// (a cell rectangle, the query bounds and an entity set) are kept in sync by insert/remove/update.
//
// Methods are `__fastcall` with an unused EDX parameter (see entity_capacity_layout.hpp): the frame is exactly the
// native __thiscall one, so the integrator registers `vtable.slots` as the object's vptr and callers through the
// replaced getter see the native ABI. Every native side effect, ordering, early return and defect is preserved;
// nothing is "simplified" (the known cell-overflow defect of 0x465400 is reproduced at 256, see the .cpp).

#include "entity_capacity_layout.hpp"
#include <cstddef>
#include <cstdint>

namespace entity_grid
{
	using entity_capacity_layout::descriptor;
	using entity_capacity_layout::EntitySet;

	struct vec3 { float x, y, z; };
	struct bounds { vec3 min, max; }; // the 24-byte (min, max) pair slots 0x28 / 0x40 read and slot 0x3c writes

	// Field offsets of one owner. The fixed head (vptr, min, max, cell, half, inverse, width, height, coarse map) is the
	// same at both capacities; everything from the block pool on scales with the world capacity.
	struct layout
	{
		std::uint32_t world = 0;             // 256 or 1024 (0: unsupported)
		std::uint32_t index_mask = 0;        // token -> dense index (0xff retail, 0x3ff raised)
		std::uint32_t cell_stride = 0;       // 0x20 retail (u8 count + 31 u8 indices), 0x804 raised (u16 count, u16 pad, 1024 u16)
		std::uint32_t cell_field = 0;        // width of the count and of each index: 1 or 2
		std::uint32_t cell_indices = 0;      // offset of index 0 inside a cell: 1 or 4
		std::uint32_t cell_append_limit = 0; // native appends while count < 0x20 (retail) / < 1024 (raised)
		std::uint32_t block_stride = 0;      // 16 cells
		std::uint32_t pool = 0;              // block storage starts here (+0x204 at both capacities)
		std::uint32_t cells_bytes = 0;       // 128 blocks
		std::uint32_t block_bitmap = 0;      // u32[4]: blocks constructed (pool + cells_bytes)
		std::uint32_t ring = 0;              // u32[128] FIFO of free block ids
		std::uint32_t ring_enqueue = 0, ring_dequeue = 0, ring_count = 0;
		std::uint32_t alloc_bitmap = 0;      // u32[4]: blocks handed out
		std::uint32_t alloc_count = 0;       // u32
		std::uint32_t extents = 0;           // u8[world][4]: {x0, x1, y0, y1} per entity, {ff,0,ff,0} = absent
		std::uint32_t caches = 0;            // two caches
		std::uint32_t cache_stride = 0;      // 4 extents bytes + 24 bytes bounds + set (32 retail / 128 raised)
		std::uint32_t set_words = 0;         // world / 32
		std::uint32_t size = 0;              // object bytes (0x108b4 retail, 0x403574 raised)
		constexpr bool valid() const { return world != 0; }
	};
	// Fixed head offsets and dimensions (native constants).
	constexpr std::uint32_t off_min = 0x4, off_max = 0x10, off_cell = 0x1c, off_half = 0x28, off_inverse = 0x34, off_width = 0x40, off_height = 0x44;
	constexpr std::uint32_t off_coarse = 0x48, coarse_dim = 21, coarse_bytes = coarse_dim * coarse_dim, block_count = 128, cells_per_block = 16, max_cells = 84;
	constexpr std::uint32_t cache_off_extents = 0, cache_off_bounds = 4, cache_off_set = 0x1c, cache_count = 2;
	constexpr std::uint8_t no_block = 0xff;

	layout layout_for(std::uint32_t world);       // invalid() when the capacity is not 256 or 1024
	std::size_t object_size(const descriptor& d); // bytes of the object proper (0 when unsupported)

	// Allocates (VirtualAlloc, zero-filled) and constructs the owner exactly as the native constructor chain does
	// (0x463de0 -> 0x463d50 / 0x463ce0 / 0x463c00 / 0x463c90). Returns the object pointer (first dword = vtable.slots)
	// or nullptr when the descriptor's world capacity is unsupported or the allocation fails.
	void* create(const descriptor& d);
	// Native destructor semantics (0x465450: vptr rewrite, block bitmap cleared) on the DLL object, then releases the
	// backing. Accepts nullptr.
	void destroy(void* object);
	// The layout the object was created with (nullptr for a pointer `create` did not return).
	const layout* layout_of(const void* object);

	// The DLL vtable, native slot order. The locator dword precedes the slots as in the retail image so RTTI readers
	// see the same prefix; the object's vptr is `slots`.
	struct vtable_image { const void* rtti_locator; const void* slots[17]; };
	extern const vtable_image vtable;
	constexpr std::uint32_t native_vtable = 0x686c54, native_object = 0x718fd0, native_rtti_locator = 0x6ac704;

	// Capacity-independent native helper the port calls by address: 0x419af0 constructs a (min, max) igVec3f pair and,
	// when the flag is set, assigns igVec3f::ZeroVector to both (slot 0x3c's "no cached bounds" result). Tests point
	// this at a fixture stand-in because the retail code is not loadable at that address in a test process.
	using bounds_pair_ctor_fn = bounds* (__fastcall*)(bounds* self, void* edx, int zero);
	extern bounds_pair_ctor_fn bounds_pair_ctor;

	// ---- the 17 slots (native target, stack cleanup) -----------------------------------------------------------
	void __fastcall reset(void* self, void* edx);                                                                   // 0x00 0x464aa0 ret 0
	void __fastcall init(void* self, void* edx, const vec3* min, const vec3* max, const vec3* cell);                // 0x04 0x463f30 ret 12
	const vec3* __fastcall cell_size(void* self, void* edx);                                                        // 0x08 0x614280 ret 0
	vec3* __fastcall snap(void* self, void* edx, vec3* out, const vec3* in);                                        // 0x0c 0x463e30 ret 8
	vec3* __fastcall cell_center(void* self, void* edx, vec3* out, int x, int y);                                   // 0x10 0x463ea0 ret 12
	int __fastcall width(void* self, void* edx);                                                                    // 0x14 0x463ed0 ret 0
	int __fastcall height(void* self, void* edx);                                                                   // 0x18 0x463ee0 ret 0
	vec3* __fastcall get_min(void* self, void* edx, vec3* out);                                                     // 0x1c 0x463ef0 ret 4
	vec3* __fastcall get_max(void* self, void* edx, vec3* out);                                                     // 0x20 0x463f10 ret 4
	void __fastcall remove(void* self, void* edx, std::uint32_t token);                                             // 0x24 0x464b20 ret 4
	void __fastcall update(void* self, void* edx, std::uint32_t token, const bounds* b);                            // 0x28 0x464fd0 ret 8
	int __fastcall present(void* self, void* edx, std::uint32_t token);                                             // 0x2c 0x4642e0 ret 4
	unsigned char __fastcall query(void* self, void* edx, const vec3* min, const vec3* max, std::uint32_t* set);     // 0x30 0x464400 ret 12
	std::uint32_t* __fastcall cache_set_ptr(void* self, void* edx, int cache);                                      // 0x34 0x464310 ret 4
	std::uint32_t* __fastcall cache_reset(void* self, void* edx, int cache);                                               // 0x38 0x464330 ret 4
	unsigned char __fastcall cache_bounds(void* self, void* edx, int cache, bounds* out);                           // 0x3c 0x464370 ret 8
	void __fastcall cache_update(void* self, void* edx, int cache, const bounds* b);                                // 0x40 0x4646d0 ret 8
}
