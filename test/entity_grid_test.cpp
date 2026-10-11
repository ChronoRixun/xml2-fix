// CEntityGrid replacement owner: synthetic checks at 256 and 1024, and a differential run of the retail grid code
// (copied from an executable given on the command line into a fixture at its own addresses) against the port at
// 256, comparing raw object memory and every result after every scripted step. No game, no game content.
#include "entity_grid.hpp"
#include <Windows.h>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace entity_grid;
using entity_capacity_layout::stock_descriptor;
using entity_capacity_layout::raised_descriptor_for;
using entity_capacity_layout::stock_world;
using entity_capacity_layout::raised_world;

#pragma section(".peerfix", read)
__declspec(allocate(".peerfix")) std::uint8_t fixture_space[0x400000]{};

int failures = 0, checks = 0;
void check(bool value, const char* message) { ++checks; std::printf("%s %s\n", value ? "PASS" : "FAIL", message); failures += !value; }
void checkf(bool value, const char* format, ...) { char text[512]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof text, format, args); va_end(args); check(value, text); }

// ---- native ABI plumbing: every slot is called through a __fastcall pointer (ECX = this, EDX unused, stack args,
// callee cleanup), which is the native __thiscall frame. The same typedefs drive the retail vtable and the DLL one.
using fn_void = void(__fastcall*)(void*, void*);
using fn_init = void(__fastcall*)(void*, void*, const vec3*, const vec3*, const vec3*);
using fn_vecp = const vec3*(__fastcall*)(void*, void*);
using fn_snap = vec3*(__fastcall*)(void*, void*, vec3*, const vec3*);
using fn_center = vec3*(__fastcall*)(void*, void*, vec3*, int, int);
using fn_int = int(__fastcall*)(void*, void*);
using fn_getv = vec3*(__fastcall*)(void*, void*, vec3*);
using fn_token = void(__fastcall*)(void*, void*, std::uint32_t);
using fn_update = void(__fastcall*)(void*, void*, std::uint32_t, const bounds*);
using fn_present = int(__fastcall*)(void*, void*, std::uint32_t);
using fn_query = unsigned char(__fastcall*)(void*, void*, const vec3*, const vec3*, std::uint32_t*);
using fn_setp = std::uint32_t*(__fastcall*)(void*, void*, int);
using fn_cache = void(__fastcall*)(void*, void*, int);
using fn_cbounds = unsigned char(__fastcall*)(void*, void*, int, bounds*);
using fn_cupdate = void(__fastcall*)(void*, void*, int, const bounds*);

struct target
{
	void* self = nullptr;
	const void* const* vt() const { return *static_cast<const void* const* const*>(self); }
	template <typename Fn> Fn fn(int slot) const { return reinterpret_cast<Fn>(vt()[slot / 4]); }
	void reset() { fn<fn_void>(0x00)(self, nullptr); }
	void init(const vec3& a, const vec3& b, const vec3& c) { fn<fn_init>(0x04)(self, nullptr, &a, &b, &c); }
	const vec3* cell_size() { return fn<fn_vecp>(0x08)(self, nullptr); }
	vec3 snap(const vec3& in) { vec3 out{}; fn<fn_snap>(0x0c)(self, nullptr, &out, &in); return out; }
	vec3 center(int x, int y) { vec3 out{}; fn<fn_center>(0x10)(self, nullptr, &out, x, y); return out; }
	int width() { return fn<fn_int>(0x14)(self, nullptr); }
	int height() { return fn<fn_int>(0x18)(self, nullptr); }
	vec3 min() { vec3 out{}; fn<fn_getv>(0x1c)(self, nullptr, &out); return out; }
	vec3 max() { vec3 out{}; fn<fn_getv>(0x20)(self, nullptr, &out); return out; }
	void remove(std::uint32_t token) { fn<fn_token>(0x24)(self, nullptr, token); }
	void update(std::uint32_t token, const bounds* b) { fn<fn_update>(0x28)(self, nullptr, token, b); }
	void update(std::uint32_t token, const bounds& b) { update(token, &b); }
	int present(std::uint32_t token) { return fn<fn_present>(0x2c)(self, nullptr, token); }
	unsigned char query(const vec3& a, const vec3& b, std::uint32_t* set) { return fn<fn_query>(0x30)(self, nullptr, &a, &b, set); }
	std::uint32_t* cache_set(int i) { return fn<fn_setp>(0x34)(self, nullptr, i); }
	void cache_reset(int i) { fn<fn_cache>(0x38)(self, nullptr, i); }
	unsigned char cache_bounds(int i, bounds& out) { return fn<fn_cbounds>(0x3c)(self, nullptr, i, &out); }
	void cache_update(int i, const bounds& b) { fn<fn_cupdate>(0x40)(self, nullptr, i, &b); }
};

// ---- fixture stand-ins for the imports and the one unreachable helper -----------------------------------------
int pair_ctor_calls = 0;
bounds* __fastcall pair_ctor_stub(bounds* self, void*, int zero) { ++pair_ctor_calls; if (zero) std::memset(self, 0, sizeof *self); return self; }
double __cdecl floor_thunk(double v) { return std::floor(v); }
double __cdecl ceil_thunk(double v) { return std::ceil(v); }
void __fastcall vec_set_ref(vec3* self, void*, const vec3* src) { *self = *src; }
void __fastcall vec_set_xyz(vec3* self, void*, float x, float y, float z) { self->x = x; self->y = y; self->z = z; }
vec3* __fastcall vec_assign(vec3* self, void*, const vec3* src) { *self = *src; return self; }

