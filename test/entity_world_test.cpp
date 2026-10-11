// CWorld replacement owner: synthetic checks at 256 and 1024 (lifecycle, names, hierarchy, relationships, properties,
// dense-index versus token domains, retirement, reset, reentrancy, invariants after every operation), and a
// differential run of the retail World code (copied from an executable given on the command line into a fixture at
// its own addresses, with the Grid/allocator/name-table dependencies stubbed identically for both sides) against the
// port at 256, comparing every result, every cross-owner call and the shared memory regions after every scripted
// step. No game, no game content.
#include "entity_world.hpp"
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace entity_world;
using entity_capacity_layout::stock_descriptor;
using entity_capacity_layout::raised_descriptor_for;
using entity_capacity_layout::stock_world;
using entity_capacity_layout::raised_world;
using entity_capacity_layout::generation_max;

#pragma section(".peerfix", read)
__declspec(allocate(".peerfix")) std::uint8_t fixture_space[0x400000]{};

struct lcg { std::uint32_t s; std::uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; } int range(int lo, int hi) { return lo + int(next() % std::uint32_t(hi - lo + 1)); } };

int failures = 0, checks = 0;
void check(bool value, const char* message) { ++checks; std::printf("%s %s\n", value ? "PASS" : "FAIL", message); failures += !value; }
void checkf(bool value, const char* format, ...) { char text[512]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof text, format, args); va_end(args); check(value, text); }
void quiet(bool value, const char* format, ...) { ++checks; if (value) return; char text[512]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof text, format, args); va_end(args); std::printf("FAIL %s\n", text); ++failures; }

// ---- native ABI plumbing: every slot is called through a __fastcall pointer with PLAIN integer argument types (ECX =
// this, EDX unused, stack args, callee cleanup): the native __thiscall frame. The same typedefs drive the retail vtable
// and the DLL one, so a mismatch between the port's typed signatures and the native frame would show up here.
using fn_dtor = void*(__fastcall*)(void*, void*, std::uint32_t);
using fn_void = void(__fastcall*)(void*, void*);
using fn_name = void*(__fastcall*)(void*, void*, const char*);
using fn_tok = void*(__fastcall*)(void*, void*, std::uint32_t);
using fn_valid = unsigned char(__fastcall*)(void*, void*, std::uint32_t);
using fn_handle = void(__fastcall*)(void*, void*, std::uint32_t*, std::uint32_t);
using fn_u32 = std::uint32_t(__fastcall*)(void*, void*, std::uint32_t);
using fn_outp = void(__fastcall*)(void*, void*, std::uint32_t*);
using fn_inp = void(__fastcall*)(void*, void*, const std::uint32_t*);
using fn_name_tok = void(__fastcall*)(void*, void*, const char*, std::uint32_t);
using fn_name_set = unsigned char(__fastcall*)(void*, void*, const char*, std::uint32_t*);
using fn_two_names = void(__fastcall*)(void*, void*, const char*, const char*);
using fn_prop = void(__fastcall*)(void*, void*, std::uint32_t, const std::uint32_t*);
using fn_ptr_u32 = std::uint32_t*(__fastcall*)(void*, void*, std::uint32_t);
using fn_rel_set = void(__fastcall*)(void*, void*, int, std::uint32_t, std::uint32_t);
using fn_rel_members = unsigned char(__fastcall*)(void*, void*, int, std::uint32_t, std::uint32_t*);
using fn_rel_get = void(__fastcall*)(void*, void*, std::uint32_t*, int, std::uint32_t);
using fn_rel_iter = std::uint32_t*(__fastcall*)(void*, void*, std::uint32_t*, int, std::uint32_t);
using fn_alloc = void*(__fastcall*)(void*, void*, std::uint32_t*, std::uint32_t);

// ---- the stubbed dependencies: entities, allocator, Grid, name table. One instance set per side ("port" / "native"),
// each with its own call log, so the differential compares the sequence of cross-owner effects as well as results.
struct side;
struct entity
{
	const void* const* vptr;          // +0
	std::uint32_t record[4];          // +4..+0x14: the 16-byte property record the native callers keep here
	std::uint32_t pad14;              // +0x14
	std::uint32_t name_id;            // +0x18
	std::uint32_t token;              // +0x1c
	std::uint32_t serial;             // test identity (pointers differ between sides)
	std::uint32_t parent_id;          // what slot 0x68 received
	std::uint32_t size;
	side* owner;
	float bounds[6];
};
struct sub_object { const void* const* vptr; entity* parent; };
struct allocator_object { const void* const* vptr; side* owner; };
struct grid_object { const void* const* vptr; side* owner; };

struct name_table // the physical shape 0x602280's table has: gen at +0, offsets at +4, used at +0x4004, strings at +0x4008
{
	std::uint32_t gen;
	std::uint32_t offsets[0x1000];
	std::uint32_t used;
	char strings[0x7800];
	std::uint32_t count = 0;
	// An id is (gen << 24) | slot with slot = the index into `offsets` (slot 0 unused: id 0 is "no name"), so the native
	// inline lookup `*(base + 4 + (id & 0xffffff) * 4) + base + 0x4008` (0x4c7a10) and 0x41aa80's read the same string.
	std::uint32_t intern(const char* text)
	{
		for (std::uint32_t k = 1; k <= count; ++k) if (std::strcmp(strings + offsets[k], text) == 0) return (gen << 24) | k;
		if (count >= 0xffe) return 0;
		const auto len = std::strlen(text) + 1;
		if (used + len > sizeof strings) return 0;
		++count;
		offsets[count] = used; std::memcpy(strings + used, text, len); used += static_cast<std::uint32_t>(len);
		return (gen << 24) | count;
	}
	const char* text_of(std::uint32_t id) const { const auto k = (id & 0xffffff); return (k == 0 || k > count) ? "" : strings + offsets[k]; }
};
name_table* shared_names = nullptr; // one table for both sides: ids must agree

struct side
{
	std::vector<std::string> log;
	std::vector<entity*> entities;
	std::uint32_t next_serial = 1;
	allocator_object allocator{};
	grid_object grid{};
	bool reentrant_release = false; // slot 4 of the entity re-releases its token (synthetic only)
	bool allocate_in_destroy = false; // slot 4 of the entity allocates (synthetic only)
	void* world = nullptr;
	int grid_clears = 0;
	void logf(const char* format, ...) { char text[256]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof text, format, args); va_end(args); log.emplace_back(text); }
	entity* find(const void* p) { for (auto* e : entities) if (e == p) return e; return nullptr; }
};
side* current_side = nullptr; // the side whose stubs are being exercised (set per call by the harness)
const void* entity_vtable[0x70 / 4 + 1];
const void* sub_vtable[2];
const void* allocator_vtable[0x14 / 4];
const void* grid_vtable[0x2c / 4];

void* __fastcall ent_slot0(void* self, void*) { auto* e = static_cast<entity*>(self); e->owner->logf("e%u.slot0", e->serial); static sub_object s; s.vptr = sub_vtable; s.parent = e; return &s; }
void __fastcall sub_slot4(void* self, void*) { auto* s = static_cast<sub_object*>(self); s->parent->owner->logf("e%u.sub.slot4", s->parent->serial); }
void __fastcall ent_slot4(void* self, void*, std::uint32_t arg)
{
	auto* e = static_cast<entity*>(self); auto* S = e->owner; S->logf("e%u.slot4(%u)", e->serial, arg);
	if (S->reentrant_release) { S->reentrant_release = false; reinterpret_cast<fn_tok>((*static_cast<const void* const* const*>(S->world))[0x64 / 4])(S->world, nullptr, e->token); }
	if (S->allocate_in_destroy) { S->allocate_in_destroy = false; std::uint32_t t = 0; auto* r = reinterpret_cast<fn_alloc>((*static_cast<const void* const* const*>(S->world))[0x60 / 4])(S->world, nullptr, &t, 64); S->logf("nested-allocate -> %s", r ? "object" : "null"); }
}
void* __fastcall ent_slot44(void* self, void*) { auto* e = static_cast<entity*>(self); e->owner->logf("e%u.slot44", e->serial); return e->bounds; }
void __fastcall ent_slot68(void* self, void*, const std::uint32_t* id) { auto* e = static_cast<entity*>(self); e->parent_id = *id; e->owner->logf("e%u.slot68(%08x)", e->serial, *id); }
void __fastcall ent_slot6c(void* self, void*, std::uint32_t arg) { auto* e = static_cast<entity*>(self); e->owner->logf("e%u.slot6c(%u)", e->serial, arg); }
void* __fastcall alloc_slotc(void* self, void*, std::uint32_t size)
{
	auto* S = static_cast<allocator_object*>(self)->owner;
	if (size == 0) { S->logf("alloc(%u) -> null", size); return nullptr; } // size 0: the stub refuses (exercises the native null path)
	auto* e = new entity{}; e->vptr = entity_vtable; e->serial = S->next_serial++; e->size = size; e->owner = S;
	for (int k = 0; k < 6; ++k) e->bounds[k] = float(e->serial * 10 + k);
	S->entities.push_back(e); S->logf("alloc(%u) -> e%u", size, e->serial); return e;
}
void __fastcall alloc_slot10(void* self, void*, std::uint32_t ptr)
{
	auto* S = static_cast<allocator_object*>(self)->owner; auto* e = S->find(reinterpret_cast<void*>(ptr));
	S->logf("free(e%u)", e ? e->serial : 0);
	if (e) { for (auto& p : S->entities) if (p == e) { p = nullptr; } delete e; }
}
void __fastcall grid_slot0(void* self, void*) { auto* S = static_cast<grid_object*>(self)->owner; ++S->grid_clears; S->logf("grid.clear"); }
void __fastcall grid_slot24(void* self, void*, std::uint32_t token) { static_cast<grid_object*>(self)->owner->logf("grid.remove(%08x)", token); }
void __fastcall grid_slot28(void* self, void*, std::uint32_t token, const void* bounds) { auto* S = static_cast<grid_object*>(self)->owner; const float* b = static_cast<const float*>(bounds); S->logf("grid.update(%08x,[%g..%g])", token, b ? b[0] : -1.f, b ? b[5] : -1.f); }

