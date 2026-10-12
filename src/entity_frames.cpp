// Phase A entity capacity: the widened caller frames. See entity_frames.hpp for the contract and the retail
// addresses. Every function below is annotated with the retail listing addresses it reproduces; the x87 compare
// semantics (which branch an FCOMP/FUCOMPP takes for less / equal / greater / unordered) are spelled out per site.

#include "entity_frames.hpp"

#include <cstdlib>
#include <cstring>

namespace entity_frames
{
	using namespace entity_capacity_layout;

	namespace
	{

		const descriptor* installed = &stock_descriptor;

		template <typename T> T& at(std::uint32_t va) { return *reinterpret_cast<T*>(static_cast<std::uintptr_t>(va)); }
		template <typename T> T& field(void* base, std::uint32_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
		template <typename T> const T& field(const void* base, std::uint32_t offset) { return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(base) + offset); }
		std::uint8_t* bytes(void* base, std::uint32_t offset) { return static_cast<std::uint8_t*>(base) + offset; }
		float* floats(void* base, std::uint32_t offset) { return reinterpret_cast<float*>(static_cast<std::uint8_t*>(base) + offset); }
		template <typename Fn> Fn slot(const void* object, std::uint32_t offset)
		{
			return reinterpret_cast<Fn>((*static_cast<void* const* const*>(object))[offset / 4]);
		}
		template <typename Fn> Fn iat_fn(std::uint32_t iat) { return *reinterpret_cast<Fn*>(static_cast<std::uintptr_t>(iat)); }

		// ---- the native interfaces the frames call (conventions from the retail listings) ------------------------
		using getter_fn = void* (__cdecl*)();
		using grid_cells_fn = std::uint8_t(__fastcall*)(void*, void*, const float*, const float*, EntitySet*);          // Grid slot 0x30, RET 0xc
		using world_find_fn = std::uint8_t(__fastcall*)(void*, void*, const char*, EntitySet*);                         // World slot 0x34, RET 8
		using world_handle_fn = std::uint32_t* (__fastcall*)(void*, void*, std::uint32_t*, std::uint32_t);           // World slot 0x18, RET 8
		using entity_of_dense_fn = void* (__cdecl*)(std::uint32_t);
		using player_handle_fn = std::uint32_t* (__cdecl*)(std::uint32_t*);
		using handle_fn = void* (__fastcall*)(std::uint32_t*, void*);
		using handle_valid_fn = std::uint8_t(__fastcall*)(std::uint32_t*, void*);
		using box_test_fn = std::uint8_t(__fastcall*)(const float*, void*, const float*);
		using context_ctor_fn = void* (__fastcall*)(void*, void*, void*, const float*, const float*, const float*, EntitySet*);
		using record_ctor_fn = void* (__fastcall*)(void*, void*);
		using record_fill_fn = std::uint8_t(__fastcall*)(void*, void*, void*);
		using context_box_fn = float* (__fastcall*)(void*, void*, float*);
		using commit_fn = void(__fastcall*)(void*, void*, void*, void*, std::uint32_t);
		using capsule_fn = void(__cdecl*)(void*);
		using entity_descriptor_fn = const std::uint8_t* (__fastcall*)(void*, void*);          // entity slot 0
		using entity_pointer_fn = float* (__fastcall*)(void*, void*);                          // entity slots 0x38 / 0x4c / 0x54 (as void*)
		using entity_object_fn = void* (__fastcall*)(void*, void*);
		using entity_out_fn = float* (__fastcall*)(void*, void*, float*);                      // entity slots 0x24 / 0x58, component slot 0x24
		using sweep9_fn = std::uint8_t(__fastcall*)(void*, void*, const float*, const float*, const float*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, float*, const float*);
		using sweep10_fn = std::uint8_t(__fastcall*)(void*, void*, const float*, const float*, const float*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, float*, const float*, const float*);
		using component_fn = void* (__cdecl*)(void*);
		using reachable_fn = std::uint8_t(__fastcall*)(void*, void*, const float*);
		using pathgrid_box_fn = void(__fastcall*)(void*, void*, std::uint8_t*, float*, float*);
		using pathgrid_center_fn = void(__fastcall*)(void*, void*, std::uint8_t*, float*);
		using pathgrid_cell_fn = std::uint8_t* (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t);
		using name_table_fn = std::uint8_t* (__cdecl*)();
		using snprintf_fn = int(__cdecl*)(char*, int, const char*, ...);
		using strncpy_fn = char* (__cdecl*)(char*, const char*, std::size_t);
		using int_method_fn = int(__fastcall*)(void*, void*);
		using set_state_fn = void(__fastcall*)(void*, void*, int);
		using timing_fn = void(__fastcall*)(void*, void*, float, float);
		using notify_fn = void(__fastcall*)(void*, void*, int, int, int);
		using bits_method_fn = void(__fastcall*)(void*, void*, std::uint32_t);
		using vec_set3_fn = void(__fastcall*)(float*, void*, float, float, float);
		using vec_copy_fn = void(__fastcall*)(float*, void*, const float*);         // set(const igVec3f&), makeMin, makeMax, preScale, setTranslation
		using vec_float_fn = float(__fastcall*)(const float*, void*);              // length, length2
		using vec_result_fn = float* (__fastcall*)(const float*, void*, float*, const float*); // operator* / operator- (hidden result first)
		using vec_assign_fn = float* (__fastcall*)(float*, void*, const float*);
		using vec_index_fn = float* (__fastcall*)(float*, void*, int);
		using vec_ctor_fn = float* (__fastcall*)(float*, void*);
		using vec_transform_fn = void(__fastcall*)(float*, void*, const float*, const float*);

		void* grid_object() { return native<getter_fn>(native_grid_getter)(); }
		void* world_object() { return native<getter_fn>(native_world_getter)(); }
		void* entity_of_dense(std::uint32_t dense) { return native<entity_of_dense_fn>(native_entity_of_dense)(dense); }
		std::uint32_t world_capacity() { return installed->world; }
		std::uint32_t words_in_use() { return installed->set_words_in_use(); }
		std::uint32_t dense_of_token(std::uint32_t token) { return installed->tokens.index(WorldToken{token}).value; }

		// The native eight-dword set operations, over descriptor.world/32 words (eight at stock, 32 raised).
		void clear_set(EntitySet& set) { for (std::uint32_t w = 0; w < words_in_use(); ++w) set.words[w] = 0; }
		void and_into(EntitySet& target, const EntitySet& source) { for (std::uint32_t w = 0; w < words_in_use(); ++w) target.words[w] &= source.words[w]; }
		std::uint32_t first_bit(const EntitySet& set) { return first_set(set.words.data(), world_capacity()); }
		std::uint32_t next_bit(const EntitySet& set, std::uint32_t from) { return next_set(set.words.data(), from, world_capacity()); }