// ---- helpers over the port's layout ----------------------------------------------------------------------------
struct view
{
	std::uint8_t* p; const layout& L;
	view(void* self) : p(static_cast<std::uint8_t*>(self)), L(*layout_of(self)) {}
	std::uint8_t* cell(std::uint32_t a, std::uint32_t b) { const auto id = p[off_coarse + (a >> 2) * coarse_dim + (b >> 2)]; return id == no_block ? nullptr : p + L.pool + id * L.block_stride + ((a & 3) * 4 + (b & 3)) * L.cell_stride; }
	std::uint32_t count(const std::uint8_t* c) const { return L.cell_field == 1 ? c[0] : *reinterpret_cast<const std::uint16_t*>(c); }
	std::uint32_t index(const std::uint8_t* c, std::uint32_t k) const { const auto* e = c + L.cell_indices + k * L.cell_field; return L.cell_field == 1 ? *e : *reinterpret_cast<const std::uint16_t*>(e); }
	std::vector<std::uint32_t> list(std::uint32_t a, std::uint32_t b) { std::vector<std::uint32_t> out; const auto* c = cell(a, b); if (!c) return out; for (std::uint32_t k = 0; k < count(c); ++k) out.push_back(index(c, k)); return out; }
	std::uint8_t* extent(std::uint32_t i) { return p + L.extents + i * 4; }
	std::uint32_t u32(std::uint32_t off) { std::uint32_t v; std::memcpy(&v, p + off, 4); return v; }
	std::uint8_t block_of(std::uint32_t a, std::uint32_t b) { return p[off_coarse + (a >> 2) * coarse_dim + (b >> 2)]; }
};
bool bit(const std::uint32_t* set, std::uint32_t i) { return (set[i >> 5] >> (i & 31)) & 1u; }
bounds box(float x0, float y0, float x1, float y1, float z0 = 0, float z1 = 10) { return bounds{{x0, y0, z0}, {x1, y1, z1}}; }
// cell (a, b) of a grid initialised with min (0,0,0) and a cell size of `c`: a box strictly inside it
bounds cell_box(int a, int b, float c = 120.f) { return box(a * c + c * 0.25f, b * c + c * 0.25f, a * c + c * 0.75f, b * c + c * 0.75f); }
bool same(const std::vector<std::uint32_t>& v, std::initializer_list<std::uint32_t> l) { return v == std::vector<std::uint32_t>(l); }

// ---- synthetic -------------------------------------------------------------------------------------------------
void layout_checks()
{
	const auto s = layout_for(stock_world);
	check(s.valid() && s.pool == 0x204 && s.cell_stride == 0x20 && s.block_stride == 0x200 && s.block_bitmap == 0x10204 && s.ring == 0x10214 && s.ring_enqueue == 0x10418 && s.ring_dequeue == 0x1041c && s.ring_count == 0x10420 && s.alloc_bitmap == 0x10424 && s.alloc_count == 0x10434,
	      "retail layout: block pool, bitmap, ring and counters at the native offsets");
	check(s.extents == 0x10438 && s.caches == 0x1083c && s.cache_stride == 0x3c && s.size == 0x108b4 && s.index_mask == 0xff && s.cell_append_limit == 0x20 && s.set_words == 8,
	      "retail layout: extents 0x10438, caches 0x1083c stride 0x3c, size 0x108b4 (getter flag follows at 0x729888)");
	const auto w = layout_for(raised_world);
	check(w.valid() && w.cell_stride == 2052 && w.block_stride == 32832 && w.pool == 516 && w.block_bitmap == 4203012 && w.ring == 4203028 && w.extents == 4203576 && w.caches == 4207676 && w.cache_stride == 156 && w.size == 4207988,
	      "raised layout matches round6/layouts.json grid (cell 2052, block 32832, bitmap 4203012, ring 4203028, extents 4203576, cache 4207676 x 156, 4207988 bytes)");
	check(w.index_mask == 0x3ff && w.cell_field == 2 && w.cell_indices == 4 && w.cell_append_limit == 1024 && w.set_words == 32, "raised cells hold u16 count, u16 pad, 1024 ordered u16 indices");
	auto d512 = stock_descriptor; d512.world = 512;
	check(!layout_for(512).valid() && object_size(stock_descriptor) == 0x108b4 && object_size(raised_descriptor_for(1, 2, 3, 4)) == 0x403574 && object_size(d512) == 0 && create(d512) == nullptr && layout_of(nullptr) == nullptr,
	      "only 256 and 1024 are constructible; object_size reports the object proper");
	check(vtable.rtti_locator == reinterpret_cast<const void*>(static_cast<std::uintptr_t>(native_rtti_locator)) && vtable.slots[0] == reinterpret_cast<const void*>(&entity_grid::reset) && vtable.slots[16] == reinterpret_cast<const void*>(&entity_grid::cache_update) && vtable.slots[9] == reinterpret_cast<const void*>(&entity_grid::remove) && vtable.slots[10] == reinterpret_cast<const void*>(&entity_grid::update),
	      "vtable has the native slot order with the retail locator prefix");
}