void* __cdecl grid_getter_stub() { return &current_side->grid; }
void* __cdecl allocator_getter_stub() { return &current_side->allocator; }
const char* __fastcall name_of_id_stub(const std::uint32_t* id, void*) { return *id == 0 ? "" : shared_names->text_of(*id); }
void __fastcall name_hash_stub(std::uint32_t* out, void*, const char* text) { *out = (!text || !*text) ? 0 : shared_names->intern(text); }
void __cdecl fold_case_stub(char* text) { for (; *text; ++text) if (*text >= 'A' && *text <= 'Z') *text = char(*text - 'A' + 'a'); }
void* __cdecl name_table_stub() { return shared_names; }

void init_stub_vtables()
{
	for (auto& s : entity_vtable) s = nullptr;
	entity_vtable[0] = reinterpret_cast<const void*>(&ent_slot0); entity_vtable[1] = reinterpret_cast<const void*>(&ent_slot4);
	entity_vtable[0x44 / 4] = reinterpret_cast<const void*>(&ent_slot44); entity_vtable[0x68 / 4] = reinterpret_cast<const void*>(&ent_slot68); entity_vtable[0x6c / 4] = reinterpret_cast<const void*>(&ent_slot6c);
	sub_vtable[0] = nullptr; sub_vtable[1] = reinterpret_cast<const void*>(&sub_slot4);
	for (auto& s : allocator_vtable) s = nullptr;
	allocator_vtable[0xc / 4] = reinterpret_cast<const void*>(&alloc_slotc); allocator_vtable[0x10 / 4] = reinterpret_cast<const void*>(&alloc_slot10);
	for (auto& s : grid_vtable) s = nullptr;
	grid_vtable[0] = reinterpret_cast<const void*>(&grid_slot0); grid_vtable[0x24 / 4] = reinterpret_cast<const void*>(&grid_slot24); grid_vtable[0x28 / 4] = reinterpret_cast<const void*>(&grid_slot28);
}
void init_side(side& S) { S.allocator.vptr = allocator_vtable; S.allocator.owner = &S; S.grid.vptr = grid_vtable; S.grid.owner = &S; }

// ---- a target: one World object (port or native) driven through its vtable ----------------------------------------
struct target
{
	void* self = nullptr; side* S = nullptr; bool port = false; std::uint32_t n = 0;
	const void* const* vt() const { return *static_cast<const void* const* const*>(self); }
	template <typename Fn> Fn fn(int slot) const { return reinterpret_cast<Fn>(vt()[slot / 4]); }
	void enter() const { current_side = S; }
	void* dtor(std::uint32_t flags) { enter(); return fn<fn_dtor>(0x00)(self, nullptr, flags); }
	void reset() { enter(); fn<fn_void>(0x04)(self, nullptr); }
	void* by_name(const char* name) { enter(); return fn<fn_name>(0x08)(self, nullptr, name); }
	void* by_token(std::uint32_t t) { enter(); return fn<fn_tok>(0x0c)(self, nullptr, t); }
	void* by_dense(std::uint32_t i) { enter(); return fn<fn_tok>(0x10)(self, nullptr, i); }
	bool valid(std::uint32_t t) { enter(); return fn<fn_valid>(0x14)(self, nullptr, t) != 0; }
	std::uint32_t handle_of(std::uint32_t i) { enter(); std::uint32_t out = 0xdeadbeef; fn<fn_handle>(0x18)(self, nullptr, &out, i); return out; }
	std::uint32_t index_of(std::uint32_t t) { enter(); return fn<fn_u32>(0x1c)(self, nullptr, t); }
	void* entity_of(std::uint32_t t) { enter(); return fn<fn_tok>(0x20)(self, nullptr, t); }
	std::uint32_t version() { enter(); std::uint32_t v = 0xdeadbeef; fn<fn_outp>(0x24)(self, nullptr, &v); return v; }
	void set_version(std::uint32_t v) { enter(); fn<fn_inp>(0x28)(self, nullptr, &v); }
	void register_name(const char* name, std::uint32_t t) { enter(); fn<fn_name_tok>(0x2c)(self, nullptr, name, t); }
	void unregister_name(const char* name, std::uint32_t t) { enter(); fn<fn_name_tok>(0x30)(self, nullptr, name, t); }
	bool named(const char* name, std::uint32_t* set) { enter(); return fn<fn_name_set>(0x34)(self, nullptr, name, set) != 0; }
	void relate(const char* a, const char* b) { enter(); fn<fn_two_names>(0x38)(self, nullptr, a, b); }
	void property_update(std::uint32_t t, const std::uint32_t* rec) { enter(); fn<fn_prop>(0x3c)(self, nullptr, t, rec); }
	std::uint32_t* members(std::uint32_t prop) { enter(); return fn<fn_ptr_u32>(0x40)(self, nullptr, prop); }
	std::uint32_t* record(std::uint32_t i) { enter(); return fn<fn_ptr_u32>(0x44)(self, nullptr, i); }
	void grid_update(std::uint32_t t) { enter(); fn<fn_tok>(0x48)(self, nullptr, t); }
	void rel_set(int table, std::uint32_t t, std::uint32_t v) { enter(); fn<fn_rel_set>(0x4c)(self, nullptr, table, t, v); }
	bool rel_members(int table, std::uint32_t t, std::uint32_t* set) { enter(); return fn<fn_rel_members>(0x50)(self, nullptr, table, t, set) != 0; }
	std::uint32_t rel_get(int table, std::uint32_t t) { enter(); std::uint32_t out = 0xdeadbeef; fn<fn_rel_get>(0x54)(self, nullptr, &out, table, t); return out; }
	std::uint32_t rel_begin(int table, std::uint32_t t, std::uint32_t** ret = nullptr) { enter(); std::uint32_t out = 0xdeadbeef; auto* r = fn<fn_rel_iter>(0x58)(self, nullptr, &out, table, t); if (ret) *ret = r; return out; }
	std::uint32_t rel_next(int table) { enter(); std::uint32_t out = 0xdeadbeef; fn<fn_rel_iter>(0x5c)(self, nullptr, &out, table, 0x77); return out; }
	void* allocate(std::uint32_t* out, std::uint32_t size) { enter(); return fn<fn_alloc>(0x60)(self, nullptr, out, size); }
	void release(std::uint32_t t) { enter(); fn<fn_tok>(0x64)(self, nullptr, t); }
	// helpers
	std::uint32_t alloc(std::uint32_t size = 64) { std::uint32_t t = 0; void* e = allocate(&t, size); if (!e) return 0; static_cast<entity*>(e)->token = t; return t; }
	entity* ent(std::uint32_t t) { return static_cast<entity*>(by_token(t)); }
	void name(std::uint32_t t, const char* text) { if (auto* e = ent(t)) { std::uint32_t id = 0; name_hash_stub(&id, nullptr, text); e->name_id = id; } register_name(text, t); }
};

// ---- synthetic -------------------------------------------------------------------------------------------------------
std::uint32_t seed_token(const target& T, std::uint32_t gen, std::uint32_t i) { return (gen << (T.n == 256 ? 8 : 10)) | i; }
void invariant(const target& T, const char* when)
{
	if (!T.port) return;
	char why[128] = ""; const bool ok = check_invariants(T.self, why, sizeof why);
	quiet(ok, "invariant after %s: %s", when, why);
}
std::uint32_t* poke_handle(target& T, std::uint32_t i) // test-only: the port's handle table through the layout
{
	const auto* L = layout_of(T.self);
	return reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(T.self) + L->core.handles) + i;
}

