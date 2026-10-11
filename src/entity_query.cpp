// Phase A entity capacity: the query owner. See entity_query.hpp for the contract and the retail addresses.
//
// Method bodies follow the retail decompiles of 0x4a9580..0x4abb40 instruction by instruction (register widths,
// x87 compare flags, call order, early returns). Only the three things the plan changes differ:
//   1. every 256-bit set / 0x100 loop bound becomes the descriptor's capacity (128-byte sets at 1024);
//   2. the collision map is indexed by the handle's dense index decoded with descriptor.tokens (the native code at
//      0x4a9e0d/0x4a9a56/0x4a9a8d/0x4aa908/0x4aa975/0x4aa9f0/0x4aaa40/0x4a9d73 only reads the low byte);
//   3. the three query-local 32-byte sets (0x4aac80/0x4aaec0/0x4aaac0/0x4ab1f0) are widened to EntitySet.
// CStaticQuery slots 0x18/0x1c keep their LegacyEmptyDenseDisposition (see slot_18/slot_1c).

#include "entity_query.hpp"
#include <windows.h>
#include <cstring>

namespace entity_query
{
	using namespace entity_capacity_layout;

	namespace
	{
		// ---- state -------------------------------------------------------------------------------------------------
		struct state
		{
			descriptor d = stock_descriptor;
			layout l = layout_for(stock_world);
			std::uint8_t* manager = nullptr;
			std::size_t manager_bytes = 0;
			manager_getter_fn getter = nullptr;
		} g;

		[[noreturn]] void fail_fast(const char* what)
		{
			OutputDebugStringA("xml2-fix entity_query: ");
			OutputDebugStringA(what);
			OutputDebugStringA("\n");
			RaiseFailFastException(nullptr, nullptr, 0);
			for (;;) {}
		}