void synthetic(std::uint32_t world)
{
	const auto d = world == stock_world ? stock_descriptor : raised_descriptor_for(0, 0, 0, 0);
	target t{create(d)};
	checkf(t.self != nullptr && layout_of(t.self) && layout_of(t.self)->world == world && *static_cast<const void* const*>(t.self) == vtable.slots, "[%u] create returns a constructed object with the DLL vptr", world);
	if (!t.self) return;
	view v(t.self);
	const auto& L = v.L;
	const auto mask = L.index_mask;
	// constructor state (0x463de0 chain)
	{
		bool coarse = true; for (std::uint32_t i = 0; i < coarse_bytes; ++i) coarse &= v.p[off_coarse + i] == 0xff;
		bool ring = true; for (std::uint32_t i = 0; i < block_count; ++i) ring &= v.u32(L.ring + i * 4) == i;
		bool absent = true; for (std::uint32_t i = 0; i < world; ++i) absent &= v.extent(i)[0] == 0xff && v.extent(i)[1] == 0 && v.extent(i)[2] == 0xff && v.extent(i)[3] == 0;
		bool caches = true; for (std::uint32_t c = 0; c < 2; ++c) { const auto* cp = v.p + L.caches + c * L.cache_stride; caches &= cp[0] == 0xff && cp[1] == 0 && cp[2] == 0xff && cp[3] == 0; for (std::uint32_t k = 0; k < L.set_words; ++k) caches &= *reinterpret_cast<const std::uint32_t*>(cp + cache_off_set + k * 4) == 0; }
		checkf(coarse && ring && v.u32(L.ring_enqueue) == 0 && v.u32(L.ring_dequeue) == 0 && v.u32(L.ring_count) == 128 && v.u32(L.alloc_count) == 0 && v.u32(L.block_bitmap) == 0 && absent && caches && t.width() == 0 && t.height() == 0,
		       "[%u] constructed: coarse map 0xff, ring 0..127 (count 128), no blocks, every entity absent, caches invalid and empty", world);
	}
	// spatial init (0x463f30): first rounding
	t.init({-5, -5, 0}, {1000, 1000, 100}, {120, 120, 100});
	{
		const auto mn = t.min(), mx = t.max(); const auto* c = t.cell_size();
		checkf(mn.x == -120.f && mn.y == -120.f && mn.z == 0.f && mx.x == 1080.f && mx.y == 1080.f && mx.z == 200.f && t.width() == 10 && t.height() == 10 && c->x == 120.f && c == reinterpret_cast<const vec3*>(v.p + off_cell),
		       "[%u] init rounds min down and max (plus half a cell) up to cell multiples: (-120,-120,0)..(1080,1080,200), 10 x 10", world);
		const auto s = t.snap({130.f, 250.f, 5.f});
		const auto ctr = t.center(1, 2);
		checkf(s.x == 120.f && s.y == 240.f && s.z == 0.f && ctr.x == 60.f && ctr.y == 180.f && ctr.z == 0.f, "[%u] snap rounds to the nearest cell origin; cell_center is origin + cell + half at min z", world);
	}
	// second rounding path (> 84 cells after the first), then the raw third path
	t.init({0, 0, 0}, {84.3f, 84.3f, 1}, {1, 1, 1});
	checkf(t.width() == 84 && t.height() == 84 && t.min().x == 0.f && t.max().x == 84.f, "[%u] init falls back to ceil(min)/floor(max)+half rounding when the first pass exceeds 84 cells", world);
	t.init({0, 0, 0}, {85.6f, 85.6f, 1}, {1, 1, 1});
	checkf(t.width() == 86 && t.height() == 86 && t.min().x == 0.f && t.max().x == 85.6f, "[%u] init keeps the raw bounds and ceil'd counts when both roundings exceed 84 cells", world);
	// reset through init: a fresh 10 x 10 grid with min (0,0,0)
	t.init({0, 0, 0}, {1100, 1100, 100}, {120, 120, 100});
	checkf(t.width() == 10 && t.height() == 10 && t.min().x == 0.f, "[%u] 10 x 10 grid at the origin for the cell tests", world);
	// ordered insert into one cell (2,3) and swap-remove
	const std::uint32_t gen = 3u << (world == stock_world ? 8 : 10); // a generation in the token: the index is the low bits
	t.update(gen | 3, cell_box(2, 3)); t.update(gen | 1, cell_box(2, 3)); t.update(2, cell_box(2, 3));
	checkf(same(v.list(2, 3), {3, 1, 2}) && v.block_of(2, 3) == 0 && v.u32(L.alloc_count) == 1 && v.u32(L.ring_count) == 127 && v.u32(L.ring_dequeue) == 1 && v.u32(L.alloc_bitmap) == 1 && v.u32(L.block_bitmap) == 1,
	       "[%u] inserts append in call order into cell (2,3); the first block comes from the ring head (block 0)", world);
	checkf(t.present(gen | 3) == 1 && t.present(3) == 1 && t.present(7) == 0 && v.extent(3)[0] == 2 && v.extent(3)[1] == 2 && v.extent(3)[2] == 3 && v.extent(3)[3] == 3, "[%u] present() reads the extents of the token's index; extents are {x0,x1,y0,y1}", world);
	t.remove(3);
	checkf(same(v.list(2, 3), {2, 1}) && t.present(3) == 0, "[%u] remove swaps the last entry into the hole (native order), marks the entity absent", world);
	t.remove(3); t.remove(7);
	checkf(same(v.list(2, 3), {2, 1}), "[%u] removing an absent entity changes nothing", world);
	t.update(1, nullptr);
	checkf(same(v.list(2, 3), {2}) && t.present(1) == 0, "[%u] update with a null box removes", world);
	t.update(2, box(-5000, -5000, -4000, -4000));
	checkf(same(v.list(2, 3), {}) && t.present(2) == 0 && v.count(v.cell(2, 3)) == 0, "[%u] update with a box off the grid removes", world);
	// boundaries of the index domain
	{
		const std::uint32_t tokens[] = {0, 255, 256, 257, 1023, 1024, 0x12345678u};
		for (auto tk : tokens) t.update(tk, cell_box(5, 5));
		const auto l = v.list(5, 5);
		std::vector<std::uint32_t> expect; for (auto tk : tokens) { bool dup = false; for (auto e : expect) dup |= e == (tk & mask); if (!dup) expect.push_back(tk & mask); } // an index already present with the same rectangle returns early
		checkf(l == expect, "[%u] tokens 0,255,256,257,1023,1024,0x12345678 index as token & 0x%x: %s", world, mask, world == stock_world ? "256 and 1024 alias 0 (early return), 257 aliases 1, 0x12345678 is 0x78" : "1024 aliases 0 (early return), 256/257/1023 are distinct, 0x12345678 is 0x278");
		checkf(t.present(255) == 1 && t.present(256) == 1 && t.present(1023) == 1 && (world == stock_world ? v.extent(0)[0] == 5 && v.extent(255)[0] == 5 : v.extent(256)[0] == 5 && v.extent(257)[0] == 5 && v.extent(1023)[0] == 5), "[%u] boundary indices keep their own extents", world);
		for (auto tk : tokens) t.remove(tk);
		checkf(same(v.list(5, 5), {}), "[%u] boundary entities removed", world);
	}
	// cell overflow: the native defect at 256 (0x465400 appends while count < 0x20 into a 32-byte cell)
	{
		const auto limit = world == stock_world ? 33u : 1024u;
		for (std::uint32_t i = 0; i < limit; ++i) t.update(i, cell_box(0, 0));
		const auto* c00 = v.cell(0, 0); const auto* c01 = v.cell(0, 1);
		if (world == stock_world)
		{
			checkf(v.count(c00) == 32 && c01 == c00 + 0x20 && v.count(c01) == 31 && v.index(c00, 31) == 31, "[256] cell overflow reproduced: the 32nd entry (index 31) is written over the next cell's count byte, count becomes 32, the 33rd is dropped");
			std::uint32_t set[8]{}; const auto found = t.query({10, 10, 0}, {20, 20, 1}, set);
			bool only = true; for (std::uint32_t i = 0; i < 32; ++i) only &= bit(set, i); for (std::uint32_t i = 33; i < 256; ++i) only &= !bit(set, i);
			checkf(found == 1 && only, "[256] a query over the overflowed cell reads 32 entries (the 32nd is the neighbour's count byte)");
		}
		else
		{
			checkf(v.count(c00) == 1024 && v.index(c00, 1023) == 1023 && v.index(c00, 0) == 0 && v.count(c01) == 0, "[1024] 1024 entities fit in one wide cell; the neighbour cell is untouched");
			t.update(1023, cell_box(0, 0)); // same rectangle: early return, no duplicate
			checkf(v.count(c00) == 1024, "[1024] re-updating an entity with its own rectangle returns early");
		}
		t.reset();
		checkf(v.block_of(0, 0) == no_block && v.count(c00) != 0 && t.present(5) == 0 && v.u32(L.ring_count) == 128 && v.u32(L.alloc_count) == 0, "[%u] reset forgets blocks and extents but leaves the stale cell contents (native)", world);
	}
	// extents update: overlapping move, disjoint move, partial clipping
	{
		t.init({0, 0, 0}, {1100, 1100, 100}, {120, 120, 100});
		t.update(9, box(130, 130, 350, 350));   // cells (1..2, 1..2)
		t.update(8, box(130, 130, 350, 350));
		checkf(same(v.list(1, 1), {9, 8}) && same(v.list(2, 2), {9, 8}) && v.extent(9)[0] == 1 && v.extent(9)[1] == 2 && v.extent(9)[2] == 1 && v.extent(9)[3] == 2, "[%u] a box spanning cells (1..2,1..2) is in all four cells", world);
		t.update(9, box(250, 250, 470, 470));   // cells (2..3, 2..3): shared row 2 (left edge), new row 3
		checkf(same(v.list(1, 1), {8}) && same(v.list(2, 1), {8}) && same(v.list(1, 2), {8}) && same(v.list(2, 2), {9, 8}) && same(v.list(3, 2), {9}) && same(v.list(2, 3), {9}) && same(v.list(3, 3), {9}) && v.extent(9)[0] == 2 && v.extent(9)[3] == 3,
		       "[%u] overlapping move: old-only row removed, shared row adjusts its left/right edge, new row appended", world);
		t.update(9, box(700, 700, 710, 710));   // cell (5,5): disjoint
		checkf(same(v.list(2, 2), {8}) && same(v.list(3, 3), {}) && same(v.list(5, 5), {9}), "[%u] disjoint move removes from the old rectangle then inserts into the new", world);
		t.update(8, box(-500, -500, 10, 10));   // clipped to cell (0,0)
		checkf(same(v.list(0, 0), {8}) && same(v.list(1, 1), {}) && v.extent(8)[0] == 0 && v.extent(8)[1] == 0, "[%u] a box partly off the grid is clipped to the grid", world);
		std::uint32_t set[32]; std::memset(set, 0xa5, sizeof set);
		const auto none = t.query({2000, 2000, 0}, {2100, 2100, 1}, set);
		checkf(none == 0 && set[0] == 0xa5a5a5a5u, "[%u] a query that finds nothing returns 0 and leaves the caller's set untouched", world);
		const auto some = t.query({0, 0, 0}, {800, 800, 1}, set);
		bool exact = bit(set, 8) && bit(set, 9); for (std::uint32_t i = 0; i < world; ++i) if (i != 8 && i != 9) exact &= !bit(set, i);
		checkf(some == 1 && exact, "[%u] a query clears the set on the first hit and sets exactly the entities of the touched cells", world);
	}
	// caches
	{
		t.cache_update(0, box(0, 0, 400, 400));  // cells (0..3,0..3): entities 8 and (not 9)
		const auto* s0 = t.cache_set(0);
		checkf(s0 == reinterpret_cast<const std::uint32_t*>(v.p + L.caches + cache_off_set) && bit(s0, 8) && !bit(s0, 9), "[%u] cache_update builds the cached set; cache_set_ptr returns it", world);
		t.update(9, box(130, 130, 140, 140));
		checkf(bit(s0, 9), "[%u] update sets the entity's bit in a cache whose rectangle overlaps", world);
		t.update(9, box(900, 900, 910, 910));
		checkf(!bit(s0, 9), "[%u] update clears the bit when the rectangle no longer overlaps", world);
		t.update(9, box(130, 130, 140, 140)); t.remove(9);
		checkf(!bit(s0, 9) && !bit(t.cache_set(1), 9), "[%u] remove clears the bit in both caches", world);
		t.update(9, box(130, 130, 140, 140));
		t.cache_update(0, box(0, 0, 400, 400));
		checkf(bit(s0, 9) && bit(s0, 8), "[%u] cache_update with the same rectangle returns early (set unchanged)", world);
		t.cache_update(0, box(130, 130, 400, 400)); // shrink: cells (1..3,1..3): 8 (cell 0,0) leaves, 9 stays
		checkf(bit(s0, 9) && !bit(s0, 8), "[%u] cache shrink scatters the dropped rows/columns", world);
		t.cache_update(0, box(0, 0, 400, 400));
		checkf(bit(s0, 9) && bit(s0, 8), "[%u] cache grow gathers the added rows/columns", world);
		bounds out{}; const auto ok = t.cache_bounds(0, out);
		checkf(ok == 1 && out.min.x == 0.f && out.max.x == 400.f, "[%u] cache_bounds returns the stored query bounds", world);
		const auto calls = pair_ctor_calls;
		t.cache_reset(0);
		out = box(1, 2, 3, 4); const auto bad = t.cache_bounds(0, out);
		const auto* c0 = v.p + L.caches;
		checkf(bad == 0 && out.min.x == 0.f && out.max.y == 0.f && pair_ctor_calls == calls + 1 && c0[0] == 0xff && c0[1] == 0 && c0[2] == 0xff && c0[3] == 0 && s0[0] == 0, "[%u] cache_reset invalidates; cache_bounds then returns the native zero pair through the helper and 0", world);
		t.cache_update(1, box(-9000, -9000, -8000, -8000));
		checkf(v.p[L.caches + L.cache_stride] == 0xff && t.cache_bounds(1, out) == 0 && reinterpret_cast<const float*>(v.p + L.caches + L.cache_stride + cache_off_bounds)[0] == -9000.f, "[%u] cache_update with a box off the grid stores the bounds then resets the cache (slot 0x38)", world);
	}
	// block exhaustion and collection on an 84 x 84 grid (441 blocks addressable, 128 available)
	{
		t.init({0, 0, 0}, {83.4f, 83.4f, 1}, {1, 1, 1});
		checkf(t.width() == 84 && t.height() == 84, "[%u] 84 x 84 grid", world);
		const auto at = [](std::uint32_t i) { const float a = float(4 * (i % 21) + 1), b = float(4 * (i / 21) + 1); return box(a + 0.1f, b + 0.1f, a + 0.9f, b + 0.9f); };
		for (std::uint32_t i = 0; i < 128; ++i) t.update(i, at(i));
		checkf(v.u32(L.alloc_count) == 128 && v.u32(L.ring_count) == 0 && v.block_of(1, 1) == 0 && v.block_of(5, 1) == 1, "[%u] 128 distinct blocks allocate in ring order", world);
		t.update(128, at(128));
		checkf(v.u32(L.alloc_count) == 128 && v.cell(4 * 2 + 1, 4 * 6 + 1) == nullptr && t.present(128) == 1 && v.extent(128 & mask)[0] == 9, "[%u] the 129th block cannot be mapped: the insert is dropped but the extents are still stored (native)", world);
		t.remove(5);
		checkf(v.u32(L.alloc_count) == 128 && v.block_of(21, 1) == 5, "[%u] an emptied block stays mapped until a failed mapping collects it", world);
		t.update(128, box(10.2f, 25.2f, 10.8f, 25.8f)); // present, moved to the neighbouring cell (10,25): disjoint path, the removal lookup fails, collection frees block 5, the retry maps it
		checkf(v.u32(L.alloc_count) == 128 && v.block_of(21, 1) == no_block && v.block_of(9, 25) == 5 && same(v.list(9, 25), {}) && same(v.list(10, 25), {128 & mask}) && v.u32(L.ring_enqueue) == 1 && v.u32(L.ring_dequeue) == 1 && v.u32(L.ring_count) == 0,
		       "[%u] collection frees the empty block (ring push at the wrapped enqueue slot), the retry maps it for the new cell", world);
	}
	destroy(t.self);
	checkf(true, "[%u] destroy released the backing", world);
}