void synthetic(std::uint32_t n)
{
	std::printf("---- synthetic at %u ----\n", n);
	side S; init_side(S);
	const auto d = n == 256 ? stock_descriptor : raised_descriptor_for(0, 0, 0, 0);
	native_hooks.grid_getter = &grid_getter_stub; native_hooks.allocator_getter = &allocator_getter_stub;
	native_hooks.name_of_id = &name_of_id_stub; native_hooks.name_hash = &name_hash_stub; native_hooks.fold_case = &fold_case_stub;
	target T; T.self = create(d); T.S = &S; T.port = true; T.n = n; S.world = T.self;
	checkf(T.self != nullptr, "%u: created", n);
	if (!T.self) return;
	checkf(object_size(d) == (n == 256 ? 0x9370u : 0x24b50u), "%u: object size %zx", n, object_size(d));
	const auto* L = layout_of(T.self);
	checkf(L && L->world == n && L->index_mask == n - 1, "%u: layout", n);
	auto st = counters(T.self);
	checkf(st.available && st.live == 0 && st.free_slots == n && st.retired == 0 && st.max_generation == 1, "%u: initial counters live %u free %u retired %u maxgen %u", n, st.live, st.free_slots, st.retired, st.max_generation);
	invariant(T, "create");
	const std::uint32_t bits = n == 256 ? 8 : 10;

	// token seeds and the handle-by-dense view of free slots
	for (std::uint32_t i : {0u, 255u, n - 1}) quiet(T.handle_of(i) == 0, "%u: handle_of(free %u) is 0", n, i);
	quiet(T.index_of(0xffffffffu) == n - 1, "%u: index_of mask", n);

	// fill: tokens are generation 1 in ascending slot order
	std::vector<std::uint32_t> tokens;
	bool order_ok = true;
	for (std::uint32_t i = 0; i < n; ++i) { const auto t = T.alloc(); tokens.push_back(t); order_ok &= t == seed_token(T, 1, i); }
	checkf(order_ok, "%u: full fill mints (1 << %u) | i in ascending order", n, bits);
	invariant(T, "fill");
	st = counters(T.self);
	checkf(st.live == n && st.free_slots == 0, "%u: full: live %u free %u", n, st.live, st.free_slots);
	{
		std::uint32_t out = 0x12345678; void* e = T.allocate(&out, 64);
		checkf(e == nullptr && out == 0x12345678 && counters(T.self).full_refusals == 1 && counters(T.self).refusals == 1, "%u: allocation %u refused, out untouched, counted", n, n + 1);
	}
	// dense vs token domains
	checkf(T.by_dense(255) == T.ent(tokens[255]) && T.handle_of(255) == tokens[255], "%u: dense 255", n);
	if (n == 1024)
	{
		checkf(T.by_dense(257) == T.ent(tokens[257]) && T.by_dense(1023) == T.ent(tokens[1023]), "1024: dense 257 and 1023 find their entities");
		checkf(T.by_token(257) == nullptr && !T.valid(257), "1024: token 257 (generation 0, slot 257) is null");
		checkf(T.by_token(0x501) == T.ent(tokens[257]) && T.valid(0x501), "1024: token 0x501 is generation 1 / slot 257");
		checkf(T.by_dense(1024) == nullptr && T.handle_of(1024) == 0, "1024: dense 1024 is out of range (null)");
		checkf(T.by_token(0x101) == nullptr, "1024: retail-shaped token 0x101 (generation 0, slot 257) is null");
	}
	else
	{
		checkf(T.by_token(0x101) == T.ent(tokens[1]) && T.by_token(0x501) == nullptr, "256: token 0x101 is slot 1, 0x501 (generation 5) is null");
		checkf(T.by_dense(256) == nullptr && T.handle_of(256) == 0 && T.by_dense(257) == nullptr, "256: dense 256/257 are out of range (null)");
	}
	checkf(T.by_token(0) == nullptr && !T.valid(0) && T.entity_of(0) == nullptr, "%u: token 0 is null", n);
	checkf(T.entity_of(tokens[3]) == T.ent(tokens[3]), "%u: slot 0x20 routes through slot 0xc", n);
	checkf(!T.valid(tokens[7] + (1u << bits)) && T.by_token(tokens[7] + (1u << bits)) == nullptr, "%u: a future generation of a live slot is invalid", n);

	// release / recycle: FIFO order, generation 2
	const auto log_before = S.log.size();
	T.release(tokens[5]); T.release(tokens[3]); T.release(tokens[9]);
	invariant(T, "three releases");
	st = counters(T.self);
	checkf(st.live == n - 3 && st.free_slots == 3, "%u: after three releases live %u free %u", n, st.live, st.free_slots);
	checkf(!T.valid(tokens[5]) && T.by_token(tokens[5]) == nullptr && T.handle_of(5) == 0 && T.by_dense(5) == nullptr, "%u: a released token is invalid and its slot reads free", n);
	{
		bool seen_order = false;
		for (std::size_t k = log_before; k < S.log.size(); ++k) if (S.log[k].rfind("e6.slot0", 0) == 0) seen_order = true;
		checkf(seen_order, "%u: release called the entity's slot 0", n);
		// the exact cleanup order of the first release
		const char* expect[] = {"e6.slot0", "e6.sub.slot4", "grid.remove", "e6.slot4(0)", "free(e6)"};
		std::size_t k = log_before; bool ok = true;
		for (const char* x : expect) { bool found = false; for (; k < S.log.size(); ++k) if (S.log[k].rfind(x, 0) == 0) { found = true; ++k; break; } ok &= found; }
		checkf(ok, "%u: native cleanup order (entity slot 0 -> sub slot 4, grid remove, entity slot 4(0), allocator free)", n);
	}
	const auto r1 = T.alloc(), r2 = T.alloc(), r3 = T.alloc();
	checkf(r1 == seed_token(T, 2, 5) && r2 == seed_token(T, 2, 3) && r3 == seed_token(T, 2, 9), "%u: recycle in FIFO order at generation 2 (%08x %08x %08x)", n, r1, r2, r3);
	tokens[5] = r1; tokens[3] = r2; tokens[9] = r3;
	checkf(counters(T.self).max_generation == 2, "%u: max generation 2", n);
	invariant(T, "recycle");
	// 255/256/257 boundary at 1024: release 255, 256, 257 and re-allocate
	if (n == 1024)
	{
		T.release(tokens[255]); T.release(tokens[256]); T.release(tokens[257]);
		const auto a = T.alloc(), b = T.alloc(), c = T.alloc();
		checkf(a == seed_token(T, 2, 255) && b == seed_token(T, 2, 256) && c == seed_token(T, 2, 257), "1024: slots 255/256/257 recycle (%08x %08x %08x)", a, b, c);
		tokens[255] = a; tokens[256] = b; tokens[257] = c;
	}
	// duplicate release: the stale token no longer resolves
	{
		const auto before = counters(T.self);
		T.release(seed_token(T, 1, 5));
		const auto after = counters(T.self);
		checkf(before.live == after.live && before.free_slots == after.free_slots, "%u: a duplicate (stale) release is a no-op", n);
	}
	// reentrant release: the entity's slot 4 releases the same token again
	{
		S.reentrant_release = true;
		const auto before = counters(T.self);
		T.release(tokens[11]);
		const auto after = counters(T.self);
		checkf(after.live == before.live - 1 && after.free_slots == before.free_slots + 1 && after.reentrant_release_refusals == 1, "%u: reentrant release destroys once (live %u -> %u, refusals %u)", n, before.live, after.live, after.reentrant_release_refusals);
		invariant(T, "reentrant release");
		tokens[11] = T.alloc();
		checkf(tokens[11] == seed_token(T, 2, 11), "%u: the reentrantly released slot recycles once", n);
	}

	// names
	{
		T.name(tokens[1], "Alpha"); T.name(tokens[2], "beta"); T.name(tokens[4], "ALPHA");
		checkf(T.by_name("alpha") == T.ent(tokens[4]) && T.by_name("Beta") == T.ent(tokens[2]), "%u: names fold case; the latest registration heads the list", n);
		std::uint32_t set[32]; std::memset(set, 0xff, sizeof set);
		checkf(T.named("ALPHA", set) && set[0] == ((1u << 1) | (1u << 4)) && set[1] == 0 && set[n / 32 - 1] == 0, "%u: entities_named clears the set and marks slots 1 and 4", n);
		checkf(!T.named("nobody", set), "%u: entities_named of an unknown name is 0", n);
		const auto log_mark = S.log.size();
		T.register_name("alpha", tokens[1]);
		checkf(S.log.size() == log_mark && T.named("alpha", set) && set[0] == ((1u << 1) | (1u << 4)), "%u: re-registering a listed handle is a no-op", n);
		T.unregister_name("alpha", tokens[4]);
		checkf(T.by_name("alpha") == T.ent(tokens[1]) && T.named("alpha", set) && set[0] == (1u << 1), "%u: unregister removes one handle", n);
		T.unregister_name("alpha", tokens[4]);
		checkf(T.named("alpha", set) && set[0] == (1u << 1), "%u: unregistering an unlisted handle changes nothing", n);
		T.unregister_name("alpha", tokens[1]);
		checkf(!T.named("alpha", set) && T.by_name("alpha") == nullptr, "%u: the name is erased when its list empties", n);
		T.name(tokens[1], "alpha");
		T.release(tokens[2]);
		checkf(T.by_name("beta") == nullptr, "%u: release unregisters the entity's name (through 0x41aa80's string)", n);
		tokens[2] = T.alloc();
		T.unregister_name("alpha", tokens[1]);
		// name node exhaustion: n distinct names consume the n name nodes; the (n+1)th is refused (native reads a stale ring entry)
		std::vector<std::string> names;
		for (std::uint32_t i = 0; i < n; ++i) { char buf[32]; std::snprintf(buf, sizeof buf, "n%u", i); names.emplace_back(buf); T.register_name(buf, tokens[i]); }
		bool all = true; std::uint32_t first_bad = n, bad = 0;
		for (std::uint32_t i = 0; i < n; ++i) { const bool ok = T.by_name(names[i].c_str()) == T.ent(tokens[i]); all &= ok; if (!ok) { ++bad; if (first_bad == n) first_bad = i; } }
		checkf(all, "%u: %u distinct names registered and found (bad %u, first %u: by_name %p ent %p valid %d, nodes active %u)", n, n, bad, first_bad, first_bad < n ? T.by_name(names[first_bad].c_str()) : nullptr, first_bad < n ? T.ent(tokens[first_bad]) : nullptr, first_bad < n ? T.valid(tokens[first_bad]) : -1,
		       *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(T.self) + L->name_nodes.pool.active));
		T.register_name("one-too-many", tokens[0]);
		st = counters(T.self);
		checkf(T.by_name("one-too-many") == nullptr && st.name_node_exhaustion_refusals == 1 && st.name_map_full_refusals == 0, "%u: name node exhaustion refused and counted (map-full check unreachable: every mapped name holds a node)", n);
		for (std::uint32_t i = 0; i < n; ++i) T.unregister_name(names[i].c_str(), tokens[i]);
		checkf(T.by_name("n7") == nullptr, "%u: all names unregistered", n);
		// the ordered map under random insert/erase: 200 names, 3000 random register/unregister operations, every name
		// checked after each
		{
			lcg rr{7u}; std::vector<bool> present(200, false);
			auto check_all = [&]() { for (std::uint32_t i = 0; i < 200; ++i) { void* e = T.by_name(names[i].c_str()); if ((e != nullptr) != present[i]) return int(i); } return -1; };
			int first_lost = -1;
			for (int op = 0; op < 3000 && first_lost < 0; ++op)
			{
				const auto i = rr.next() % 200;
				if (present[i]) { T.unregister_name(names[i].c_str(), tokens[i]); present[i] = false; }
				else { T.register_name(names[i].c_str(), tokens[i]); present[i] = true; }
				first_lost = check_all();
			}
			checkf(first_lost < 0, "%u: random name insert/erase keeps every name findable (first lost %d)", n, first_lost);
			for (std::uint32_t i = 0; i < 200; ++i) if (present[i]) T.unregister_name(names[i].c_str(), tokens[i]);
		}
		T.register_name("again", tokens[0]);
		checkf(T.by_name("again") == T.ent(tokens[0]), "%u: the pool recovers after unregistration", n);
		T.unregister_name("again", tokens[0]);
	}
	// hierarchy records
	{
		T.relate("Boss", "minion1"); T.relate("boss", "Minion2"); T.relate("boss", "minion3"); T.relate("boss", "minion4"); T.relate("boss", "minion5");
		T.name(tokens[20], "minion1");
		entity* m1 = T.ent(tokens[20]);
		std::uint32_t boss_id = 0; name_hash_stub(&boss_id, nullptr, "boss");
		checkf(m1->parent_id == boss_id, "%u: registering a child applies the parent id through slot 0x68", n);
		T.name(tokens[21], "minion5");
		checkf(T.ent(tokens[21])->parent_id == boss_id, "%u: the fifth child still records its parent", n);
		T.name(tokens[22], "minion2");
		const auto mark = S.log.size();
		T.name(tokens[23], "boss");
		int set_parent = 0; for (std::size_t k = mark; k < S.log.size(); ++k) if (S.log[k].find(".slot68(") != std::string::npos) ++set_parent;
		std::uint32_t minion1_id = 0; name_hash_stub(&minion1_id, nullptr, "minion1");
		checkf(set_parent == 2 && m1->parent_id == boss_id && T.ent(tokens[22])->parent_id == boss_id, "%u: registering the parent re-parents the two present children of its four-name record (not the dropped fifth) [set_parent %d, m1 %08x, m2 %08x, boss %08x]", n, set_parent, m1->parent_id, T.ent(tokens[22])->parent_id, boss_id);
		for (std::size_t k = mark; k < S.log.size(); ++k) std::printf("    log: %s\n", S.log[k].c_str());
		T.unregister_name("minion1", tokens[20]); T.unregister_name("minion5", tokens[21]); T.unregister_name("minion2", tokens[22]); T.unregister_name("boss", tokens[23]);
		// full refusal: the map takes at most n records; relate needs room for two
		std::uint32_t used = 2 + 4; // boss + minion1..5 records (6) after the five relates
		bool refused = false;
		for (std::uint32_t k = 0; used + 2 <= n + 2 && k < n; ++k)
		{
			char a[32], b[32]; std::snprintf(a, sizeof a, "pa%u", k); std::snprintf(b, sizeof b, "ch%u", k);
			const auto before = counters(T.self).hierarchy_full_refusals;
			T.relate(a, b);
			if (counters(T.self).hierarchy_full_refusals != before) { refused = true; break; }
			used += 2;
		}
		checkf(refused && counters(T.self).hierarchy_full_refusals == 1, "%u: hierarchy relate refused when active + 2 > capacity", n);
		checkf(T.version() == 0, "%u: hierarchy version starts at 0", n);
		T.set_version(0x5a5a); checkf(T.version() == 0x5a5a, "%u: hierarchy version set/get", n);
	}
	// properties
	{
		const auto e = tokens[9] & (n - 1);
		std::uint32_t rec[4] = {1u | (1u << 31), 1u, 0, 1u << 15}; // properties 0, 31, 32, 111
		T.property_update(tokens[9], rec);
		checkf(T.members(0)[e >> 5] == (1u << (e & 31)) && T.members(31)[e >> 5] == (1u << (e & 31)) && T.members(32)[e >> 5] == (1u << (e & 31)) && T.members(111)[e >> 5] == (1u << (e & 31)) && T.members(1)[e >> 5] == 0,
		       "%u: property deltas set the entity's bit in sets 0/31/32/111", n);
		checkf(std::memcmp(T.record(e), rec, 16) == 0, "%u: the 16-byte record is stored", n);
		std::uint32_t rec2[4] = {1u, 0, 0, 0};
		T.property_update(tokens[9], rec2);
		checkf(T.members(0)[e >> 5] == (1u << (e & 31)) && T.members(31)[e >> 5] == 0 && T.members(111)[e >> 5] == 0, "%u: deltas clear the bits that went away", n);
		checkf(T.members(111) == T.members(0) + 111 * (n / 32) && T.record(5) == T.record(0) + 20, "%u: set/record pointer arithmetic (stride %u words / 16 bytes)", n, n / 32);
		T.release(tokens[9]);
		checkf(T.members(0)[e >> 5] == 0 && T.record(e)[0] == 0, "%u: release clears the record and the membership bits", n);
		tokens[9] = T.alloc();
	}
	// relationships
	{
		const auto a = tokens[30], b = tokens[31], c = tokens[32], v = tokens[40];
		T.rel_set(0, a, v); T.rel_set(0, b, v); T.rel_set(0, c, v);
		checkf(T.rel_get(0, a) == v && T.rel_get(0, v) == 0 && T.rel_get(0, 0) == 0 && T.rel_get(0, 0x7f000001) == 0, "%u: relation_get", n);
		std::uint32_t set[32]; std::memset(set, 0xff, sizeof set);
		checkf(T.rel_members(0, v, set) && set[0] == ((1u << 30) | (1u << 31)) && set[1] == 1u && set[2] == 0, "%u: relation_members = {30, 31, 32}", n);
		checkf(T.rel_begin(0, v) == c && T.rel_next(0) == b && T.rel_next(0) == a && T.rel_next(0) == 0, "%u: enumeration is most-recent first", n);
		std::uint32_t* ret = nullptr; std::uint32_t first = T.rel_begin(0, a, &ret);
		checkf(first == 0 && ret != nullptr, "%u: begin on an entity without members gives 0 and returns the out pointer", n);
		std::memset(set, 0xff, sizeof set);
		checkf(!T.rel_members(0, a, set) && set[0] == 0xffffffffu, "%u: relation_members with no first member returns 0 and leaves the set untouched (native)", n);
		T.rel_set(0, b, tokens[41]);
		checkf(T.rel_begin(0, v) == c && T.rel_next(0) == a && T.rel_next(0) == 0 && T.rel_begin(0, tokens[41]) == b, "%u: changing a value moves the entity between lists", n);
		T.rel_set(0, b, tokens[41]);
		checkf(T.rel_begin(0, tokens[41]) == b && T.rel_next(0) == 0, "%u: setting the same value is a no-op", n);
		T.rel_set(0, c, 0);
		checkf(T.rel_get(0, c) == 0 && T.rel_begin(0, v) == a && T.rel_next(0) == 0, "%u: value 0 unlinks", n);
		T.release(v);
		checkf(T.rel_get(0, a) == 0, "%u: releasing the value clears its members' values", n);
		tokens[40] = T.alloc();
		T.release(b);
		checkf(T.rel_begin(0, tokens[41]) == 0, "%u: releasing a member unlinks it", n);
		tokens[31] = T.alloc();
		T.rel_set(1, a, tokens[41]);
		checkf(T.rel_get(0, a) == 0 && counters(T.self).relation_table_refusals == 1 && T.rel_get(2, a) == 0, "%u: a table index other than 0 is refused", n);
		invariant(T, "relations");
	}
	// grid update
	{
		const auto mark = S.log.size();
		T.grid_update(tokens[12]);
		const auto* e = T.ent(tokens[12]);
		char expect[64]; std::snprintf(expect, sizeof expect, "grid.update(%08x,[%g..%g])", tokens[12], double(e->bounds[0]), double(e->bounds[5]));
		bool ok = false; for (std::size_t k = mark; k < S.log.size(); ++k) ok |= S.log[k] == expect;
		checkf(ok, "%u: grid_update hands slot 0x44's bounds to Grid slot 0x28", n);
		const auto mark2 = S.log.size(); T.grid_update(0x7e000000u);
		checkf(S.log.size() == mark2, "%u: grid_update of an invalid token does nothing", n);
	}
	// allocator null: size 0 makes the stub refuse
	{
		const auto before = counters(T.self);
		std::uint32_t out = 0; T.release(tokens[13]);
		void* e = T.allocate(&out, 0);
		const auto after = counters(T.self);
		checkf(e == nullptr && after.live == before.live - 1 && after.free_slots == before.free_slots + 1 && after.allocator_null_refusals == 1, "%u: allocator null is a refusal with the world unchanged", n);
		tokens[13] = T.alloc();
		checkf(tokens[13] == seed_token(T, 2, 13), "%u: the slot was not consumed by the null allocation", n);
	}
	// reset: double advancement, ascending ring, counts; allocation inside reset refused and counted
	{
		T.release(tokens[50]); T.release(tokens[60]); // free slots advance once in reset (now at generation 2)
		S.allocate_in_destroy = true;
		T.name(tokens[70], "survivor");
		T.reset();
		invariant(T, "reset");
		st = counters(T.self);
		checkf(st.live == 0 && st.free_slots == n && st.retired == 0 && st.resets == 1 && !st.resetting, "%u: after reset live %u free %u retired %u", n, st.live, st.free_slots, st.retired);
		checkf(st.reset_time_attempts == 1 && st.reset_failure_latched && st.refusals == 3, "%u: the allocation during reset was refused, counted and latched", n);
		checkf(S.grid_clears == 1, "%u: reset cleared the Grid (slot 0)", n);
		checkf(T.by_name("survivor") == nullptr && T.ent(tokens[70]) == nullptr, "%u: names and tokens are gone after reset", n);
		bool asc = true;
		std::vector<std::uint32_t> fresh;
		for (std::uint32_t i = 0; i < n; ++i) { const auto t = T.alloc(); fresh.push_back(t); asc &= (t & (n - 1)) == i; }
		checkf(asc, "%u: the rebuilt ring is ascending 0..%u", n, n - 1);
		// occupied slots advanced twice (gen 1 -> 3), the three freed ones (5, 3, 9 ... now at 2) -> 3 too; 50/60 were at generation 2 free -> 3
		checkf(fresh[0] == seed_token(T, 3, 0) && fresh[50] == seed_token(T, 3, 50) && fresh[60] == seed_token(T, 3, 60), "%u: occupied slots advanced twice through reset, free ones once (%08x %08x %08x)", n, fresh[0], fresh[50], fresh[60]);
		checkf(fresh[5] == seed_token(T, 4, 5) && fresh[11] == seed_token(T, 4, 11), "%u: slots 5 and 11 (generation 2 live) advanced to 4 (%08x %08x)", n, fresh[5], fresh[11]);
		tokens = fresh;
		checkf(T.version() == 0x5a5a, "%u: the hierarchy version survives reset (native)", n);
		std::uint32_t set[32];
		checkf(!T.named("minion1", set), "%u: name map cleared by reset", n);
		T.relate("x", "y"); T.name(tokens[2], "y");
		std::uint32_t x_id = 0; name_hash_stub(&x_id, nullptr, "x");
		checkf(T.ent(tokens[2])->parent_id == x_id, "%u: the hierarchy map was cleared by reset and accepts new records", n);
	}
	// retirement: a slot at generation 0x3fff retires on release and never returns; 0x00ffffff at 1024
	{
		const std::uint32_t i = n - 1;
		auto* h = poke_handle(T, i);
		T.release(tokens[i]);
		*h = (generation_max << bits) | i; // the last generation that may be minted
		const auto t = T.alloc();
		checkf(t == ((generation_max << bits) | i) && (n != 1024 || t == 0x00ffffffu), "%u: the largest minted token is %08x", n, t);
		checkf(T.valid(t) && T.by_token(t) != nullptr, "%u: generation 0x3fff validates", n);
		checkf(!T.valid(t + (1u << bits)) && !T.valid(t - (1u << bits)), "%u: other generations of that slot do not", n);
		T.release(t);
		invariant(T, "retiring release");
		st = counters(T.self);
		checkf(st.retired == 1 && st.retired_by_release == 1 && st.free_slots == 0 && st.live == n - 1 && st.max_generation == generation_max, "%u: the slot retired (retired %u free %u live %u maxgen %x)", n, st.retired, st.free_slots, st.live, st.max_generation);
		checkf(!T.valid(t) && T.by_token(t) == nullptr && T.handle_of(i) == 0, "%u: the retired slot's token is invalid", n);
		std::uint32_t out = 0; checkf(T.allocate(&out, 64) == nullptr && counters(T.self).full_refusals == 2, "%u: allocation refused with free 0 although live < capacity", n);
		T.release(tokens[0]);
		checkf(T.alloc() == seed_token(T, 4, 0), "%u: slot 0 recycles, the retired slot does not", n);
		tokens[0] = (4u << bits);
		T.reset();
		invariant(T, "reset with a retired slot");
		st = counters(T.self);
		checkf(st.retired == 1 && st.free_slots == n - 1 && st.live == 0, "%u: reset preserves the retired slot (free %u)", n, st.free_slots);
		bool never = true; std::vector<std::uint32_t> all;
		for (std::uint32_t k = 0; k + 1 < n; ++k) { const auto tk = T.alloc(); all.push_back(tk); never &= (tk & (n - 1)) != i; }
		checkf(never && T.alloc() == 0, "%u: %u allocations never hand out the retired slot; the next is refused", n, n - 1);
		for (auto tk : all) T.release(tk);
		invariant(T, "release all with a retired slot");
		// a reset where a slot retires during the all-slot advancement
		auto* h2 = poke_handle(T, 7); *h2 = (generation_max << bits) | 7;
		T.reset();
		st = counters(T.self);
		checkf(st.retired == 2 && st.retired_by_reset == 1 && st.free_slots == n - 2, "%u: reset retires a free slot at the last generation (retired %u)", n, st.retired);
		invariant(T, "reset retiring");
	}
	// test fixture retirement (P3.4)
	{
		checkf(retire_slot(T.self, 9) && counters(T.self).retired == 3 && counters(T.self).retired_by_test == 1, "%u: retire_slot on a free slot", n);
		checkf(!retire_slot(T.self, 9) && !retire_slot(T.self, n), "%u: retire_slot refuses a retired / out-of-range slot", n);
		const auto t = T.alloc();
		checkf(!retire_slot(T.self, t & (n - 1)), "%u: retire_slot refuses a live slot", n);
		invariant(T, "retire_slot");
		bool never = true; std::vector<std::uint32_t> all{t};
		for (std::uint32_t k = 0; k + 4 < n; ++k) { const auto tk = T.alloc(); all.push_back(tk); never &= (tk & (n - 1)) != 9; }
		checkf(never && T.alloc() == 0, "%u: the test-retired slot is never handed out", n);
		for (auto tk : all) T.release(tk);
		invariant(T, "after fixture retirement");
	}
	// slot 0 with the delete flag: recorded, nothing freed
	{
		T.dtor(1);
		checkf(counters(T.self).delete_flag_seen && layout_of(T.self) != nullptr, "%u: slot 0 with the delete flag is recorded and the object survives", n);
	}
	destroy(T.self);
	for (auto* e : S.entities) delete e;
	checkf(true, "%u: destroyed", n);
}