		// 0x44d9c4..0x44dc9c: the context constructor zeroes eight dwords of *set and stores ctx+0xd8 = 0 on both of its
		// failure paths (unsupported entity kind at 0x44d9d4; zero-length sweep at 0x44db8c); the success path keeps the
		// 1.0 stored at 0x44d9a8. The clear is widened to the installed set after the native call (same bytes at stock).
		void construct_context(void* context, void* entity, const float* start, const float* end, const float* extents, EntitySet* set)
		{
			native<context_ctor_fn>(native_context_ctor)(context, nullptr, entity, start, end, extents, set);
			if (field<float>(context, 0xd8) == 0.0f) clear_set(*set);
		}
		// 0x44de39 / 0x44e1d9 / 0x44e40d / 0x44e499 / 0x44e551 / 0x44e61a / 0x44e793: on a miss the record's entity leaves the
		// borrowed context set (*(ctx+0xd4)): native `token & 0xff`, here the descriptor's dense index.
		void clear_record_entity(void* context, void* record)
		{
			auto* set = field<EntitySet*>(context, 0xd4);
			const auto token = field<std::uint32_t>(field<void*>(record, 0), 0x1c);
			set->clear(dense_of_token(token));
		}
		// Class-descriptor flag test (0x49136c pattern): bit (id + 0x24) of the bitmap at descriptor+0x14.
		bool class_bit(const std::uint8_t* class_descriptor, std::uint32_t id_cell)
		{
			const std::int32_t id = at<std::int32_t>(id_cell) + 0x24;
			const auto word = field<std::uint32_t>(class_descriptor, 0x14 + static_cast<std::uint32_t>(id >> 5) * 4);
			return ((word >> (id & 31)) & 1u) != 0;
		}
		const std::uint8_t* entity_class(void* entity) { return slot<entity_descriptor_fn>(entity, 0)(entity, nullptr); }
		bool parity_even(std::uint32_t low_byte)
		{
			std::uint32_t bits = 0;
			for (std::uint32_t v = low_byte & 0xff; v; v >>= 1) bits += v & 1;
			return (bits & 1) == 0;
		}
		// igVec3f::length2(vector) compared with *rhs exactly as the retail `CALL length2; FCOMP [rhs]` pair: the
		// import's ST(0) result keeps whatever precision libIGMath leaves in it. Returns the status word's high byte
		// (C0 = 0x01, C2 = 0x04, C3 = 0x40).
		std::uint8_t compare_length2(const float* vector, const float* rhs)
		{
			void* function = iat_fn<void*>(iat_vec_length2);
			const float threshold = *rhs; // the FCOMP operand: a float32 in memory, as natively
			std::uint16_t status = 0;
			__asm {
				mov ecx, vector
				call function
				fcomp threshold
				fnstsw ax
				mov status, ax
			}
			return static_cast<std::uint8_t>(status >> 8);
		}
		float vec_length2(const float* vector) { return iat_fn<vec_float_fn>(iat_vec_length2)(vector, nullptr); }
		void vec_set3(float* vector, float x, float y, float z) { iat_fn<vec_set3_fn>(iat_vec_set3)(vector, nullptr, x, y, z); }
		void vec_copy(std::uint32_t iat, float* target, const float* source) { iat_fn<vec_copy_fn>(iat)(target, nullptr, source); }
		std::uint8_t box_overlaps(const float* bounds, const float* box) { return native<box_test_fn>(native_box_overlaps)(bounds, nullptr, box); }
		void commit_hit(void* context, void* record, void* filter, std::uint32_t flag) { native<commit_fn>(native_commit_hit)(context, nullptr, record, filter, flag); }
		std::uint8_t record_fill(void* record, void* entity) { return native<record_fill_fn>(native_record_fill)(record, nullptr, entity); }

		// 0x44dd49..0x44de0e and 0x44e33e..0x44e400: publish the capsule sweep's hit into the record and commit it.
		void commit_capsule_hit(void* context, void* record, void* filter)
		{
			// FLD [ctx+0x13c]; FLD 0.0; FCOMP: keep t unless 0.0 > t (less: C0), i.e. clamp negatives; NaN keeps t.
			const float t = field<float>(context, 0x13c);
			field<float>(record, 0x74) = t < at<float>(const_zero) ? at<float>(const_zero) : t;
			field<std::uint32_t>(record, 0x60) = field<std::uint32_t>(context, 0x178);
			field<std::uint32_t>(record, 0x58) = field<std::uint32_t>(context, 0x170);
			field<std::uint32_t>(record, 0x5c) = field<std::uint32_t>(context, 0x174);
			field<std::uint32_t>(record, 0x40) = field<std::uint32_t>(context, 0x160);
			field<std::uint32_t>(record, 0x38) = field<std::uint32_t>(context, 0x158);
			field<std::uint32_t>(record, 0x3c) = field<std::uint32_t>(context, 0x15c);
			field<float>(record, 0x38) = field<float>(context, 0x164) + field<float>(record, 0x38);
			field<float>(record, 0x3c) = field<float>(context, 0x168) + field<float>(record, 0x3c);
			field<float>(record, 0x40) = field<float>(context, 0x16c) + field<float>(record, 0x40);
			const float half = at<float>(const_half);
			field<float>(record, 0x38) = field<float>(record, 0x38) * half;
			field<float>(record, 0x3c) = field<float>(record, 0x3c) * half;
			field<float>(record, 0x40) = field<float>(record, 0x40) * half;
			commit_hit(context, record, filter, 1);
		}
		// 0x44dca8..0x44dcf2 / 0x44e219..0x44e272: the record's capsule parameters into the context's second capsule.
		// FLD a=[rec+8]; FLD b=[rec+4]; FCOMP: keep a unless b > a (greater: neither C0 nor C3); NaN keeps a.
		void publish_capsule(void* context, const void* record, float& radius, float& height)
		{
			field<std::uint32_t>(context, 0x110) = field<std::uint32_t>(record, 0x10);
			field<std::uint32_t>(context, 0x114) = field<std::uint32_t>(record, 0x14);
			field<std::uint32_t>(context, 0x118) = field<std::uint32_t>(record, 0x18);
			const float a = field<float>(record, 0x8), b = field<float>(record, 0x4);
			radius = b > a ? b : a;
			height = field<float>(record, 0xc);
			field<float>(context, 0x11c) = radius;
			field<float>(context, 0x120) = height;
		}
		// 0x44e650: the shorter extents choose which entity's bounds the model sweeps.
		// FSTP l1 (rounded to float); length2(record+4) in ST(0); FCOMP l1; TEST AH,5; JP -> 0x44e590. Even parity
		// (greater, equal, unordered) takes 0x44e590; only "less" takes 0x44e6a0.
		void dispatch_0044e650(void* context, void* record, void* filter)
		{
			const float context_extents2 = vec_length2(floats(context, 0x4));
			const auto status = compare_length2(floats(record, 0x4), &context_extents2);
			if (!parity_even(status & 0x5)) entity_capacity::Physics_0044e6a0(context, nullptr, record, filter);
			else entity_capacity::Physics_0044e590(context, nullptr, record, filter);
		}

		NavigationScratch scratch_table[navigation_scratch_slots];
		std::size_t scratch_recycle = 0;
	}

	void set_descriptor(const descriptor* d) { installed = d ? d : &stock_descriptor; }
	const descriptor& current_descriptor() { return *installed; }

	NavigationScratch& navigation_scratch(const void* owner)
	{
		for (auto& entry : scratch_table) if (entry.owner == owner) return entry;
		for (auto& entry : scratch_table) if (entry.owner == nullptr) { entry.owner = owner; return entry; }
		auto& entry = scratch_table[scratch_recycle++ % navigation_scratch_slots];
		entry.owner = owner;
		return entry;
	}