// ---- differential ----------------------------------------------------------------------------------------------
std::vector<std::uint8_t> binary;
std::size_t file_offset(std::uint32_t va)
{
	auto u16 = [](std::size_t o) { std::uint16_t n; std::memcpy(&n, binary.data() + o, 2); return n; };
	auto u32 = [](std::size_t o) { std::uint32_t n; std::memcpy(&n, binary.data() + o, 4); return n; };
	const auto pe = u32(0x3c); const auto sections = pe + 24 + u16(pe + 20);
	for (unsigned i = 0; i < u16(pe + 6); ++i) { const auto row = sections + i * 40; const auto rva = u32(row + 12); if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(row + 16)) return u32(row + 20) + va - 0x400000 - rva; }
	return binary.size();
}
void* at(std::uint32_t va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }
bool writable(std::uint32_t va, std::size_t n) { DWORD old; return VirtualProtect(at(va), n, PAGE_EXECUTE_READWRITE, &old) != 0; }
bool native_bytes(std::uint32_t va, std::size_t n, std::uint32_t to = 0)
{
	const auto offset = file_offset(va); if (offset + n > binary.size()) return false;
	if (!to) to = va;
	if (!writable(to, n)) return false;
	std::memcpy(at(to), binary.data() + offset, n); return true;
}
bool patch_call(std::uint32_t site, const void* target)
{
	auto* code = static_cast<std::uint8_t*>(at(site));
	if (code[0] != 0xe8) return false;
	const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target) - (site + 5));
	std::memcpy(code + 1, &rel, 4); return true;
}
void put_pointer(std::uint32_t va, const void* value) { writable(va, 4); std::memcpy(at(va), &value, 4); }