// ---- differential -------------------------------------------------------------------------------------------------------
std::vector<std::uint8_t> binary;
std::size_t file_offset(std::uint32_t va)
{
	auto u16 = [](std::size_t o) { std::uint16_t v; std::memcpy(&v, binary.data() + o, 2); return v; };
	auto u32 = [](std::size_t o) { std::uint32_t v; std::memcpy(&v, binary.data() + o, 4); return v; };
	const auto pe = u32(0x3c); const auto sections = pe + 24 + u16(pe + 20);
	for (unsigned i = 0; i < u16(pe + 6); ++i) { const auto row = sections + i * 40; const auto rva = u32(row + 12); if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(row + 16)) return u32(row + 20) + va - 0x400000 - rva; }
	return binary.size();
}
void* at(std::uint32_t va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }
bool native_bytes(std::uint32_t va, std::size_t n)
{
	const auto offset = file_offset(va); if (offset + n > binary.size()) return false;
	std::memcpy(at(va), binary.data() + offset, n); return true;
}
void put_jmp(std::uint32_t va, const void* target) { auto* c = static_cast<std::uint8_t*>(at(va)); c[0] = 0xe9; const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target) - (va + 5)); std::memcpy(c + 1, &rel, 4); }
void put_ret(std::uint32_t va) { *static_cast<std::uint8_t*>(at(va)) = 0xc3; }
// Retarget every E8 rel32 in [lo, hi) whose target is `from` to `to`; returns the count.
int retarget_calls(std::uint32_t lo, std::uint32_t hi, std::uint32_t from, const void* to)
{
	int count = 0;
	for (auto va = lo; va + 5 <= hi; ++va)
	{
		auto* c = static_cast<std::uint8_t*>(at(va));
		if (c[0] != 0xe8) continue;
		std::int32_t rel; std::memcpy(&rel, c + 1, 4);
		if (static_cast<std::uint32_t>(va + 5 + rel) != from) continue;
		const auto nrel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (va + 5));
		std::memcpy(c + 1, &nrel, 4); ++count; va += 4;
	}
	return count;
}

