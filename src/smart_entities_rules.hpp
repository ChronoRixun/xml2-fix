#pragma once

// CSmartEntMgr (the dormant "smart entity" pool) at the W2 layout: ledger row W2-E15-13 (docs/limits-wave2.md
// sections 3.5, 4.6 (c), 15.2; SPEC-old 2.7, the [R2] CSmartEntMgr block). Pure: layout constants of the retail
// and the W2 object, the native addresses the replacement calls or guards, and the record / bitmap / box rules the
// replacement and its test share. No Windows calls.
//
// Retail (XMen2.exe sha1 7d95cdb4..., decompiled from the owner region [0x4b6e70, 0x4b7744)): a static object at
// 0x767390 created on first use by the getter 0x4b76f0 (init flag 0x76bbfc bit 0, exit handler 0x67dd50 registered
// by the getter alone) with the eight-slot vtable 0x68e440:
//   +0x00 0x4b76d0 deleting destructor (0x4b7680: own vtable, pool clear 0x4b6f10, base vtable 0x68e41c)
//   +0x04 0x4b7100 update(bool all): respawns the records inside a box around the camera, 40 per call round robin
//   +0x08 0x4b7000 allocate: 0 when count == 768, else constructs record [count] (0x487610) and returns it
//   +0x0c 0x4b70e0 reset: pool clear (every allocated record cleared without disposal, bitmap zeroed), count 0, cursor 0
//   +0x10 0x4b6fc0 clear records: 0x465e70(record, 1) for every record below the count (count and bitmap kept)
//   +0x14 0x4b6fb0 count
//   +0x18 0x4b6ff0 record at index (no bound)
//   +0x1c 0x4b75c0 append-copy: copies a 24-byte record (0x4b7630) to [count], sets its bit, count + 1 (no bound)
// Object: +0 vtable, +4 update cursor, +8 the pool {records, bitmap, count}. Retail pool: 768 x 0x18 records,
// 24-dword bitmap at object+0x4808, count at object+0x4868. W2: 2048 x 0x20 records (pool+0 = object+8), 64-dword
// bitmap at object+0x10008, count at object+0x10108, object size 0x1010c.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace smart_entities_rules
{
	// ---- object layouts -----------------------------------------------------------------------------------------
	struct layout
	{
		std::uint32_t capacity, record_size, records, bitmap, bitmap_dwords, count, object_size;
	};
	inline constexpr std::uint32_t vtable_offset = 0x0;
	inline constexpr std::uint32_t cursor_offset = 0x4;
	inline constexpr layout retail{768, 0x18, 0x8, 0x4808, 24, 0x4868, 0x486c};
	inline constexpr layout w2{2048, 0x20, 0x8, 0x10008, 64, 0x10108, 0x1010c};
	static_assert(retail.records + retail.capacity * retail.record_size == retail.bitmap);
	static_assert(retail.bitmap + retail.bitmap_dwords * 4 == retail.count && retail.capacity == retail.bitmap_dwords * 32);
	static_assert(w2.records + w2.capacity * w2.record_size == w2.bitmap && w2.bitmap + w2.bitmap_dwords * 4 == w2.count);
	static_assert(w2.count + 4 == w2.object_size && w2.capacity == w2.bitmap_dwords * 32);

	// ---- the W2 state record (docs/limits-wave2.md 3.5) ---------------------------------------------------------
	struct w2_record
	{
		std::uint32_t definition;  // +0x00 definition-name handle
		std::uint32_t instance;    // +0x04 instance-name handle
		std::uint8_t payload[8];   // +0x08 class payload (unchanged; +8..+0xa are also the orientation bytes 0x465a00 reads)
		std::uint8_t flags_a;      // +0x10 bit 0: owns a world-item slot (tested by the disposal 0x479c10)
		std::uint8_t flags_b;      // +0x11 bit 0: the record clear 0x465e70 calls the disposal only when it is set
		std::uint16_t slot;        // +0x12 world-item slot, 0xffff = none
		float position[3];         // +0x14 bit-exact floats (retail: 3 x i16 at +0x12)
	};
	static_assert(sizeof(w2_record) == 0x20 && offsetof(w2_record, slot) == 0x12 && offsetof(w2_record, position) == 0x14);
	inline constexpr std::uint16_t no_slot = 0xffff;

	// 0x487610 (record constructor, reached from allocate through 0x4b7090) on the W2 record: every field zero,
	// payload byte 0 = 0xff, and the two W2 fields initialised (slot none, positions 0.0).
	inline void construct(w2_record& r)
	{
		r.definition = 0; r.instance = 0; r.flags_b = 0; r.slot = no_slot;
		r.position[0] = r.position[1] = r.position[2] = 0.0f;
		std::memset(r.payload, 0, sizeof r.payload); r.flags_a = 0;
		r.payload[0] = 0xff;
	}
	// 0x465e70 (record clear) on the W2 record: whether it calls the disposal 0x479c10 first...
	inline bool clear_disposes(const w2_record& r, bool dispose) { return dispose && (r.flags_b & 1) != 0; }
	// ...and its field writes: handles 0, positions 0.0 (retail: the three i16), payload bytes 1 and 2 and both flag
	// bytes 0, payload byte 0 = 0xff, slot none. Payload bytes 3..7 keep their value exactly as retail leaves them.
	inline void clear_fields(w2_record& r)
	{
		r.definition = 0; r.instance = 0; r.slot = no_slot;
		r.position[0] = r.position[1] = r.position[2] = 0.0f;
		r.payload[2] = 0; r.payload[1] = 0; r.flags_b = 0; r.flags_a = 0;
		r.payload[0] = 0xff;
	}

	// ---- bitmap (the pool's allocated bits: index >> 5 dword, bit index & 31) -------------------------------------
	inline void set_bit(std::uint32_t* bits, std::int32_t i) { bits[i >> 5] |= 1u << (i & 31); }
	inline void clear_bit(std::uint32_t* bits, std::int32_t i) { bits[i >> 5] &= ~(1u << (i & 31)); }
	inline bool test_bit(const std::uint32_t* bits, std::int32_t i) { return (bits[i >> 5] >> (i & 31)) & 1u; }

	// ---- update (0x4b7100) ----------------------------------------------------------------------------------------
	inline constexpr std::int32_t update_budget = 40;  // records examined per incremental call (0x4b7139 / 0x4b713f)
	inline constexpr float look_ahead = 350.0f;        // 0x68e468: camera row 2 scale
	inline constexpr float near_half = 250.0f;          // 0x68e464: half box around the camera position
	inline constexpr float far_half = 750.0f;           // 0x68e460: half box around the look-ahead point
	inline constexpr float negate = -1.0f;              // 0x68117c
	struct update_plan { std::int32_t first, budget, cursor_after; };
	// The start record, the number of records to examine and the cursor stored before the loop (count > 0).
	inline update_plan plan(bool all, std::int32_t cursor, std::int32_t count)
	{
		if (all) return {0, count, cursor};
		if (cursor >= count) cursor = 0;
		return {cursor, update_budget, cursor + update_budget};
	}
	// Spawn failure at examined-record k: the cursor moves back so the next call starts at the failed record.
	inline std::int32_t rewind(std::int32_t cursor, std::int32_t k) { const std::int32_t c = cursor + (k - update_budget); return c < 0 ? 0 : c; }
	// 0x4b74a0: x and y inside [min, max] (z ignored); a NaN on either side passes, exactly as the x87 tests do.
	inline bool box_contains(const float* min, const float* max, const float* p)
	{
		return !(min[0] > p[0]) && !(max[0] < p[0]) && !(min[1] > p[1]) && !(max[1] < p[1]);
	}

	// ---- native addresses ------------------------------------------------------------------------------------------
	inline constexpr std::uint32_t region_lo = 0x4b6e70, region_hi = 0x4b7744; // owner region (functions 0x4b6e70..0x4b773e)
	inline constexpr const char* region_sha256 = "1969cfd96a7b2701a2f30d915956192d795e1a43fd1f6248c5d0d52b27d04a4b";
	inline constexpr std::uint32_t getter_va = 0x4b76f0;
	inline constexpr const char* getter_entry_hex = "8a0dfcbb7600";            // mov cl, byte ptr [0x76bbfc]
	inline constexpr std::uint32_t native_vtable = 0x68e440, base_vtable = 0x68e41c;
	inline constexpr std::uint32_t static_object = 0x767390, init_flag = 0x76bbfc;
	inline constexpr std::uint32_t exit_handler = 0x67dd50;
	inline constexpr const char* exit_handler_hex = "b990737600e92699e3ff";   // mov ecx, 0x767390; jmp 0x4b7680
	// Engine routines the replacement calls unchanged (none of them depends on the pool layout).
	inline constexpr std::uint32_t native_dispose = 0x479c10;     // __cdecl(record*): releases the record's world-item slot (W2: adapter W2-E3c-10)
	inline constexpr std::uint32_t native_orientation = 0x465a00; // __thiscall(record*, igVec3f* out) -> out (payload bytes +8..+0xa)
	inline constexpr std::uint32_t native_handle_name = 0x465be0; // __thiscall(u32* handle) -> const char*
	inline constexpr std::uint32_t native_spawn = 0x4612e0;       // __cdecl(def, inst, const igVec3f* pos, const igVec3f* orient, 0) -> entity*
	inline constexpr std::uint32_t native_as_actor = 0x402b60;    // __cdecl(entity*) -> actor* or 0
	inline constexpr std::uint32_t native_entity_flag = 0x419c90; // __thiscall(entity+4, 0x11, 1)
	inline constexpr std::uint32_t native_entity_prepare = 0x45fe40; // __thiscall(entity*)
	inline constexpr std::uint32_t native_camera_owner = 0x4457e0;   // __cdecl() -> object whose vt+0xd0 fills an igMatrix44f
	inline constexpr std::uint32_t camera_matrix_slot = 0xd0, actor_wake_slot = 0xbc, entity_restore_slot = 0x10;
	inline constexpr std::uint32_t actor_flags_offset = 0x3d8, actor_flag_keep = 0x40;
	inline constexpr std::uint32_t wake_actors_flag = 0x70b703;      // byte: call actor vt+0xbc(0, 0, 0) after a spawn
	inline constexpr std::uint32_t iat_vec3_set = 0x67fa1c;          // libIGMath igVec3f::set(float, float, float)
	inline constexpr std::uint32_t iat_matrix_identity = 0x67fa28;   // libIGMath igMatrix44f::makeIdentity()
	inline constexpr std::uint32_t iat_vec3_assign = 0x67fa2c;       // libIGMath igVec3f::operator=(const igVec3f&)

	// Every native user of the object: the 17 calls of the getter, each followed by a call through the vtable slot
	// named here (no caller touches a field; a sweep of the image finds no other reference to 0x767390..0x76bbfc
	// than the getter and the exit handler, and no call into the region from outside except 0x60de99 -> 0x4b6e70,
	// the generic bitset<768>::findNext of another object, which stays retail because the region is not written).
	struct caller { std::uint32_t va; const char* call_hex; std::uint32_t slot; };
	inline constexpr caller getter_callers[] = {
		{0x45fc51, "e89a7a0500", 0x08}, {0x466114, "e8d7150500", 0x14}, {0x466130, "e8bb150500", 0x18},
		{0x466167, "e884150500", 0x18}, {0x466331, "e8ba130500", 0x18}, {0x466357, "e894130500", 0x18},
		{0x466441, "e8aa120500", 0x18}, {0x466465, "e886120500", 0x18}, {0x4666c9, "e822100500", 0x14},
		{0x46672e, "e8bd0f0500", 0x18}, {0x466805, "e8e60e0500", 0x0c}, {0x4668d9, "e8120e0500", 0x1c},
		{0x46f2df, "e80c840400", 0x04}, {0x4843a0, "e84b330300", 0x0c}, {0x48445a, "e891320300", 0x10},
		{0x484642, "e8a9300300", 0x0c}, {0x48af03, "e8e8c70200", 0x08},
	};
}