constexpr std::uint32_t region_lo = 0x463c00, region_hi = 0x4654aa, native_size = 0x108b4;
constexpr std::uint32_t reloc_ehvec = 0x466000, reloc_nextbit = 0x466100, reloc_ringpop = 0x466200;

bool build_fixture()
{
	bool ok = native_bytes(region_lo, region_hi - region_lo);
	ok &= native_bytes(0x67217c, 0x75) && native_bytes(0x6721f8, 6) && native_bytes(0x672348, 6); // _ftol, floor/ceil thunks
	ok &= native_bytes(native_vtable, 17 * 4) && native_bytes(0x680488, 4) && native_bytes(0x68012c, 4); // vtable, 0.5f, 1.0f
	ok &= native_bytes(0x614280, 4); // slot 8 (shared accessor outside the owner region): lea eax,[ecx+0x1c]; ret
	ok &= native_bytes(0x401600, 0x30, reloc_ehvec) && native_bytes(0x403ca0, 0x99, reloc_nextbit) && native_bytes(0x403f20, 0x2c, reloc_ringpop);
	ok &= patch_call(0x464de7, at(reloc_ehvec)) && patch_call(0x463c69, at(reloc_nextbit)) && patch_call(0x464e49, at(reloc_ringpop)) && patch_call(0x464393, reinterpret_cast<const void*>(&pair_ctor_stub));
	put_pointer(0x67f13c, reinterpret_cast<const void*>(&floor_thunk)); put_pointer(0x67f11c, reinterpret_cast<const void*>(&ceil_thunk));
	put_pointer(0x67fa64, reinterpret_cast<const void*>(&vec_set_ref)); put_pointer(0x67fa1c, reinterpret_cast<const void*>(&vec_set_xyz)); put_pointer(0x67fa2c, reinterpret_cast<const void*>(&vec_assign));
	float half, one; std::memcpy(&half, at(0x680488), 4); std::memcpy(&one, at(0x68012c), 4);
	std::uint32_t slot0; std::memcpy(&slot0, at(native_vtable), 4);
	return ok && half == 0.5f && one == 1.0f && slot0 == 0x464aa0;
}

