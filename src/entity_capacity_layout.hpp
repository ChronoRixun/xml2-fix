#pragma once

// Phase A entity capacity: the one contract every replacement owner (World, Grid, Query manager, physics
// frames, allocator, save adapters, test state, observers) is written against. Actor 32 / generic 768 /
// World 1024, contiguous ten-bit dense indices, generations 1..0x3fff (docs/entity-capacity-phase-a-plan.md).
//
// Nothing here touches the game: pure constants, layouts and helpers, checked by the native tests without a
// process. Every retail address is the retail build's (XMen2.exe sha1 7d95cdb4a9a599b982147a3389ea7aff211cb82b,
// image base 0x400000). Hex offsets are the native owner's field offsets as decompiled from that build.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace entity_capacity_layout
{
	// ---- capacities and tokens ---------------------------------------------------------------------------

	constexpr std::uint32_t stock_actors = 32, stock_generic = 224, stock_world = 256, stock_index_bits = 8;
	constexpr std::uint32_t raised_actors = 32, raised_generic = 768, raised_world = 1024, raised_index_bits = 10;
	constexpr std::uint32_t max_world = 1024; // every fixed container below is sized for this; a descriptor may be smaller
	constexpr std::uint32_t generation_min = 1, generation_max = 0x3fff; // float32-exact for every slot (plan section 3)
	constexpr std::uint32_t layout_rule_id = 1, layout_revision_stock = 1, layout_revision_raised = 3; // GCAP rule 1
	constexpr std::uint16_t gcap_protocol_revision = 2;                                                 // plan section 5

	// A dense slot number (0..capacity-1) and a whole World token ((generation << index_bits) | index) are
	// different domains: a dense index must never be passed where a token is read (plan section 2, the
	// LegacyEmptyDenseDisposition of CStaticQuery slots 0x18/0x1c) and vice versa.
	struct DenseIndex { std::uint32_t value; };
	struct WorldToken { std::uint32_t value; };

	struct token_layout
	{
		std::uint32_t index_bits; // 8 stock, 10 raised
		constexpr std::uint32_t mask() const { return (1u << index_bits) - 1; }
		constexpr DenseIndex index(WorldToken t) const { return {t.value & mask()}; }
		constexpr std::uint32_t generation(WorldToken t) const { return t.value >> index_bits; }
		constexpr WorldToken token(std::uint32_t generation, DenseIndex i) const { return {(generation << index_bits) | i.value}; }
		// The native constructor seeds every slot at generation 1 (0x4c6440); the native advance adds 1 << index_bits.
		constexpr WorldToken seed(DenseIndex i) const { return token(generation_min, i); }
		constexpr bool positive_retail_token(std::uint32_t v) const { return v != 0 && (v >> 31) == 0; }
	};
	constexpr token_layout stock_tokens{stock_index_bits}, raised_tokens{raised_index_bits};

	// Legacy (feature-0 LC, retail eight-bit) typed field -> contiguous (plan section 4): null when the value is
	// not a positive retail token, its generation is 0, or its generation exceeds the operational maximum.
	constexpr std::uint32_t migrate_typed_field(std::uint32_t retail_value)
	{
		if (!stock_tokens.positive_retail_token(retail_value)) return 0;
		const auto g = stock_tokens.generation({retail_value});
		if (g == 0 || g > generation_max) return 0;
		return raised_tokens.token(g, stock_tokens.index({retail_value})).value;
	}

	// ---- entity sets -------------------------------------------------------------------------------------

	// A 1024-bit entity set: the wide form of the native 256-bit (eight-dword, 32-byte) set that every World/
	// Grid/Query/physics interface passes by pointer. Dword `i >> 5`, bit `i & 31`, as the native unrolled code.
	constexpr std::size_t set_words = max_world / 32, set_bytes = set_words * 4; // 32 dwords, 128 bytes
	constexpr std::size_t stock_set_words = stock_world / 32;                     // 8 dwords, 32 bytes
	struct EntitySet
	{
		std::array<std::uint32_t, set_words> words{};
		constexpr bool test(std::uint32_t i) const { return (words[i >> 5] >> (i & 31)) & 1u; }
		constexpr void set(std::uint32_t i) { words[i >> 5] |= 1u << (i & 31); }
		constexpr void clear(std::uint32_t i) { words[i >> 5] &= ~(1u << (i & 31)); }
		constexpr void clear_all() { words.fill(0); }
		constexpr bool empty(std::uint32_t capacity) const
		{
			for (std::uint32_t w = 0; w < capacity / 32; ++w) if (words[w]) return false;
			return true;
		}
	};
	static_assert(sizeof(EntitySet) == set_bytes);

	// The native "next set bit at or after `from`" (shared_256_bit_helper, 0x403ca0 family): returns the first
	// set index >= from, or `capacity` when none. Word-at-a-time, lowest bit first, as the native code iterates.
	constexpr std::uint32_t next_set(const std::uint32_t* words, std::uint32_t from, std::uint32_t capacity)
	{
		for (std::uint32_t i = from; i < capacity;)
		{
			const std::uint32_t w = words[i >> 5] >> (i & 31);
			if (w == 0) { i = (i | 31) + 1; continue; }
			std::uint32_t bit = i;
			std::uint32_t v = w;
			while (!(v & 1u)) { v >>= 1; ++bit; }
			return bit;
		}
		return capacity;
	}
	constexpr std::uint32_t first_set(const std::uint32_t* words, std::uint32_t capacity) { return next_set(words, 0, capacity); }

	// ---- the installed descriptor --------------------------------------------------------------------------

	// Immutable after publication. Every consumer (owners, test state, observers, save adapters, ladder and
	// break-rule caches) reads capacities and owner addresses from here, never from retail constants.
	struct descriptor
	{
		std::uint32_t actors = stock_actors, generic = stock_generic, world = stock_world;
		token_layout tokens = stock_tokens;
		std::uint16_t layout_revision = layout_revision_stock;
		bool raised = false;
		// Prepared owner objects (0 until published; retail statics when not raised).
		std::uint32_t world_object = 0x778b70, grid_object = 0x718fd0, query_manager = 0x756b10, allocator = 0;
		// World handle-core field offsets from the World object (plan layouts.json; retail values when not raised).
		struct world_fields
		{
			std::uint32_t entities;   // void* [world]           retail +0x4
			std::uint32_t free_ring;  // u32 [world]             retail +0x408
			std::uint32_t enqueue;    // u32                     retail +0x80c
			std::uint32_t dequeue;    // u32                     retail +0x810
			std::uint32_t free_count; // u32                     retail +0x814
			std::uint32_t live_bits;  // u32 [world/32]          retail +0x818
			std::uint32_t live_count; // u32                     retail +0x838
			std::uint32_t handles;    // u32 [world]             retail +0x83c
			std::uint32_t mask;       // u32                     retail +0xc3c
			std::uint32_t shift;      // u32                     retail +0xc40
		} world_core{0x4, 0x408, 0x80c, 0x810, 0x814, 0x818, 0x838, 0x83c, 0xc3c, 0xc40};
		constexpr std::uint32_t set_words_in_use() const { return world / 32; }
	};
	constexpr descriptor stock_descriptor{};

	// Raised World handle-core layout, derived the way the native constructor lays the core out (pointers,
	// free ring, three ring words, live bitmap, live count, handle table, mask, shift) for N entities.
	constexpr descriptor::world_fields world_core_for(std::uint32_t n)
	{
		descriptor::world_fields f{};
		f.entities = 0x4;
		f.free_ring = f.entities + n * 4 + 4;  // one native dword sits between the pointer table and the ring (retail +0x404)
		f.enqueue = f.free_ring + n * 4 + 4;   // and one between the ring and its indices (retail +0x808)
		f.dequeue = f.enqueue + 4;
		f.free_count = f.dequeue + 4;
		f.live_bits = f.free_count + 4;
		f.live_count = f.live_bits + n / 8;
		f.handles = f.live_count + 4;
		f.mask = f.handles + n * 4;
		f.shift = f.mask + 4;
		return f;
	}
	static_assert(world_core_for(256).enqueue == 0x80c && world_core_for(256).live_bits == 0x818 && world_core_for(256).live_count == 0x838 &&
	              world_core_for(256).handles == 0x83c && world_core_for(256).mask == 0xc3c && world_core_for(256).shift == 0xc40);
	static_assert(world_core_for(1024).shift + 4 == 0x30a4); // plan layouts.json: handle_core 0x4..0x30a4

	constexpr descriptor raised_descriptor_for(std::uint32_t world_object, std::uint32_t grid_object, std::uint32_t query_manager, std::uint32_t allocator)
	{
		descriptor d{};
		d.actors = raised_actors; d.generic = raised_generic; d.world = raised_world; d.tokens = raised_tokens;
		d.layout_revision = layout_revision_raised; d.raised = true;
		d.world_object = world_object; d.grid_object = grid_object; d.query_manager = query_manager; d.allocator = allocator;
		d.world_core = world_core_for(raised_world);
		return d;
	}

	// ---- native ABI conventions ------------------------------------------------------------------------------

	// Replacement methods are declared `__fastcall` with an unused second (EDX) parameter: the callee sees ECX =
	// this and the remaining arguments on the stack with callee cleanup, exactly as the native __thiscall methods
	// the DLL vtables replace. Float results return in ST(0) under both conventions on x86 MSVC.
	//   ret __fastcall method(Owner* self, void* /*edx*/, args...);
	// A native __thiscall is called the same way through a function pointer of that shape.
	template <typename Fn>
	inline Fn native(std::uint32_t va) { return reinterpret_cast<Fn>(static_cast<std::uintptr_t>(va)); }

	// Retail helpers the owners call unchanged (none of them is capacity-dependent).
	constexpr std::uint32_t native_name_of_handle = 0x41aa80;   // __fastcall(uint* handle) -> const char* (global name table)
	constexpr std::uint32_t native_fold_case = 0x5602b0;        // __cdecl(char* text) in place
	constexpr std::uint32_t native_name_hash = 0x41ab70;        // __cdecl(const char* text) -> u32
	constexpr std::uint32_t native_world_getter = 0x4c7f20;     // replaced: returns the installed World
	constexpr std::uint32_t native_grid_getter = 0x465470;      // replaced: returns the installed Grid
	constexpr std::uint32_t native_query_manager_getter = 0x4ab550; // replaced
	constexpr std::uint32_t native_allocator_getter = 0x461910; // replaced
	constexpr std::uint32_t world_pointer_cell = 0x718430;      // CWorld* global assigned from the getter
	constexpr std::uint32_t allocator_pointer_cell = 0x718fbc;  // CEntityAllocator* singleton (0x461910 publishes it)
	constexpr std::uint32_t entity_token_offset = 0x1c;         // every CEntity stores its World token here
}