// stand-ins for the helpers the fixture cannot hold (below the fixture base) or must not run
void* __cdecl memcpy_stub(void* dst, const void* src, std::size_t n) { std::memcpy(dst, src, n); return dst; }
void __stdcall array_ctor_stub(void* p, int stride, int count, std::uint32_t ctor) // 0x401600: calls `ctor` (thiscall) on each element
{
	for (int i = 0; i < count; ++i)
	{
		auto* e = static_cast<std::uint8_t*>(p) + i * stride;
		if (ctor == 0x419cd0) *reinterpret_cast<std::uint32_t*>(e) = 0; // 0x419cd0: zero the element's first dword
		else reinterpret_cast<void(__fastcall*)(void*, void*)>(static_cast<std::uintptr_t>(ctor))(e, nullptr);
	}
}
void __stdcall eh_vector_ctor_stub(void* p, std::size_t size, int count, void(__fastcall* ctor)(void*, void*), void*) // 0x6722d4 (__ehvec_ctor: callee cleans 0x14, the native caller does not adjust ESP)
{
	for (int i = 0; i < count; ++i) ctor(static_cast<std::uint8_t*>(p) + i * size, nullptr);
}
int __cdecl atexit_stub(void*) { return 0; }
void __fastcall set_color_stub(std::uint32_t* node, void*, std::uint32_t flag) { if (flag & 0xff) *node |= 0x40000000u; else *node &= 0xbfffffffu; } // 0x417f90

