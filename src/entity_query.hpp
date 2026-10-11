#pragma once

// Phase A entity capacity: the query owner (CStaticQuery x3, CStaticOrder x3, the embedded CCollisions maps and
// the QueryRef/OrderRef/TraceRef bridge API) ported from the retail XMen2.exe (sha1 7d95cdb4a9a599b982147a3389ea7aff211cb82b)
// region 0x4a9580..0x4abb40 so the entity domain can grow from 256 to 1024 World handles.
//
// One code path serves both capacities: with the stock descriptor (world 256) the objects are laid out exactly as
// the retail statics (query stride 0x1e4, order stride 0x814, manager 0x1dec bytes, eight-dword sets); with the
// raised descriptor (world 1024) the layout is round6/layouts.json "query"/"collisions"/"sorted"/"query_manager"
// (query stride 0x544, order stride 0x2014, manager 0x700c bytes, 32-dword sets). Every native side effect,
// ordering, early return, return register and x87 result is preserved; nothing is "simplified".
//
// Nothing here is game content: retail addresses, field offsets and the decompiled control flow only.

#include "entity_capacity_layout.hpp"
#include <cstddef>
#include <cstdint>

namespace entity_query
{
	using entity_capacity_layout::descriptor;

	// ---- retail addresses this owner depends on (not in the shared header yet; see the report) ----------------
	constexpr std::uint32_t native_collide_getter = 0x44cd20;      // __cdecl() -> physics/collide owner (vtable 0x685660)
	constexpr std::uint32_t native_handle_valid = 0x4654d0;        // __thiscall(uint32* handle) -> bool  (World slot 0x14)
	constexpr std::uint32_t native_entity_of_handle = 0x4654b0;    // __thiscall(uint32* handle) -> CEntity* (World slot 0xc)
	constexpr std::uint32_t native_player_handle = 0x45fe70;       // __cdecl(uint32* out) -> out (World slot 0x24 via 0x718430)
	constexpr std::uint32_t native_record_by_name = 0x4611a0;      // __cdecl(const char*) -> type record (+4 = class id)
	constexpr std::uint32_t native_angles_to_direction = 0x560990; // __cdecl(float out[3], const float angles[3]) -> out
	constexpr std::uint32_t native_trace_pool = 0x758910;          // 50 x 64-byte trace records + ring + bitmap (NOT widened)
	constexpr std::uint32_t native_trace_pool_count = 0x759674;    // in-use count, max 0x32
	constexpr std::uint32_t native_trace_pool_take = 0x4a9e90;     // __thiscall(pool) -> slot index
	constexpr std::uint32_t native_trace_pool_return = 0x4a9bf0;   // __thiscall(pool, slot)
	constexpr std::uint32_t native_policy_vtable = 0x68d684;       // trace-score policy {vtable, Query*}; slot 0 = 0x4a9a80
	constexpr std::uint32_t iat_distance2 = 0x67f9cc;              // libIGMath ?distance2@igVec3f@Math@Gap@@QBEMABV123@@Z
	constexpr std::uint32_t iat_normalize = 0x67fa5c;              // libIGMath ?normalize@igVec3f@Math@Gap@@QAEMXZ
	constexpr std::uint32_t iat_zero_vector = 0x67fa54;            // libIGMath ?ZeroVector@igVec3f@Math@Gap@@2V123@B (data)
	constexpr std::uint32_t global_trace_stamp = 0x72988c;         // dword copied into trace record +0xc on insert
	constexpr std::uint32_t global_class_base = 0x718428;          // int: World slot 0x40 argument base for named classes
	constexpr std::uint32_t global_character_class = 0x718448;     // int: the "all characters" class id (+0x24 -> slot 0x40)
	constexpr std::uint32_t global_record_718444 = 0x718444;       // type record used by Query slot 0x58
	constexpr std::uint32_t global_class_flag_id = 0x72a0ac;       // int: class-descriptor flag id tested by 0x4a97a0
	constexpr std::uint32_t global_material_byte = 0x756b08;       // byte returned by 0x4a97a0 (+ its init flag at 0x756b0c)
	constexpr std::uint32_t global_material_init = 0x756b0c;
	constexpr std::size_t trace_record_bytes = 0x40, trace_pool_records = 50;