	// 0x44e2c2..0x44e2e5: |a/a_scale - b/b_scale| computed in x87 extended precision, FCOMP threshold, TEST AH,5, JP.
	// Odd parity (less) selects the ratio sweep 0x601280; even (greater, equal, unordered) selects 0x600dc0.
	bool capsule_ratio_below_threshold(float a, float a_scale, float b, float b_scale, float threshold)
	{
		std::uint16_t status = 0;
		__asm {
			fld a
			fdiv a_scale
			fld b
			fdiv b_scale
			_emit 0xde
			_emit 0xe9 // FSUBP ST(1),ST  (ST1 = ST1 - ST0; pop)
			fabs
			fcomp threshold
			fnstsw ax
			mov status, ax
		}
		return !parity_even((status >> 8) & 0x5);
	}
	// 0x4912ab: FLD delta; FMUL 0.25; FADD [target]; FSTP [target]   (one x87 rounding at the store)
	void add_quarter_scaled(float* target, float delta)
	{
		const float quarter = at<float>(const_quarter);
		float value = *target;
		__asm {
			fld delta
			fmul quarter
			fadd value
			fstp value
		}
		*target = value;
	}
	// 0x4912c4: FLD delta; FMUL 0.25; FSUBR [target]; FSTP [target]
	void subtract_quarter_scaled(float* target, float delta)
	{
		const float quarter = at<float>(const_quarter);
		float value = *target;
		__asm {
			fld delta
			fmul quarter
			fsubr value
			fstp value
		}
		*target = value;
	}
}

namespace entity_capacity
{
	using namespace entity_frames;

	// 0x44cd70 (RET 0xc). 0x44cd7b: 0x44ce30(local, 0, 0) is a no-op with a zero first argument (0x44ce36 JZ) - the
	// native local stays uninitialized until the grid fills it.
	std::uint32_t __fastcall Physics_0044cd70(void*, void*, const float* lo, const float* hi, EntitySet* set)
	{
		EntitySet cells{};
		void* grid = grid_object();
		if (slot<grid_cells_fn>(grid, 0x30)(grid, nullptr, lo, hi, &cells)) // 0x44cd97
		{
			and_into(*set, cells); // 0x44cda2..0x44cdfc
			return 1;              // 0x44cdff MOV AL,1 (callers test AL)
		}
		clear_set(*set); // 0x44ce07
		return 0;
	}