constexpr std::uint32_t fixture_lo = 0x4654b0; // lowest native code the fixture holds (0x4654b0/0x4654d0 token helpers)
struct code_range { std::uint32_t lo, hi; };
const code_range native_code[] = {
	{native_region_lo, native_region_hi}, {0x4654b0, 0x4654e8}, {0x482ab0, 0x482ac7}, {0x483080, 0x48308c}, {0x483410, 0x48343c}, {0x4c1bf0, 0x4c1c30},
	{0x597b30, 0x597b85}, {0x59eb10, 0x59eb13}, {0x5ab7d0, 0x5ab80d}, {0x5d7940, 0x5d794e}, {0x6015a0, 0x602080}};

bool build_fixture()
{
	DWORD old; if (!VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old)) return false;
	bool ok = true;
	for (const auto& r : native_code) ok &= native_bytes(r.lo, r.hi - r.lo);
	ok &= native_bytes(0x68f7a8, 0x68f874 - 0x68f7a8); // the four map vtables and the World vtable
	ok &= native_bytes(0x68f73c, 12);                  // the base vtable the destructor restores
	if (!ok) return false;
	std::uint32_t slot0; std::memcpy(&slot0, at(native_vtable), 4);
	if (slot0 != 0x4c7f00) return false;
	// helpers below the fixture: retarget their call sites
	int n1 = 0, n2 = 0, n3 = 0, n4 = 0, n5 = 0;
	for (const auto& r : native_code)
	{
		n1 += retarget_calls(r.lo, r.hi, 0x4015d0, reinterpret_cast<const void*>(&memcpy_stub));
		n2 += retarget_calls(r.lo, r.hi, 0x401600, reinterpret_cast<const void*>(&array_ctor_stub));
		n3 += retarget_calls(r.lo, r.hi, 0x417f90, reinterpret_cast<const void*>(&set_color_stub));
		n4 += retarget_calls(r.lo, r.hi, 0x41aa80, reinterpret_cast<const void*>(&name_of_id_stub));
		n5 += retarget_calls(r.lo, r.hi, 0x41ab70, reinterpret_cast<const void*>(&name_hash_stub));
	}
	std::printf("fixture: retargeted memcpy %d, array ctor %d, set-colour %d, name-of-id %d, name-hash %d call sites\n", n1, n2, n3, n4, n5);
	if (n1 < 4 || n2 != 1 || n3 < 1 || n4 != 2 || n5 < 8) return false;
	// helpers inside the fixture that must not run natively
	put_jmp(0x465470, reinterpret_cast<const void*>(&grid_getter_stub));
	put_jmp(0x461910, reinterpret_cast<const void*>(&allocator_getter_stub));
	put_jmp(0x5602b0, reinterpret_cast<const void*>(&fold_case_stub));
	put_jmp(0x602280, reinterpret_cast<const void*>(&name_table_stub));
	put_jmp(0x6722d4, reinterpret_cast<const void*>(&eh_vector_ctor_stub));
	put_jmp(0x67211e, reinterpret_cast<const void*>(&atexit_stub));
	put_ret(0x672161); // __security_check_cookie
	{ auto* c = static_cast<std::uint8_t*>(at(0x672274)); c[0] = 0xc2; c[1] = 0x10; c[2] = 0x00; } // _eh_vector_destructor_iterator_: ret 0x10 (not reached)
	return true;
}

bool same_log(side& A, side& B, const char* step)
{
	if (A.log == B.log) { A.log.clear(); B.log.clear(); return true; }
	std::printf("  log mismatch at %s: port %zu lines, native %zu lines\n", step, A.log.size(), B.log.size());
	for (std::size_t k = 0; k < A.log.size() || k < B.log.size(); ++k)
		std::printf("    %-40s | %s\n", k < A.log.size() ? A.log[k].c_str() : "-", k < B.log.size() ? B.log[k].c_str() : "-");
	A.log.clear(); B.log.clear();
	return false;
}