	// ---- the derived layout ------------------------------------------------------------------------------------
	// All offsets are bytes from the object; identical to retail for world 256 (static_asserts below).
	struct layout
	{
		std::uint32_t world = 0, set_words = 0, set_bytes = 0;
		// CStaticQuery: vtable +0, World* +4, bitmap +8, then the trailing fields and the embedded CCollisions.
		std::uint32_t q_world = 4, q_bitmap = 8, q_flags_a = 0, q_flags_b = 0, q_b0 = 0, q_b1 = 0, q_b2 = 0, q_busy = 0, q_result = 0, q_collisions = 0, q_stride = 0;
		// CCollisions (relative to the embedded object): vtable +0, byte map +4 (world entries), used keys u16[52] + count,
		// used trace slots u8[52] + count.
		std::uint32_t c_map = 4, c_keys = 0, c_key_count = 0, c_slots = 0, c_slot_count = 0, c_size = 0;
		static constexpr std::uint32_t c_key_capacity = 52; // 0x68 bytes of u16 keys; 0x34 bytes of u8 slots (retail sizes)
		// CStaticOrder: vtable +0, {CEntity*, float score} pairs +4 (world pairs), a pad dword, count, Query*, busy byte.
		std::uint32_t o_pairs = 4, o_count = 0, o_query = 0, o_busy = 0, o_stride = 0;
		// Manager: three queries, the four-byte array tail, three orders.
		std::uint32_t m_queries = 0, m_tail = 0, m_orders = 0, m_size = 0;
		static constexpr std::uint32_t query_count = 3, order_count = 3;
	};
	constexpr layout layout_for(std::uint32_t world)
	{
		layout l{};
		l.world = world; l.set_words = world / 32; l.set_bytes = world / 8;
		l.q_flags_a = l.q_bitmap + l.set_bytes; l.q_flags_b = l.q_flags_a + 4;
		l.q_b0 = l.q_flags_b + 4; l.q_b1 = l.q_b0 + 1; l.q_b2 = l.q_b0 + 2; l.q_busy = l.q_b0 + 3;
		l.q_result = l.q_b0 + 4; l.q_collisions = l.q_result + 4;
		l.c_keys = l.c_map + world + 4; l.c_key_count = l.c_keys + layout::c_key_capacity * 2;
		l.c_slots = l.c_key_count + 4; l.c_slot_count = l.c_slots + layout::c_key_capacity; l.c_size = l.c_slot_count + 4;
		l.q_stride = l.q_collisions + l.c_size;
		l.o_count = l.o_pairs + world * 8 + 4; l.o_query = l.o_count + 4; l.o_busy = l.o_query + 4; l.o_stride = l.o_busy + 4; // +4: the retail dword between the pairs and the count (0x4a9870 writes the count at pairs+0x804)
		l.m_tail = l.m_queries + layout::query_count * l.q_stride; l.m_orders = l.m_tail + 4;
		l.m_size = l.m_orders + layout::order_count * l.o_stride;
		return l;
	}
	// Retail (0x4aa030 / 0x4a9cd0 / 0x4a9d10 / 0x4ab4e0 / 0x4ab5b0 / 0x4ab770 constants).
	static_assert(layout_for(256).q_flags_a == 0x28 && layout_for(256).q_flags_b == 0x2c && layout_for(256).q_b0 == 0x30 && layout_for(256).q_b1 == 0x31 &&
	              layout_for(256).q_b2 == 0x32 && layout_for(256).q_busy == 0x33 && layout_for(256).q_result == 0x34 && layout_for(256).q_collisions == 0x38 &&
	              layout_for(256).q_stride == 0x1e4);
	static_assert(layout_for(256).c_keys == 0x108 && layout_for(256).c_key_count == 0x170 && layout_for(256).c_slots == 0x174 && layout_for(256).c_slot_count == 0x1a8 && layout_for(256).c_size == 0x1ac);
	static_assert(layout_for(256).o_count == 0x808 && layout_for(256).o_query == 0x80c && layout_for(256).o_busy == 0x810 && layout_for(256).o_stride == 0x814);
	static_assert(layout_for(256).m_tail == 0x5ac && layout_for(256).m_orders == 0x5b0 && layout_for(256).m_size == 0x1dec);
	// Raised (round6/layouts.json query/collisions/sorted/query_manager).
	static_assert(layout_for(1024).q_flags_a == 0x88 && layout_for(1024).q_collisions == 0x98 && layout_for(1024).q_stride == 0x544);
	static_assert(layout_for(1024).c_keys == 0x408 && layout_for(1024).c_key_count == 0x470 && layout_for(1024).c_slots == 0x474 && layout_for(1024).c_slot_count == 0x4a8 && layout_for(1024).c_size == 0x4ac);
	static_assert(layout_for(1024).o_count == 0x2008 && layout_for(1024).o_query == 0x200c && layout_for(1024).o_busy == 0x2010 && layout_for(1024).o_stride == 0x2014);
	static_assert(layout_for(1024).m_tail == 0xfcc && layout_for(1024).m_orders == 0xfd0 && layout_for(1024).m_size == 0x700c);

	// ---- the manager ---------------------------------------------------------------------------------------------
	// The DLL allocates and constructs the manager (VirtualAlloc, zero-filled, constructed as 0x4ab4e0 does: three
	// queries by the 0x4aa030 sequence, then three orders by 0x4a9d10). The integrator's QueryManagerGet (replacing
	// 0x4ab550) returns this pointer; the bridge functions below fetch it through the getter address exactly as the
	// native code does. One manager per process (the native static is one too).
	std::size_t manager_size(const descriptor& d);
	void* create_manager(const descriptor& d); // nullptr when allocation fails or a manager already exists
	void destroy_manager(void* manager);        // runs the (empty) native destructors' observable effect: nothing; frees
	void* installed_manager();                  // the manager created by create_manager (nullptr when none)
	const descriptor& installed_descriptor();
	const layout& installed_layout();
	bool owns_query(const void* query);         // one of the installed manager's three queries
	bool owns_order(const void* order);