		// ---- raw field access (objects are 4-aligned VirtualAlloc memory or the retail statics) -------------------
		inline std::uint32_t& u32(void* o, std::uint32_t off) { return *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(o) + off); }
		inline std::uint32_t u32(const void* o, std::uint32_t off) { return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(o) + off); }
		inline std::uint16_t& u16(void* o, std::uint32_t off) { return *reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(o) + off); }
		inline std::uint8_t& u8(void* o, std::uint32_t off) { return *(static_cast<std::uint8_t*>(o) + off); }
		inline float& f32(void* o, std::uint32_t off) { return *reinterpret_cast<float*>(static_cast<std::uint8_t*>(o) + off); }
		inline std::uint32_t* words(void* o, std::uint32_t off) { return reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(o) + off); }
		inline const std::uint32_t* words(const void* o, std::uint32_t off) { return reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(o) + off); }
		inline std::uint8_t* at(std::uint32_t va) { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(va)); }
		inline std::uint32_t& global32(std::uint32_t va) { return *reinterpret_cast<std::uint32_t*>(at(va)); }
		inline std::uint8_t* bytes(void* o) { return static_cast<std::uint8_t*>(o); }

		// ---- native callees (capacity independent) ---------------------------------------------------------------
		using world_getter_fn = void* (__cdecl*)();
		using handle_valid_fn = bool (__fastcall*)(const std::uint32_t* handle, void* edx);
		using entity_of_handle_fn = std::uint8_t* (__fastcall*)(const std::uint32_t* handle, void* edx);
		using player_handle_fn = std::uint32_t* (__cdecl*)(std::uint32_t* out);
		using record_by_name_fn = const std::uint8_t* (__cdecl*)(const char* name);
		using name_of_handle_fn = const char* (__fastcall*)(const std::uint32_t* handle, void* edx);
		using angles_to_direction_fn = float* (__cdecl*)(float* out, const float* angles);
		using pool_take_fn = std::uint32_t (__fastcall*)(void* pool, void* edx);
		using pool_return_fn = void (__fastcall*)(void* pool, void* edx, std::uint32_t slot);
		using distance2_fn = double (__fastcall*)(const float* self, void* edx, const float* other); // ST0 float
		using normalize_fn = double (__fastcall*)(float* self, void* edx);                           // ST0 length, discarded
		// Other owners' vtable slots, called exactly as the native code does.
		using world_find_token_fn = std::uint8_t* (__fastcall*)(void* world, void* edx, std::uint32_t token);   // slot 0xc
		using world_find_dense_fn = std::uint8_t* (__fastcall*)(void* world, void* edx, std::uint32_t dense);   // slot 0x10
		using world_named_set_fn = bool (__fastcall*)(void* world, void* edx, const char* name, std::uint32_t* out); // slot 0x34
		using world_property_set_fn = const std::uint32_t* (__fastcall*)(void* world, void* edx, std::uint32_t id);  // slot 0x40
		using world_pair_set_fn = bool (__fastcall*)(void* world, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t* out); // slot 0x50
		using grid_cell_set_fn = const std::uint32_t* (__fastcall*)(void* grid, void* edx, std::uint32_t cell);  // slot 0x34
		using collide_0_fn = void (__fastcall*)(void* c, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t* set);
		using collide_4_fn = void (__fastcall*)(void* c, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t* set, std::uint32_t flag);
		using collide_8_fn = collide_4_fn;
		using collide_c_fn = void (__fastcall*)(void* c, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c3, std::uint32_t* set, std::uint32_t d, void* collisions);
		using collide_10_fn = std::uint32_t* (__fastcall*)(void* c, void* edx, std::uint32_t* out, std::uint32_t a, std::uint32_t b, std::uint32_t c3,
		                                                 std::uint32_t flags_a, std::uint32_t flags_b, std::uint32_t b0, std::uint32_t b1,
		                                                 std::uint32_t* set, void* collisions, std::uint32_t x, std::uint32_t y, std::uint32_t z);
		using entity_vec_fn = float* (__fastcall*)(void* entity, void* edx, float* out); // entity slots 0x24 / 0x28
		using entity_void_fn = std::uint8_t* (__fastcall*)(void* entity, void* edx);     // entity slots 0x0 / 0x194
		using policy_score_fn = double (__fastcall*)(void* policy, void* edx, void* entity); // ST0 (x87) score

		template <typename Fn> Fn slot(void* object, std::uint32_t slot_offset)
		{
			return reinterpret_cast<Fn>(reinterpret_cast<void* const*>(u32(object, 0))[slot_offset / 4]);
		}
		void* world_of(void* query) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(u32(query, g.l.q_world))); }
		void* current_world() { return native<world_getter_fn>(native_world_getter)(); }
		void* collide_owner() { return native<world_getter_fn>(native_collide_getter)(); }
		void* grid_owner() { return native<world_getter_fn>(native_grid_getter)(); }
		bool handle_valid(const std::uint32_t* handle) { return native<handle_valid_fn>(native_handle_valid)(handle, nullptr); }
		std::uint8_t* entity_of(const std::uint32_t* handle) { return native<entity_of_handle_fn>(native_entity_of_handle)(handle, nullptr); }
		std::uint32_t* player_handle(std::uint32_t* out) { return native<player_handle_fn>(native_player_handle)(out); }
		double distance2(const float* a, const float* b) { return (*reinterpret_cast<distance2_fn*>(at(iat_distance2)))(a, nullptr, b); }
		void normalize(float* v) { (void)(*reinterpret_cast<normalize_fn*>(at(iat_normalize)))(v, nullptr); }
		const float* zero_vector() { return *reinterpret_cast<const float**>(at(iat_zero_vector)); }
		std::uint8_t* pool_record(std::uint32_t index) { return at(native_trace_pool) + index * trace_record_bytes; }
		DenseIndex dense_of(std::uint32_t token) { return g.d.tokens.index(WorldToken{token}); }

		void* manager()
		{
			void* m = g.getter ? g.getter() : native<manager_getter_fn>(native_query_manager_getter)();
			if (m != g.manager || !m) fail_fast("query manager getter does not return the installed manager");
			return m;
		}
		std::uint8_t* query_at(void* m, std::uint32_t i) { return bytes(m) + g.l.m_queries + i * g.l.q_stride; }
		std::uint8_t* order_at(void* m, std::uint32_t i) { return bytes(m) + g.l.m_orders + i * g.l.o_stride; }

		// ---- set helpers (private wide forms of the shared 256-bit helpers) --------------------------------------
		void set_copy(std::uint32_t* dst, const std::uint32_t* src) { for (std::uint32_t w = 0; w < g.l.set_words; ++w) dst[w] = src[w]; }
		void set_zero(std::uint32_t* dst) { for (std::uint32_t w = 0; w < g.l.set_words; ++w) dst[w] = 0; }
		void set_invert(std::uint32_t* s) { for (std::uint32_t w = 0; w < g.l.set_words; ++w) s[w] = ~s[w]; } // 0x4aa500
		void bit_set(std::uint32_t* s, std::uint32_t i) { s[i >> 5] |= 1u << (i & 31); }
		void bit_clear(std::uint32_t* s, std::uint32_t i) { s[i >> 5] &= ~(1u << (i & 31)); }
		bool bit_test(const std::uint32_t* s, std::uint32_t i) { return (s[i >> 5] & (1u << (i & 31))) != 0; }
		void bit_assign(std::uint32_t* s, std::uint32_t i, bool value) { if (value) bit_set(s, i); else bit_clear(s, i); } // 0x419c90
		// The inline first-bit scan the query methods open with (0x4aa330 etc.): first set index, or capacity.
		std::uint32_t first_bit(const std::uint32_t* s) { return first_set(s, g.l.world); }
		std::uint32_t next_bit(const std::uint32_t* s, std::uint32_t from) { return next_set(s, from, g.l.world); } // 0x44d0d0(from, 1)

		// ---- CCollisions -------------------------------------------------------------------------------------------
		std::uint8_t* collisions_of(void* query) { return bytes(query) + g.l.q_collisions; }
		std::uint8_t* map_of(void* collisions) { return bytes(collisions) + g.l.c_map; }
		// 0x4a9a50: trace record of a handle, or null.
		std::uint8_t* collisions_find(void* collisions, const std::uint32_t* handle)
		{
			const auto b = map_of(collisions)[dense_of(*handle).value];
			return b != 0xff ? pool_record(b) : nullptr;
		}
		// 0x4aa900: position of an entity: its trace record's, else its virtual position (entity slot 0x24).
		const float* collisions_position(void* collisions, float* out, std::uint8_t* entity)
		{
			const auto b = map_of(collisions)[dense_of(u32(entity, entity_token_offset)).value];
			if (b != 0xff)
			{
				const auto* rec = pool_record(b);
				std::memcpy(out, rec, 12);
				return out;
			}
			slot<entity_vec_fn>(entity, 0x24)(entity, nullptr, out);
			return out;
		}
		// 0x4a9ee0: return every trace to the pool and clear the used map entries.
		void collisions_release_all(void* collisions)
		{
			auto* c = bytes(collisions);
			const auto& l = g.l;
			if (u32(c, l.c_slot_count) == 0) return;
			for (std::uint32_t i = 0; i < u32(c, l.c_slot_count); ++i)
				native<pool_return_fn>(native_trace_pool_return)(at(native_trace_pool), nullptr, c[l.c_slots + i]);
			u32(c, l.c_slot_count) = 0;
			for (std::uint32_t i = 0; i < u32(c, l.c_key_count); ++i) map_of(c)[u16(c, l.c_keys + i * 2)] = 0xff;
			u32(c, l.c_key_count) = 0;
		}
		// 0x4a9de0 (CCollisions vtable slot 0): take a pool trace for a handle; null when the pool is full.
		std::uint8_t* __fastcall collisions_insert(void* collisions, void*, const std::uint32_t* handle)
		{
			if (global32(native_trace_pool_count) == trace_pool_records) return nullptr;
			auto* c = bytes(collisions);
			const auto& l = g.l;
			const auto slot_index = static_cast<std::uint8_t>(native<pool_take_fn>(native_trace_pool_take)(at(native_trace_pool), nullptr));
			const auto dense = dense_of(*handle).value; // native 0x4a9e0d: the handle's low byte
			map_of(c)[dense] = slot_index;
			u16(c, l.c_keys + u32(c, l.c_key_count) * 2) = static_cast<std::uint16_t>(dense);
			u32(c, l.c_key_count) += 1;
			c[l.c_slots + u32(c, l.c_slot_count)] = slot_index; // 0x4a9e70
			u32(c, l.c_slot_count) += 1;
			auto* rec = pool_record(slot_index);
			u32(rec, 0xc) = global32(global_trace_stamp);
			u32(rec, 0x3c) = 0;
			return rec;
		}
		const void* const collisions_vtable_table[1] = {reinterpret_cast<const void*>(&collisions_insert)};
		// 0x4a9cd0
		void collisions_construct(void* collisions)
		{
			auto* c = bytes(collisions);
			u32(c, 0) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(collisions_vtable_table));
			u32(c, g.l.c_key_count) = 0;  // 0x4a98a0 -> 0x4a9860
			u32(c, g.l.c_slot_count) = 0; // 0x4a98b0 -> 0x58b6b0
			std::memset(map_of(c), 0xff, g.l.world);
		}

		// ---- CStaticQuery ------------------------------------------------------------------------------------------
		std::uint32_t* set_of(void* q) { return words(q, g.l.q_bitmap); }
		// 0x4aaac0: combine the query set with a set (mode 0 union + clear flags, 1 subtract, 2 intersect).
		void set_op(void* q, std::uint32_t mode, const std::uint32_t* src)
		{
			auto* set = set_of(q);
			if (mode == 0)
			{
				for (std::uint32_t w = 0; w < g.l.set_words; ++w) set[w] |= src[w];
				u32(q, g.l.q_flags_a) = 0;
				u32(q, g.l.q_flags_b) = 0;
			}
			else if (mode == 1)
			{
				EntitySet local{}; // 0x4aac10 (zero, copy) then 0x4aa500 (invert): the widened 32-byte local
				set_copy(local.words.data(), src);
				set_invert(local.words.data());
				for (std::uint32_t w = 0; w < g.l.set_words; ++w) set[w] &= local.words[w];
			}
			else if (mode == 2)
			{
				for (std::uint32_t w = 0; w < g.l.set_words; ++w) set[w] &= src[w];
			}
		}
		// 0x4aa420: one dense bit (mode 0 add + clear flags, 1 remove, 2 keep only when already set and flag).
		void bit_op(void* q, std::uint32_t mode, std::uint32_t index, std::uint8_t flag)
		{
			auto* set = set_of(q);
			if (mode == 0)
			{
				if (flag != 0)
				{
					bit_set(set, index);
					u32(q, g.l.q_flags_a) = 0;
					u32(q, g.l.q_flags_b) = 0;
				}
			}
			else if (mode == 1)
			{
				if (flag != 0) bit_clear(set, index);
			}
			else if (mode == 2)
			{
				bit_assign(set, index, (bit_test(set, index) ? 1 : 0) & flag);
			}
		}
		// Common tail of the reset methods 0x4a9f70 / 0x4ab3d0 / 0x4ab300.
		void reset_tail(void* q)
		{
			collisions_release_all(collisions_of(q));
			u32(q, g.l.q_flags_a) = 0;
			u32(q, g.l.q_flags_b) = 0;
			u8(q, g.l.q_b2) = 1;
			u8(q, g.l.q_b0) = 1;
			u8(q, g.l.q_b1) = 0;
		}
		// 0x4a9f70 (slot 0x8): all characters minus one handle.
		void __fastcall slot_08(void* q, void*, const std::uint32_t* exclude)
		{
			void* world = world_of(q);
			set_copy(set_of(q), slot<world_property_set_fn>(world, 0x40)(world, nullptr, global32(global_character_class) + 0x24));
			if (*exclude != 0 && handle_valid(exclude)) bit_clear(set_of(q), dense_of(*exclude).value); // native: *exclude & 0xff
			u32(q, g.l.q_result) = 0;
			reset_tail(q);
		}
		// 0x4ab3d0 (slot 0x4): a type record's class set minus one handle.
		void __fastcall slot_04(void* q, void*, const std::uint8_t* record, const std::uint32_t* exclude)
		{
			void* world = world_of(q);
			set_copy(set_of(q), slot<world_property_set_fn>(world, 0x40)(world, nullptr, u32(record, 4) + 0x24));
			if (*exclude != 0 && handle_valid(exclude)) bit_clear(set_of(q), dense_of(*exclude).value);
			u32(q, g.l.q_result) = 0;
			reset_tail(q);
		}
		// 0x4aa090 (slot 0x0): slot 0x4 with the record found by name and no exclusion.
		void __fastcall slot_00(void* q, void*, const char* name)
		{
			std::uint32_t none = 0;
			const auto* record = native<record_by_name_fn>(native_record_by_name)(name);
			slot<void (__fastcall*)(void*, void*, const std::uint8_t*, const std::uint32_t*)>(q, 0x4)(q, nullptr, record, &none);
		}
		// 0x4ab370 (slot 0xc): copy another query's set and flags.
		void __fastcall slot_0c(void* q, void*, void* const* source_ref)
		{
			void* src = *source_ref;
			set_copy(set_of(q), set_of(src));
			u32(q, g.l.q_result) = 0;
			collisions_release_all(collisions_of(q));
			u32(q, g.l.q_flags_a) = u32(src, g.l.q_flags_a);
			u32(q, g.l.q_flags_b) = u32(src, g.l.q_flags_b);
			u8(q, g.l.q_b0) = u8(src, g.l.q_b0);
			u8(q, g.l.q_b1) = u8(src, g.l.q_b1);
			u8(q, g.l.q_b2) = u8(src, g.l.q_b2);
		}
		// 0x4ab300 (slot 0x10): empty, or all characters.
		void __fastcall slot_10(void* q, void*, std::uint8_t all)
		{
			if (all == 0) set_zero(set_of(q));
			else
			{
				void* world = world_of(q);
				set_copy(set_of(q), slot<world_property_set_fn>(world, 0x40)(world, nullptr, global32(global_character_class) + 0x24));
			}
			collisions_release_all(collisions_of(q));
			u32(q, g.l.q_result) = 0;
			u32(q, g.l.q_flags_a) = 0;
			u32(q, g.l.q_flags_b) = 0;
			u8(q, g.l.q_b1) = 0;
			u8(q, g.l.q_b2) = 1;
			u8(q, g.l.q_b0) = 1;
		}
		// 0x4aa330 (slot 0x14): the first entity of the set (World slot 0x10, dense).
		std::uint8_t* __fastcall slot_14(void* q, void*)
		{
			const auto i = first_bit(set_of(q));
			if (i >= g.l.world) return nullptr;
			void* world = world_of(q);
			return slot<world_find_dense_fn>(world, 0x10)(world, nullptr, i);
		}
		void mask(float* v, std::uint32_t flags)
		{
			if ((flags & 1) == 0) v[0] = 0.0f;
			if ((flags & 2) == 0) v[1] = 0.0f;
			if ((flags & 4) == 0) v[2] = 0.0f;
		}
		// 0x4aa5b0 (slot 0x18) and 0x4aa760 (slot 0x1c): nearest entity to a position on the flagged axes.
		//
		// LegacyEmptyDenseDisposition (plan section 2): the native code passes the DENSE bitmap index to World slot
		// 0xc, the full-token lookup (0x4aa683 / 0x4aa833), so no index ever resolves (a dense index is a token of
		// generation zero) and both methods return null for every set. The replacement keeps exactly that: it calls
		// the World's token lookup with the dense index, as native, and does NOT repair it to the dense lookup (slot
		// 0x10). Every other side effect (the masked target copy, the loop, the distance compare) is preserved.
		std::uint8_t* nearest(void* q, const float* position, std::uint32_t flags, bool via_virtual_position)
		{
			float target[3] = {position[0], position[1], position[2]};
			std::uint8_t* best = nullptr;
			float best_distance = 0.0f;
			mask(target, flags);
			auto i = first_bit(set_of(q));
			if (i >= g.l.world) return nullptr;
			void* world = world_of(q);
			do
			{
				// Dense index passed as a token on purpose: LegacyEmptyDenseDisposition.
				std::uint8_t* entity = slot<world_find_token_fn>(world, 0xc)(world, nullptr, static_cast<std::uint32_t>(DenseIndex{i}.value));
				if (entity)
				{
					float p[3];
					if (via_virtual_position) collisions_position(collisions_of(q), p, entity);
					else
					{
						std::uint32_t handle = u32(entity, entity_token_offset);
						const float* src = reinterpret_cast<const float*>(collisions_find(collisions_of(q), &handle));
						if (!src) src = reinterpret_cast<const float*>(entity + 0x20);
						std::memcpy(p, src, 12);
					}
					mask(p, flags);
					const double d = distance2(p, target); // ST0 compared against the float local (FCOM)
					if (best == nullptr || d < best_distance)
					{
						best_distance = static_cast<float>(d); // FSTP float
						best = entity;
					}
				}
				i = next_bit(set_of(q), i + 1);
			} while (i < g.l.world);
			return best;
		}
		std::uint8_t* __fastcall slot_18(void* q, void*, const float* position, std::uint32_t flags) { return nearest(q, position, flags, false); }
		std::uint8_t* __fastcall slot_1c(void* q, void*, const float* position, std::uint32_t flags) { return nearest(q, position, flags, true); }
		// 0x4aa950 (slot 0x20): the result handle's trace record, else the player's, else null.
		std::uint32_t* __fastcall slot_20(void* q, void*, std::uint32_t* out)
		{
			auto* c = collisions_of(q);
			if (u32(c, g.l.c_slot_count) != 0)
			{
				if (u32(q, g.l.q_result) != 0 && handle_valid(words(q, g.l.q_result)))
				{
					const auto b = map_of(c)[dense_of(u32(q, g.l.q_result)).value];
					*out = b != 0xff ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pool_record(b))) : 0;
					return out;
				}
				if (slot<bool (__fastcall*)(void*, void*)>(q, 0x28)(q, nullptr))
				{
					slot<void (__fastcall*)(void*, void*, std::uint32_t*)>(q, 0x2c)(q, nullptr, out);
					return out;
				}
			}
			*out = 0;
			return out;
		}
		// 0x4aa3a0 (slot 0x24): "nothing found": no traces and an empty set, or traces, no result and no player trace.
		std::uint8_t __fastcall slot_24(void* q, void*)
		{
			auto* c = collisions_of(q);
			if (u32(c, g.l.c_slot_count) == 0)
			{
				const auto* set = set_of(q);
				for (std::uint32_t w = 0; w < g.l.set_words; ++w) if (set[w] != 0) return 0;
				return 1;
			}
			if (u32(q, g.l.q_result) == 0)
				return static_cast<std::uint8_t>(1 - (slot<bool (__fastcall*)(void*, void*)>(q, 0x28)(q, nullptr) ? 1 : 0));
			return 0;
		}
		// 0x4aa9e0 (slot 0x28): the player's handle has a trace record.
		bool __fastcall slot_28(void* q, void*)
		{
			std::uint32_t local = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(q));
			const auto* handle = player_handle(&local);
			return map_of(collisions_of(q))[dense_of(*handle).value] != 0xff;
		}
		// 0x4aaa30 (slot 0x2c): the player's trace record or null.
		void __fastcall slot_2c(void* q, void*, std::uint32_t* out)
		{
			std::uint32_t local = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(q));
			const auto* handle = player_handle(&local);
			const auto b = map_of(collisions_of(q))[dense_of(*handle).value];
			*out = b != 0xff ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pool_record(b))) : 0;
		}
		// 0x4aa3e0 (slot 0x30): one handle's bit.
		void __fastcall slot_30(void* q, void*, std::uint32_t mode, const std::uint32_t* handle)
		{
			if (*handle != 0 && handle_valid(handle)) bit_op(q, mode, dense_of(*handle).value, 1); // native: *handle & 0xff
		}
		// 0x4aaa80 (slot 0x34): combine with another query; mode 2 also merges its flags.
		void __fastcall slot_34(void* q, void*, std::uint32_t mode, void* const* source_ref)
		{
			set_op(q, mode, set_of(*source_ref));
			if (mode == 2)
			{
				u32(q, g.l.q_flags_a) |= u32(*source_ref, g.l.q_flags_a);
				u32(q, g.l.q_flags_b) |= u32(*source_ref, g.l.q_flags_b);
			}
		}
		// 0x4aac50 (slot 0x38): combine with a grid cell's set (Grid slot 0x34).
		void __fastcall slot_38(void* q, void*, std::uint32_t mode, std::uint32_t cell)
		{
			void* grid = grid_owner();
			set_op(q, mode, slot<grid_cell_set_fn>(grid, 0x34)(grid, nullptr, cell));
		}
		// 0x4aac80 (slot 0x3c): combine with the set named by a handle's name (World slot 0x34); widened local.
		void __fastcall slot_3c(void* q, void*, std::uint32_t mode, const std::uint32_t* handle)
		{
			EntitySet local{};
			void* world = world_of(q);
			const char* name = native<name_of_handle_fn>(native_name_of_handle)(handle, nullptr);
			if (slot<world_named_set_fn>(world, 0x34)(world, nullptr, name, local.words.data())) set_op(q, mode, local.words.data());
			else if (mode == 2) set_zero(set_of(q));
		}
		std::uint32_t property_flag(std::uint32_t property)
		{
			switch (property)
			{
			case 7: return 1; case 8: return 2; case 9: return 4; case 10: return 8; case 0xb: return 0x10;
			case 0xc: return 0x20; case 0xd: return 0x40; case 0xe: return 0x80; case 0x1a: return 0x100;
			default: return 0;
			}
		}
		// 0x4aacf0 (slot 0x40): combine with a property set; modes 1/2 record the property in the flag words.
		void __fastcall slot_40(void* q, void*, std::uint32_t mode, std::uint32_t property)
		{
			if (mode == 1) u32(q, g.l.q_flags_b) |= property_flag(property);
			else if (mode == 2) u32(q, g.l.q_flags_a) |= property_flag(property);
			void* world = world_of(q);
			set_op(q, mode, slot<world_property_set_fn>(world, 0x40)(world, nullptr, property));
		}
		// 0x4aaec0 (slot 0x44): combine with a World pair set (slot 0x50); widened local.
		void __fastcall slot_44(void* q, void*, std::uint32_t mode, std::uint32_t a, const std::uint32_t* b)
		{
			EntitySet local{};
			void* world = world_of(q);
			if (slot<world_pair_set_fn>(world, 0x50)(world, nullptr, a, *b, local.words.data())) set_op(q, mode, local.words.data());
			else if (mode == 2) set_zero(set_of(q));
		}
		// 0x4aaf60 / 0x4aaf30 / 0x4aafd0 / 0x4aafa0 (slots 0x48 / 0x4c / 0x50 / 0x54): class sets of a record.
		void __fastcall slot_48(void* q, void*, std::uint32_t mode, const char* name)
		{
			const auto* record = native<record_by_name_fn>(native_record_by_name)(name);
			void* world = world_of(q);
			set_op(q, mode, slot<world_property_set_fn>(world, 0x40)(world, nullptr, global32(global_class_base) + 0x24 + u32(record, 4)));
		}
		void __fastcall slot_4c(void* q, void*, std::uint32_t mode, const std::uint8_t* record)
		{
			void* world = world_of(q);
			set_op(q, mode, slot<world_property_set_fn>(world, 0x40)(world, nullptr, global32(global_class_base) + 0x24 + u32(record, 4)));
		}
		void __fastcall slot_50(void* q, void*, std::uint32_t mode, const char* name)
		{
			const auto* record = native<record_by_name_fn>(native_record_by_name)(name);
			void* world = world_of(q);
			set_op(q, mode, slot<world_property_set_fn>(world, 0x40)(world, nullptr, u32(record, 4) + 0x24));
		}
		void __fastcall slot_54(void* q, void*, std::uint32_t mode, const std::uint8_t* record)
		{
			void* world = world_of(q);
			set_op(q, mode, slot<world_property_set_fn>(world, 0x40)(world, nullptr, u32(record, 4) + 0x24));
		}
		// 0x4aa550 (slot 0x58) / 0x4aa4d0 (slot 0x5c): invert, clear flags, intersect with a record's class set.
		void __fastcall slot_58(void* q, void*)
		{
			set_invert(set_of(q));
			u32(q, g.l.q_flags_a) = 0;
			u32(q, g.l.q_flags_b) = 0;
			slot<void (__fastcall*)(void*, void*, std::uint32_t, const std::uint8_t*)>(q, 0x54)(q, nullptr, 2, at(global_record_718444));
		}
		void __fastcall slot_5c(void* q, void*, const std::uint8_t* record)
		{
			set_invert(set_of(q));
			u32(q, g.l.q_flags_a) = 0;
			u32(q, g.l.q_flags_b) = 0;
			slot<void (__fastcall*)(void*, void*, std::uint32_t, const std::uint8_t*)>(q, 0x54)(q, nullptr, 2, record);
		}
		// 0x4ab000 (slot 0x60): keep the entities inside a cone around a direction. The dot product is x87 in the
		// native code (products and sums at the process's precision control); the same instruction sequence is used
		// here so the result equals the native one under whichever precision mode the game runs.
		double dot_x87(const float* v, const float* d)
		{
			double result;
			// (v.z * d.z + v.y * d.y) + v.x * d.x, the 0x4ab173..0x4ab18d sequence, popped at full x87 width
			__asm mov eax, v
			__asm mov ecx, d
			__asm fld dword ptr [eax + 8]
			__asm fmul dword ptr [ecx + 8]
			__asm fld dword ptr [eax + 4]
			__asm fmul dword ptr [ecx + 4]
			__asm faddp st(1), st(0)
			__asm fld dword ptr [eax]
			__asm fmul dword ptr [ecx]
			__asm faddp st(1), st(0)
			__asm fstp qword ptr [result]
			return result;
		}
		void __fastcall slot_60(void* q, void*, const float* origin, const float* direction, float max_dot, float min_dot, std::uint32_t flags)
		{
			float dir[3] = {direction[0], direction[1], direction[2]};
			mask(dir, flags);
			normalize(dir);
			auto* set = set_of(q);
			auto i = first_bit(set);
			if (i >= g.l.world) return;
			void* world = world_of(q);
			do
			{
				std::uint8_t* entity = slot<world_find_dense_fn>(world, 0x10)(world, nullptr, i);
				std::uint32_t handle = u32(entity, entity_token_offset);
				const float* p = reinterpret_cast<const float*>(collisions_find(collisions_of(q), &handle));
				if (!p) p = reinterpret_cast<const float*>(entity + 0x20);
				float v[3] = {p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
				mask(v, flags);
				normalize(v);
				const double dot = dot_x87(v, dir);
				if (max_dot < dot || dot < min_dot) bit_clear(set, i);
				i = next_bit(set, i + 1);
			} while (i < g.l.world);
		}
		// 0x4aa0c0 (slot 0x64): slot 0x60 with the direction built from angles (0x560990).
		void __fastcall slot_64(void* q, void*, const float* origin, const float* angles, float max_dot, float min_dot, std::uint32_t flags)
		{
			float local[3];
			const float* dir = native<angles_to_direction_fn>(native_angles_to_direction)(local, angles);
			slot<void (__fastcall*)(void*, void*, const float*, const float*, float, float, std::uint32_t)>(q, 0x60)(q, nullptr, origin, dir, max_dot, min_dot, flags);
		}
		// 0x4aa100 (slot 0x68) / 0x4aa130 (slot 0x6c): physics owner slots 8 / 0 / 4 on the query set.
		void __fastcall slot_68(void* q, void*, std::uint32_t a, std::uint32_t b, std::uint32_t, std::uint8_t inverse)
		{
			void* collide = collide_owner();
			slot<collide_8_fn>(collide, 0x8)(collide, nullptr, a, b, set_of(q), inverse == 0 ? 1u : 0u);
		}
		void __fastcall slot_6c(void* q, void*, std::uint32_t a, std::uint32_t b, std::uint8_t inverse, std::uint8_t simple)
		{
			void* collide = collide_owner();
			if (simple != 0) slot<collide_0_fn>(collide, 0x0)(collide, nullptr, a, b, set_of(q));
			else slot<collide_4_fn>(collide, 0x4)(collide, nullptr, a, b, set_of(q), inverse == 0 ? 1u : 0u);
		}
		// 0x4aa180 (slot 0x74): a physics trace (collide slot 0x10) with this query's set and collisions; the
		// returned handle becomes the result.
		void __fastcall slot_74(void* q, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c)
		{
			void* collide = collide_owner();
			std::uint32_t local = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(q));
			const std::uint32_t b2 = u8(q, g.l.q_b2);
			const auto* result = slot<collide_10_fn>(collide, 0x10)(collide, nullptr, &local, a, b, c, u32(q, g.l.q_flags_a), u32(q, g.l.q_flags_b),
			                                                     u8(q, g.l.q_b0), u8(q, g.l.q_b1), set_of(q), collisions_of(q), b2, b2, 0);
			u32(q, g.l.q_result) = *result;
		}
		// 0x4aa1e0 (slot 0x70): slot 0x74 from a base point along a direction built from angles, scaled. The x87
		// sequence of the native code: x and y products stay in x87 precision until the add, z is rounded first.
		void __fastcall slot_70(void* q, void*, const float* base, const float* angles, float scale, std::uint32_t flags)
		{
			float local[3];
			float z_product;
			native<angles_to_direction_fn>(native_angles_to_direction)(local, angles);
			// the 0x4aa1f5..0x4aa23d sequence: x, y products stay on the x87 stack until their add; z is rounded first
			__asm lea eax, local
			__asm mov ecx, base
			__asm fld dword ptr [eax]
			__asm fmul dword ptr [scale]
			__asm fld dword ptr [eax + 4]
			__asm fmul dword ptr [scale]
			__asm fld dword ptr [eax + 8]
			__asm fmul dword ptr [scale]
			__asm fstp dword ptr [z_product]
			__asm fxch st(1)
			__asm fadd dword ptr [ecx]
			__asm fstp dword ptr [eax]
			__asm fadd dword ptr [ecx + 4]
			__asm fstp dword ptr [eax + 4]
			__asm fld dword ptr [z_product]
			__asm fadd dword ptr [ecx + 8]
			__asm fstp dword ptr [eax + 8]
			slot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t)>(q, 0x74)(q, nullptr,
				static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base)), static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(local)), flags);
		}
		// 0x4aa250 (slot 0x78): a physics trace from an entity's own position (entity slot 0x28).
		void __fastcall slot_78(void* q, void*, std::uint32_t a, std::uint32_t b, const std::uint32_t* handle)
		{
			if (*handle == 0 || !handle_valid(handle)) return;
			std::uint8_t* entity = entity_of(handle);
			void* collide = collide_owner();
			std::uint8_t* again = entity_of(handle);
			float position[3];
			const float* p = slot<entity_vec_fn>(entity, 0x28)(entity, nullptr, position);
			std::uint32_t local = 0;
			const auto* result = slot<collide_10_fn>(collide, 0x10)(collide, nullptr, &local, a, b, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)),
			                                                     u32(q, g.l.q_flags_a), u32(q, g.l.q_flags_b), u8(q, g.l.q_b0), u8(q, g.l.q_b1), set_of(q), collisions_of(q),
			                                                     0, 1, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(again)));
			u32(q, g.l.q_result) = *result;
		}
		// 0x4aa2e0 (slot 0x7c): physics owner slot 0xc with the set and the collisions.
		void __fastcall slot_7c(void* q, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
		{
			void* collide = collide_owner();
			slot<collide_c_fn>(collide, 0xc)(collide, nullptr, a, b, c, set_of(q), d, collisions_of(q));
		}
		// 0x4aa310 / own setter for the shared 0x61fde0 / 0x4aa320 (slots 0x80 / 0x84 / 0x88): the three flag bytes.
		void __fastcall slot_80(void* q, void*, std::uint8_t value) { u8(q, g.l.q_b2) = value; }
		void __fastcall slot_84(void* q, void*, std::uint8_t value) { u8(q, g.l.q_b0) = value; } // 0x61fde0 writes +0x30; the field moved
		void __fastcall slot_88(void* q, void*, std::uint8_t value) { u8(q, g.l.q_b1) = value; }
		// 0x4ab1f0 (slot 0x8c): export the set into the caller's set (union / subtract / intersect); widened local.
		void __fastcall slot_8c(void* q, void*, std::uint32_t mode, std::uint32_t* out)
		{
			const auto* set = set_of(q);
			if (mode == 0) { for (std::uint32_t w = 0; w < g.l.set_words; ++w) out[w] |= set[w]; }
			else if (mode == 1)
			{
				EntitySet local{};
				set_copy(local.words.data(), set);
				set_invert(local.words.data());
				for (std::uint32_t w = 0; w < g.l.set_words; ++w) out[w] &= local.words[w];
			}
			else if (mode == 2) { for (std::uint32_t w = 0; w < g.l.set_words; ++w) out[w] &= set[w]; }
		}

		const void* const query_vtable_table[36] = {
			reinterpret_cast<const void*>(&slot_00), reinterpret_cast<const void*>(&slot_04), reinterpret_cast<const void*>(&slot_08), reinterpret_cast<const void*>(&slot_0c),
			reinterpret_cast<const void*>(&slot_10), reinterpret_cast<const void*>(&slot_14), reinterpret_cast<const void*>(&slot_18), reinterpret_cast<const void*>(&slot_1c),
			reinterpret_cast<const void*>(&slot_20), reinterpret_cast<const void*>(&slot_24), reinterpret_cast<const void*>(&slot_28), reinterpret_cast<const void*>(&slot_2c),
			reinterpret_cast<const void*>(&slot_30), reinterpret_cast<const void*>(&slot_34), reinterpret_cast<const void*>(&slot_38), reinterpret_cast<const void*>(&slot_3c),
			reinterpret_cast<const void*>(&slot_40), reinterpret_cast<const void*>(&slot_44), reinterpret_cast<const void*>(&slot_48), reinterpret_cast<const void*>(&slot_4c),
			reinterpret_cast<const void*>(&slot_50), reinterpret_cast<const void*>(&slot_54), reinterpret_cast<const void*>(&slot_58), reinterpret_cast<const void*>(&slot_5c),
			reinterpret_cast<const void*>(&slot_60), reinterpret_cast<const void*>(&slot_64), reinterpret_cast<const void*>(&slot_68), reinterpret_cast<const void*>(&slot_6c),
			reinterpret_cast<const void*>(&slot_70), reinterpret_cast<const void*>(&slot_74), reinterpret_cast<const void*>(&slot_78), reinterpret_cast<const void*>(&slot_7c),
			reinterpret_cast<const void*>(&slot_80), reinterpret_cast<const void*>(&slot_84), reinterpret_cast<const void*>(&slot_88), reinterpret_cast<const void*>(&slot_8c)};

		// 0x4aa030: the query constructor (World* from the getter, zeroed fields, collisions, reset).
		void query_construct(void* q)
		{
			u32(q, 0) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(query_vtable_table));
			// The World pointer the native constructor fetches through the getter; at preparation (DllMain, before the
			// executable's CRT ran) the retail getter must not run (its constructor registers atexit), so the installed
			// World's address comes from the descriptor when the integrator has it.
			u32(q, g.l.q_world) = g.d.raised && g.d.world_object ? g.d.world_object : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(current_world()));
			set_zero(set_of(q));
			u8(q, g.l.q_busy) = 0;
			u32(q, g.l.q_result) = 0;
			collisions_construct(collisions_of(q));
			std::uint32_t none = 0;
			slot_08(q, nullptr, &none);
		}

		// ---- CStaticOrder ------------------------------------------------------------------------------------------
		std::uint8_t* pair_at(void* order, std::uint32_t i) { return bytes(order) + g.l.o_pairs + i * 8; }
		float pair_score(void* order, std::uint32_t i) { return f32(order, g.l.o_pairs + i * 8 + 4); }
		void pair_swap(void* order, std::uint32_t a, std::uint32_t b)
		{
			std::uint8_t t[8];
			std::memcpy(t, pair_at(order, a), 8);
			std::memcpy(pair_at(order, a), pair_at(order, b), 8);
			std::memcpy(pair_at(order, b), t, 8);
		}
		// 0x4a98d0: ascending heap sort of the pairs by score. "!(a < b)" is the native FCOMP/TEST AH,5/JP test
		// (taken when a >= b or unordered).
		void order_sort(void* order)
		{
			const int count = static_cast<int>(u32(order, g.l.o_count));
			if (count > 1)
			{
				for (int n = 1; n < count; ++n)
				{
					int child = n;
					for (;;)
					{
						const int parent = (child - 1) / 2;
						if (!(pair_score(order, parent) < pair_score(order, child))) break;
						pair_swap(order, parent, child);
						child = parent;
					}
				}
			}
			int end = count - 1;
			while (end > 0)
			{
				pair_swap(order, 0, end);
				int node = 0;
				int child;
				if (end < 2) child = 0;
				else if (end < 3 || pair_score(order, 2) < pair_score(order, 1)) child = 1;
				else child = 2;
				for (;;)
				{
					if (!(pair_score(order, node) < pair_score(order, child))) break;
					pair_swap(order, node, child);
					node = child;
					const int left = child * 2 + 1;
					if (left < end)
					{
						const int right = child * 2 + 2;
						if (right < end && !(pair_score(order, right) < pair_score(order, left))) child = right;
						else child = left;
					}
				}
				--end;
			}
		}
		// 0x4a9ac0: fill the order from a query's set with a scoring policy (sorted when any score is non-zero).
		void order_fill(void* order, void* query, void* policy)
		{
			const auto* set = set_of(query);
			u32(order, g.l.o_query) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(query));
			void* world = current_world();
			u32(order, g.l.o_count) = 0;
			bool need_sort = false;
			auto i = first_bit(set);
			if (i >= g.l.world) return;
			do
			{
				std::uint8_t* entity = slot<world_find_dense_fn>(world, 0x10)(world, nullptr, i);
				const double score = slot<policy_score_fn>(policy, 0x0)(policy, nullptr, entity); // ST0
				auto* pair = pair_at(order, u32(order, g.l.o_count));
				u32(pair, 0) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(entity));
				f32(pair, 4) = static_cast<float>(score); // FST float
				u32(order, g.l.o_count) += 1;
				if (!(score == 0.0)) need_sort = true;    // FUCOMPP: non-zero or unordered
				i = next_bit(set, i + 1);
			} while (i < g.l.world);
			if (need_sort) order_sort(order);
		}
		std::uint32_t __fastcall order_slot_0(void* order, void*) { return u32(order, g.l.o_count); }
		void* __fastcall order_slot_4(void* order, void*, std::uint32_t i) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(u32(pair_at(order, i), 0))); }
		// 0x4a9d50: the trace record of the i-th entity in the order's query (null when the query has no traces).
		void __fastcall order_slot_8(void* order, void*, std::uint32_t* out, std::uint32_t i)
		{
			void* query = reinterpret_cast<void*>(static_cast<std::uintptr_t>(u32(order, g.l.o_query)));
			// Native reads query+trace_count before testing query for null (a null query faults there); the test
			// order is swapped so a null query reaches the same "*out = 0" result instead of faulting.
			if (query != nullptr && u32(collisions_of(query), g.l.c_slot_count) != 0)
			{
				const auto entity = u32(pair_at(order, i), 0);
				if (entity != 0)
				{
					const auto b = map_of(collisions_of(query))[dense_of(u32(reinterpret_cast<void*>(static_cast<std::uintptr_t>(entity)), entity_token_offset)).value];
					*out = b != 0xff ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pool_record(b))) : 0;
					return;
				}
			}
			*out = 0;
		}
		const void* const order_vtable_table[3] = {reinterpret_cast<const void*>(&order_slot_0), reinterpret_cast<const void*>(&order_slot_4), reinterpret_cast<const void*>(&order_slot_8)};
		// 0x4a9d10
		void order_construct(void* order)
		{
			u32(order, 0) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(order_vtable_table));
			std::memset(pair_at(order, 0), 0, g.l.world * 8); // 0x4a9870
			u32(order, g.l.o_count) = 0;
			u8(order, g.l.o_busy) = 0;
		}

		// ---- QueryRef acquire/release ------------------------------------------------------------------------------
		std::uint8_t* acquire()
		{
			void* m = manager();
			for (std::uint32_t i = 0; i < layout::query_count; ++i)
			{
				auto* q = query_at(m, i);
				if (u8(q, g.l.q_busy) == 0)
				{
					u8(q, g.l.q_busy) = 1;
					return q;
				}
			}
			return nullptr; // native: *ref = 0 then a call through the null vtable (access violation at 0x4..0x10)
		}
		void release(void** ref)
		{
			void* q = *ref;
			manager();
			if (q)
			{
				u8(q, g.l.q_busy) = 0;
				collisions_release_all(collisions_of(q));
			}
		}
		struct scoped_query
		{
			void* q = nullptr;
			~scoped_query() { release(&q); } // the native SEH frames of 0x4ab7f0/0x4ab890/0x4ab960/0x4aba30
		};
		std::uint8_t* trace_record(void** ref) { return static_cast<std::uint8_t*>(*ref); }
	}

	// ---- public: manager ---------------------------------------------------------------------------------------------
	std::size_t manager_size(const descriptor& d) { return layout_for(d.world).m_size; }
	void* create_manager(const descriptor& d)
	{
		if (g.manager || d.world == 0 || d.world > max_world || d.world % 32 != 0) return nullptr;
		const auto l = layout_for(d.world);
		auto* m = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, l.m_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!m) return nullptr;
		g.d = d;
		g.l = l;
		g.manager = m;
		g.manager_bytes = l.m_size;
		for (std::uint32_t i = 0; i < layout::query_count; ++i) query_construct(query_at(m, i)); // 0x4ab4e0 order
		for (std::uint32_t i = 0; i < layout::order_count; ++i) order_construct(order_at(m, i));
		return m;
	}
	void destroy_manager(void* manager_pointer)
	{
		if (!manager_pointer || manager_pointer != g.manager) return;
		VirtualFree(manager_pointer, 0, MEM_RELEASE);
		g.manager = nullptr;
		g.manager_bytes = 0;
		g.d = stock_descriptor;
		g.l = layout_for(stock_world);
	}
	void* installed_manager() { return g.manager; }
	const descriptor& installed_descriptor() { return g.d; }
	const layout& installed_layout() { return g.l; }
	bool owns_query(const void* query)
	{
		if (!g.manager || !query) return false;
		const auto* p = static_cast<const std::uint8_t*>(query);
		if (p < g.manager + g.l.m_queries || p >= g.manager + g.l.m_tail) return false;
		return (p - (g.manager + g.l.m_queries)) % g.l.q_stride == 0;
	}
	bool owns_order(const void* order)
	{
		if (!g.manager || !order) return false;
		const auto* p = static_cast<const std::uint8_t*>(order);
		if (p < g.manager + g.l.m_orders || p >= g.manager + g.l.m_size) return false;
		return (p - (g.manager + g.l.m_orders)) % g.l.o_stride == 0;
	}
	const void* const* query_vtable() { return query_vtable_table; }
	const void* const* order_vtable() { return order_vtable_table; }
	const void* const* collisions_vtable() { return collisions_vtable_table; }
	void set_manager_getter(manager_getter_fn getter) { g.getter = getter; }

	// ---- public: the bridge functions ----------------------------------------------------------------------------------
	bool __fastcall QueryApi_004a9580(void** ref, void*) { return *ref != nullptr; }
	void* __fastcall QueryApi_004a9590(void** ref, void*)
	{
		if (!*ref) return nullptr;
		void* world = current_world();
		return slot<world_find_token_fn>(world, 0xc)(world, nullptr, u32(trace_record(ref), 0xc));
	}
	bool __fastcall QueryApi_004a95c0(void** ref, void*)
	{
		if (!*ref) return false;
		std::uint32_t local = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ref));
		const auto* player = player_handle(&local);
		return u32(trace_record(ref), 0xc) == *player;
	}
	const float* __fastcall QueryApi_004a95f0(void** ref, void*) { return *ref ? reinterpret_cast<const float*>(trace_record(ref)) : zero_vector(); }
	const float* __fastcall QueryApi_004a9600(void** ref, void*) { return *ref ? reinterpret_cast<const float*>(trace_record(ref) + 0x10) : zero_vector(); }
	const float* __fastcall QueryApi_004a9610(void** ref, void*) { return *ref ? reinterpret_cast<const float*>(trace_record(ref) + 0x20) : zero_vector(); }
	const float* __fastcall QueryApi_004a9620(void** ref, void*) { return *ref ? reinterpret_cast<const float*>(trace_record(ref) + 0x30) : zero_vector(); }
	float __fastcall QueryApi_004a9630(void** ref, void*) { return *ref ? f32(trace_record(ref), 0x3c) : 0.0f; }
	const std::uint8_t* __fastcall QueryApi_004a97a0(void** ref, void*)
	{
		auto& init = global32(global_material_init);
		auto* material = at(global_material_byte);
		if ((init & 1) == 0)
		{
			init |= 1;
			*material = 0;
		}
		*material = 0x10; // 0x4876e0(0x756b08, 0x10)
		if (*ref)
		{
			void* world = current_world();
			std::uint8_t* entity = slot<world_find_token_fn>(world, 0xc)(world, nullptr, u32(trace_record(ref), 0xc));
			if (entity)
			{
				const std::uint8_t* descriptor_record = slot<entity_void_fn>(entity, 0x0)(entity, nullptr);
				const std::uint32_t id = global32(global_class_flag_id) + 0x24;
				if (u32(descriptor_record, 0x14 + (static_cast<std::int32_t>(id) >> 5) * 4) & (1u << (id & 31)))
				{
					if (*slot<entity_void_fn>(entity, 0x194)(entity, nullptr) != 0x10)
					{
						*material = *slot<entity_void_fn>(entity, 0x194)(entity, nullptr);
						return material;
					}
				}
			}
			if (*ref) *material = static_cast<std::uint8_t>(u32(trace_record(ref), 0x2c));
		}
		return material;
	}
	void** __fastcall QueryApi_004a9730(void** policy, void*, void** query_ref)
	{
		policy[0] = reinterpret_cast<void*>(static_cast<std::uintptr_t>(native_policy_vtable));
		policy[1] = *query_ref;
		return policy; // EAX = this, as the native function: callers pass it straight to 0x4ab770 as the policy
	}
	double __fastcall QueryApi_004a9a80(void** policy, void*, const std::uint8_t* entity)
	{
		void* query = policy[1];
		const std::uint32_t token = u32(entity, entity_token_offset);
		std::uint8_t b;
		if (owns_query(query)) b = map_of(collisions_of(query))[dense_of(token).value];   // the wide layout
		else b = bytes(query)[0x38 + 4 + (token & 0xff)];                                  // retail layout (0x4a9a8a..0x4a9a92)
		if (b != 0xff) return f32(pool_record(b), 0x3c);
		return 0.0;
	}
	void* __fastcall QueryApi_004a98b0(void* list, void*)
	{
		u32(list, 0x34) = 0; // 0x58b6b0
		return list;
	}
	bool __cdecl QueryApi_004a9650(std::uint32_t a, std::uint32_t b, const float* pos, const float* delta, std::uint32_t* out, void** query_ref)
	{
		if (a != 0 && handle_valid(&a))
		{
			void* q = *query_ref;
			slot<void (__fastcall*)(void*, void*, std::uint32_t, const std::uint32_t*)>(q, 0x30)(q, nullptr, 1, &a);
			if (b != 0 && handle_valid(&b)) slot<void (__fastcall*)(void*, void*, std::uint32_t, const std::uint32_t*)>(q, 0x30)(q, nullptr, 1, &b);
			slot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t)>(q, 0x40)(q, nullptr, 1, 1);
			float sum[3] = {pos[0] + delta[0], pos[1] + delta[1], pos[2] + delta[2]};
			slot<void (__fastcall*)(void*, void*, std::uint8_t)>(q, 0x88)(q, nullptr, 1);
			slot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, const std::uint32_t*)>(q, 0x78)(q, nullptr,
				static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pos)), static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(sum)), &a);
			std::uint32_t local = 0;
			const auto* result = slot<std::uint32_t* (__fastcall*)(void*, void*, std::uint32_t*)>(q, 0x20)(q, nullptr, &local);
			*out = *result;
			return *result != 0;
		}
		*out = 0;
		return *out != 0;
	}
	void** __fastcall QueryApi_004ab5b0(void** ref, void*, std::uint32_t all)
	{
		std::uint8_t* q = acquire();
		*ref = q;
		if (!q) fail_fast("all three static queries are busy (0x4ab5b0)");
		slot<void (__fastcall*)(void*, void*, std::uint8_t)>(q, 0x10)(q, nullptr, static_cast<std::uint8_t>(all));
		return ref;
	}
	void** __fastcall QueryApi_004ab610(void** ref, void*, void** source_ref)
	{
		std::uint8_t* q = acquire();
		*ref = q;
		if (!q) fail_fast("all three static queries are busy (0x4ab610)");
		slot<void (__fastcall*)(void*, void*, void* const*)>(q, 0xc)(q, nullptr, source_ref);
		return ref;
	}
	void** __fastcall QueryApi_004ab670(void** ref, void*, const std::uint32_t* exclude)
	{
		std::uint8_t* q = acquire();
		*ref = q;
		if (!q) fail_fast("all three static queries are busy (0x4ab670)");
		slot<void (__fastcall*)(void*, void*, const std::uint32_t*)>(q, 0x8)(q, nullptr, exclude);
		return ref;
	}
	void** __fastcall QueryApi_004ab6d0(void** ref, void*, const void* record, const std::uint32_t* exclude)
	{
		std::uint8_t* q = acquire();
		*ref = q;
		if (!q) fail_fast("all three static queries are busy (0x4ab6d0)");
		slot<void (__fastcall*)(void*, void*, const void*, const std::uint32_t*)>(q, 0x4)(q, nullptr, record, exclude);
		return ref;
	}
	void __fastcall QueryApi_004ab720(void** ref, void*) { release(ref); }
	int __cdecl QueryApi_004ab740()
	{
		void* m = manager();
		int free = 0;
		for (std::uint32_t i = 0; i < layout::query_count; ++i) if (u8(query_at(m, i), g.l.q_busy) == 0) ++free;
		return free;
	}
	void* __cdecl QueryApi_004ab770(void* query, void* policy)
	{
		void* m = manager();
		for (std::uint32_t i = 0; i < layout::order_count; ++i)
		{
			auto* o = order_at(m, i);
			if (u8(o, g.l.o_busy) == 0)
			{
				u8(o, g.l.o_busy) = 1;
				order_fill(o, query, policy);
				return o;
			}
		}
		return nullptr;
	}
	void* __cdecl QueryApi_004ab7d0(void* order)
	{
		manager();
		if (order) u8(order, g.l.o_busy) = 0;
		return order; // EAX = the argument, as the native function
	}
	bool __cdecl QueryApi_004ab7f0(std::uint32_t a, std::uint32_t b, const float* pos, const float* delta, std::uint32_t* out, void** source_ref)
	{
		scoped_query ref;
		QueryApi_004ab610(&ref.q, nullptr, source_ref);
		return QueryApi_004a9650(a, b, pos, delta, out, &ref.q);
	}
	bool __cdecl QueryApi_004ab890(std::uint32_t a, std::uint32_t b, const float* pos, const float* delta, std::uint32_t* out, std::uint8_t keep_player)
	{
		scoped_query ref;
		std::uint32_t none = 0;
		QueryApi_004ab670(&ref.q, nullptr, &none);
		void* q = ref.q;
		if (keep_player == 0)
		{
			std::uint32_t player = 0; // the native code reuses its argument slot as the out cell
			const auto* handle = player_handle(&player);
			slot<void (__fastcall*)(void*, void*, std::uint32_t, const std::uint32_t*)>(q, 0x30)(q, nullptr, 1, handle);
		}
		slot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t)>(q, 0x40)(q, nullptr, 1, 1);
		return QueryApi_004a9650(a, b, pos, delta, out, &ref.q);
	}
	namespace
	{
		bool trace_and_report(void* q, std::uint32_t exclude, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t* out)
		{
			if (exclude != 0 && handle_valid(&exclude)) slot<void (__fastcall*)(void*, void*, std::uint32_t, const std::uint32_t*)>(q, 0x30)(q, nullptr, 1, &exclude);
			slot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t)>(q, 0x40)(q, nullptr, 1, 1);
			slot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t)>(q, 0x74)(q, nullptr, a, b, c);
			std::uint32_t local = 0;
			const auto* result = slot<std::uint32_t* (__fastcall*)(void*, void*, std::uint32_t*)>(q, 0x20)(q, nullptr, &local);
			*out = *result;
			return *result != 0;
		}
	}
	bool __cdecl QueryApi_004ab960(std::uint32_t exclude, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t* out)
	{
		scoped_query ref;
		std::uint32_t none = 0;
		QueryApi_004ab670(&ref.q, nullptr, &none);
		return trace_and_report(ref.q, exclude, a, b, c, out);
	}
	bool __cdecl QueryApi_004aba30(std::uint32_t exclude, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t* out)
	{
		scoped_query ref;
		std::uint32_t none = 0;
		QueryApi_004ab670(&ref.q, nullptr, &none);
		void* q = ref.q;
		using record_op = void (__fastcall*)(void*, void*, std::uint32_t, const std::uint8_t*);
		using property_op = void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t);
		slot<record_op>(q, 0x54)(q, nullptr, 2, at(0x778a4c));
		slot<record_op>(q, 0x4c)(q, nullptr, 0, at(0x753f64));
		slot<record_op>(q, 0x54)(q, nullptr, 0, at(0x717ac4));
		slot<record_op>(q, 0x54)(q, nullptr, 0, at(0x72bac4));
		slot<property_op>(q, 0x40)(q, nullptr, 2, 0xe);
		return trace_and_report(q, exclude, a, b, c, out);
	}
}