void differential()
{
	std::printf("---- differential at 256 ----\n");
	if (!build_fixture()) { std::puts("FAIL fixture: the executable's World region/helpers could not be placed"); ++failures; return; }
	check(true, "fixture built from the retail executable");
	side SP, SN; init_side(SP); init_side(SN);
	native_hooks.grid_getter = &grid_getter_stub; native_hooks.allocator_getter = &allocator_getter_stub;
	native_hooks.name_of_id = &name_of_id_stub; native_hooks.name_hash = &name_hash_stub; native_hooks.fold_case = &fold_case_stub;
	target P; P.S = &SP; P.port = true; P.n = 256; P.self = create(stock_descriptor); SP.world = P.self;
	target N; N.S = &SN; N.port = false; N.n = 256;
	current_side = &SN;
	std::printf("fixture: calling the native getter\n");
	N.self = reinterpret_cast<void*(__cdecl*)()>(static_cast<std::uintptr_t>(0x4c7f20))(); // the native getter constructs the static
	std::printf("fixture: native getter returned %p\n", N.self);
	SN.world = N.self;
	checkf(N.self == at(native_object) && *static_cast<std::uint32_t*>(N.self) == native_vtable, "native getter constructed the static at %08x", native_object);
	if (!P.self || N.self != at(native_object)) return;
	// the native constructor's layout against the port's at 256
	{
		const auto* L = layout_of(P.self);
		auto nat = [&](std::uint32_t off) { return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(N.self) + off); };
		auto prt = [&](std::uint32_t off) { return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(P.self) + off); };
		bool same = nat(L->core.mask) == 0xff && nat(L->core.shift) == 8 && nat(L->core.free_count) == 256 && nat(L->core.enqueue) == 0 && nat(L->core.dequeue) == 0;
		for (std::uint32_t i = 0; i < 256; ++i) same &= nat(L->core.handles + i * 4) == prt(L->core.handles + i * 4) && nat(L->core.free_ring + i * 4) == prt(L->core.free_ring + i * 4);
		same &= nat(L->names.root) == map_null && nat(L->names.last) == 0xffffffffu && nat(L->names.nodes.count) == 256 && nat(L->hierarchy.root) == map_null && nat(L->hierarchy.nodes.count) == 256;
		same &= nat(L->name_nodes.mask) == 0xff && nat(L->name_nodes.shift) == 8 && nat(L->relations.nodes.mask) == 0xff && nat(L->relations.nodes.shift) == 8;
		for (std::uint32_t i = 0; i < 256; ++i) same &= nat(L->name_nodes.gens + i * 4) == prt(L->name_nodes.gens + i * 4) && nat(L->relations.nodes.gens + i * 4) == prt(L->relations.nodes.gens + i * 4);
		check(same, "the native constructor's core/map/pool words equal the port's at 256 (layout confirmed)");
		check(std::memcmp(static_cast<const std::uint8_t*>(N.self) + L->relations.base, static_cast<const std::uint8_t*>(P.self) + L->relations.base, L->relations.size) == 0, "the relationship table is byte-identical after construction");
	}
	// the scripted sequence
	lcg r{20261011u};
	std::vector<std::uint32_t> live;      // tokens (identical on both sides)
	std::vector<std::uint32_t> dead;      // stale tokens
	const char* names[] = {"alpha", "Beta", "gamma", "DELTA", "eps", "zeta", "eta", "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega"};
	constexpr int name_count = sizeof names / sizeof names[0];
	int steps = 0, mismatches = 0, resets = 0, full_refusals = 0;
	std::vector<std::pair<std::string, std::uint32_t>> listed; // (folded name, token) pairs the registry holds (both sides)
	auto fold = [](const char* nm) { std::string f = nm; for (auto& ch : f) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a'); return f; };
	auto unlist = [&](const std::string& nm, std::uint32_t t) { for (auto it = listed.begin(); it != listed.end(); ++it) if (it->first == nm && it->second == t) { listed.erase(it); return; } };
	auto name_nodes_active = [&]() { const auto* L = layout_of(P.self); return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(P.self) + L->name_nodes.pool.active); };
	auto rnd_live = [&]() -> std::uint32_t { return live.empty() ? 0 : live[r.next() % live.size()]; };
	auto rnd_any = [&]() -> std::uint32_t {
		const int k = r.range(0, 9);
		if (k < 6 && !live.empty()) return rnd_live();
		if (k < 8 && !dead.empty()) return dead[r.next() % dead.size()];
		const std::uint32_t pool[] = {0, 1, 255, 256, 257, 0x101, 0x501, 0x7f0000ff, 0xffffffff, 0x100, 0x1ff};
		return pool[r.next() % (sizeof pool / sizeof pool[0])];
	};
	// The hierarchy records compared by lookup: the retail find (0x5ab7d0, thiscall(map, &key, root) -> node index) walks
	// the port's map too (same node field layout), so both records are fetched the native way.
	auto native_find = [](const std::uint8_t* map, std::uint32_t key) {
		using fn = std::uint32_t(__fastcall*)(const void*, void*, const std::uint32_t*, std::uint32_t);
		return reinterpret_cast<fn>(static_cast<std::uintptr_t>(0x5ab7d0))(map, nullptr, &key, *reinterpret_cast<const std::uint32_t*>(map + 4));
	};
	auto compare_hierarchy = [&](const char* step) {
		const auto* L = layout_of(P.self);
		const auto* a = static_cast<const std::uint8_t*>(P.self) + L->hierarchy.base; const auto* b = static_cast<const std::uint8_t*>(N.self) + L->hierarchy.base;
		bool ok = true;
		for (const char* nm : names)
		{
			std::uint32_t id = 0; name_hash_stub(&id, nullptr, fold(nm).c_str());
			const auto ip = native_find(a, id), in = native_find(b, id);
			if ((ip == map_null) != (in == map_null)) { std::printf("  hierarchy mismatch at %s: %s present port %d native %d\n", step, nm, ip != map_null, in != map_null); ok = false; continue; }
			if (ip == map_null) continue;
			const auto* rp = a + 0x1444 + ip * 20; const auto* rn = b + 0x1444 + in * 20;
			if (std::memcmp(rp, rn, 20) != 0)
			{
				ok = false;
				std::printf("  hierarchy mismatch at %s: %s record port", step, nm);
				for (int k = 0; k < 5; ++k) std::printf(" %08x", reinterpret_cast<const std::uint32_t*>(rp)[k]);
				std::printf(" native");
				for (int k = 0; k < 5; ++k) std::printf(" %08x", reinterpret_cast<const std::uint32_t*>(rn)[k]);
				std::printf("\n");
			}
		}
		return ok;
	};
	// The name registry compared by lookup: presence and the head node token of each name's list (the name node pool
	// itself is compared byte for byte below, so equal heads mean equal lists).
	auto compare_names = [&](const char* step) {
		const auto* L = layout_of(P.self);
		const auto* a = static_cast<const std::uint8_t*>(P.self) + L->names.base; const auto* b = static_cast<const std::uint8_t*>(N.self) + L->names.base;
		bool ok = true;
		for (const char* nm : names)
		{
			std::uint32_t id = 0; name_hash_stub(&id, nullptr, fold(nm).c_str());
			const auto ip = native_find(a, id), in = native_find(b, id);
			if ((ip == map_null) != (in == map_null)) { std::printf("  name mismatch at %s: %s present port %d native %d\n", step, nm, ip != map_null, in != map_null); ok = false; continue; }
			if (ip == map_null) continue;
			const auto hp = *reinterpret_cast<const std::uint32_t*>(a + 0x1444 + ip * 4), hn = *reinterpret_cast<const std::uint32_t*>(b + 0x1444 + in * 4);
			if (hp != hn) { std::printf("  name mismatch at %s: %s head port %08x native %08x\n", step, nm, hp, hn); ok = false; }
		}
		return ok;
	};
	auto compare_memory = [&](const char* step) {
		const auto* L = layout_of(P.self);
		const auto* a = static_cast<const std::uint8_t*>(P.self); const auto* b = static_cast<const std::uint8_t*>(N.self);
		bool ok = std::memcmp(a + L->core.free_ring, b + L->core.free_ring, L->core.shift + 4 - L->core.free_ring) == 0;         // ring, counts, bitmap, handles, mask, shift
		for (std::uint32_t i = 0; i < 256; ++i) ok &= (reinterpret_cast<void* const*>(a + L->core.entities)[i] == nullptr) == (reinterpret_cast<void* const*>(b + L->core.entities)[i] == nullptr);
		ok &= std::memcmp(a + L->name_nodes.pool.elems, b + L->name_nodes.pool.elems, L->name_nodes.size) == 0;                       // name node pool (same allocation order)
		ok &= std::memcmp(a + L->relations.base, b + L->relations.base, L->relations.size) == 0;                                      // the relationship table
		ok &= std::memcmp(a + L->properties, b + L->properties, L->size - L->properties) == 0;                                        // records, reserved, membership sets
		ok &= std::memcmp(a + L->hierarchy_version, b + L->hierarchy_version, 4) == 0;
		ok &= *reinterpret_cast<const std::uint32_t*>(a + L->names.nodes.active) == *reinterpret_cast<const std::uint32_t*>(b + L->names.nodes.active);
		ok &= *reinterpret_cast<const std::uint32_t*>(a + L->hierarchy.nodes.active) == *reinterpret_cast<const std::uint32_t*>(b + L->hierarchy.nodes.active);
		if (!ok) { std::printf("  memory mismatch at %s\n", step); ++mismatches; }
		return ok;
	};
	for (int step = 0; step < 6000; ++step)
	{
		char label[96];
		const int k = r.range(0, 99);
		bool ok = true;
		if (k < 30)
		{
			const std::uint32_t size = r.range(0, 20) == 0 ? 0 : std::uint32_t(r.range(16, 400));
			std::uint32_t tp = 0xabcdef, tn = 0xabcdef;
			void* ep = P.allocate(&tp, size); void* en = N.allocate(&tn, size);
			std::snprintf(label, sizeof label, "step %d allocate(%u) -> %08x/%08x", step, size, tp, tn);
			ok = (ep == nullptr) == (en == nullptr) && tp == tn;
			if (ep) { static_cast<entity*>(ep)->token = tp; static_cast<entity*>(en)->token = tn; live.push_back(tp); }
			else if (size != 0 && live.size() == 256) ++full_refusals;
		}
		else if (k < 48)
		{
			const auto t = rnd_any();
			if (auto* e = static_cast<entity*>(P.by_token(t))) if (e->name_id) unlist(fold(shared_names->text_of(e->name_id)), t);
			P.release(t); N.release(t);
			std::snprintf(label, sizeof label, "step %d release(%08x)", step, t);
			for (auto it = live.begin(); it != live.end(); ++it) if (*it == t) { live.erase(it); dead.push_back(t); break; }
		}
		else if (k < 58 && name_nodes_active() < 200)
		{
			const auto t = rnd_any(); const char* nm = names[r.next() % name_count];
			std::uint32_t id = 0; name_hash_stub(&id, nullptr, nm);
			if (auto* e = static_cast<entity*>(P.by_token(t))) { if (e->name_id) unlist(fold(shared_names->text_of(e->name_id)), t); e->name_id = id; }
			if (auto* e = static_cast<entity*>(N.by_token(t))) e->name_id = id;
			P.register_name(nm, t); N.register_name(nm, t);
			bool present = false; for (const auto& pr : listed) present |= pr.first == fold(nm) && pr.second == t;
			if (!present) listed.emplace_back(fold(nm), t);
			std::snprintf(label, sizeof label, "step %d register(%s, %08x)", step, nm, t);
			if (SP.log != SN.log) // debug: the record of the registered name and both sides' lookups of its children
			{
				const auto* L = layout_of(P.self);
				const auto* hb = static_cast<const std::uint8_t*>(N.self) + L->hierarchy.base;
				std::uint32_t nid = 0; name_hash_stub(&nid, nullptr, fold(nm).c_str());
				const auto in = native_find(hb, nid);
				std::printf("  debug %s: record %s", label, in == map_null ? "absent" : "");
				if (in != map_null)
				{
					const auto* rec = reinterpret_cast<const std::uint32_t*>(hb + 0x1444 + in * 20);
					for (int c = 0; c < 5; ++c) std::printf(" %08x(%s)", rec[c], shared_names->text_of(rec[c]));
					std::printf("\n");
					for (int c = 1; c < 5 && rec[c]; ++c)
					{
						const char* cn = shared_names->text_of(rec[c]);
						auto* ep = static_cast<entity*>(P.by_name(cn)); auto* en = static_cast<entity*>(N.by_name(cn));
						std::printf("    child %s: port e%u native e%u\n", cn, ep ? ep->serial : 0, en ? en->serial : 0);
					}
					auto* rp = static_cast<entity*>(P.by_token(t)); auto* rn = static_cast<entity*>(N.by_token(t));
					std::printf("    registering: port e%u native e%u\n", rp ? rp->serial : 0, rn ? rn->serial : 0);
					for (auto* e : SN.entities) if (e && (e->serial == 4 || e->serial == 6)) std::printf("    native e%u: token %08x name %s\n", e->serial, e->token, shared_names->text_of(e->name_id));
					for (auto* e : SP.entities) if (e && (e->serial == 4 || e->serial == 6)) std::printf("    port e%u: token %08x name %s\n", e->serial, e->token, shared_names->text_of(e->name_id));
				}
				else std::printf("\n");
			}
		}
		else if (k < 63)
		{
			std::uint32_t t; std::string nm;
			if (!listed.empty() && r.range(0, 3) != 0) { const auto& pr = listed[r.next() % listed.size()]; nm = pr.first; t = pr.second; }
			else { t = rnd_any(); nm = names[r.next() % name_count]; }
			P.unregister_name(nm.c_str(), t); N.unregister_name(nm.c_str(), t);
			unlist(fold(nm.c_str()), t);
			std::snprintf(label, sizeof label, "step %d unregister(%s, %08x)", step, nm.c_str(), t);
		}
		else if (k < 68)
		{
			const char* nm = names[r.next() % name_count];
			std::uint32_t sp[8], sn[8]; std::memset(sp, 0xee, sizeof sp); std::memset(sn, 0xee, sizeof sn);
			const bool bp = P.named(nm, sp), bn = N.named(nm, sn);
			void* ep = P.by_name(nm); void* en = N.by_name(nm);
			ok = bp == bn && std::memcmp(sp, sn, sizeof sp) == 0 && (ep == nullptr) == (en == nullptr) && (!ep || static_cast<entity*>(ep)->serial == static_cast<entity*>(en)->serial);
			std::snprintf(label, sizeof label, "step %d named/by_name(%s) -> %d/%d", step, nm, bp, bn);
		}
		else if (k < 72)
		{
			const char* a = names[r.next() % name_count]; const char* b = names[r.next() % name_count];
			P.relate(a, b); N.relate(a, b);
			std::snprintf(label, sizeof label, "step %d relate(%s, %s)", step, a, b);
		}
		else if (k < 80)
		{
			const auto t = rnd_any(); const auto v = r.range(0, 5) == 0 ? 0 : rnd_any();
			P.rel_set(0, t, v); N.rel_set(0, t, v);
			const auto gp = P.rel_get(0, t), gn = N.rel_get(0, t);
			std::uint32_t sp[8], sn[8]; std::memset(sp, 0xee, sizeof sp); std::memset(sn, 0xee, sizeof sn);
			const bool bp = P.rel_members(0, v, sp), bn = N.rel_members(0, v, sn);
			ok = gp == gn && bp == bn && std::memcmp(sp, sn, sizeof sp) == 0;
			std::uint32_t cp = P.rel_begin(0, v), cn = N.rel_begin(0, v);
			for (int guard = 0; guard < 300 && ok && (cp != 0 || cn != 0); ++guard) { ok &= cp == cn; cp = P.rel_next(0); cn = N.rel_next(0); }
			ok &= cp == cn;
			std::snprintf(label, sizeof label, "step %d relation(%08x -> %08x) get %08x/%08x", step, t, v, gp, gn);
		}
		else if (k < 86)
		{
			const auto t = rnd_any();
			std::uint32_t rec[4] = {r.next(), r.next(), r.next(), r.next() & 0xffff};
			if (r.range(0, 3) == 0) rec[1] = rec[2] = 0;
			P.property_update(t, rec); N.property_update(t, rec);
			std::snprintf(label, sizeof label, "step %d property_update(%08x)", step, t);
		}
		else if (k < 90)
		{
			const auto t = rnd_any();
			P.grid_update(t); N.grid_update(t);
			std::snprintf(label, sizeof label, "step %d grid_update(%08x)", step, t);
		}
		else if (k < 97)
		{
			const auto t = rnd_any(); const auto i = r.next() % 256;
			void* ep = P.by_token(t); void* en = N.by_token(t);
			const bool vp = P.valid(t), vn = N.valid(t);
			void* dp = P.by_dense(i); void* dn = N.by_dense(i);
			const auto hp = P.handle_of(i), hn = N.handle_of(i);
			const auto ip = P.index_of(t), in = N.index_of(t);
			void* op = P.entity_of(t); void* on = N.entity_of(t);
			ok = (ep == nullptr) == (en == nullptr) && (!ep || static_cast<entity*>(ep)->serial == static_cast<entity*>(en)->serial) && vp == vn &&
			     (dp == nullptr) == (dn == nullptr) && (!dp || static_cast<entity*>(dp)->serial == static_cast<entity*>(dn)->serial) && hp == hn && ip == in && (op == nullptr) == (on == nullptr);
			std::snprintf(label, sizeof label, "step %d lookups(%08x, dense %u) valid %d/%d handle %08x/%08x", step, t, i, vp, vn, hp, hn);
		}
		else if (k < 99)
		{
			const auto v = r.next();
			P.set_version(v); N.set_version(v);
			ok = P.version() == N.version() && P.version() == v;
			std::snprintf(label, sizeof label, "step %d version(%08x)", step, v);
		}
		else
		{
			if (resets < 6 && step > 500)
			{
				P.reset(); N.reset(); ++resets;
				for (auto t : live) dead.push_back(t);
				live.clear(); listed.clear();
				std::snprintf(label, sizeof label, "step %d reset", step);
			}
			else std::snprintf(label, sizeof label, "step %d (no-op)", step);
		}
		++steps;
		ok &= same_log(SP, SN, label);
		ok &= compare_memory(label);
		ok &= compare_hierarchy(label);
		ok &= compare_names(label);
		if (!ok) { std::printf("FAIL %s\n", label); ++failures; ++mismatches; if (mismatches > 10) { std::puts("too many mismatches, stopping"); break; } }
		if (P.port) { char why[128] = ""; if (!check_invariants(P.self, why, sizeof why)) { std::printf("FAIL invariant at %s: %s\n", label, why); ++failures; break; } }
	}
	checkf(mismatches == 0, "differential: %d scripted steps, %d resets, %d full refusals, %d mismatches (results, cross-owner call logs, core/pool/relation/property memory)", steps, resets, full_refusals, mismatches);
	// end state: counts
	{
		const auto* L = layout_of(P.self);
		const auto nl = *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(N.self) + L->core.live_count);
		const auto st = counters(P.self);
		checkf(nl == st.live && nl == live.size(), "differential: native live %u == port live %u == script %zu", nl, st.live, live.size());
	}
	destroy(P.self);
	for (auto* e : SP.entities) delete e;
	for (auto* e : SN.entities) delete e;
}