	// The DLL vtables (for the integrator's installation checks and the tests).
	const void* const* query_vtable();      // 36 slots, native order of 0x68d6a4
	const void* const* order_vtable();      // 3 slots, native order of 0x68d694
	const void* const* collisions_vtable(); // 1 slot, native order of 0x68d68c

	// Test hook: where the bridge functions fetch the manager from (default: the retail getter address, which the
	// integrator replaces). The differential test points it at its own selector.
	using manager_getter_fn = void* (__cdecl*)();
	void set_manager_getter(manager_getter_fn getter); // nullptr restores the default

	// ---- the 25 bridge functions -------------------------------------------------------------------------------------
	// Each replaces the whole native function at its address (E9 jump at the entry) and keeps the native calling
	// convention, argument list, stack cleanup and return register. "__fastcall(self, void* edx, ...)" is the
	// header's spelling of __thiscall; the SEH frames of the 0x4ab7f0 family become RAII (same release on unwind).
	// TraceRef accessors (ref -> 64-byte trace record pointer, or null).
	bool __fastcall QueryApi_004a9580(void** ref, void* edx);                          // ref has a record
	void* __fastcall QueryApi_004a9590(void** ref, void* edx);                         // World slot 0xc of record +0xc token
	bool __fastcall QueryApi_004a95c0(void** ref, void* edx);                          // record token == player handle
	const float* __fastcall QueryApi_004a95f0(void** ref, void* edx);                  // record +0 or ZeroVector
	const float* __fastcall QueryApi_004a9600(void** ref, void* edx);                  // record +0x10 or ZeroVector
	const float* __fastcall QueryApi_004a9610(void** ref, void* edx);                  // record +0x20 or ZeroVector
	const float* __fastcall QueryApi_004a9620(void** ref, void* edx);                  // record +0x30 or ZeroVector
	float __fastcall QueryApi_004a9630(void** ref, void* edx);                         // record +0x3c or 0.0 (ST0)
	const std::uint8_t* __fastcall QueryApi_004a97a0(void** ref, void* edx);           // material byte at 0x756b08
	// Trace-score policy {vtable 0x68d684, Query*}: constructor and its slot 0 callback (owner-aware).
	// Returns the policy: the native 0x4a9730 ends `mov eax, ecx` and ten callers (0x441950, 0x441c00, 0x432130, ...)
	// push that EAX as the policy argument of 0x4ab770.
	void** __fastcall QueryApi_004a9730(void** policy, void* edx, void** query_ref);
	double __fastcall QueryApi_004a9a80(void** policy, void* edx, const std::uint8_t* entity); // ST0 float: trace score or 0.0
	// The 52-entry byte list constructor shared with 0x58d2d0 (count at +0x34 := 0; returns this).
	void* __fastcall QueryApi_004a98b0(void* list, void* edx);
	// Movement trace helper (cdecl, six args): exclude a, b; AND property 1; mark; trace from pos to pos+delta.
	bool __cdecl QueryApi_004a9650(std::uint32_t a, std::uint32_t b, const float* pos, const float* delta, std::uint32_t* out, void** query_ref);
	// QueryRef acquire/release (thiscall on the caller's QueryRef cell).
	void** __fastcall QueryApi_004ab5b0(void** ref, void* edx, std::uint32_t all);                 // slot 0x10 reset
	void** __fastcall QueryApi_004ab610(void** ref, void* edx, void** source_ref);                 // slot 0xc copy
	void** __fastcall QueryApi_004ab670(void** ref, void* edx, const std::uint32_t* exclude);      // slot 0x8 reset
	void** __fastcall QueryApi_004ab6d0(void** ref, void* edx, const void* record, const std::uint32_t* exclude); // slot 0x4
	void __fastcall QueryApi_004ab720(void** ref, void* edx);                                      // release
	int __cdecl QueryApi_004ab740();                                                                // free query count
	// OrderRef: fill a free order from a query with a scoring policy; release.
	void* __cdecl QueryApi_004ab770(void* query, void* policy);
	void* __cdecl QueryApi_004ab7d0(void* order); // returns its argument (native: `mov eax, [esp+4]` before ret)
	// Scoped trace queries (cdecl; the native SEH frames release the query on unwind).
	bool __cdecl QueryApi_004ab7f0(std::uint32_t a, std::uint32_t b, const float* pos, const float* delta, std::uint32_t* out, void** source_ref);
	bool __cdecl QueryApi_004ab890(std::uint32_t a, std::uint32_t b, const float* pos, const float* delta, std::uint32_t* out, std::uint8_t keep_player);
	bool __cdecl QueryApi_004ab960(std::uint32_t exclude, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t* out);
	bool __cdecl QueryApi_004aba30(std::uint32_t exclude, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t* out);
}
