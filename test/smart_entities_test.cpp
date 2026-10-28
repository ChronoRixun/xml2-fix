// CSmartEntMgr at the W2 layout (ledger row W2-E15-13), executed. The retail methods run at their own addresses (the
// owner region [0x4b6e70, 0x4b7744), the record constructor 0x487610, the record clear 0x465e70, the position reader
// 0x4654f0 and the orientation reader 0x465a00, copied from the executable given on the command line or in
// XML2_FIX_TEST_EXE into the test's own image range) on the retail static object at 768 records; the DLL replacement
// runs on the W2 object at 2048. Every engine routine outside those (disposal, names, spawn, actor cast, entity calls,
// camera, libIGMath vector calls, atexit, delete) is a logging stub reached through the native address or import
// cell, so both sides are compared call for call: a differential over the whole method family at <= 768 records
// (same returns, same calls with the same arguments, same per-record fields, bitmap, count and cursor) under three
// x87 precisions, then the W2-only cases (2047 / 2048 records, the 2049th refused like retail's 769th, float
// positions bit-exact, the slot field carried), then the contribution's guards and region hash against the
// executable and a committed install that makes 0x4b76f0 return the DLL object. No game, no game content.
#include "patch_transaction.hpp"
#include "smart_entities.hpp"
#include "smart_entities_rules.hpp"

#include <Windows.h>
#include <float.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" std::uint8_t fixture_space[0x400000];

namespace
{
	using namespace smart_entities_rules;
	using u8 = std::uint8_t; using u16 = std::uint16_t; using u32 = std::uint32_t; using i32 = std::int32_t;