struct lcg { std::uint32_t s; std::uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; } int range(int lo, int hi) { return lo + int(next() % std::uint32_t(hi - lo + 1)); } float f(float lo, float hi) { return lo + (hi - lo) * float(next() & 0xffff) / 65535.f; } };

struct op { int kind; std::uint32_t token; bounds b; vec3 v; int i, x, y; bool null; };
std::vector<op> script()
{
	std::vector<op> ops; lcg r{20261011u};
	auto init = [&](vec3 a, vec3 b, vec3 c) { ops.push_back({0, 0, {a, b}, c, 0, 0, 0, false}); };
	auto upd = [&](std::uint32_t tk, bounds b) { ops.push_back({1, tk, b, {}, 0, 0, 0, false}); };
	auto rnd_token = [&] { const std::uint32_t pool[] = {0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 254, 255, 256, 257, 300, 0x100 | 7, 0x1200 | 7, 0x7f000000 | 9, 42, 43, 44, 45, 46, 47, 48, 49, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150}; return pool[r.next() % (sizeof pool / sizeof pool[0])]; };
	auto rnd_box = [&] { const float x = r.f(-300, 1300), y = r.f(-300, 1300); const float w = r.f(0, 450), h = r.f(0, 450); return box(x, y, x + w, y + h, r.f(-10, 10), r.f(0, 50)); };
	// configuration A: 10 x 10 at (-120,-120)
	init({-5, -5, 0}, {1000, 1000, 100}, {120, 120, 100});
	for (int n = 0; n < 1500; ++n)
	{
		const auto k = r.range(0, 99);
		if (k < 55) upd(rnd_token(), rnd_box());
		else if (k < 63) ops.push_back({2, rnd_token(), {}, {}, 0, 0, 0, false});                                   // remove
		else if (k < 66) upd(rnd_token(), box(0, 0, 0, 0)), ops.back().null = true;                               // update(nullptr)
		else if (k < 76) ops.push_back({3, 0, rnd_box(), {}, 0, 0, 0, false});                                     // query
		else if (k < 84) ops.push_back({4, 0, rnd_box(), {}, r.range(0, 1), 0, 0, false});                         // cache_update
		else if (k < 86) ops.push_back({5, 0, {}, {}, r.range(0, 1), 0, 0, false});                                // cache_reset
		else if (k < 90) ops.push_back({6, 0, {}, {}, r.range(0, 1), 0, 0, false});                                // cache_bounds + cache_off_set
		else if (k < 94) ops.push_back({7, rnd_token(), {}, {r.f(-500, 1500), r.f(-500, 1500), r.f(-10, 300)}, 0, r.range(-3, 12), r.range(-3, 12), false}); // readers
		else if (k < 96) upd(rnd_token(), box(-1e10f, r.f(0, 100), 1e10f, r.f(100, 200)));                        // huge extents: _ftol out-of-range paths
		else if (k < 97) upd(rnd_token(), box(r.f(0, 100), -3e9f, r.f(100, 200), 2.5e9f));
		else if (k < 98) upd(rnd_token(), box(r.f(0, 100), r.f(0, 100), -1e30f, 1e30f));                          // degenerate x (min > max) with indefinite y
		else if (k < 99) ops.push_back({8, 0, {}, {}, 0, 0, 0, false});                                            // reset
		else init({r.f(-200, 50), r.f(-200, 50), 0}, {r.f(800, 1200), r.f(800, 1200), 100}, {r.f(60, 200), r.f(60, 200), 100});
	}
	// overflow scenario: 40 entities into one cell (the 32nd corrupts the neighbour's count; later ops read it)
	init({0, 0, 0}, {1150, 1150, 100}, {120, 120, 100});
	for (std::uint32_t i = 0; i < 40; ++i) upd(i, cell_box(4, 4));
	ops.push_back({3, 0, box(400, 400, 700, 700), {}, 0, 0, 0, false});
	ops.push_back({4, 0, box(0, 0, 1150, 1150), {}, 0, 0, 0, false});
	for (std::uint32_t i = 0; i < 40; i += 3) upd(i, cell_box(4, 5));
	for (std::uint32_t i = 1; i < 40; i += 4) ops.push_back({2, i, {}, {}, 0, 0, 0, false});
	ops.push_back({3, 0, box(400, 400, 800, 800), {}, 0, 0, 0, false});
	// block exhaustion: 84 x 84, 180 spread entities (441 blocks addressable, 128 mappable), then churn
	init({0, 0, 0}, {83.4f, 83.4f, 1}, {1, 1, 1});
	auto spread = [&](std::uint32_t i) { const float a = float(4 * (i % 21) + 1), b = float(4 * ((i / 21) % 21) + 1); return box(a + 0.1f, b + 0.1f, a + 0.9f, b + 0.9f); };
	for (std::uint32_t i = 0; i < 180; ++i) upd(i, spread(i));
	for (int n = 0; n < 400; ++n)
	{
		const auto tk = std::uint32_t(r.range(0, 199));
		const auto k = r.range(0, 9);
		if (k < 4) ops.push_back({2, tk, {}, {}, 0, 0, 0, false});
		else if (k < 8) upd(tk, spread(std::uint32_t(r.range(0, 440))));
		else if (k < 9) ops.push_back({3, 0, box(r.f(0, 80), r.f(0, 80), r.f(0, 84), r.f(0, 84)), {}, 0, 0, 0, false});
		else ops.push_back({4, 0, box(r.f(0, 80), r.f(0, 80), r.f(0, 84), r.f(0, 84)), {}, r.range(0, 1), 0, 0, false});
	}
	// configuration B then the second/third init paths
	init({0, 0, 0}, {84.3f, 84.3f, 1}, {1, 1, 1});
	for (std::uint32_t i = 0; i < 60; ++i) upd(i, box(r.f(0, 84), r.f(0, 84), r.f(0, 84), r.f(0, 84)));
	init({0, 0, 0}, {85.6f, 85.6f, 1}, {1, 1, 1});
	ops.push_back({7, 5, {}, {33.3f, 44.4f, 1.f}, 0, 3, 4, false});
	init({-5, -5, 0}, {1000, 1000, 100}, {120, 120, 100});
	for (std::uint32_t i = 0; i < 20; ++i) upd(i, rnd_box());
	ops.push_back({8, 0, {}, {}, 0, 0, 0, false});
	return ops;
}

