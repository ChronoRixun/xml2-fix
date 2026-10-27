// CSmartEntMgr at the W2 layout (ledger row W2-E15-13). Every method names the native routine it reproduces; the
// behaviour is the retail behaviour decompiled from the owner region [0x4b6e70, 0x4b7744) of XMen2.exe (sha1
// 7d95cdb4...), quirks included, on 2048 records of 0x20 bytes. The only intended differences: the capacity, the
// record size and the two W2 record fields (slot 0xffff and float positions 0.0 wherever retail constructs or clears
// a record; whole 0x20-byte copies), the append-copy refusing at capacity (retail has no bound there and would write
// over its own bitmap at 769), and the static object never being passed to operator delete (see destroy).

#include "smart_entities.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace smart_entities
{
	namespace
	{
		using namespace smart_entities_rules;

		struct w2_object
		{
			const void* vtable;           // +0x0
			std::int32_t cursor;          // +0x4 update round-robin start
			w2_record records[2048];      // +0x8
			std::uint32_t bitmap[64];     // +0x10008
			std::int32_t count;           // +0x10108
		};
		static_assert(offsetof(w2_object, cursor) == cursor_offset && offsetof(w2_object, records) == w2.records);
		static_assert(offsetof(w2_object, bitmap) == w2.bitmap && offsetof(w2_object, count) == w2.count && sizeof(w2_object) == w2.object_size);
		static_assert(sizeof(w2_object::records) / sizeof(w2_record) == w2.capacity && sizeof(w2_object::bitmap) / 4 == w2.bitmap_dwords);

		w2_object g_object; // zero-initialised like the retail static; the getter installs the vtable on first use
		counters g_counters{};

		template <typename Fn> Fn native(std::uint32_t va) { return reinterpret_cast<Fn>(static_cast<std::uintptr_t>(va)); }
		template <typename Fn> Fn imported(std::uint32_t iat) { return *reinterpret_cast<Fn const*>(static_cast<std::uintptr_t>(iat)); }
		std::uint32_t address_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

		// ---- record and pool helpers ----------------------------------------------------------------------------
		// 0x465e70 (record clear) on a W2 record.
		void clear_record(w2_record* r, bool dispose)
		{
			if (clear_disposes(*r, dispose)) native<void(__cdecl*)(void*)>(native_dispose)(r);
			clear_fields(*r);
		}
		// 0x4b6f10 (pool clear): every allocated record in index order cleared without disposal, then the bitmap
		// zeroed; the count is left as it is (the callers 0x4b70e0 / 0x4b7680 decide).
		void clear_pool(w2_object* o)
		{
			for (std::int32_t i = 0; i < static_cast<std::int32_t>(w2.capacity); ++i)
				if (test_bit(o->bitmap, i)) clear_record(&o->records[i], false);
			std::memset(o->bitmap, 0, sizeof o->bitmap);
		}
		// 0x4b7530: clear record i (no disposal) and its bit.
		void release(w2_object* o, std::int32_t i) { clear_record(&o->records[i], false); clear_bit(o->bitmap, i); }
		// 0x4b7570: set bit dst, then copy record src over record dst (0x4b7630, the whole record).
		void move(w2_object* o, std::int32_t dst, std::int32_t src) { set_bit(o->bitmap, dst); std::memcpy(&o->records[dst], &o->records[src], sizeof(w2_record)); }
		// 0x4b74e0: remove record i by moving the last record into it.
		void remove(w2_object* o, std::int32_t i)
		{
			if (i != o->count - 1) { release(o, i); move(o, i, o->count - 1); }
			release(o, o->count - 1);
			o->count = o->count - 1;
		}

		// ---- the camera box of 0x4b7100 (0x4b7147..0x4b7361) ------------------------------------------------------
		// The x87 sequence is kept instruction for instruction (products stay in registers, the first minimum compares
		// a register value): the box is then bit-identical to retail under any FPU precision the game runs with.
		struct box_work
		{
			float t[3];      // +0  camera position (matrix row 3)
			float v[3];      // +12 camera row 2, negated in place
			float c[3];      // +24 look-ahead point t + v * 350
			float a2;        // +36
			float unused;    // +40
			float l1, l2;    // +44, +48 t.y - 250, t.z - 250
			float d[3];      // +52 c - 750
			float mn[3];     // +64 box minimum
			float k_negate, k_look, k_near, k_far; // +76 .. +88
			float e0[3];     // +92 t + 250 (igVec3f::set arguments)
			float e1[3];     // +104 c + 750
		};
		static_assert(offsetof(box_work, v) == 12 && offsetof(box_work, c) == 24 && offsetof(box_work, a2) == 36 && offsetof(box_work, l1) == 44);
		static_assert(offsetof(box_work, d) == 52 && offsetof(box_work, mn) == 64 && offsetof(box_work, k_negate) == 76 && offsetof(box_work, e0) == 92 && offsetof(box_work, e1) == 104);

		void box_arithmetic(box_work* w)
		{
			__asm
			{
				mov ecx, w
				fld dword ptr [ecx+12]
				fmul dword ptr [ecx+76]
				fstp dword ptr [ecx+12]
				fld dword ptr [ecx+16]
				fmul dword ptr [ecx+76]
				fstp dword ptr [ecx+16]
				fld dword ptr [ecx+20]
				fmul dword ptr [ecx+76]
				fstp dword ptr [ecx+20]
				fld dword ptr [ecx+12]
				fmul dword ptr [ecx+80]
				fld dword ptr [ecx+16]
				fmul dword ptr [ecx+80]
				fld dword ptr [ecx+20]
				fmul dword ptr [ecx+80]
				fstp dword ptr [ecx+36]
				fxch st(1)
				fadd dword ptr [ecx+0]
				fstp dword ptr [ecx+24]
				fld dword ptr [ecx+4]
				fadd st, st(1)
				fstp dword ptr [ecx+28]
				fstp st(0)
				fld dword ptr [ecx+8]
				fadd dword ptr [ecx+36]
				fstp dword ptr [ecx+32]
				fld dword ptr [ecx+0]
				fsub dword ptr [ecx+84]
				fld dword ptr [ecx+4]
				fsub dword ptr [ecx+84]
				fst dword ptr [ecx+44]
				fld dword ptr [ecx+8]
				fsub dword ptr [ecx+84]
				fst dword ptr [ecx+48]
				fld st(2)
				fstp dword ptr [ecx+64]
				fxch st(1)
				fstp dword ptr [ecx+68]
				fstp dword ptr [ecx+72]
				fld dword ptr [ecx+24]
				fsub dword ptr [ecx+88]
				fstp dword ptr [ecx+52]
				fld dword ptr [ecx+28]
				fsub dword ptr [ecx+88]
				fstp dword ptr [ecx+56]
				fld dword ptr [ecx+32]
				fsub dword ptr [ecx+88]
				fstp dword ptr [ecx+60]
				fcomp dword ptr [ecx+52]
				fnstsw ax
				test ah, 41h
				jne min_y
				mov edx, dword ptr [ecx+52]
				mov dword ptr [ecx+64], edx
			min_y:
				fld dword ptr [ecx+44]
				fcomp dword ptr [ecx+56]
				fnstsw ax
				test ah, 41h
				jne min_z
				mov edx, dword ptr [ecx+56]
				mov dword ptr [ecx+68], edx
			min_z:
				fld dword ptr [ecx+48]
				fcomp dword ptr [ecx+60]
				fnstsw ax
				test ah, 41h
				jne min_done
				mov edx, dword ptr [ecx+60]
				mov dword ptr [ecx+72], edx
			min_done:
				fld dword ptr [ecx+8]
				fadd dword ptr [ecx+84]
				fstp dword ptr [ecx+100]
				fld dword ptr [ecx+4]
				fadd dword ptr [ecx+84]
				fstp dword ptr [ecx+96]
				fld dword ptr [ecx+0]
				fadd dword ptr [ecx+84]
				fstp dword ptr [ecx+92]
				fld dword ptr [ecx+32]
				fadd dword ptr [ecx+88]
				fstp dword ptr [ecx+112]
				fld dword ptr [ecx+28]
				fadd dword ptr [ecx+88]
				fstp dword ptr [ecx+108]
				fld dword ptr [ecx+24]
				fadd dword ptr [ecx+88]
				fstp dword ptr [ecx+104]
			}
		}

		using vec3_set_fn = void(__fastcall*)(float*, void*, float, float, float);
		using matrix_identity_fn = void(__fastcall*)(float*, void*);
		using vec3_assign_fn = float*(__fastcall*)(float*, void*, const float*);

		// ---- the vtable methods (native __thiscall as __fastcall with an unused EDX) -------------------------------

		// +0x00 0x4b76d0 deleting destructor: 0x4b7680 (own vtable, pool clear, base vtable 0x68e41c). The delete
		// bit is ignored: the object is static (retail's is too, and no caller passes the bit).
		void* __fastcall destroy(w2_object* o, void*, std::uint32_t /*flags*/);
		void __fastcall update(w2_object* o, void*, std::uint32_t all);
		w2_record* __fastcall allocate(w2_object* o, void*);
		void __fastcall reset(w2_object* o, void*);
		void __fastcall clear_records(w2_object* o, void*);
		std::int32_t __fastcall count(w2_object* o, void*);
		w2_record* __fastcall at(w2_object* o, void*, std::int32_t i);
		void __fastcall append_copy(w2_object* o, void*, const void* source);

		struct vtable_layout
		{
			decltype(&destroy) slot0; decltype(&update) slot1; decltype(&allocate) slot2; decltype(&reset) slot3;
			decltype(&clear_records) slot4; decltype(&count) slot5; decltype(&at) slot6; decltype(&append_copy) slot7;
		};
		static_assert(sizeof(vtable_layout) == 0x20);
		const vtable_layout g_vtable{&destroy, &update, &allocate, &reset, &clear_records, &count, &at, &append_copy};

		void* __fastcall destroy(w2_object* o, void*, std::uint32_t)
		{
			o->vtable = &g_vtable;
			clear_pool(o);
			o->vtable = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(base_vtable));
			return o;
		}

		// +0x04 0x4b7100 update(all): with records present, builds the box around the camera (min of position - 250
		// and look-ahead - 750, max of position + 250 and look-ahead + 750, per axis), then examines `budget`
		// records from the plan's start, wrapping at the count: a record whose x / y lie in the box is spawned
		// (0x4612e0 with its definition and instance names, position and orientation); no entity -> the cursor
		// moves back to it and the call ends; an actor without flag 0x40 at +0x3d8 keeps its record; otherwise the
		// entity is flagged (0x419c90), prepared (0x45fe40), restored from the record (vt+0x10) and the record is
		// removed (the last record moves into its index, which is examined next).
		void __fastcall update(w2_object* o, void*, std::uint32_t all)
		{
			if (o->count == 0) return;
			const update_plan p = plan((all & 0xff) != 0, o->cursor, o->count);
			o->cursor = p.cursor_after;
			std::int32_t i = p.first;

			float matrix[16];
			imported<matrix_identity_fn>(iat_matrix_identity)(matrix, nullptr);
			void* owner = native<void*(__cdecl*)()>(native_camera_owner)();
			(*reinterpret_cast<void(__fastcall* const* const*)(void*, void*, float*)>(owner))[camera_matrix_slot / 4](owner, nullptr, matrix);
			box_work w{};
			const auto assign = imported<vec3_assign_fn>(iat_vec3_assign);
			assign(w.t, nullptr, matrix + 12);
			assign(w.v, nullptr, matrix + 8);
			w.k_negate = negate; w.k_look = look_ahead; w.k_near = near_half; w.k_far = far_half;
			box_arithmetic(&w);
			float max[3], far_corner[3];
			const auto set = imported<vec3_set_fn>(iat_vec3_set);
			set(max, nullptr, w.e0[0], w.e0[1], w.e0[2]);
			set(far_corner, nullptr, w.e1[0], w.e1[1], w.e1[2]);
			for (int axis = 0; axis < 3; ++axis) if (max[axis] < far_corner[axis]) max[axis] = far_corner[axis];

			for (std::int32_t k = 0; k < p.budget; ++k)
			{
				w2_record* r = &o->records[i];
				if (box_contains(w.mn, max, r->position))
				{
					float position[3] = {r->position[0], r->position[1], r->position[2]};
					float orientation[3];
					native<float*(__fastcall*)(void*, void*, float*)>(native_orientation)(r, nullptr, orientation);
					const auto name = native<const char*(__fastcall*)(void*, void*)>(native_handle_name);
					const char* instance = name(&r->instance, nullptr);
					const char* definition = name(&r->definition, nullptr);
					auto* entity = native<std::uint8_t*(__cdecl*)(const char*, const char*, const float*, const float*, std::uint32_t)>(native_spawn)(definition, instance, position, orientation, 0);
					if (!entity) { o->cursor = rewind(o->cursor, k); return; }
					std::uint8_t* actor = native<std::uint8_t*(__cdecl*)(void*)>(native_as_actor)(entity);
					bool keep = false;
					if (*reinterpret_cast<volatile std::uint8_t*>(static_cast<std::uintptr_t>(wake_actors_flag)) && actor)
						(*reinterpret_cast<void(__fastcall* const* const*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t)>(actor))[actor_wake_slot / 4](actor, nullptr, 0, 0, 0);
					if (actor && !(actor[actor_flags_offset] & actor_flag_keep)) keep = true;
					if (!keep)
					{
						native<void(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t)>(native_entity_flag)(entity + 4, nullptr, 0x11, 1);
						native<void(__fastcall*)(void*, void*)>(native_entity_prepare)(entity, nullptr);
						(*reinterpret_cast<void(__fastcall* const* const*)(void*, void*, void*)>(entity))[entity_restore_slot / 4](entity, nullptr, r);
						remove(o, i);
						if (o->count == 0) return;
						--i;
					}
				}
				++i;
				if (i >= o->count) i = 0;
			}
		}

		// +0x08 0x4b7000 allocate: 0 at capacity, else record [count] constructed (0x487610), its bit set, count + 1.
		w2_record* __fastcall allocate(w2_object* o, void*)
		{
			if (o->count >= static_cast<std::int32_t>(w2.capacity)) { ++g_counters.allocate_full; return nullptr; }
			const std::int32_t i = o->count;
			construct(o->records[i]);
			set_bit(o->bitmap, i);
			o->count = o->count + 1;
			return &o->records[i];
		}

		// +0x0c 0x4b70e0 reset: pool clear, count 0, cursor 0.
		void __fastcall reset(w2_object* o, void*)
		{
			clear_pool(o);
			o->count = 0;
			o->cursor = 0;
		}

		// +0x10 0x4b6fc0 clear records: 0x465e70(record, 1) for each index below the (re-read) count.
		void __fastcall clear_records(w2_object* o, void*)
		{
			for (std::int32_t i = 0; i < o->count; ++i) clear_record(&o->records[i], true);
		}

		// +0x14 0x4b6fb0
		std::int32_t __fastcall count(w2_object* o, void*) { return o->count; }

		// +0x18 0x4b6ff0: record i, unbounded like retail (callers index below the count).
		w2_record* __fastcall at(w2_object* o, void*, std::int32_t i) { return o->records + i; }

		// +0x1c 0x4b75c0 append-copy: the 0x20-byte record copied to [count], its bit set, count + 1.
		void __fastcall append_copy(w2_object* o, void*, const void* source)
		{
			if (o->count >= static_cast<std::int32_t>(w2.capacity)) { ++g_counters.append_full; return; }
			const std::int32_t i = o->count;
			std::memcpy(&o->records[i], source, sizeof(w2_record));
			set_bit(o->bitmap, i);
			o->count = o->count + 1;
		}
	}

	// 0x4b76f0: the object, its vtable installed on first use (retail: vtable, bitmap, count and cursor set once).
	void* __cdecl getter()
	{
		if (!g_object.vtable) g_object.vtable = &g_vtable;
		return &g_object;
	}
	void* object() { return &g_object; }
	const void* vtable() { return &g_vtable; }
	counters stats() { return g_counters; }

	bool contribute_w2(patch_transaction::contribution& c, std::string& why)
	{
		using namespace patch_transaction;
		std::uint8_t flag = 0;
		if (!read_memory(init_flag, &flag, 1)) { why = "CSmartEntMgr: init flag 0x76bbfc unreadable"; return false; }
		if (flag & 1) { why = "CSmartEntMgr: the retail object at 0x767390 already exists (init flag 0x76bbfc set)"; return false; }

		contribution part;
		part.guards.push_back({getter_va, bytes_of(getter_entry_hex), "CSmartEntMgr getter 0x4b76f0 entry (mov cl, byte ptr [0x76bbfc])"});
		for (const auto& k : getter_callers)
		{
			char what[96];
			std::snprintf(what, sizeof what, "CSmartEntMgr getter call at 0x%x (then vt+0x%x)", static_cast<unsigned>(k.va), static_cast<unsigned>(k.slot));
			part.guards.push_back({k.va, bytes_of(k.call_hex), what});
		}
		part.guards.push_back({exit_handler, bytes_of(exit_handler_hex), "CSmartEntMgr exit handler 0x67dd50 (registered by the retail getter only)"});
		part.regions.push_back({"CSmartEntMgr owner [0x4b6e70, 0x4b7744)", region_lo, region_hi, sha256_of(region_sha256)});
		part.writes.push_back(relocation(getter_va, "e9", true, address_of(reinterpret_cast<const void*>(&getter)), 1, "CSmartEntMgr getter -> DLL object (2048 x 0x20)"));
		if (!verify_expectations(part, why)) { why = "CSmartEntMgr: " + why; return false; }

		if (c.owner.empty()) c.owner = "smart_entities";
		c.guards.insert(c.guards.end(), part.guards.begin(), part.guards.end());
		c.regions.insert(c.regions.end(), part.regions.begin(), part.regions.end());
		c.writes.insert(c.writes.end(), part.writes.begin(), part.writes.end());
		return true;
	}
}