	int failures = 0, checks = 0;
	bool check(bool ok, const std::string& what) { ++checks; std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); failures += !ok; return ok; }

	void* at(u32 va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }
	u32 addr(const void* p) { return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p)); }
	u32 bits(float f) { u32 b; std::memcpy(&b, &f, 4); return b; }
	float from_bits(u32 b) { float f; std::memcpy(&f, &b, 4); return f; }
	std::string hex(u32 v) { char b[16]; std::snprintf(b, sizeof b, "0x%x", v); return b; }

	// ---- the executable -------------------------------------------------------------------------------------------
	std::vector<u8> binary;
	std::size_t file_offset(u32 va)
	{
		if (binary.size() < 0x400) return binary.size();
		auto u16_at = [](std::size_t o) { u16 n; std::memcpy(&n, binary.data() + o, 2); return n; };
		auto u32_at = [](std::size_t o) { u32 n; std::memcpy(&n, binary.data() + o, 4); return n; };
		const auto pe = u32_at(0x3c); const auto sections = pe + 24 + u16_at(pe + 20);
		for (unsigned i = 0; i < u16_at(pe + 6); ++i)
		{
			const auto row = sections + i * 40; const auto rva = u32_at(row + 12);
			if (va - 0x400000 >= rva && va - 0x400000 < rva + u32_at(row + 16)) return u32_at(row + 20) + va - 0x400000 - rva;
		}
		return binary.size();
	}
	bool exe_bytes(u32 va, std::size_t n, std::vector<u8>& out)
	{
		const auto o = file_offset(va);
		if (o + n > binary.size()) return false;
		out.assign(binary.begin() + static_cast<std::ptrdiff_t>(o), binary.begin() + static_cast<std::ptrdiff_t>(o + n));
		return true;
	}
	bool native_bytes(u32 va, std::size_t n)
	{
		std::vector<u8> b;
		if (!exe_bytes(va, n, b)) return false;
		std::memcpy(at(va), b.data(), n);
		return true;
	}
	void jump(u32 va, const void* target)
	{
		auto* p = static_cast<u8*>(at(va));
		const auto rel = static_cast<i32>(addr(target) - (va + 5));
		p[0] = 0xe9; std::memcpy(p + 1, &rel, 4);
	}
	void cell(u32 va, const void* target) { const u32 v = addr(target); std::memcpy(at(va), &v, 4); }

	// ---- the call log ---------------------------------------------------------------------------------------------
	using event = std::array<u32, 12>;
	std::vector<event> g_log;
	void log(std::initializer_list<u32> v) { event e{}; std::size_t i = 0; for (const u32 x : v) if (i < e.size()) e[i++] = x; g_log.push_back(e); }

	// Record pointers are logged as (index << 8 | offset in record) of the object the current run uses.
	struct view { const u8* object; const layout* L; } g_view{};
	u32 rec_id(const void* p)
	{
		const u32 a = addr(p), base = addr(g_view.object) + g_view.L->records, size = g_view.L->capacity * g_view.L->record_size;
		if (a < base || a >= base + size) return 0xffffffffu;
		return ((a - base) / g_view.L->record_size) << 8 | ((a - base) % g_view.L->record_size);
	}

	// A record in either layout, decoded.
	struct canon
	{
		u32 def = 0, inst = 0; u8 payload[8]{}; u8 fa = 0, fb = 0; u16 slot = no_slot; u32 pos[3]{};
		bool same_fields(const canon& o) const { return def == o.def && inst == o.inst && !std::memcmp(payload, o.payload, 8) && fa == o.fa && fb == o.fb && !std::memcmp(pos, o.pos, 12); }
	};
	void encode(const layout& L, u8* r, const canon& c)
	{
		std::memcpy(r, &c.def, 4); std::memcpy(r + 4, &c.inst, 4); std::memcpy(r + 8, c.payload, 8); r[0x10] = c.fa; r[0x11] = c.fb;
		if (L.record_size == 0x18) { for (int k = 0; k < 3; ++k) { const auto v = static_cast<std::int16_t>(from_bits(c.pos[k])); std::memcpy(r + 0x12 + 2 * k, &v, 2); } }
		else { std::memcpy(r + 0x12, &c.slot, 2); std::memcpy(r + 0x14, c.pos, 12); }
	}
	canon decode(const layout& L, const u8* r)
	{
		canon c;
		std::memcpy(&c.def, r, 4); std::memcpy(&c.inst, r + 4, 4); std::memcpy(c.payload, r + 8, 8); c.fa = r[0x10]; c.fb = r[0x11];
		if (L.record_size == 0x18) { for (int k = 0; k < 3; ++k) { std::int16_t v; std::memcpy(&v, r + 0x12 + 2 * k, 2); c.pos[k] = bits(static_cast<float>(v)); } }
		else { std::memcpy(&c.slot, r + 0x12, 2); std::memcpy(c.pos, r + 0x14, 12); }
		return c;
	}

	// ---- stubs for the engine routines the methods call --------------------------------------------------------------
	enum : u32 { ev_dispose = 1, ev_name, ev_spawn, ev_actor, ev_wake, ev_flag, ev_prepare, ev_restore, ev_owner, ev_camera, ev_identity, ev_set, ev_atexit, ev_delete };

	// Spawn decisions: a script first, then a PRNG. 0 = no entity, 1..3 = not an actor, 4..6 = actor with flag 0x40
	// (restored), 7..9 = actor without it (its record is kept).
	std::vector<int> g_script; std::size_t g_script_at = 0; u32 g_policy = 1;
	int decide()
	{
		if (g_script_at < g_script.size()) return g_script[g_script_at++];
		g_policy ^= g_policy << 13; g_policy ^= g_policy >> 17; g_policy ^= g_policy << 5;
		return static_cast<int>(g_policy % 10);
	}
	constexpr u32 entity_id_offset = 0x200, entity_kind_offset = 0x204;
	struct alignas(16) fake_entity { u8 raw[0x400]; };
	fake_entity g_entities[4096]; u32 g_next_entity = 0;
	const void* g_entity_vtable[0x60];
	u32 entity_id(const void* e) { u32 v; std::memcpy(&v, static_cast<const u8*>(e) + entity_id_offset, 4); return v; }

	void __cdecl stub_dispose(void* record) { log({ev_dispose, rec_id(record)}); }
	const char* __fastcall stub_name(u32* handle, void*) { log({ev_name, rec_id(handle), *handle}); return static_cast<const char*>(at(0x50000000u | (*handle & 0xffffffu))); }
	void* __cdecl stub_spawn(const char* def, const char* inst, const float* pos, const float* orient, u32 flag)
	{
		log({ev_spawn, addr(def), addr(inst), bits(pos[0]), bits(pos[1]), bits(pos[2]), bits(orient[0]), bits(orient[1]), bits(orient[2]), flag});
		const int d = decide();
		if (d == 0) return nullptr;
		auto* e = g_entities[g_next_entity % 4096].raw;
		std::memset(e, 0, sizeof(fake_entity));
		const void* vt = g_entity_vtable; std::memcpy(e, &vt, 4);
		const u32 id = ++g_next_entity, kind = d <= 3 ? 0u : d <= 6 ? 1u : 2u;
		std::memcpy(e + entity_id_offset, &id, 4); std::memcpy(e + entity_kind_offset, &kind, 4);
		e[actor_flags_offset] = kind == 1 ? static_cast<u8>(actor_flag_keep | 0x81) : 0x81;
		return e;
	}
	u8* __cdecl stub_as_actor(u8* e) { u32 kind; std::memcpy(&kind, e + entity_kind_offset, 4); log({ev_actor, entity_id(e)}); return kind ? e : nullptr; }
	void __fastcall stub_wake(u8* a, void*, u32 x, u32 y, u32 z) { log({ev_wake, entity_id(a), x, y, z}); }
	void __fastcall stub_flag(u8* sub, void*, u32 a, u32 b) { log({ev_flag, entity_id(sub - 4), a, b}); }
	void __fastcall stub_prepare(u8* e, void*) { log({ev_prepare, entity_id(e)}); }
	void __fastcall stub_restore(u8* e, void*, u8* record)
	{
		const canon c = decode(*g_view.L, record);
		u32 p0, p1; std::memcpy(&p0, c.payload, 4); std::memcpy(&p1, c.payload + 4, 4);
		log({ev_restore, entity_id(e), rec_id(record), c.def, c.inst, p0, p1, u32(c.fa) | u32(c.fb) << 8, c.pos[0], c.pos[1], c.pos[2]});
	}
	float g_cam_pos[3], g_cam_row2[3];
	const void* g_owner_vtable[0x40];
	struct { const void* vt; } g_owner{g_owner_vtable};
	void* __cdecl stub_camera_owner() { log({ev_owner}); return &g_owner; }
	void __fastcall stub_camera(void*, void*, float* m) { log({ev_camera}); std::memcpy(m + 8, g_cam_row2, 12); std::memcpy(m + 12, g_cam_pos, 12); }
	void __fastcall stub_identity(float* m, void*) { log({ev_identity}); for (int i = 0; i < 16; ++i) m[i] = (i % 5) ? 0.0f : 1.0f; }
	float* __fastcall stub_assign(float* d, void*, const float* s) { std::memcpy(d, s, 12); return d; }
	void __fastcall stub_set(float* d, void*, float x, float y, float z) { log({ev_set, bits(x), bits(y), bits(z)}); d[0] = x; d[1] = y; d[2] = z; }
	float* __fastcall stub_vec_ctor(float* d, void*) { return d; }
	float* __fastcall stub_vec_index(float* d, void*, int i) { return d + i; }
	float* __fastcall stub_vec_copy(float* d, void*, const float* s) { std::memcpy(d, s, 12); return d; }
	float* __fastcall stub_orientation(u8* r, void*, float* out) { for (int k = 0; k < 3; ++k) out[k] = static_cast<float>(r[8 + (k + 1) % 3]) * 2.0f * 0.0174532924f; return out; }
	int __cdecl stub_atexit(void* fn) { log({ev_atexit, addr(fn)}); return 0; }
	void __cdecl stub_delete(void* p) { log({ev_delete, addr(p)}); }

	// ---- driving an object through its vtable ------------------------------------------------------------------------
	struct mgr { u8* obj; const layout* L; };
	void* const* vt(const mgr& m) { void* const* v; std::memcpy(&v, m.obj, 4); return v; }
	template <typename Fn> Fn slot(const mgr& m, u32 off) { return reinterpret_cast<Fn>(vt(m)[off / 4]); }
	void* m_destroy(const mgr& m, u32 flags) { return slot<void*(__fastcall*)(void*, void*, u32)>(m, 0x0)(m.obj, nullptr, flags); }
	void m_update(const mgr& m, u32 all) { slot<void(__fastcall*)(void*, void*, u32)>(m, 0x4)(m.obj, nullptr, all); }
	u8* m_allocate(const mgr& m) { return static_cast<u8*>(slot<void*(__fastcall*)(void*, void*)>(m, 0x8)(m.obj, nullptr)); }
	void m_reset(const mgr& m) { slot<void(__fastcall*)(void*, void*)>(m, 0xc)(m.obj, nullptr); }
	void m_clear(const mgr& m) { slot<void(__fastcall*)(void*, void*)>(m, 0x10)(m.obj, nullptr); }
	i32 m_count(const mgr& m) { return slot<i32(__fastcall*)(void*, void*)>(m, 0x14)(m.obj, nullptr); }
	u8* m_at(const mgr& m, i32 i) { return static_cast<u8*>(slot<void*(__fastcall*)(void*, void*, i32)>(m, 0x18)(m.obj, nullptr, i)); }
	void m_append(const mgr& m, const void* src) { slot<void(__fastcall*)(void*, void*, const void*)>(m, 0x1c)(m.obj, nullptr, src); }

	i32 count_of(const mgr& m) { i32 v; std::memcpy(&v, m.obj + m.L->count, 4); return v; }
	i32 cursor_of(const mgr& m) { i32 v; std::memcpy(&v, m.obj + cursor_offset, 4); return v; }
	const u32* bitmap_of(const mgr& m) { return reinterpret_cast<const u32*>(m.obj + m.L->bitmap); }
	u8* record(const mgr& m, i32 i) { return m.obj + m.L->records + static_cast<u32>(i) * m.L->record_size; }
	u32 ptr_id(const mgr& m, const void* p) { if (!p) return 0xfffffffeu; view saved = g_view; g_view = {m.obj, m.L}; const u32 id = rec_id(p); g_view = saved; return id; }

	// ---- the differential --------------------------------------------------------------------------------------------
	// Records above the high-water mark are unobservable; both objects start a comparison from the same zero storage.
	void wipe(const mgr& m) { std::memset(m.obj + m.L->records, 0, m.L->object_size - m.L->records); std::memset(m.obj + cursor_offset, 0, 4); }
	u32 g_rng = 0x12345678;
	u32 rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
	canon random_record()
	{
		canon c;
		c.def = rnd() | 1; c.inst = rnd();
		for (auto& b : c.payload) b = static_cast<u8>(rnd());
		c.fa = static_cast<u8>(rnd() & 0x83); c.fb = static_cast<u8>(rnd() & 0x41);
		for (auto& p : c.pos) p = bits(static_cast<float>(static_cast<i32>(rnd() % 6001) - 3000));
		return c;
	}
	struct op { int kind; u32 arg; canon rec; u32 policy; u8 wake; float cam_pos[3], cam_row2[3]; };
	struct outcome { u32 ret; std::vector<event> events; };
	outcome apply(const mgr& m, const op& o)
	{
		g_view = {m.obj, m.L}; g_log.clear(); g_script.clear(); g_script_at = 0; g_policy = o.policy; g_next_entity = 0;
		*static_cast<u8*>(at(wake_actors_flag)) = o.wake;
		std::memcpy(g_cam_pos, o.cam_pos, 12); std::memcpy(g_cam_row2, o.cam_row2, 12);
		u32 ret = 0;
		switch (o.kind)
		{
		case 0: { u8* r = m_allocate(m); ret = ptr_id(m, r); if (r) encode(*m.L, r, o.rec); break; }
		case 1: { u8 src[0x20]; std::memset(src, 0xcc, sizeof src); canon c = o.rec; c.slot = no_slot; encode(*m.L, src, c); m_append(m, src); break; }
		case 2: m_update(m, o.arg); break;
		case 3: m_clear(m); break;
		case 4: m_reset(m); break;
		case 5: ret = static_cast<u32>(m_count(m)); break;
		case 6: ret = ptr_id(m, m_at(m, static_cast<i32>(o.arg))); break;
		case 7: ret = m_destroy(m, 0) == m.obj ? 1u : 0u; break;
		}
		return {ret, g_log};
	}
	std::string compare_state(const mgr& a, const mgr& b)
	{
		if (count_of(a) != count_of(b)) return "count " + std::to_string(count_of(a)) + " vs " + std::to_string(count_of(b));
		if (cursor_of(a) != cursor_of(b)) return "cursor " + std::to_string(cursor_of(a)) + " vs " + std::to_string(cursor_of(b));
		for (i32 i = 0; i < 768; ++i)
			if (test_bit(bitmap_of(a), i) != test_bit(bitmap_of(b), i)) return "bitmap bit " + std::to_string(i);
		for (i32 i = 768; i < static_cast<i32>(b.L->capacity); ++i) if (test_bit(bitmap_of(b), i)) return "W2 bitmap bit " + std::to_string(i) + " set";
		for (i32 i = 0; i < 768; ++i)
		{
			const canon x = decode(*a.L, record(a, i)), y = decode(*b.L, record(b, i));
			if (!x.same_fields(y)) return "record " + std::to_string(i) + " fields";
			if (i < count_of(b) && y.slot != no_slot) return "W2 record " + std::to_string(i) + " slot " + hex(y.slot) + " (expected 0xffff)";
		}
		return {};
	}

	void differential(const mgr& retail_mgr, const mgr& w2_mgr, unsigned precision, const char* precision_name, u32 seed, int steps)
	{
		unsigned old_cw = 0, new_cw = 0;
		_controlfp_s(&old_cw, 0, 0);
		_controlfp_s(&new_cw, precision, _MCW_PC);
		g_rng = seed;
		wipe(retail_mgr); wipe(w2_mgr);
		int mismatches = 0, refusals = 0; std::string first;
		std::size_t events = 0, spawns = 0, restores = 0, disposals = 0, removes_seen = 0;
		int max_count = 0;
		for (int s = 0; s < steps && mismatches == 0; ++s)
		{
			op o{};
			const u32 r = rnd() % 100;
			const i32 count = count_of(retail_mgr);
			// alternating 1000-step phases: filling (the pool reaches 768 and refuses), then a mixed workload
			if ((s % 2000) < 1000) o.kind = r < 65 ? 0 : r < 98 ? 1 : r < 99 ? 2 : 5;
			else o.kind = r < 25 ? 0 : r < 40 ? 1 : r < 60 ? 2 : r < 66 ? 3 : r < 67 ? 4 : r < 80 ? 5 : r < 97 ? 6 : 2;
			if ((o.kind == 0 || o.kind == 1) && count >= 768) o.kind = o.kind == 0 ? 0 : 5; // allocate at 768: the refusal itself is compared below
			if (o.kind == 0 && count >= 768) { ++refusals; }
			o.rec = random_record();
			o.arg = o.kind == 2 ? (rnd() % 6 == 0 ? 1u : 0u) : o.kind == 6 ? (count ? rnd() % static_cast<u32>(count) : 0u) : 0u;
			o.policy = rnd() | 1; o.wake = static_cast<u8>(rnd() % 3 == 0);
			for (int k = 0; k < 3; ++k) { o.cam_pos[k] = static_cast<float>(static_cast<i32>(rnd() % 4001) - 2000) + static_cast<float>(rnd() % 1000) / 1000.0f; o.cam_row2[k] = static_cast<float>(static_cast<i32>(rnd() % 20001) - 10000) / 10007.0f; }
			if (o.kind == 0 && count >= 768)
			{
				// retail refuses its 769th record; the W2 pool would accept it, so only retail runs this step
				const outcome x = apply(retail_mgr, o);
				if (x.ret != 0xfffffffeu || !x.events.empty() || count_of(retail_mgr) != 768) { ++mismatches; first = "retail 769th allocate not refused"; }
				continue;
			}
			const outcome x = apply(retail_mgr, o);
			const outcome y = apply(w2_mgr, o);
			events += x.events.size();
			for (const auto& e : x.events) { spawns += e[0] == ev_spawn; restores += e[0] == ev_restore; disposals += e[0] == ev_dispose; }
			removes_seen += o.kind == 2 && count_of(retail_mgr) < count;
			if (count_of(retail_mgr) > max_count) max_count = count_of(retail_mgr);
			if (x.ret != y.ret) { ++mismatches; first = "step " + std::to_string(s) + " op " + std::to_string(o.kind) + ": return " + hex(x.ret) + " vs " + hex(y.ret); break; }
			if (x.events != y.events)
			{
				++mismatches;
				std::size_t i = 0; while (i < x.events.size() && i < y.events.size() && x.events[i] == y.events[i]) ++i;
				first = "step " + std::to_string(s) + " op " + std::to_string(o.kind) + ": calls differ at #" + std::to_string(i) + " (" + std::to_string(x.events.size()) + " vs " + std::to_string(y.events.size()) + ")";
				if (i < x.events.size()) first += " retail kind " + std::to_string(x.events[i][0]);
				if (i < y.events.size()) first += " W2 kind " + std::to_string(y.events[i][0]);
				break;
			}
			const std::string d = compare_state(retail_mgr, w2_mgr);
			if (!d.empty()) { ++mismatches; first = "step " + std::to_string(s) + " op " + std::to_string(o.kind) + ": " + d; }
		}
		_controlfp_s(&new_cw, old_cw, _MCW_PC);
		check(mismatches == 0, std::string("differential (") + precision_name + ", seed " + hex(seed) + "): " + std::to_string(steps) + " steps over the 8 methods, retail at 768 vs W2 at 2048: identical returns, calls, records, bitmap, count, cursor" +
		      (first.empty() ? "" : " - FIRST MISMATCH " + first));
		check(max_count == 768 && refusals > 0 && spawns > 100 && restores > 50 && disposals > 10 && removes_seen > 10,
		      std::string("differential coverage (") + precision_name + "): peak " + std::to_string(max_count) + " records, " + std::to_string(refusals) + " refused 769th allocations, " + std::to_string(spawns) + " spawns, " +
		      std::to_string(restores) + " restores, " + std::to_string(disposals) + " disposals, " + std::to_string(removes_seen) + " updates that removed records, " + std::to_string(events) + " calls");
	}

	// ---- W2-only cases -------------------------------------------------------------------------------------------------
	u32 hash_bytes(const u8* p, std::size_t n) { u32 h = 2166136261u; for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; } return h; }

	void w2_cases(const mgr& m)
	{
		g_view = {m.obj, m.L}; g_script.clear(); g_script_at = 0; g_log.clear();
		*static_cast<u8*>(at(wake_actors_flag)) = 0;
		m_reset(m);
		check(m_count(m) == 0 && cursor_of(m) == 0, "W2 reset: count 0, cursor 0");

		// 2047 / 2048 allocations, the 2049th refused like retail's 769th
		bool sequential = true;
		for (i32 i = 0; i < 2048; ++i) { u8* r = m_allocate(m); sequential = sequential && r == record(m, i) && m_count(m) == i + 1; }
		const canon c2046 = decode(*m.L, record(m, 2046)), c2047 = decode(*m.L, record(m, 2047));
		auto constructed = [](const canon& c) { return c.def == 0 && c.inst == 0 && c.payload[0] == 0xff && c.payload[1] == 0 && c.payload[7] == 0 && c.fa == 0 && c.fb == 0 && c.slot == no_slot && c.pos[0] == 0 && c.pos[1] == 0 && c.pos[2] == 0; };
		bool all_bits = true; for (u32 k = 0; k < 64; ++k) all_bits = all_bits && bitmap_of(m)[k] == 0xffffffffu;
		check(sequential && constructed(c2046) && constructed(c2047) && all_bits, "W2 allocate: records 1..2048 returned in order (2047th and 2048th constructed: payload[0] 0xff, slot 0xffff, floats 0.0), all 64 bitmap dwords set");
		const u32 before = hash_bytes(m.obj, m.L->object_size); const auto refused0 = smart_entities::stats().allocate_full;
		g_log.clear();
		u8* refused = m_allocate(m);
		check(refused == nullptr && m_count(m) == 2048 && g_log.empty() && hash_bytes(m.obj, m.L->object_size) == before && smart_entities::stats().allocate_full == refused0 + 1,
		      "W2 allocate: the 2049th returns 0 with the object unchanged and no call (retail's refusal at 769, counted)");
		const auto append0 = smart_entities::stats().append_full;
		u8 src[0x20]; std::memset(src, 0x5a, sizeof src);
		m_append(m, src);
		check(m_count(m) == 2048 && hash_bytes(m.obj, m.L->object_size) == before && smart_entities::stats().append_full == append0 + 1,
		      "W2 append-copy at 2048: refused, object unchanged (retail has no bound and would overwrite its bitmap at 769)");
		check(m_at(m, 2047) == record(m, 2047) && m_at(m, 0) == m.obj + 8 && m_at(m, 768) == m.obj + 8 + 768 * 0x20, "W2 indexed access: 0, 768 and 2047 at object+8+i*0x20");

		// 2048 appended records with distinct float positions and slots, bit-exact
		m_reset(m);
		std::vector<canon> want(2048);
		for (i32 i = 0; i < 2048; ++i)
		{
			canon c; c.def = 0x1000u + static_cast<u32>(i); c.inst = 0x9000u ^ static_cast<u32>(i);
			for (int k = 0; k < 8; ++k) c.payload[k] = static_cast<u8>(i * 7 + k);
			c.fa = static_cast<u8>(i & 1); c.fb = static_cast<u8>((i % 3 == 0) ? 1 : 0); c.slot = static_cast<u16>(i & 1 ? i % 1024 : no_slot);
			c.pos[0] = bits(static_cast<float>(i) * 0.5f + 0.25f); c.pos[1] = bits(-40000.0f + static_cast<float>(i) / 3.0f); c.pos[2] = i == 7 ? 0x7fc00123u : i == 8 ? 0x80000000u : i == 9 ? 0x00000001u : bits(1.0e-30f * static_cast<float>(i));
			want[static_cast<std::size_t>(i)] = c;
			u8 buf[0x20]; encode(*m.L, buf, c); m_append(m, buf);
		}
		bool exact = m_count(m) == 2048;
		for (i32 i = 0; i < 2048 && exact; ++i) { const canon got = decode(*m.L, record(m, i)); exact = got.same_fields(want[static_cast<std::size_t>(i)]) && got.slot == want[static_cast<std::size_t>(i)].slot; }
		check(exact, "W2 append-copy: 2048 records carry handles, payload, flags, the slot field and float positions bit-exact (0.5 steps, -40000 + i/3, NaN 0x7fc00123, -0.0, the smallest denormal)");

		// clear records: disposal exactly for flags B bit 0, then slot 0xffff, floats 0.0, payload bytes 3..7 kept
		g_log.clear();
		m_clear(m);
		std::vector<u32> disposed; for (const auto& e : g_log) if (e[0] == ev_dispose) disposed.push_back(e[1] >> 8);
		bool dispose_ok = true; std::size_t n = 0;
		for (u32 i = 0; i < 2048; ++i) if (i % 3 == 0) dispose_ok = dispose_ok && n < disposed.size() && disposed[n++] == i;
		dispose_ok = dispose_ok && n == disposed.size() && g_log.size() == disposed.size();
		bool cleared = m_count(m) == 2048;
		for (i32 i = 0; i < 2048 && cleared; ++i)
		{
			const canon got = decode(*m.L, record(m, i)); const canon& w = want[static_cast<std::size_t>(i)];
			cleared = got.def == 0 && got.inst == 0 && got.payload[0] == 0xff && got.payload[1] == 0 && got.payload[2] == 0 && !std::memcmp(got.payload + 3, w.payload + 3, 5) && got.fa == 0 && got.fb == 0 && got.slot == no_slot && got.pos[0] == 0 && got.pos[1] == 0 && got.pos[2] == 0;
		}
		check(dispose_ok && cleared, "W2 clear records at 2048: disposal called for exactly the " + std::to_string(disposed.size()) + " records with flags B bit 0, in order; every record cleared (slot 0xffff, floats 0.0, payload 3..7 kept, count kept)");

		// update: removal moves the last record into the hole with its slot and floats intact
		m_reset(m);
		canon a = want[1], b = want[3], c = want[5];
		for (auto* r : {&a, &b, &c}) { r->pos[0] = bits(10.5f); r->pos[1] = bits(-20.25f); }
		c.pos[2] = bits(-40000.0f); c.slot = 1023; c.fa = 1; b.slot = 300; a.slot = 255;
		for (auto* r : {&a, &b, &c}) { u8 buf[0x20]; encode(*m.L, buf, *r); m_append(m, buf); }
		g_cam_pos[0] = 0; g_cam_pos[1] = 0; g_cam_pos[2] = 0; g_cam_row2[0] = 0; g_cam_row2[1] = 0; g_cam_row2[2] = 1;
		g_script = {1, 8, 8}; g_script_at = 0; g_next_entity = 0; g_log.clear();
		m_update(m, 1);
		std::vector<event> spawn_events; for (const auto& e : g_log) if (e[0] == ev_spawn) spawn_events.push_back(e);
		const canon at0 = decode(*m.L, record(m, 0)), at1 = decode(*m.L, record(m, 1)), at2 = decode(*m.L, record(m, 2));
		check(m_count(m) == 2 && at0.same_fields(c) && at0.slot == 1023 && at1.same_fields(b) && at1.slot == 300 && at2.slot == no_slot && at2.def == 0 && at2.pos[2] == 0 && !test_bit(bitmap_of(m), 2) && test_bit(bitmap_of(m), 0),
		      "W2 update: the restored record 0 is replaced by the last one (slot 1023, z -40000.0 bit-exact), the vacated record cleared (slot 0xffff) and its bit cleared");
		check(spawn_events.size() == 3 && spawn_events[0][3] == bits(10.5f) && spawn_events[0][4] == bits(-20.25f) && spawn_events[0][5] == a.pos[2] && spawn_events[1][5] == bits(-40000.0f) && spawn_events[2][5] == b.pos[2],
		      "W2 update: the spawn receives the record's float position bit-exact (x 10.5, y -20.25, z as stored)");

		// update round robin beyond 768: 2048 records outside the box, 40 per call, the cursor wraps at the count
		m_reset(m);
		for (i32 i = 0; i < 2048; ++i) { canon r = want[static_cast<std::size_t>(i)]; r.pos[0] = bits(30000.0f); r.pos[1] = bits(30000.0f); u8 buf[0x20]; encode(*m.L, buf, r); m_append(m, buf); }
		g_cam_pos[0] = g_cam_pos[1] = g_cam_pos[2] = 0; g_log.clear();
		bool cursor_ok = true;
		for (int call = 1; call <= 52; ++call) { m_update(m, 0); cursor_ok = cursor_ok && cursor_of(m) == call * 40; }
		m_update(m, 0); cursor_ok = cursor_ok && cursor_of(m) == 40; // 2080 >= 2048: restarts at 0
		bool no_spawn = true; for (const auto& e : g_log) no_spawn = no_spawn && e[0] != ev_spawn;
		check(cursor_ok && no_spawn && m_count(m) == 2048, "W2 update: the incremental cursor runs 40 .. 2080 over 2048 records, then restarts at 0; nothing outside the box is spawned");

		// update all at 2048 with every record in the box: all 2048 restored and removed, the pool ends empty
		m_reset(m);
		for (i32 i = 0; i < 2048; ++i) { canon r = want[static_cast<std::size_t>(i)]; r.pos[0] = bits(1.0f); r.pos[1] = bits(2.0f); u8 buf[0x20]; encode(*m.L, buf, r); m_append(m, buf); }
		g_script.assign(2048, 2); g_script_at = 0; g_next_entity = 0; g_log.clear();
		m_update(m, 1);
		std::size_t restored = 0; bool order = true; u32 expect_def = 0x1000;
		for (const auto& e : g_log) if (e[0] == ev_restore) { ++restored; order = order && e[2] == 0 && e[3] == expect_def; expect_def = restored == 1 ? 0x1000u + 2047u : expect_def - 1; }
		bool empty_bits = true; for (u32 k = 0; k < 64; ++k) empty_bits = empty_bits && bitmap_of(m)[k] == 0;
		check(restored == 2048 && order && m_count(m) == 0 && empty_bits, "W2 update(all) at 2048: 2048 restores, each at record 0 (the first, then the last moved in: 2047, 2046, ...), the pool ends empty with a clear bitmap");

		// the destructor
		m_reset(m);
		for (i32 i = 0; i < 1500; ++i) { u8* r = m_allocate(m); encode(*m.L, r, want[static_cast<std::size_t>(i)]); }
		const void* own = vt(m);
		g_log.clear();
		void* self = m_destroy(m, 1);
		u32 vtable_now; std::memcpy(&vtable_now, m.obj, 4);
		bool zeroed = true; for (u32 k = 0; k < 64; ++k) zeroed = zeroed && bitmap_of(m)[k] == 0;
		const canon d0 = decode(*m.L, record(m, 1499));
		check(self == m.obj && vtable_now == base_vtable && zeroed && count_of(m) == 1500 && d0.def == 0 && d0.slot == no_slot && g_log.empty(),
		      "W2 destructor: 1500 records cleared without disposal, bitmap zeroed, count kept, base vtable 0x68e41c installed, the static object never deleted");
		std::memcpy(m.obj, &own, 4);
		m_reset(m);
	}
}