struct outcome { std::uint32_t set[8]; unsigned char flag; int ints[4]; float floats[12]; std::uint32_t off; bounds b; };
outcome apply(target& t, const op& o)
{
	outcome out{};
	std::memset(out.set, 0x5a, sizeof out.set);
	switch (o.kind)
	{
	case 0: t.init(o.b.min, o.b.max, o.v); break;
	case 1: t.update(o.token, o.null ? nullptr : &o.b); break;
	case 2: t.remove(o.token); break;
	case 3: out.flag = t.query(o.b.min, o.b.max, out.set); break;
	case 4: t.cache_update(o.i, o.b); break;
	case 5: t.cache_reset(o.i); break;
	case 6: out.flag = t.cache_bounds(o.i, out.b); { const auto* s = t.cache_set(o.i); out.off = std::uint32_t(reinterpret_cast<const std::uint8_t*>(s) - static_cast<const std::uint8_t*>(t.self)); std::memcpy(out.set, s, 32); } break;
	case 7:
	{
		out.ints[0] = t.present(o.token); out.ints[1] = t.width(); out.ints[2] = t.height();
		const auto* c = t.cell_size(); out.off = std::uint32_t(reinterpret_cast<const std::uint8_t*>(c) - static_cast<const std::uint8_t*>(t.self));
		const auto s = t.snap(o.v), ce = t.center(o.x, o.y), mn = t.min(), mx = t.max();
		const float fl[12] = {s.x, s.y, s.z, ce.x, ce.y, ce.z, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z};
		std::memcpy(out.floats, fl, sizeof fl);
		break;
	}
	case 8: t.reset(); break;
	}
	return out;
}
const char* region_name(std::uint32_t off)
{
	const auto L = layout_for(stock_world);
	if (off < off_coarse) return "head"; if (off < L.pool) return "coarse map"; if (off < L.block_bitmap) return "cells"; if (off < L.ring) return "block bitmap";
	if (off < L.ring_enqueue) return "ring"; if (off < L.alloc_bitmap) return "ring counters"; if (off < L.extents) return "alloc bitmap/count"; if (off < L.caches) return "extents"; return "caches";
}
int differential_run(unsigned precision, const char* label)
{
	alignas(16) static std::uint8_t native_obj[native_size + 0x100];
	std::memset(native_obj, 0, sizeof native_obj); std::memset(native_obj + native_size, 0xcd, 0x100);
	unsigned old = 0; _controlfp_s(&old, precision, _MCW_PC);
	reinterpret_cast<void*(__fastcall*)(void*, void*)>(at(0x463de0))(native_obj, nullptr); // native constructor
	target nat{native_obj}, port{create(stock_descriptor)};
	int mismatches = 0, steps = 0, kinds[9]{}, steps_at_exhaustion = 0; std::uint32_t max_blocks = 0, max_cell = 0;
	bool ctor_equal = std::memcmp(native_obj, port.self, native_size) == 0 && nat.vt() == static_cast<const void* const*>(at(native_vtable)) && port.vt() == vtable.slots;
	// the vptr differs by design; compare everything after it
	ctor_equal = std::memcmp(native_obj + 4, static_cast<std::uint8_t*>(port.self) + 4, native_size - 4) == 0 && nat.vt() == static_cast<const void* const*>(at(native_vtable)) && port.vt() == vtable.slots;
	checkf(ctor_equal, "[%s] constructed objects are byte-identical after the vptr", label);
	const auto ops = script();
	std::string first;
	for (const auto& o : ops)
	{
		++steps;
		const auto a = apply(nat, o);
		const auto b = apply(port, o);
		const auto memory = std::memcmp(native_obj + 4, static_cast<std::uint8_t*>(port.self) + 4, native_size - 4) == 0;
		{
			view pv(port.self); ++kinds[o.kind];
			if (pv.u32(pv.L.alloc_count) > max_blocks) max_blocks = pv.u32(pv.L.alloc_count);
			for (std::uint32_t i = 0; i < coarse_bytes; ++i) if (pv.p[off_coarse + i] != no_block) for (std::uint32_t k = 0; k < cells_per_block; ++k) { const auto c = pv.count(pv.p + pv.L.pool + pv.p[off_coarse + i] * pv.L.block_stride + k * pv.L.cell_stride); if (c > max_cell) max_cell = c; }
			if (pv.u32(pv.L.ring_count) == 0 && (o.kind == 1 || o.kind == 2)) ++steps_at_exhaustion;
		}
		const auto results = std::memcmp(a.set, b.set, sizeof a.set) == 0 && a.flag == b.flag && std::memcmp(a.ints, b.ints, sizeof a.ints) == 0 && std::memcmp(a.floats, b.floats, sizeof a.floats) == 0 && a.off == b.off && std::memcmp(&a.b, &b.b, sizeof a.b) == 0;
		if (!memory || !results)
		{
			++mismatches;
			if (first.empty())
			{
				std::uint32_t off = 4; while (off < native_size && native_obj[off] == static_cast<std::uint8_t*>(port.self)[off]) ++off;
				char text[400]; std::snprintf(text, sizeof text, "step %d kind %d token 0x%x box (%.9g,%.9g,%.9g)-(%.9g,%.9g,%.9g) v (%.9g,%.9g,%.9g): %s%s first byte 0x%x (%s) native %02x port %02x", steps, o.kind, o.token, o.b.min.x, o.b.min.y, o.b.min.z, o.b.max.x, o.b.max.y, o.b.max.z, o.v.x, o.v.y, o.v.z, memory ? "" : "memory ", results ? "" : "results", off, off < native_size ? region_name(off) : "-", off < native_size ? native_obj[off] : 0, off < native_size ? static_cast<std::uint8_t*>(port.self)[off] : 0);
				first = text;
			}
		}
	}
	const bool guard = [&] { for (int i = 0; i < 0x100; ++i) if (native_obj[native_size + i] != 0xcd) return false; return true; }();
	checkf(guard, "[%s] native code stayed inside the 0x108b4-byte object", label);
	checkf(mismatches == 0, "[%s] %d scripted steps: %d mismatch(es)%s%s", label, steps, mismatches, first.empty() ? "" : "; first: ", first.c_str());
	checkf(max_blocks == 128 && max_cell >= 32 && steps_at_exhaustion > 50 && kinds[0] >= 20 && kinds[3] >= 100 && kinds[4] >= 100 && kinds[7] >= 40 && kinds[8] >= 10 && pair_ctor_calls > 0,
	       "[%s] coverage: init %d, update %d, remove %d, query %d, cache_update %d, cache_reset %d, cache_bounds %d, readers %d, reset %d; max blocks %u, max cell count %u (overflow), %d insert/remove steps with the ring empty", label, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], kinds[7], kinds[8], max_blocks, max_cell, steps_at_exhaustion);
	reinterpret_cast<void(__fastcall*)(void*, void*)>(at(0x465450))(native_obj, nullptr); // native destructor
	destroy(port.self);
	_controlfp_s(&old, old, _MCW_PC);
	return steps;
}