	// 0x44ce70 (RET 0x10).
	std::uint32_t __fastcall Physics_0044ce70(void*, void*, const float* lo, const float* hi, EntitySet* set, std::uint8_t overlap)
	{
		EntitySet cells{};
		void* grid = grid_object();
		if (!slot<grid_cells_fn>(grid, 0x30)(grid, nullptr, lo, hi, &cells)) // 0x44ce9a
		{
			clear_set(*set); // 0x44cea1
			return 0;
		}
		and_into(cells, *set); // 0x44cec8..0x44cf50: local &= *set
		clear_set(*set);       // 0x44cf2b
		const float box[6] = {lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]}; // 0x44cf42..0x44cf7d
		std::uint32_t count = 0;
		for (std::uint32_t i = first_bit(cells); i < world_capacity(); i = next_bit(cells, i + 1)) // 0x44cf81 / 0x44cfee
		{
			void* entity = entity_of_dense(i);                                      // 0x44cf95
			const float* bounds = slot<entity_pointer_fn>(entity, 0x4c)(entity, nullptr); // 0x44cfa1
			if (!bounds) continue;
			// 0x44cfa8: TEST BL,BL; JZ -> 0x44d070 (inside), else 0x44d010 (overlap)
			const auto hit = overlap ? native<box_test_fn>(native_box_overlaps)(bounds, nullptr, box) : native<box_test_fn>(native_box_inside)(bounds, nullptr, box);
			if (hit) { set->set(i); ++count; } // 0x44cfca
		}
		return count;
	}

	// 0x44d170 (RET 0x10).
	std::uint32_t __fastcall Physics_0044d170(void*, void*, const float* center, float radius, EntitySet* set, std::uint8_t flag)
	{
		float lo[3], hi[3]; // 0x44d178..0x44d1c8: single x87 subtractions / additions
		for (int k = 0; k < 3; ++k) { lo[k] = center[k] - radius; hi[k] = radius + center[k]; }
		EntitySet cells{};
		void* grid = grid_object();
		if (!slot<grid_cells_fn>(grid, 0x30)(grid, nullptr, lo, hi, &cells)) // 0x44d1e4
		{
			clear_set(*set); // 0x44d1eb
			return 0;
		}
		and_into(cells, *set); // 0x44d222
		// 0x44d239: FLD radius; FCOM 1.0; TEST AH,5; JP keep: "less" replaces the radius by 1.0; equal/greater/NaN keep it.
		const float one = at<float>(const_one);
		clear_set(*set); // 0x44d247
		const float sphere[4] = {center[0], center[1], center[2], radius < one ? one : radius}; // 0x44d26d..0x44d28b
		std::uint32_t count = 0;
		for (std::uint32_t i = first_bit(cells); i < world_capacity(); i = next_bit(cells, i + 1)) // 0x44d285 / 0x44d343
		{
			void* entity = entity_of_dense(i);                                      // 0x44d2f1
			const float* bounds = slot<entity_pointer_fn>(entity, 0x4c)(entity, nullptr); // 0x44d2fd
			if (!bounds) continue;
			// 0x44d304: TEST BL,BL; JZ -> 0x5631d0, else 0x563160
			const auto hit = flag ? native<box_test_fn>(native_sphere_hits_box)(sphere, nullptr, bounds) : native<box_test_fn>(native_sphere_hits_box_exact)(sphere, nullptr, bounds);
			if (hit) { set->set(i); ++count; } // 0x44d31d
		}
		return count;
	}

	// 0x44e7d0 (RET 0x10; ECX unused).
	std::uint32_t __fastcall Physics_0044e7d0(void*, void*, const float* lo, const float* hi, EntitySet* set, const float* sweep)
	{
		float box[6] = {lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]}; // local_50 (min) and local_44 (max): one six-float box
		float* box_lo = box;
		float* box_hi = box + 3;
		float swept_lo[3], swept_hi[3], scratch[3];
		swept_lo[0] = lo[0] + sweep[0];   // 0x44e7d9 / 0x44e809
		swept_lo[1] = sweep[1] + lo[1];   // 0x44e80e
		swept_lo[2] = sweep[2] + lo[2];   // 0x44e814
		vec_set3(scratch, hi[0] + sweep[0], sweep[1] + hi[1], sweep[2] + hi[2]); // 0x44e84f igVec3f::set
		swept_hi[0] = scratch[0]; swept_hi[1] = scratch[1]; swept_hi[2] = scratch[2];
		vec_copy(iat_vec_make_min, box_lo, swept_lo); // 0x44e875
		vec_copy(iat_vec_make_max, box_hi, swept_hi); // 0x44e884
		const float one = at<float>(const_one);
		box_lo[0] = box_lo[0] - one; box_lo[1] = box_lo[1] - one; box_lo[2] = box_lo[2] - one; // 0x44e88a..0x44e8c3
		vec_set3(scratch, 1.0f, 1.0f, 1.0f); // 0x44e8c7
		box_hi[0] = scratch[0] + box_hi[0]; box_hi[1] = scratch[1] + box_hi[1]; box_hi[2] = scratch[2] + box_hi[2]; // 0x44e8cd..0x44e8f4
		// 0x44e8f8: 0x44ce30(local, 0, 0) no-op
		EntitySet cells{};
		void* grid = grid_object();
		if (!slot<grid_cells_fn>(grid, 0x30)(grid, nullptr, box_lo, box_hi, &cells)) // 0x44e915
		{
			clear_set(*set); // 0x44e91c
			return 0;
		}
		and_into(cells, *set); // 0x44e952
		clear_set(*set);       // 0x44e96d
		std::uint32_t count = 0;
		for (std::uint32_t i = first_bit(cells); i < world_capacity(); i = next_bit(cells, i + 1)) // 0x44e984 / 0x44ea26
		{
			void* entity = entity_of_dense(i);                                      // 0x44e9e4
			const float* bounds = slot<entity_pointer_fn>(entity, 0x4c)(entity, nullptr); // 0x44e9f0
			if (bounds && box_overlaps(bounds, box_lo)) { set->set(i); ++count; }   // 0x44e9fe / 0x44ea07
		}
		return count;
	}

	// 0x44d360 (RET 0x34).
	std::uint32_t* __fastcall Physics_0044d360(void* self, void*, std::uint32_t* result, const float* start, const float* end, const float* extents,
	                                           std::uint32_t a5, std::uint32_t a6, std::uint8_t a7, std::uint8_t a8, EntitySet* set, void* filter,
	                                           std::uint8_t force_player_test, std::uint8_t scale_direction, void* entity)
	{
		alignas(4) std::uint8_t context[collide_context_bytes];
		alignas(4) std::uint8_t record[trace_record_bytes];
		construct_context(context, entity, start, end, extents, set);      // 0x44d399 (widened clear on failure)
		native<record_ctor_fn>(native_record_ctor)(record, nullptr);        // 0x44d3a2
		field<std::uint8_t>(record, 0x34) = a7;                              // 0x44d3cb
		field<std::uint32_t>(record, 0x2c) = a5;                             // 0x44d3cf
		field<std::uint32_t>(record, 0x30) = a6;                             // 0x44d3dc
		field<std::uint8_t>(record, 0x35) = a8;                              // 0x44d3e3
		// 0x44d3a7: FLD 0.0; FLD ctx+0xd8; FUCOMPP; TEST AH,0x44; JP continue - equal returns a null hit.
		if (field<float>(context, 0xd8) == at<float>(const_zero)) { *result = 0; return result; } // 0x44d3e9
		std::uint32_t handle = 0; // local_260
		bool test_player = force_player_test != 0; // 0x44d401: a non-zero a11 tests the player without the set check
		if (!test_player)
		{
			const std::uint32_t* player = native<player_handle_fn>(native_player_handle)(&handle); // 0x44d419
			test_player = set->test(dense_of_token(*player));                                      // 0x44d420 (token & 0xff)
		}
		if (test_player)
		{
			std::uint32_t* player = native<player_handle_fn>(native_player_handle)(&handle);        // 0x44d443
			void* player_entity = native<handle_fn>(native_entity_of_handle)(player, nullptr);       // 0x44d44d
			if (record_fill(record, player_entity) && field<std::uint32_t>(record, 0x28) != 2)       // 0x44d457 / 0x44d460
			{
				if (field<std::uint32_t>(context, 0x28) == 1) Physics_0044e590(context, nullptr, record, filter); // 0x44d467
				else Physics_0044e440(context, nullptr, record, filter);
			}
		}
		float direction[3], box_a[3], box_b[3]; // local_25c / local_250 / local_244
		const float* box_max;
		const float* box_min;
		if (scale_direction) // 0x44d496
		{
			iat_fn<vec_result_fn>(iat_vec_scale)(floats(context, 0xbc), nullptr, direction, floats(context, 0xd8)); // 0x44d4b5: dir * best t
			box_max = native<context_box_fn>(native_context_max)(context, nullptr, box_a); // 0x44d4cd
			box_min = native<context_box_fn>(native_context_min)(context, nullptr, box_b); // 0x44d50c
		}
		else
		{
			vec_copy(iat_vec_set, direction, floats(context, 0xbc)); // 0x44d4e2
			box_max = native<context_box_fn>(native_context_max)(context, nullptr, box_b); // 0x44d4fa
			box_min = native<context_box_fn>(native_context_min)(context, nullptr, box_a); // 0x44d50c
		}
		if (Physics_0044e7d0(self, nullptr, box_min, box_max, set, direction) == 0) // 0x44d514
		{
			*result = field<std::uint32_t>(context, 0xe0); // 0x44d51d
			return result;
		}
		field<std::uint8_t>(record, 0x34) = 1; // 0x44d53f
		field<std::uint8_t>(record, 0x35) = 0; // 0x44d544
		for (std::uint32_t i = first_bit(*set); i < world_capacity(); i = next_bit(*set, i + 1)) // 0x44d549 / 0x44d68b (live set)
		{
			void* candidate = entity_of_dense(i); // 0x44d561
			if (!record_fill(record, candidate))  // 0x44d56e
			{
				set->clear(i); // 0x44d577
				continue;
			}
			const auto kind = field<std::uint32_t>(context, 0x28), shape = field<std::uint32_t>(record, 0x28); // 0x44d598 / 0x44d61a
			if (kind == 3 || kind == 2)
			{
				if (shape == 3) Physics_0044e210(context, nullptr, record, filter);                      // 0x44d630
				else if (shape == 1 || shape == 4) Physics_0044e440(context, nullptr, record, filter);   // 0x44d680
				else if (shape == 2) Physics_0044dca0(context, nullptr, record, filter);                 // 0x44d653
				else if (shape == 5) Physics_0044e140(context, nullptr, record, filter);                 // 0x44d66c
			}
			else if (kind == 4)
			{
				if (shape == 1 || shape == 4) Physics_0044e440(context, nullptr, record, filter);        // 0x44d680
				else Physics_0044e4d0(context, nullptr, record, filter);                                 // 0x44d5d1
			}
			else if (kind == 1)
			{
				if (shape == 1 || shape == 4) dispatch_0044e650(context, record, filter);                // 0x44d613
				else Physics_0044e4d0(context, nullptr, record, filter);                                 // 0x44d5ff
			}
		}
		*result = field<std::uint32_t>(context, 0xe0); // 0x44d69e
		return result;
	}

	// 0x44ea40 (RET 0x18): CCollide slot 3. Not in the plan's enumerated frames (report); the fix is the widened clear in
	// construct_context, everything else is the native sequence.
	std::uint32_t __fastcall Physics_0044ea40(void* self, void*, const float* start, const float* end, const float* extents, EntitySet* set, void* entity, void*)
	{
		alignas(4) std::uint8_t context[collide_context_bytes];
		construct_context(context, entity, start, end, extents, set); // 0x44ea70
		// 0x44ea75: FLD 0.0; FLD ctx+0xd8; FUCOMPP; TEST AH,0x44; JP continue
		if (field<float>(context, 0xd8) == at<float>(const_zero)) return 0; // 0x44ea8b
		float direction[3] = {field<float>(context, 0xbc), field<float>(context, 0xc0), field<float>(context, 0xc4)}; // 0x44ea98 local_1a0
		float box_a[3], box_b[3];
		const float* box_max = native<context_box_fn>(native_context_max)(context, nullptr, box_a); // 0x44eac8
		const float* box_min = native<context_box_fn>(native_context_min)(context, nullptr, box_b); // 0x44ead7
		Physics_0044e7d0(self, nullptr, box_min, box_max, set, direction);                           // 0x44eadf (count discarded)
		return field<std::uint32_t>(context, 0xe0);                                                 // 0x44eae4
	}

	// 0x44dca0 (RET 8): capsule (record) swept against the context's capsule by 0x600dc0.
	void __fastcall Physics_0044dca0(void* context, void*, void* record, void* filter)
	{
		float radius = 0, height = 0;
		publish_capsule(context, record, radius, height); // 0x44dca8..0x44dcf2
		// 0x44dcf2 / 0x44dd07: FCOMP 0.0; TEST AH,0x41; JNZ fail - "greater" is the only continuing case (NaN fails).
		if (!(radius > at<float>(const_zero)) || !(height > at<float>(const_zero)))
		{
			auto& counter = at<std::int32_t>(counter_bad_capsule); // 0x44de1b: saturating diagnostic counter
			if (counter < 100) ++counter;
			else if (counter == 100) counter = 0x65;
			clear_record_entity(context, record); // 0x44de39
			return;
		}
		native<capsule_fn>(native_capsule_sweep)(bytes(context, 0xf0)); // 0x44dd1f
		if (field<std::uint8_t>(context, 0x146) || field<std::uint8_t>(context, 0x144) || field<std::uint8_t>(context, 0x145)) // 0x44dd24..0x44dd43
		{
			commit_capsule_hit(context, record, filter); // 0x44dd49..0x44de0e
			return;
		}
		clear_record_entity(context, record); // 0x44de39
	}

	// 0x44e140 (RET 8): the record's scaled box swept through the physics world (0x56c7f0, slot 0x10).
	void __fastcall Physics_0044e140(void* context, void*, void* record, void* filter)
	{
		// 0x44e14e: length2(record+4) in ST(0); FCOMP epsilon; TEST AH,0x41; JNP return - "less" and "equal" (odd parity)
		// return without touching the set; "greater" and unordered continue.
		const auto status = compare_length2(floats(record, 0x4), &at<float>(const_epsilon));
		if (!parity_even(status & 0x41)) return;
		void* entity = field<void*>(record, 0);
		float* matrix = floats(record, 0x7c);
		slot<entity_out_fn>(entity, 0x58)(entity, nullptr, matrix);        // 0x44e16e
		vec_copy(iat_mat_pre_scale, matrix, floats(record, 0x4));          // 0x44e174
		vec_copy(iat_mat_translation, matrix, floats(record, 0x10));       // 0x44e180
		if (native<getter_fn>(native_physics_world)())                     // 0x44e186
		{
			void* physics = native<getter_fn>(native_physics_world)();      // 0x44e18f
			// 0x44e1bc: (ctx+0x10, ctx+0xbc, ctx+4, 0, 0, record[0x34], record[0x35], record+0x38, record+0x7c)
			if (slot<sweep9_fn>(physics, 0x10)(physics, nullptr, floats(context, 0x10), floats(context, 0xbc), floats(context, 0x4), 0, 0,
			                                     field<std::uint8_t>(record, 0x34), field<std::uint8_t>(record, 0x35), floats(record, 0x38), matrix))
			{
				commit_hit(context, record, filter, 1); // 0x44e1cd
				return;
			}
		}
		clear_record_entity(context, record); // 0x44e1d9
	}

	// 0x44e210 (RET 8): capsule vs capsule; the sweep variant is chosen by the radius/height ratio of the two capsules.
	void __fastcall Physics_0044e210(void* context, void*, void* record, void* filter)
	{
		float radius = 0, height = 0;
		publish_capsule(context, record, radius, height); // 0x44e219..0x44e272
		// 0x44e261 / 0x44e28b: FCOMP 0.0; TEST AH,0x41; JNZ -> clear (no counter here)
		if (!(radius > at<float>(const_zero)) || !(height > at<float>(const_zero)))
		{
			clear_record_entity(context, record); // 0x44e40d
			return;
		}
		// 0x44e29c / 0x44e2af: a non-positive (or NaN) context radius/height takes 0x600dc0; otherwise the ratio decides.
		const float context_radius = field<float>(context, 0xfc), context_height = field<float>(context, 0x100);
		bool ratio_sweep = false;
		if (context_radius > at<float>(const_zero) && context_height > at<float>(const_zero))
			ratio_sweep = capsule_ratio_below_threshold(radius, context_radius, height, context_height, at<float>(const_ratio_threshold)); // 0x44e2c2
		if (ratio_sweep) native<capsule_fn>(native_capsule_sweep_ratio)(bytes(context, 0xf0)); // 0x44e2ee
		else native<capsule_fn>(native_capsule_sweep)(bytes(context, 0xf0));                   // 0x44e2fc
		// 0x44e301..0x44e338: fail when no contact flag is set, or when the +0x144 contact is marked +0x148.
		const bool f144 = field<std::uint8_t>(context, 0x144) != 0, f145 = field<std::uint8_t>(context, 0x145) != 0, f146 = field<std::uint8_t>(context, 0x146) != 0;
		const bool f148 = field<std::uint8_t>(context, 0x148) != 0;
		if ((!f146 && !f144 && !f145) || (f144 && f148))
		{
			clear_record_entity(context, record); // 0x44e40d
			return;
		}
		commit_capsule_hit(context, record, filter); // 0x44e33e..0x44e400
	}

	// 0x44e440 (RET 8): the record's collision model swept by the context box.
	void __fastcall Physics_0044e440(void* context, void*, void* record, void* filter)
	{
		if (void* model = field<void*>(record, 0x78)) // 0x44e445
		{
			// 0x44e47e: (ctx+0x10, ctx+0xbc, ctx+4, record+0x2c, record+0x30, record[0x34], record[0x35], record+0x38, record+0x7c)
			if (slot<sweep9_fn>(model, 0x10)(model, nullptr, floats(context, 0x10), floats(context, 0xbc), floats(context, 0x4),
			                                  field<std::uint32_t>(record, 0x2c), field<std::uint32_t>(record, 0x30),
			                                  field<std::uint8_t>(record, 0x34), field<std::uint8_t>(record, 0x35), floats(record, 0x38), floats(record, 0x7c)))
			{
				commit_hit(context, record, filter, 1); // 0x44e48f
				return;
			}
		}
		clear_record_entity(context, record); // 0x44e499
	}

	// 0x44e4d0 (RET 8): the context's collision model swept by the record box along the reversed direction.
	void __fastcall Physics_0044e4d0(void* context, void*, void* record, void* filter)
	{
		if (void* model = field<void*>(context, 0x78)) // 0x44e4d6
		{
			const float reversed[3] = {-field<float>(context, 0xbc), -field<float>(context, 0xc0), -field<float>(context, 0xc4)}; // 0x44e4e2..0x44e511
			// 0x44e533: (record+0x10, &reversed, record+4, record+0x2c, record+0x30, record[0x34], record[0x35], record+0x38, ctx+0x7c)
			if (slot<sweep9_fn>(model, 0x10)(model, nullptr, floats(record, 0x10), reversed, floats(record, 0x4),
			                                  field<std::uint32_t>(record, 0x2c), field<std::uint32_t>(record, 0x30),
			                                  field<std::uint8_t>(record, 0x34), field<std::uint8_t>(record, 0x35), floats(record, 0x38), floats(context, 0x7c)))
			{
				commit_hit(context, record, filter, 0); // 0x44e544
				return;
			}
		}
		clear_record_entity(context, record); // 0x44e551
	}

	// 0x44e590 (RET 8): the record's model tested against the context entity's bounds (entity slot 0x38, no arguments).
	void __fastcall Physics_0044e590(void* context, void*, void* record, void* filter)
	{
		if (void* model = field<void*>(record, 0x78)) // 0x44e595
		{
			void* entity = field<void*>(context, 0);                                         // 0x44e59f
			const float* bounds_a = slot<entity_pointer_fn>(entity, 0x38)(entity, nullptr); // 0x44e5d2
			const float* bounds_b = slot<entity_pointer_fn>(entity, 0x38)(entity, nullptr); // 0x44e5dd
			// 0x44e5eb: (ctx+0xbc, bounds_b, bounds_a+0xc, record+0x2c, record+0x30, record[0x34], record[0x35], record+0x38, record+0x7c, ctx+0x7c)
			if (slot<sweep10_fn>(model, 0x14)(model, nullptr, floats(context, 0xbc), bounds_b, bounds_a + 3,
			                                   field<std::uint32_t>(record, 0x2c), field<std::uint32_t>(record, 0x30),
			                                   field<std::uint8_t>(record, 0x34), field<std::uint8_t>(record, 0x35), floats(record, 0x38), floats(record, 0x7c), floats(context, 0x7c)))
			{
				commit_hit(context, record, filter, 1);                                       // 0x44e5fe
				field<std::uint32_t>(record, 0x48) = field<std::uint32_t>(record, 0x58);      // 0x44e603..0x44e613
				field<std::uint32_t>(record, 0x4c) = field<std::uint32_t>(record, 0x5c);
				field<std::uint32_t>(record, 0x50) = field<std::uint32_t>(record, 0x60);
				return;
			}
		}
		clear_record_entity(context, record); // 0x44e61a
	}

	// 0x44e6a0 (RET 8): the context's model tested against the record entity's bounds along the reversed direction.
	void __fastcall Physics_0044e6a0(void* context, void*, void* record, void* filter)
	{
		float delta[3], transformed_delta[3], transformed_direction[3]; // local_24 / local_18 / local_c
		delta[0] = field<float>(record, 0x10) - field<float>(record, 0x1c); // 0x44e6aa
		delta[1] = field<float>(record, 0x14) - field<float>(record, 0x20); // 0x44e6bf
		delta[2] = field<float>(record, 0x18) - field<float>(record, 0x24); // 0x44e6ce
		iat_fn<vec_transform_fn>(iat_vec_transform)(transformed_delta, nullptr, delta, floats(record, 0x7c));                       // 0x44e6d8
		iat_fn<vec_transform_fn>(iat_vec_transform)(transformed_direction, nullptr, floats(context, 0xbc), floats(record, 0x7c));   // 0x44e6ea
		if (void* model = field<void*>(context, 0x78)) // 0x44e6f0
		{
			delta[0] = -field<float>(context, 0xbc); delta[1] = -field<float>(context, 0xc0); delta[2] = -field<float>(context, 0xc4); // 0x44e6fb..0x44e720
			void* entity = field<void*>(record, 0);                                          // 0x44e6fd
			const float* bounds_a = slot<entity_pointer_fn>(entity, 0x38)(entity, nullptr);  // 0x44e746
			const float* bounds_b = slot<entity_pointer_fn>(entity, 0x38)(entity, nullptr);  // 0x44e751
			// 0x44e761: (&delta, bounds_b, bounds_a+0xc, record+0x2c, record+0x30, record[0x34], record[0x35], record+0x38, ctx+0x7c, record+0x7c)
			if (slot<sweep10_fn>(model, 0x14)(model, nullptr, delta, bounds_b, bounds_a + 3,
			                                   field<std::uint32_t>(record, 0x2c), field<std::uint32_t>(record, 0x30),
			                                   field<std::uint8_t>(record, 0x34), field<std::uint8_t>(record, 0x35), floats(record, 0x38), floats(context, 0x7c), floats(record, 0x7c)))
			{
				commit_hit(context, record, filter, 0);                                       // 0x44e772
				field<std::uint32_t>(record, 0x48) = field<std::uint32_t>(record, 0x58);      // 0x44e777..0x44e787
				field<std::uint32_t>(record, 0x4c) = field<std::uint32_t>(record, 0x5c);
				field<std::uint32_t>(record, 0x50) = field<std::uint32_t>(record, 0x60);
				return;
			}
		}
		clear_record_entity(context, record); // 0x44e793
	}

	// 0x491210 (RET 0x18).
	std::uint32_t __fastcall EntitySetCaller_00491210(void* grid, void*, const std::uint8_t* cell, std::uint32_t x, std::uint32_t y, std::uint32_t layer, EntitySet* set, std::uint8_t* output)
	{
		clear_set(*set);                                                       // 0x491224
		*output = 0;                                                           // 0x49123f
		std::uint32_t count = 0;                                               // 0x491241 (reuses the set argument slot natively)
		const std::uint8_t cell_byte = cell[layer & 0xff];                     // 0x491245
		field<std::uint8_t>(grid, 0x20b1b) = static_cast<std::uint8_t>(y);     // 0x491256
		field<std::uint8_t>(grid, 0x20b1c) = cell_byte;                        // 0x491266
		field<std::uint8_t>(grid, 0x20b1a) = static_cast<std::uint8_t>(x);     // 0x49126c
		float probe[3];
		iat_fn<vec_ctor_fn>(iat_vec_ctor)(probe, nullptr);                     // 0x49126e
		float* lo = floats(grid, 0x20ac0);
		float* hi = floats(grid, 0x20acc);
		native<pathgrid_box_fn>(native_pathgrid_box)(grid, nullptr, bytes(grid, 0x20b1a), lo, hi);  // 0x491289
		native<pathgrid_center_fn>(native_pathgrid_center)(grid, nullptr, bytes(grid, 0x20b1a), probe); // 0x491296
		for (int k = 0; k < 2; ++k) // 0x4912a0..0x4912d8
		{
			float* lo_k = iat_fn<vec_index_fn>(iat_vec_index)(lo, nullptr, k);
			add_quarter_scaled(lo_k, field<float>(grid, 0x20ad8));
			float* hi_k = iat_fn<vec_index_fn>(iat_vec_index)(hi, nullptr, k);
			subtract_quarter_scaled(hi_k, field<float>(grid, 0x20ad8));
		}
		void* entity_grid = grid_object();                                                      // 0x4912da
		if (!slot<grid_cells_fn>(entity_grid, 0x30)(entity_grid, nullptr, lo, hi, set)) return 0; // 0x4912ea
		for (std::uint32_t i = first_bit(*set); i < world_capacity(); i = next_bit(*set, i + 1)) // 0x4912f5 / 0x491524 (live set)
		{
			void* entity = entity_of_dense(i); // 0x491358
			bool keep = false;
			do
			{
				if (!entity) break;                                                                   // 0x491362
				if (!class_bit(entity_class(entity), class_id_a)) break;                             // 0x49136a
				// 0x49139e: length2(entity+0x120) FCOMP speed threshold; TEST AH,0x41; JNZ continue - only "greater" prunes.
				if ((compare_length2(floats(entity, 0x120), &at<float>(const_nav_speed2)) & 0x41) == 0) break;
				if (field<std::uint8_t>(entity, 0x258) & 8) break;                                   // 0x4913bf
				if ((field<std::uint32_t>(entity, 0x4) >> 1) & 1) break;                              // 0x4913d6
				if (class_bit(entity_class(entity), class_id_b)) break;                              // 0x4913f1
				if (class_bit(entity_class(entity), class_id_c)) break;                              // 0x491419
				if (!box_overlaps(floats(entity, 0x50), lo)) break;                                  // 0x491445
				bool removed = false;
				if (void* component = native<component_fn>(native_nav_component)(entity))            // 0x49145d
				{
					float position[3], offset[3];
					const float* at_position = slot<entity_out_fn>(component, 0x24)(component, nullptr, position); // 0x49147e
					iat_fn<vec_result_fn>(iat_vec_sub)(at_position, nullptr, offset, probe);           // 0x491483: offset = position - probe
					*iat_fn<vec_index_fn>(iat_vec_index)(offset, nullptr, 2) = 0.0f;                   // 0x49148f / 0x491499
					// 0x49149f: length2(offset) FCOMP distance threshold; TEST AH,5; JP skip - only "less" (odd parity) continues.
					if (!parity_even(compare_length2(offset, &at<float>(const_nav_distance2)) & 0x5))
					{
						const auto flags = field<std::uint32_t>(component, 0x4); // 0x4914b2
						if ((flags >> 18) & 1)
						{
							if ((~(flags >> 22)) & 1) { set->clear(i); removed = true; } // 0x4914cc (0x570130: plain bit clear)
							else *output = 1;                                         // 0x4914d7
						}
					}
				}
				keep = true;
				if (removed) break;
				void* reachable = native<component_fn>(native_nav_component_b)(entity); // 0x4914db
				if (reachable == nullptr || native<reachable_fn>(native_nav_reachable)(reachable, nullptr, probe)) ++count; // 0x4914ee / 0x491501
				else set->clear(i); // 0x4914fa (0x570130)
			} while (false);
			if (!keep) set->clear(i); // 0x491510
		}
		return count; // 0x491599
	}

	// 0x493330 (RET 0xc).
	std::uint32_t __fastcall EntitySetCaller_00493330(void* grid, void*, std::int32_t x, std::int32_t y, std::uint8_t height)
	{
		std::uint8_t* cell = native<pathgrid_cell_fn>(native_pathgrid_cell_b)(grid, nullptr, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)); // 0x493341
		// 0x49334b / 0x4934e2: layer 1 when the cell has a second surface and the height reaches its midpoint.
		const auto layer_of = [height](const std::uint8_t* c) -> std::uint32_t
		{
			if (c[1] == 0xff) return 0;
			const int lo = c[0], hi = c[1];
			return static_cast<int>(height) < (hi - lo) / 2 + lo ? 0 : 1;
		};
		const std::uint32_t layer = layer_of(cell);
		field<std::uint8_t>(grid, 0x20b18) = static_cast<std::uint8_t>(layer);                           // 0x493374
		cell[4 + layer] &= 0xe1;                                                                          // 0x49337e
		field<std::uint16_t>(cell, 2) = static_cast<std::uint16_t>(field<std::uint16_t>(grid, 0x20b08) + 1); // 0x493381
		NavigationScratch& scratch = navigation_scratch(grid);                                           // CPathGrid+0x20b20 / +0x20b40 natively
		const auto blocking = EntitySetCaller_00491210(grid, nullptr, cell, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
		                                                field<std::uint8_t>(grid, 0x20b18), &scratch.set, &scratch.output); // 0x4933ae
		if (blocking != 0) // 0x4933b3
		{
			const std::uint32_t l = field<std::uint8_t>(grid, 0x20b18);
			cell[4 + l] |= static_cast<std::uint8_t>((scratch.output == 0 ? 8 : 0) + 8);                 // 0x4933b7..0x4933d5
			return cell[4 + field<std::uint8_t>(grid, 0x20b18)];                                         // 0x4933d7
		}
		field<std::uint8_t>(grid, 0x20b1d) = 0; // 0x4933e9
		int axis_neighbours = 0;                // [ESP+0x10]
		for (field<std::int32_t>(grid, 0x20b44) = -1;;) // 0x4933f8
		{
			for (field<std::int32_t>(grid, 0x20b48) = -1;;) // 0x493410
			{
				const std::int32_t dx = field<std::int32_t>(grid, 0x20b44), dy = field<std::int32_t>(grid, 0x20b48);
				if (dx != 0 || dy != 0) // 0x493420
				{
					const std::int32_t cx = dx + x, cy = dy + y; // 0x493442 / 0x493444
					if (cx >= 0 && cy >= 0 && cx < field<std::uint8_t>(grid, 0x20b10) && cy < field<std::uint8_t>(grid, 0x20b11)) // 0x493446..0x49346c
					{
						const std::uint8_t* neighbour = native<pathgrid_cell_fn>(native_pathgrid_cell)(grid, nullptr,
							static_cast<std::uint8_t>(dx + x), static_cast<std::uint8_t>(dy + y)); // 0x49348c (byte sums)
						if (neighbour[4] != 0) // 0x493493
						{
							// 0x49349e..0x4934c8: the surface the height selects
							const std::uint8_t value = (neighbour[1] == 0xff || static_cast<int>(height) < (static_cast<int>(neighbour[1]) - static_cast<int>(neighbour[0])) / 2 + static_cast<int>(neighbour[0]))
								? neighbour[0] : neighbour[1];
							const int difference = static_cast<int>(value) - static_cast<int>(cell[field<std::uint8_t>(grid, 0x20b18)]); // 0x4934c8
							if (std::abs(difference) < 2) // 0x4934d8..0x4934e0
							{
								const std::uint8_t flags = neighbour[4 + layer_of(neighbour)];       // 0x4934e2..0x49350d
								field<std::uint8_t>(grid, 0x20b19) = flags;                           // 0x493514
								if ((flags & 0x38) == 0)                                              // 0x493511
								{
									field<std::uint8_t>(grid, 0x20b1d) += 1;                          // 0x49351c
									if (dx == 0 || dy == 0) ++axis_neighbours;                        // 0x49352a..0x49353e
								}
							}
						}
					}
				}
				if (++field<std::int32_t>(grid, 0x20b48) >= 2) break; // 0x493546
			}
			if (++field<std::int32_t>(grid, 0x20b44) >= 2) break;     // 0x49355e
		}
		const std::uint32_t l = field<std::uint8_t>(grid, 0x20b18);
		if (field<std::uint8_t>(grid, 0x20b1d) < 5 || axis_neighbours <= 2) cell[4 + l] |= 4; // 0x493576..0x493591
		if (field<std::uint8_t>(grid, 0x20b1d) == 8) cell[4 + l] |= 2;                        // 0x493594..0x4935af
		return cell[4 + l];                                                                    // 0x4935b1 / 0x4935c4
	}

	// 0x4cd350 (RET 0x14).
	void __fastcall EntitySetCaller_004cd350(void* self, void*, std::uint32_t* output, const std::uint32_t* name_token, std::uint8_t apply, std::int32_t order, std::uint8_t set_state)
	{
		field<std::uint32_t>(output, named_output_count) = 0; // 0x4cd378
		if (*name_token == 0) return;                          // 0x4cd37e
		EntitySet found{};                                     // 0x4cd386..0x4cd3a5 (eight dwords natively)
		char name[0x80];
		std::memset(name, 0, sizeof name);                     // 0x4cd3a9 (first byte natively)
		const auto name_of = [name_token]() -> const char*     // 0x4cd3ca / 0x4cd412 / 0x4cd44c: global name table lookup
		{
			const std::uint32_t token = *name_token;
			if (token == 0) return &at<const char>(string_empty);
			const std::uint8_t* base = native<name_table_fn>(native_name_table)();
			const auto offset = field<std::uint32_t>(base, 4 + (token & 0xffffff) * 4);
			return reinterpret_cast<const char*>(base + offset + 0x8008);
		};
		for (int index = -1; index < 15; ++index) // 0x4cd5c9
		{
			for (int variant = 0;;) // 0x4cd3b1 / 0x4cd5b7
			{
				if (index < 0) // 0x4cd3b5
				{
					iat_fn<strncpy_fn>(iat_strncpy)(name, name_of(), 0x80); // 0x4cd47a
					name[0x7f] = 0;                                          // 0x4cd482
				}
				else // 0x4cd3bd: "%s%d" for the first variant, "%s0%d" for the second
				{
					native<snprintf_fn>(native_snprintf)(name, 0x80, variant == 0 ? &at<const char>(format_name_index) : &at<const char>(format_name_zero_index), name_of(), index); // 0x4cd3fe
				}
				void* world = world_object();                                             // 0x4cd48a
				if (slot<world_find_fn>(world, 0x34)(world, nullptr, name, &found))       // 0x4cd49d
				{
					for (std::uint32_t i = first_bit(found); i < world_capacity(); i = next_bit(found, i + 1)) // 0x4cd4a8 / 0x4cd539
					{
						if (field<std::uint32_t>(output, named_output_count) == named_output_capacity) continue; // 0x4cd50d: the output stays 50
						void* lookup = world_object();                                                      // 0x4cd516
						std::uint32_t handle = 0;                                                           // iStack_ac
						const std::uint32_t* found_handle = slot<world_handle_fn>(lookup, 0x18)(lookup, nullptr, &handle, i); // 0x4cd525 (dense index)
						output[field<std::uint32_t>(output, named_output_count)] = *found_handle;           // 0x4cd530
						field<std::uint32_t>(output, named_output_count) += 1;                              // 0x4cd533
					}
				}
				if (index < 0) break;        // 0x4cd5b3
				if (++variant >= 2) break;   // 0x4cd5bb
			}
		}
		if (field<std::uint32_t>(output, named_output_count) == 0) return; // 0x4cd5d3
		float scale = field<float>(self, 0x2158) * at<float>(const_four);    // 0x4cd5e3
		const std::uint8_t danger = at<std::uint8_t>(danger_room_flag);      // 0x4cd5e9
		bool fixed = false;
		if (danger != 0xfe && danger != 0xfd && field<std::uint32_t>(self, 0x1df4) == 0) // 0x4cd5ee..0x4cd606
		{
			const std::uint8_t* course = &at<std::uint8_t>(danger_room_courses + static_cast<std::uint32_t>(danger) * 0x78); // 0x4cd608
			std::uint32_t value = course[0];
			if (course[0] != 0) // 0x4cd614
			{
				void* game = native<getter_fn>(native_game_object)();                          // 0x4cd619
				const int difficulty = slot<int_method_fn>(game, 0x268)(game, nullptr);      // 0x4cd622
				value = difficulty > 1 ? static_cast<std::uint32_t>(course[0]) + 0x28 : course[0]; // 0x4cd628
			}
			if (value == 1) { scale = 8.0f; fixed = true; } // 0x4cd638
		}
		if (!fixed && order != 0x1d) scale = scale / static_cast<float>(static_cast<std::int32_t>(field<std::uint32_t>(output, named_output_count))); // 0x4cd647..0x4cd65b
		for (std::int32_t n = 0; n < static_cast<std::int32_t>(field<std::uint32_t>(output, named_output_count)); ++n) // 0x4cd65f / 0x4cd81f (count re-read)
		{
			std::uint32_t* entry = output + n;
			if (*entry == 0) continue;                                                  // 0x4cd680
			if (!native<handle_valid_fn>(native_handle_valid)(entry, nullptr)) continue; // 0x4cd68b
			std::uint32_t handle = *entry;                                              // 0x4cd69e (iStack_ac)
			void* entity = native<handle_fn>(native_entity_of_handle)(&handle, nullptr); // 0x4cd6a2
			void* subject = nullptr;  // EDI: class d
			void* character = nullptr; // EBP: class a
			void* actor = nullptr;     // ESI: class e
			if (entity && class_bit(entity_class(entity), class_id_d)) // 0x4cd6a9 / 0x4cd6b5
			{
				subject = entity;
				if (class_bit(entity_class(entity), class_id_a)) // 0x4cd6db
				{
					character = entity;
					if (class_bit(entity_class(entity), class_id_e)) actor = entity; // 0x4cd701
				}
			}
			if (apply == 0) // 0x4cd727
			{
				slot<notify_fn>(subject, 0xbc)(subject, nullptr, 0, 0, 0); // 0x4cd73c (native dereferences the class-d entity unconditionally)
				continue;
			}
			if (order == 0x1f) // 0x4cd74d
			{
				if (character) // 0x4cd757
				{
					native<set_state_fn>(native_actor_set_state)(character, nullptr, 4);                             // 0x4cd763
					iat_fn<vec_assign_fn>(iat_vec_assign)(floats(self, 0x20d0), nullptr, floats(character, 0x20)); // 0x4cd776
					if (actor) field<std::uint8_t>(actor, 0x31c) = 2;                                               // 0x4cd784
				}
				continue;
			}
			native<timing_fn>(native_actor_timing)(subject, nullptr, 5.0f, 35.0f); // 0x4cd79c (0x40a00000, 0x420c0000)
			native<set_state_fn>(native_actor_order)(subject, nullptr, order);      // 0x4cd7ab
			if (subject && class_bit(entity_class(subject), class_id_a))            // 0x4cd7b0 / 0x4cd7b8
			{
				if (set_state) native<set_state_fn>(native_actor_set_state)(subject, nullptr, 4); // 0x4cd7e7
				if (actor) // 0x4cd7ec
				{
					std::uint32_t scale_bits;
					std::memcpy(&scale_bits, &scale, 4);
					field<std::uint32_t>(actor, 0x284) = scale_bits;                                // 0x4cd7f9
					slot<bits_method_fn>(actor, 0x1a4)(actor, nullptr, scale_bits);                 // 0x4cd7ff
					field<std::uint8_t>(actor, 0x31c) = 0;                                          // 0x4cd80d
					if (order == 0x1d) native<set_state_fn>(native_actor_flag)(actor, nullptr, 0x22); // 0x4cd81a
				}
			}
		}
		if (apply == 0) field<std::uint32_t>(output, named_output_count) = 0; // 0x4cd844
	}
}