int main(int argc, char** argv)
{
	std::setbuf(stdout, nullptr);
	const auto b = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || b > 0x402b60 || b + sizeof fixture_space < 0x76bc00)
	{
		std::puts("FAIL fixture must cover 0x402b60..0x76bc00 at image base 0x400000");
		return 2;
	}
	DWORD old = 0;
	VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old);

	const char* path = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	if (path) { std::ifstream file(path, std::ios::binary); binary.assign(std::istreambuf_iterator<char>(file), {}); }
	bool have_retail = path && !binary.empty();
	if (have_retail)
	{
		have_retail = native_bytes(region_lo, region_hi - region_lo) && native_bytes(0x465e70, 0x465eb3 - 0x465e70) && native_bytes(0x487610, 0x48763d - 0x487610) &&
		         native_bytes(0x4654f0, 0x465568 - 0x4654f0) && native_bytes(0x465a00, 0x465aa8 - 0x465a00) && native_bytes(0x68117c, 4) && native_bytes(0x680198, 4) &&
		         native_bytes(base_vtable, 0x44) && native_bytes(0x68e460, 12) && native_bytes(exit_handler, 10);
		for (const auto& k : getter_callers) have_retail = have_retail && native_bytes(k.va, 5);
	}

	// stubs at the native addresses and import cells
	jump(native_dispose, reinterpret_cast<const void*>(&stub_dispose));
	jump(native_handle_name, reinterpret_cast<const void*>(&stub_name));
	jump(native_spawn, reinterpret_cast<const void*>(&stub_spawn));
	jump(native_as_actor, reinterpret_cast<const void*>(&stub_as_actor));
	jump(native_entity_flag, reinterpret_cast<const void*>(&stub_flag));
	jump(native_entity_prepare, reinterpret_cast<const void*>(&stub_prepare));
	jump(native_camera_owner, reinterpret_cast<const void*>(&stub_camera_owner));
	jump(0x67211e, reinterpret_cast<const void*>(&stub_atexit));
	jump(0x6721f2, reinterpret_cast<const void*>(&stub_delete));
	if (!have_retail) jump(native_orientation, reinterpret_cast<const void*>(&stub_orientation));
	cell(iat_vec3_set, reinterpret_cast<const void*>(&stub_set));
	cell(iat_matrix_identity, reinterpret_cast<const void*>(&stub_identity));
	cell(iat_vec3_assign, reinterpret_cast<const void*>(&stub_assign));
	cell(0x67fa60, reinterpret_cast<const void*>(&stub_vec_ctor));
	cell(0x67f9c4, reinterpret_cast<const void*>(&stub_vec_index));
	cell(0x67f9e0, reinterpret_cast<const void*>(&stub_vec_copy));
	g_entity_vtable[entity_restore_slot / 4] = reinterpret_cast<const void*>(&stub_restore);
	g_entity_vtable[actor_wake_slot / 4] = reinterpret_cast<const void*>(&stub_wake);
	g_owner_vtable[camera_matrix_slot / 4] = reinterpret_cast<const void*>(&stub_camera);
	FlushInstructionCache(GetCurrentProcess(), fixture_space, sizeof fixture_space);

	// the DLL object
	auto* w2_object = static_cast<u8*>(smart_entities::getter());
	const mgr w2_mgr{w2_object, &w2};
	u32 vt_now; std::memcpy(&vt_now, w2_object, 4);
	check(w2_object == smart_entities::object() && vt_now == addr(smart_entities::vtable()) && count_of(w2_mgr) == 0 && cursor_of(w2_mgr) == 0 && smart_entities::getter() == w2_object,
	      "W2 getter: the DLL object (vtable installed once, count 0, cursor 0), the same pointer on every call");
	w2_cases(w2_mgr);

	if (!have_retail)
	{
		patch_transaction::contribution c; std::string why;
		const bool built = smart_entities::contribute_w2(c, why);
		check(!built && c.writes.empty() && !why.empty(), "contribution refused without the retail bytes in memory (" + why + ")");
		std::printf("SKIP retail: no executable given (argv[1] or XML2_FIX_TEST_EXE)\nSmart entities: %d checks, %d failure(s)\n", checks, failures);
		return failures ? 1 : 0;
	}

	// the retail object through the retail getter
	g_log.clear();
	auto* retail_object = static_cast<u8*>(reinterpret_cast<void*(__cdecl*)()>(at(getter_va))());
	const mgr retail_mgr{retail_object, &smart_entities_rules::retail};
	u32 retail_vt; std::memcpy(&retail_vt, retail_object, 4);
	check(retail_object == at(static_object) && retail_vt == native_vtable && g_log.size() == 1 && g_log[0][0] == ev_atexit && g_log[0][1] == exit_handler && (*static_cast<u8*>(at(init_flag)) & 1),
	      "retail getter: static object 0x767390, vtable 0x68e440, exit handler 0x67dd50 registered, init flag set");

	differential(retail_mgr, w2_mgr, _PC_53, "x87 53-bit", 0x2468ace1u, 6000);
	differential(retail_mgr, w2_mgr, _PC_24, "x87 24-bit", 0x13579bdfu, 6000);
	differential(retail_mgr, w2_mgr, _PC_64, "x87 64-bit", 0x0badf00du, 6000);

	// both destructors on the same state
	{
		g_rng = 0x55aa55aau;
		wipe(retail_mgr); wipe(w2_mgr);
		for (int i = 0; i < 700; ++i) { op o{}; o.kind = 0; o.rec = random_record(); apply(retail_mgr, o); apply(w2_mgr, o); }
		op o{}; o.kind = 2; o.arg = 0; o.cam_row2[2] = 1; apply(retail_mgr, o); apply(w2_mgr, o);
		const outcome x = apply(retail_mgr, {7}), y = apply(w2_mgr, {7});
		u32 rv, wv; std::memcpy(&rv, retail_object, 4); std::memcpy(&wv, w2_object, 4);
		const std::string d = compare_state(retail_mgr, w2_mgr);
		check(x.ret == y.ret && x.events == y.events && d.empty() && rv == base_vtable && wv == base_vtable,
		      "differential destructor (vt+0, 700 records): identical calls and state, both end on the base vtable 0x68e41c" + (d.empty() ? std::string() : " - " + d));
		const u32 own_retail = native_vtable, own_w2 = addr(smart_entities::vtable());
		std::memcpy(retail_object, &own_retail, 4); std::memcpy(w2_object, &own_w2, 4);
	}

	// the contribution against the executable
	{
		std::vector<u8> region;
		const bool have = exe_bytes(region_lo, region_hi - region_lo, region);
		check(have && patch_transaction::sha256(region.data(), region.size()) == patch_transaction::sha256_of(region_sha256), "region [0x4b6e70, 0x4b7744) of the executable hashes to 1969cfd9...04a4b");
		patch_transaction::contribution refused; std::string why;
		const bool built = smart_entities::contribute_w2(refused, why);
		check(!built && refused.guards.empty() && why.find("0x76bbfc") != std::string::npos, "contribution refused while the retail object exists (" + why + ")");
		*static_cast<u8*>(at(init_flag)) = 0;
		patch_transaction::contribution c; c.owner = "test";
		const bool ok = smart_entities::contribute_w2(c, why);
		check(ok && c.owner == "test", "contribution built against the retail bytes" + (ok ? std::string() : " - " + why));
		bool guards_match = !c.guards.empty();
		for (const auto& g : c.guards) { std::vector<u8> e; guards_match = guards_match && exe_bytes(g.va, g.bytes.size(), e) && e == g.bytes; }
		check(guards_match && c.guards.size() == 19, "contribution: " + std::to_string(c.guards.size()) + " guards (getter entry, 17 getter calls, exit handler) equal the executable's bytes");
		check(c.regions.size() == 1 && c.regions[0].lo == region_lo && c.regions[0].hi_exclusive == region_hi && c.regions[0].sha256 == patch_transaction::sha256(region.data(), region.size()),
		      "contribution: one region hash, [0x4b6e70, 0x4b7744), equal to the executable's");
		const u32 target = addr(reinterpret_cast<const void*>(&smart_entities::getter));
		const i32 rel = static_cast<i32>(target - (getter_va + 5));
		std::vector<u8> expect{0xe9}; for (int k = 0; k < 4; ++k) expect.push_back(static_cast<u8>(static_cast<u32>(rel) >> (8 * k))); expect.push_back(0x90);
		check(c.writes.size() == 1 && c.writes[0].va == getter_va && c.writes[0].bytes == expect, "contribution: one write, 0x4b76f0 = jmp smart_entities::getter + nop (6 bytes over the guarded getter entry)");
		wipe(w2_mgr);
		patch_transaction::transaction t;
		t.add(c);
		const auto outcome = t.commit();
		void* got = reinterpret_cast<void*(__cdecl*)()>(at(getter_va))();
		check(outcome == patch_transaction::outcome::committed && got == smart_entities::object() && m_count(w2_mgr) == 0,
		      std::string("installed: transaction ") + patch_transaction::name(outcome) + ", 0x4b76f0 now returns the DLL object");
		g_view = {w2_object, &w2}; g_log.clear();
		u8* r = static_cast<u8*>(reinterpret_cast<void*(__fastcall*)(void*, void*)>(static_cast<void* const*>(vt(w2_mgr))[2])(got, nullptr));
		check(r == record(w2_mgr, 0) && g_log.empty(), "installed: allocate through the object the native getter returns uses the W2 records");
	}

	std::printf("Smart entities: %d checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