int main(int argc, char** argv)
{
	std::setbuf(stdout, nullptr);
	bounds_pair_ctor = &pair_ctor_stub;
	layout_checks();
	synthetic(stock_world);
	synthetic(raised_world);
	const char* path = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	int steps = 0;
	if (!path) std::puts("SKIPPED differential: no retail executable path given (argument 1 or XML2_FIX_TEST_EXE)");
	else
	{
		std::ifstream file(path, std::ios::binary); binary.assign(std::istreambuf_iterator<char>(file), {});
		const auto begin = reinterpret_cast<std::uintptr_t>(fixture_space), end = begin + sizeof fixture_space;
		if (binary.size() < 0x1000) { std::puts("SKIPPED differential: executable unreadable"); }
		else if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || region_lo < begin || 0x686c54 + 0x44 > end) { std::printf("SKIPPED differential: fixture does not cover the image (fixture %08x-%08x)\n", unsigned(begin), unsigned(end)); ++failures; }
		else if (!build_fixture()) { check(false, "fixture built from the executable (region, helpers, imports, constants, vtable)"); }
		else
		{
			check(true, "fixture built: 0x463c00-0x4654aa, _ftol, floor/ceil thunks, relocated 0x401600/0x403ca0/0x403f20, stubbed 0x419af0 and igVec3f imports");
			steps = differential_run(_PC_24, "x87 single precision (the D3D device default)");
			differential_run(_PC_53, "x87 double precision");
		}
	}
	std::printf("Entity grid: %d checks, %d failure(s), %d differential steps per precision\n", checks, failures, steps);
	return failures ? 1 : 0;
}