LONG WINAPI crash_report(EXCEPTION_POINTERS* info)
{
	const auto* r = info->ExceptionRecord;
	std::printf("CRASH code %08lx at %p (eip %08lx esp %08lx ecx %08lx) access %p\n", r->ExceptionCode, r->ExceptionAddress, info->ContextRecord->Eip, info->ContextRecord->Esp, info->ContextRecord->Ecx,
	            r->NumberParameters > 1 ? reinterpret_cast<void*>(r->ExceptionInformation[1]) : nullptr);
	const auto* sp = reinterpret_cast<const std::uint32_t*>(info->ContextRecord->Esp);
	for (int k = 0; k < 24; ++k) std::printf("  stack[%2d] %08x\n", k, sp[k]);
	std::exit(3);
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	AddVectoredExceptionHandler(1, &crash_report);
	static_assert(offsetof(entity, name_id) == 0x18 && offsetof(entity, token) == 0x1c, "the stub entity keeps the native name id / token offsets");
	init_stub_vtables();
	static name_table table{}; table.gen = 1; shared_names = &table;
	synthetic(256);
	synthetic(1024);
	const int synthetic_checks = checks;
	const char* path = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	bool skipped = true;
	if (!path) std::puts("SKIPPED differential: no retail executable path given (argument 1 or XML2_FIX_TEST_EXE)");
	else
	{
		std::ifstream in(path, std::ios::binary);
		binary.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		const auto begin = reinterpret_cast<std::uintptr_t>(fixture_space), end = begin + sizeof fixture_space;
		if (binary.size() < 0x1000) std::puts("SKIPPED differential: executable unreadable");
		else if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || fixture_lo < begin || 0x781ee8 > end) { std::printf("SKIPPED differential: fixture does not cover the image (fixture %08x-%08x)\n", unsigned(begin), unsigned(end)); ++failures; }
		else { skipped = false; differential(); }
	}
	std::printf("checks %d (synthetic %d, differential %d%s), failures %d\n", checks, synthetic_checks, checks - synthetic_checks, skipped ? " SKIPPED" : "", failures);
	return failures ? 1 : 0;
}
