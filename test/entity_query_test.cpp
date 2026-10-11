// Entity capacity, query owner: synthetic checks at 256 and 1024, and a differential run of the retail query code
// (copied from the exe given on the command line into a fixture at the retail addresses) against the port at 256.
// No game, no game content: the exe is a read-only test input, never committed.
#include <windows.h>
#include <shellapi.h>
#include "entity_query.hpp"
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace entity_capacity_layout;
namespace eq = entity_query;

// The fixture section belongs to the launcher executable (test/entity_query_launch.cpp): this body is a DLL.
std::uint8_t* fixture_begin = nullptr;
std::size_t fixture_size = 0;
bool find_fixture_section()
{
	const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
	if (!image) return false;
	std::uint32_t pe; std::memcpy(&pe, image + 0x3c, 4);
	std::uint16_t count, optional; std::memcpy(&count, image + pe + 6, 2); std::memcpy(&optional, image + pe + 20, 2);
	for (unsigned i = 0; i < count; ++i)
	{
		const auto* row = image + pe + 24 + optional + i * 40;
		if (std::memcmp(row, ".peerfix", 8) != 0) continue;
		std::uint32_t rva, size; std::memcpy(&rva, row + 12, 4); std::memcpy(&size, row + 8, 4);
		fixture_begin = const_cast<std::uint8_t*>(image) + rva;
		fixture_size = size;
		return true;
	}
	return false;
}

int failures = 0;
void check(bool value, const char* message) { std::printf("%s %s\n", value ? "PASS" : "FAIL", message); failures += !value; }
std::uint8_t* at(std::uint32_t va) { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(va)); }
std::uint32_t va_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
bool writable(std::uint32_t va, std::size_t size) { DWORD old; return VirtualProtect(at(va), size, PAGE_EXECUTE_READWRITE, &old) != 0; }
void jump(std::uint32_t site, const void* target)
{
	writable(site, 5);
	auto* code = at(site);
	code[0] = 0xe9;
	const auto delta = static_cast<std::int32_t>(va_of(target) - site - 5);
	std::memcpy(code + 1, &delta, 4);
}
std::uint32_t& g32(std::uint32_t va) { return *reinterpret_cast<std::uint32_t*>(at(va)); }

// ---- the exe image (read-only test input) ------------------------------------------------------------------------
std::vector<std::uint8_t> binary;
std::size_t file_offset(std::uint32_t va)
{
	auto u16 = [](std::size_t o) { std::uint16_t n; std::memcpy(&n, binary.data() + o, 2); return n; };
	auto u32 = [](std::size_t o) { std::uint32_t n; std::memcpy(&n, binary.data() + o, 4); return n; };
	const auto pe = u32(0x3c);
	const auto sections = pe + 24 + u16(pe + 20);
	for (unsigned i = 0; i < u16(pe + 6); ++i)
	{
		const auto row = sections + i * 40;
		const auto rva = u32(row + 12);
		if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(row + 16)) return u32(row + 20) + va - 0x400000 - rva;
	}
	return binary.size();
}
bool native_bytes(std::uint32_t va, std::size_t n)
{
	const auto offset = file_offset(va);
	if (offset + n > binary.size()) return false;
	if (!writable(va, n)) return false;
	std::memcpy(at(va), binary.data() + offset, n);
	return true;
}
// SHA-256 (FIPS 180-4) of the retail query region, compared with round6/guard-union.json "query_trace".
std::string sha256_hex(const std::uint8_t* data, std::size_t len)
{
	static const std::uint32_t k[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
	std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::vector<std::uint8_t> m(data, data + len);
	m.push_back(0x80);
	while (m.size() % 64 != 56) m.push_back(0);
	const std::uint64_t bits = static_cast<std::uint64_t>(len) * 8;
	for (int i = 7; i >= 0; --i) m.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
	auto rotr = [](std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); };
	for (std::size_t off = 0; off < m.size(); off += 64)
	{
		std::uint32_t w[64];
		for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(m[off + i * 4]) << 24) | (std::uint32_t(m[off + i * 4 + 1]) << 16) | (std::uint32_t(m[off + i * 4 + 2]) << 8) | m[off + i * 4 + 3];
		for (int i = 16; i < 64; ++i)
		{
			const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; ++i)
		{
			const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			const auto ch = (e & f) ^ (~e & g);
			const auto t1 = hh + s1 + ch + k[i] + w[i];
			const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			const auto maj = (a & b) ^ (a & c) ^ (b & c);
			const auto t2 = s0 + maj;
			hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	char out[65];
	for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
	return out;
}

// ---- the fixture: World, Grid, collide owner, entities, records, math imports -----------------------------------------
namespace fx
{
	descriptor d = stock_descriptor;
	eq::layout L = eq::layout_for(stock_world);
	[[noreturn]] void fail(const char* what) { std::printf("FAIL fixture: %s\n", what); std::fflush(stdout); std::exit(99); }

	// Entities: +0x1c token, +0x20 position, a vtable with the slots the query code calls.
	struct entity_object
	{
		const void* const* vtable;
		std::uint8_t pad[0x18];
		std::uint32_t token;
		float pos[3];
		float collide_pos[3];
		std::uint8_t material;
		std::uint8_t pad2[3];
		const std::uint8_t* class_record;
		std::uint32_t dense;
	};
	static_assert(offsetof(entity_object, token) == 0x1c && offsetof(entity_object, pos) == 0x20);
	entity_object entities[max_world];
	bool live[max_world];
	std::uint32_t handles[max_world];
	std::uint32_t player_token = 0;
	std::uint8_t class_record_flagged[0x40], class_record_plain[0x40];
	const std::uint8_t* __fastcall entity_class(entity_object* e, void*) { return e->class_record; }
	float* __fastcall entity_position(entity_object* e, void*, float* out) { std::memcpy(out, e->pos, 12); return out; }
	float* __fastcall entity_collide_position(entity_object* e, void*, float* out) { std::memcpy(out, e->collide_pos, 12); return out; }
	std::uint8_t* __fastcall entity_material(entity_object* e, void*) { return &e->material; }
	void __fastcall unexpected_slot(void*, void*) { fail("unexpected vtable slot called"); }
	constexpr std::size_t entity_slots = 0x198 / 4 + 1;
	const void* entity_vtable[entity_slots];

	// Sets handed out by the World/Grid (128 bytes each; only the first set_bytes matter at 256).
	std::uint32_t property_sets[64][set_words];
	std::uint32_t grid_sets[16][set_words];
	void fill_pattern(std::uint32_t* set, std::uint32_t salt)
	{
		for (std::uint32_t i = 0; i < L.world; ++i)
		{
			const bool on = live[i] && ((i * 7 + salt) % 3) != 0;
			if (on) set[i >> 5] |= 1u << (i & 31); else set[i >> 5] &= ~(1u << (i & 31));
		}
	}
	// The World (fixture): the slots the query owner reads, with the installed token layout.
	struct world_object { const void* const* vtable; } world;
	const void* world_vtable[0x60 / 4 + 1];
	entity_object* __fastcall world_find_token(world_object*, void*, std::uint32_t token)
	{
		const auto i = d.tokens.index({token}).value;
		return (i < L.world && live[i] && handles[i] == token) ? &entities[i] : nullptr;
	}
	entity_object* __fastcall world_find_dense(world_object*, void*, std::uint32_t dense) { return (dense < L.world && live[dense]) ? &entities[dense] : nullptr; }
	bool __fastcall world_valid(world_object* w, void*, std::uint32_t token) { return world_find_token(w, nullptr, token) != nullptr; }
	std::uint32_t* __fastcall world_player(world_object*, void*, std::uint32_t* out) { *out = player_token; return out; }
	bool __fastcall world_named_set(world_object*, void*, const char* name, std::uint32_t* out)
	{
		unsigned n = 0;
		if (std::sscanf(name, "ent%u", &n) != 1 || (n & 1)) return false;
		for (std::uint32_t w = 0; w < L.set_words; ++w) out[w] = 0;
		for (auto i : {n, (n + 1) % L.world, 3u}) out[i >> 5] |= 1u << (i & 31);
		return true;
	}
	const std::uint32_t* __fastcall world_property_set(world_object*, void*, std::uint32_t id) { return property_sets[id % 64]; }
	bool __fastcall world_pair_set(world_object*, void*, std::uint32_t a, std::uint32_t b, std::uint32_t* out)
	{
		if (a >= 4) return false;
		for (std::uint32_t w = 0; w < L.set_words; ++w) out[w] = 0;
		for (auto i : {a, b % L.world, (a * 10) % L.world}) out[i >> 5] |= 1u << (i & 31);
		return true;
	}
	void* __cdecl world_getter() { return &world; }
	// The Grid (fixture): slot 0x34 only.
	struct grid_object { const void* const* vtable; } grid;
	const void* grid_vtable[0x40 / 4];
	const std::uint32_t* __fastcall grid_cell_set(grid_object*, void*, std::uint32_t cell) { return grid_sets[cell % 16]; }
	void* __cdecl grid_getter() { return &grid; }
	// The collide owner (fixture): records every call, hashes the set it was handed, inserts traces through the
	// collisions vtable (as the physics code does) and answers slot 0x10 with a handle.
	struct collide_object { const void* const* vtable; } collide;
	const void* collide_vtable[8];
	std::vector<std::uint32_t> collide_log;
	std::uint32_t result_handle_cell = 0;
	std::uint32_t set_hash(const std::uint32_t* set)
	{
		std::uint32_t h = 2166136261u;
		for (std::uint32_t w = 0; w < L.set_words; ++w) { h ^= set[w]; h *= 16777619u; }
		return h;
	}
	std::uint32_t first_live_in(const std::uint32_t* set, std::uint32_t from)
	{
		for (std::uint32_t i = from; i < L.world; ++i) if ((set[i >> 5] >> (i & 31)) & 1u) return i;
		return L.world;
	}
	using insert_fn = std::uint8_t* (__fastcall*)(void* collisions, void* edx, const std::uint32_t* handle);
	std::uint8_t* insert_trace(void* collisions, std::uint32_t dense)
	{
		auto fn = reinterpret_cast<insert_fn>(reinterpret_cast<const void* const*>(*reinterpret_cast<std::uint32_t*>(collisions))[0]);
		g32(eq::global_trace_stamp) = handles[dense]; // the insert copies this global into the record's +0xc (the token the accessors read)
		return fn(collisions, nullptr, &handles[dense]);
	}
	std::uint32_t pool_index(const void* record)
	{
		if (!record) return 0xffffffff;
		return (va_of(record) - eq::native_trace_pool) / eq::trace_record_bytes;
	}
	void __fastcall collide_slot_0(collide_object*, void*, std::uint32_t a, std::uint32_t b, std::uint32_t* set)
	{
		collide_log.insert(collide_log.end(), {0x0, a, b, set_hash(set)});
	}
	void __fastcall collide_slot_4(collide_object*, void*, std::uint32_t a, std::uint32_t b, std::uint32_t* set, std::uint32_t flag)
	{
		collide_log.insert(collide_log.end(), {0x4, a, b, set_hash(set), flag & 0xff});
	}
	void __fastcall collide_slot_8(collide_object*, void*, std::uint32_t a, std::uint32_t b, std::uint32_t* set, std::uint32_t flag)
	{
		collide_log.insert(collide_log.end(), {0x8, a, b, set_hash(set), flag & 0xff});
	}
	void __fastcall collide_slot_c(collide_object*, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t* set, std::uint32_t dd, void* collisions)
	{
		collide_log.insert(collide_log.end(), {0xc, a, b, c, set_hash(set), dd, va_of(collisions) - va_of(set)});
		// the physics code records the player's trace in the query's collisions
		const auto player = d.tokens.index({player_token}).value;
		collide_log.push_back(pool_index(insert_trace(collisions, player)));
	}
	std::uint32_t* __fastcall collide_slot_10(collide_object*, void*, std::uint32_t* out, std::uint32_t a, std::uint32_t b, std::uint32_t c,
	                                          std::uint32_t flags_a, std::uint32_t flags_b, std::uint32_t b0, std::uint32_t b1,
	                                          std::uint32_t* set, void* collisions, std::uint32_t x, std::uint32_t y, std::uint32_t z)
	{
		collide_log.insert(collide_log.end(), {0x10, *out == va_of(set) - L.q_bitmap ? 1u : 0u, set_hash(set), flags_a, flags_b, b0 & 0xff, b1 & 0xff,
		                                       va_of(collisions) - va_of(set), x & 0xff, y & 0xff, z});
		// a / b / c are opaque to the query owner; when one points into the current stack it is a caller-built vector
		// (slot 0x70's scaled direction, slot 0x78's entity position, 0x4a9650's end point): log its three floats'
		// bits (the stack address itself differs between the native and the port frames)
		const auto stack_limit = __readfsdword(8), stack_base = __readfsdword(4);
		for (auto v : {a, b, c})
		{
			if (v >= stack_limit && v + 12 <= stack_base)
			{
				std::uint32_t bits[3];
				std::memcpy(bits, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(v)), 12);
				collide_log.insert(collide_log.end(), {bits[0], bits[1], bits[2]});
			}
			else collide_log.push_back(v);
		}
		// traces for the first two set members (through the collisions vtable, as physics does)
		const auto first = first_live_in(set, 0);
		const auto second = first < L.world ? first_live_in(set, first + 1) : L.world;
		for (auto i : {first, second})
			collide_log.push_back(i < L.world ? pool_index(insert_trace(collisions, i)) : 0xfffffffe);
		result_handle_cell = first < L.world ? handles[first] : 0;
		return &result_handle_cell;
	}
	void* __cdecl collide_getter() { return &collide; }
	// Type records (+4 = class id) and names.
	struct record_object { std::uint32_t pad; std::uint32_t id; } records[8];
	const std::uint8_t* __cdecl record_by_name(const char* name)
	{
		std::uint32_t h = 0;
		for (; *name; ++name) h = h * 31 + static_cast<std::uint8_t>(*name);
		return reinterpret_cast<const std::uint8_t*>(&records[h % 8]);
	}
	char name_buffer[32];
	const char* __fastcall name_of_handle(const std::uint32_t* handle, void*)
	{
		std::snprintf(name_buffer, sizeof name_buffer, "ent%u", d.tokens.index({*handle}).value);
		return name_buffer;
	}
	// libIGMath imports: the same code serves both runs (the native code calls through the IAT slot).
	double __fastcall distance2(const float* self, void*, const float* other)
	{
		const float dx = self[0] - other[0], dy = self[1] - other[1], dz = self[2] - other[2];
		return static_cast<double>(dx * dx + dy * dy + dz * dz);
	}
	double __fastcall normalize(float* v, void*)
	{
		const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (len > 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
		return static_cast<double>(len);
	}
	float zero_vector_data[3] = {0, 0, 0};
	// CRT stand-ins for the native manager constructor path.
	void __stdcall vector_constructor_iterator(void* base, std::uint32_t size, int count, void (__fastcall* ctor)(void*, void*), void*)
	{
		for (int i = 0; i < count; ++i) ctor(static_cast<std::uint8_t*>(base) + static_cast<std::size_t>(size) * i, nullptr);
	}
	int __cdecl atexit_stub(void*) { return 0; }
	void __cdecl ret_stub() {}

	void build_tables()
	{
		for (auto& s : entity_vtable) s = reinterpret_cast<const void*>(&unexpected_slot);
		entity_vtable[0] = reinterpret_cast<const void*>(&entity_class);
		entity_vtable[0x24 / 4] = reinterpret_cast<const void*>(&entity_position);
		entity_vtable[0x28 / 4] = reinterpret_cast<const void*>(&entity_collide_position);
		entity_vtable[0x194 / 4] = reinterpret_cast<const void*>(&entity_material);
		for (auto& s : world_vtable) s = reinterpret_cast<const void*>(&unexpected_slot);
		world_vtable[0xc / 4] = reinterpret_cast<const void*>(&world_find_token);
		world_vtable[0x10 / 4] = reinterpret_cast<const void*>(&world_find_dense);
		world_vtable[0x14 / 4] = reinterpret_cast<const void*>(&world_valid);
		world_vtable[0x24 / 4] = reinterpret_cast<const void*>(&world_player);
		world_vtable[0x34 / 4] = reinterpret_cast<const void*>(&world_named_set);
		world_vtable[0x40 / 4] = reinterpret_cast<const void*>(&world_property_set);
		world_vtable[0x50 / 4] = reinterpret_cast<const void*>(&world_pair_set);
		world.vtable = world_vtable;
		for (auto& s : grid_vtable) s = reinterpret_cast<const void*>(&unexpected_slot);
		grid_vtable[0x34 / 4] = reinterpret_cast<const void*>(&grid_cell_set);
		grid.vtable = grid_vtable;
		for (auto& s : collide_vtable) s = reinterpret_cast<const void*>(&unexpected_slot);
		collide_vtable[0] = reinterpret_cast<const void*>(&collide_slot_0);
		collide_vtable[1] = reinterpret_cast<const void*>(&collide_slot_4);
		collide_vtable[2] = reinterpret_cast<const void*>(&collide_slot_8);
		collide_vtable[3] = reinterpret_cast<const void*>(&collide_slot_c);
		collide_vtable[4] = reinterpret_cast<const void*>(&collide_slot_10);
		collide.vtable = collide_vtable;
		for (std::uint32_t i = 0; i < 8; ++i) records[i] = {0, i * 3 + 1};
		std::memset(class_record_flagged, 0, sizeof class_record_flagged);
		std::memset(class_record_plain, 0, sizeof class_record_plain);
	}
	// Populate the world for a capacity: live entities at the boundaries, generations 1, 2 and the maximum.
	void populate(const descriptor& desc)
	{
		d = desc;
		L = eq::layout_for(d.world);
		std::memset(entities, 0, sizeof entities);
		std::memset(live, 0, sizeof live);
		std::memset(handles, 0, sizeof handles);
		std::vector<std::uint32_t> dense = {0, 1, 2, 3, 5, 31, 32, 33, 63, 64, 100, 127, 128, 200, 254, 255};
		if (d.world > 256) for (auto i : {256u, 257u, 300u, 511u, 512u, 513u, 767u, 768u, 1000u, 1022u, 1023u}) dense.push_back(i);
		for (auto i : dense)
		{
			const std::uint32_t generation = (i % 3 == 0) ? 1 : (i % 3 == 1) ? 2 : generation_max;
			handles[i] = d.tokens.token(generation, DenseIndex{i}).value;
			live[i] = true;
			auto& e = entities[i];
			e.vtable = entity_vtable;
			e.token = handles[i];
			e.pos[0] = static_cast<float>(i) * 1.5f; e.pos[1] = static_cast<float>(i % 7) - 3.0f; e.pos[2] = static_cast<float>((i * 13) % 50) * 0.25f;
			e.collide_pos[0] = e.pos[0] + 0.5f; e.collide_pos[1] = e.pos[1] - 0.25f; e.collide_pos[2] = e.pos[2] + 2.0f;
			e.material = static_cast<std::uint8_t>(i % 8 == 0 ? 0x10 : 0x20 + (i & 0xf));
			e.class_record = (i % 4 == 0) ? class_record_flagged : class_record_plain;
			e.dense = i;
		}
		player_token = handles[5];
		std::memset(property_sets, 0, sizeof property_sets);
		for (std::uint32_t s = 0; s < 64; ++s) fill_pattern(property_sets[s], s);
		for (std::uint32_t i = 0; i < L.world; ++i) if (live[i]) property_sets[0x24 % 64][i >> 5] |= 1u << (i & 31); // "all characters"
		std::memset(grid_sets, 0, sizeof grid_sets);
		for (std::uint32_t s = 0; s < 16; ++s) fill_pattern(grid_sets[s], s + 100);
		// class flag bit (0x72a0ac + 0x24) set in the flagged record
		g32(eq::global_class_flag_id) = 5;
		const std::uint32_t id = 5 + 0x24;
		std::uint32_t word;
		std::memcpy(&word, class_record_flagged + 0x14 + (id >> 5) * 4, 4);
		word |= 1u << (id & 31);
		std::memcpy(class_record_flagged + 0x14 + (id >> 5) * 4, &word, 4);
		g32(eq::global_trace_stamp) = 0x51ac;
		g32(eq::global_class_base) = 0x10;
		g32(eq::global_character_class) = 0;
		g32(world_pointer_cell) = va_of(&world);
	}
	std::uint32_t dense_of_entity(const void* p)
	{
		if (!p) return 0xffffffff;
		const auto* e = static_cast<const entity_object*>(p);
		if (e < entities || e >= entities + max_world) return 0xfffffffe;
		return static_cast<std::uint32_t>(e - entities);
	}
}

// ---- the fixture image: native bytes at the retail addresses, stubs for the rest -----------------------------------------
void* __cdecl manager_select();
bool fixture_covers(std::uint32_t lo, std::uint32_t hi)
{
	const auto begin = va_of(fixture_begin), end = begin + static_cast<std::uint32_t>(fixture_size);
	return fixture_begin && begin <= lo && hi <= end;
}
bool install_native_image()
{
	struct range { std::uint32_t va; std::size_t n; };
	const range code[] = {
		{0x4a9570, 0x4abb40 - 0x4a9570}, // the query owner, its API and the manager getter/constructor
		{0x4654b0, 0x40}, {0x4564e0, 0xc}, {0x456290, 0x17}, {0x455a20, 0x2a}, {0x45fe70, 0x15},
		{0x419c90, 0x40}, {0x58b6b0, 0xa}, {0x560990, 0x66}, {0x44d0d0, 0xa0}, {0x61fde0, 0xa}, {0x4876e0, 0x9}, {0x48ef90, 0x1},
		{0x68d680, 0xc0}, // native vtables 0x68d684 / 0x68d68c / 0x68d694 / 0x68d6a4
		{0x680030, 0x4},  // float 0.0
		{0x6f38f8, 0x4}}; // security cookie
	for (const auto& r : code) if (!native_bytes(r.va, r.n)) { std::printf("cannot place native bytes at %x\n", r.va); return false; }
	const auto region = sha256_hex(at(0x4a9860), 0x4ab770 - 0x4a9860);
	if (region != "cef96ae82c73b7e197ea4137ea09665ccdad585e4b244c8af13c773114b47f3e") { std::printf("query region sha256 %s is not the retail build\n", region.c_str()); return false; }
	// stand-ins for the other owners and the CRT
	jump(native_world_getter, reinterpret_cast<const void*>(&fx::world_getter));
	jump(native_grid_getter, reinterpret_cast<const void*>(&fx::grid_getter));
	jump(eq::native_collide_getter, reinterpret_cast<const void*>(&fx::collide_getter));
	jump(native_name_of_handle, reinterpret_cast<const void*>(&fx::name_of_handle));
	jump(eq::native_record_by_name, reinterpret_cast<const void*>(&fx::record_by_name));
	jump(0x672161, reinterpret_cast<const void*>(&fx::ret_stub));  // __security_check_cookie (tail-jumped from 0x4a98d0)
	jump(0x67211e, reinterpret_cast<const void*>(&fx::atexit_stub));
	jump(0x6722d4, reinterpret_cast<const void*>(&fx::vector_constructor_iterator));
	jump(native_query_manager_getter, reinterpret_cast<const void*>(&manager_select)); // the integrator's replacement, selecting the run's manager
	writable(eq::iat_distance2, 4); g32(eq::iat_distance2) = va_of(&fx::distance2);
	writable(eq::iat_normalize, 4); g32(eq::iat_normalize) = va_of(&fx::normalize);
	writable(eq::iat_zero_vector, 4); g32(eq::iat_zero_vector) = va_of(fx::zero_vector_data);
	return true;
}
// The trace pool is a retail global shared by both runs: rebuilt before each by the native static initializer path.
void reset_trace_pool()
{
	writable(eq::native_trace_pool, 0x1000);
	std::memset(at(eq::native_trace_pool), 0, 0xd68);
	if (binary.empty())
	{
		// no native code: the ring as 0x4a9c70 leaves it (entries 0..49 in order, enqueue wrapped to 0, count 50)
		auto* pool = at(eq::native_trace_pool);
		for (std::uint32_t i = 0; i < 50; ++i) *reinterpret_cast<std::uint32_t*>(pool + 0xc84 + i * 4) = i;
		*reinterpret_cast<std::uint32_t*>(pool + 0xd50) = 0;
		*reinterpret_cast<std::uint32_t*>(pool + 0xd54) = 0;
		*reinterpret_cast<std::uint32_t*>(pool + 0xd58) = 50;
		return;
	}
	native<void (__fastcall*)(void*, void*)>(0x4ab4d0)(at(eq::native_trace_pool), nullptr);
}
// Private port of the two pool helpers for the no-exe synthetic runs (the DLL calls the retail code by address).
std::uint32_t __fastcall pool_take_stub(void* pool, void*)
{
	auto* p = static_cast<std::uint8_t*>(pool);
	auto u = [&](std::uint32_t off) -> std::uint32_t& { return *reinterpret_cast<std::uint32_t*>(p + off); };
	const auto index = u(0xc84 + u(0xd54) * 4);
	u(0xd5c + (index >> 5) * 4) |= 1u << (index & 31);
	if (++u(0xd54) >= 50) u(0xd54) = 0;
	u(0xd58) -= 1;
	u(0xd64) += 1;
	return index;
}
void __fastcall pool_return_stub(void* pool, void*, std::uint32_t index)
{
	auto* p = static_cast<std::uint8_t*>(pool);
	auto u = [&](std::uint32_t off) -> std::uint32_t& { return *reinterpret_cast<std::uint32_t*>(p + off); };
	u(0xd5c + (index >> 5) * 4) &= ~(1u << (index & 31));
	auto enqueue = u(0xd50) + 1;
	u(0xd58) += 1;
	std::uint32_t slot;
	if (enqueue < 50) { u(0xd50) = enqueue; slot = enqueue - 1; }
	else { u(0xd50) = 0; slot = 49; }
	u(0xc84 + slot * 4) = index;
	u(0xd64) -= 1;
}
void install_synthetic_stubs()
{
	// Everything the port calls by address, when no exe is available (stubs at the retail addresses).
	jump(native_world_getter, reinterpret_cast<const void*>(&fx::world_getter));
	jump(native_grid_getter, reinterpret_cast<const void*>(&fx::grid_getter));
	jump(eq::native_collide_getter, reinterpret_cast<const void*>(&fx::collide_getter));
	jump(native_name_of_handle, reinterpret_cast<const void*>(&fx::name_of_handle));
	jump(eq::native_record_by_name, reinterpret_cast<const void*>(&fx::record_by_name));
	jump(eq::native_trace_pool_take, reinterpret_cast<const void*>(&pool_take_stub));
	jump(eq::native_trace_pool_return, reinterpret_cast<const void*>(&pool_return_stub));
	// the trace-score policy vtable: slot 0 is the port's callback (in the game: 0x4a9a80 jumps there)
	writable(eq::native_policy_vtable, 4); g32(eq::native_policy_vtable) = va_of(&eq::QueryApi_004a9a80);
	writable(eq::iat_distance2, 4); g32(eq::iat_distance2) = va_of(&fx::distance2);
	writable(eq::iat_normalize, 4); g32(eq::iat_normalize) = va_of(&fx::normalize);
	writable(eq::iat_zero_vector, 4); g32(eq::iat_zero_vector) = va_of(fx::zero_vector_data);
}
// Synthetic stand-ins for the three World-backed helpers and the angle conversion (only without an exe).
bool __fastcall handle_valid_stub(const std::uint32_t* handle, void*) { return fx::world_find_token(&fx::world, nullptr, *handle) != nullptr; }
std::uint8_t* __fastcall entity_of_handle_stub(const std::uint32_t* handle, void*) { return reinterpret_cast<std::uint8_t*>(fx::world_find_token(&fx::world, nullptr, *handle)); }
std::uint32_t* __cdecl player_handle_stub(std::uint32_t* out) { return fx::world_player(&fx::world, nullptr, out); }
float* __cdecl angles_to_direction_stub(float* out, const float* angles)
{
	// 0x560990: pitch at +4, yaw at +8; pitch 0 -> (cos yaw, sin yaw, 0); else (cos p cos y, cos p sin y, -sin p)
	if (angles[1] == 0.0f) { out[0] = std::cos(angles[2]); out[1] = std::sin(angles[2]); out[2] = 0.0f; }
	else { out[0] = std::cos(angles[1]) * std::cos(angles[2]); out[1] = std::cos(angles[1]) * std::sin(angles[2]); out[2] = -std::sin(angles[1]); }
	return out;
}
void install_helper_stubs()
{
	jump(eq::native_handle_valid, reinterpret_cast<const void*>(&handle_valid_stub));
	jump(eq::native_entity_of_handle, reinterpret_cast<const void*>(&entity_of_handle_stub));
	jump(eq::native_player_handle, reinterpret_cast<const void*>(&player_handle_stub));
	jump(eq::native_angles_to_direction, reinterpret_cast<const void*>(&angles_to_direction_stub));
}

// ---- the scripted sequence, run against the native code or the port, with a snapshot after every step ------------------
enum class mode { native, port };
std::uint32_t d_stale() { return fx::d.tokens.token(generation_max - 1, DenseIndex{5}).value; } // a generation never minted for slot 5
struct snapshot
{
	std::string label;
	std::vector<std::uint8_t> manager, pool;
	std::vector<std::uint32_t> values, collide;
};
struct driver
{
	mode m;
	std::uint8_t* manager = nullptr;
	std::vector<snapshot> steps;
	std::vector<std::uint32_t> values;
	// the API set of this mode
	bool (__fastcall* has_record)(void**, void*);
	void* (__fastcall* record_entity)(void**, void*);
	bool (__fastcall* record_is_player)(void**, void*);
	const float* (__fastcall* record_v0)(void**, void*);
	const float* (__fastcall* record_v10)(void**, void*);
	const float* (__fastcall* record_v20)(void**, void*);
	const float* (__fastcall* record_v30)(void**, void*);
	float (__fastcall* record_score)(void**, void*);
	const std::uint8_t* (__fastcall* record_material)(void**, void*);
	void** (__fastcall* policy_construct)(void**, void*, void**);
	void* (__fastcall* list_construct)(void*, void*);
	bool (__cdecl* movement_trace)(std::uint32_t, std::uint32_t, const float*, const float*, std::uint32_t*, void**);
	void** (__fastcall* acquire_all)(void**, void*, std::uint32_t);
	void** (__fastcall* acquire_copy)(void**, void*, void**);
	void** (__fastcall* acquire_excluding)(void**, void*, const std::uint32_t*);
	void** (__fastcall* acquire_record)(void**, void*, const void*, const std::uint32_t*);
	void (__fastcall* release)(void**, void*);
	int (__cdecl* free_count)();
	void* (__cdecl* order_fill)(void*, void*);
	void* (__cdecl* order_release)(void*);
	bool (__cdecl* trace_copy)(std::uint32_t, std::uint32_t, const float*, const float*, std::uint32_t*, void**);
	bool (__cdecl* trace_player)(std::uint32_t, std::uint32_t, const float*, const float*, std::uint32_t*, std::uint8_t);
	bool (__cdecl* trace_simple)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t*);
	bool (__cdecl* trace_classes)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t*);
};
driver* current = nullptr;
void* __cdecl manager_select() { return current ? current->manager : nullptr; }
std::uint32_t query_index(const driver& d, const void* q)
{
	if (!q) return 0xffffffff;
	return (va_of(q) - va_of(d.manager) - fx::L.m_queries) / fx::L.q_stride;
}
std::uint32_t order_index(const driver& d, const void* o)
{
	if (!o) return 0xffffffff;
	return (va_of(o) - va_of(d.manager) - fx::L.m_orders) / fx::L.o_stride;
}
template <typename Fn> Fn vslot(void* object, std::uint32_t slot)
{
	return reinterpret_cast<Fn>(reinterpret_cast<const void* const*>(*reinterpret_cast<std::uint32_t*>(object))[slot / 4]);
}
std::uint32_t u32at(const void* o, std::uint32_t off) { std::uint32_t v; std::memcpy(&v, static_cast<const std::uint8_t*>(o) + off, 4); return v; }
void record_step(driver& d, const char* label)
{
	snapshot s;
	s.label = label;
	const auto& L = fx::L;
	s.manager.assign(d.manager, d.manager + L.m_size);
	// normalize: vtable pointers and the orders' query pointers (manager-relative in the two modes)
	auto zero = [&](std::uint32_t off) { std::memset(s.manager.data() + off, 0, 4); };
	for (std::uint32_t i = 0; i < 3; ++i)
	{
		const auto q = L.m_queries + i * L.q_stride;
		zero(q);
		zero(q + L.q_collisions);
	}
	for (std::uint32_t i = 0; i < 3; ++i)
	{
		const auto o = L.m_orders + i * L.o_stride;
		zero(o);
		const auto query = u32at(s.manager.data(), o + L.o_query);
		const std::uint32_t index = query ? (query - va_of(d.manager) - L.m_queries) / L.q_stride : 0xffffffff;
		std::memcpy(s.manager.data() + o + L.o_query, &index, 4);
		// entity pointers in the pairs -> dense indices
		for (std::uint32_t p = 0; p < u32at(s.manager.data(), o + L.o_count) && p < L.world; ++p)
		{
			const auto e = u32at(s.manager.data(), o + L.o_pairs + p * 8);
			const auto dense = fx::dense_of_entity(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(e)));
			std::memcpy(s.manager.data() + o + L.o_pairs + p * 8, &dense, 4);
		}
	}
	s.pool.assign(at(eq::native_trace_pool), at(eq::native_trace_pool) + 0xd68);
	s.values = d.values;
	d.values.clear();
	s.collide = fx::collide_log;
	fx::collide_log.clear();
	d.steps.push_back(std::move(s));
	std::printf("    step %zu: %s\n", d.steps.size(), label);
}
void value(driver& d, std::uint32_t v) { d.values.push_back(v); }
void value_bits(driver& d, float f) { std::uint32_t v; std::memcpy(&v, &f, 4); d.values.push_back(v); }
void value_set(driver& d, const std::uint32_t* set) { d.values.push_back(fx::set_hash(set)); }

// Scoring policies for the order tests: the native trace policy (0x4a9730) plus two test policies.
double __fastcall score_by_dense(void**, void*, fx::entity_object* e) { return static_cast<double>(static_cast<float>((e->dense * 37) % 11) - 5.0f); }
double __fastcall score_zero(void**, void*, fx::entity_object*) { return 0.0; }
const void* dense_policy_vtable[1] = {reinterpret_cast<const void*>(&score_by_dense)};
const void* zero_policy_vtable[1] = {reinterpret_cast<const void*>(&score_zero)};

using set_op_fn = void (__fastcall*)(void*, void*, std::uint32_t, const std::uint32_t*);
using mode_u32_fn = void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t);
using mode_ptr_fn = void (__fastcall*)(void*, void*, std::uint32_t, const void*);
using byte_fn = void (__fastcall*)(void*, void*, std::uint8_t);
void run_script(driver& d)
{
	using namespace fx;
	current = &d;
	auto& h = handles;
	void* ref0 = nullptr; void* ref1 = nullptr; void* ref2 = nullptr;
	const std::uint32_t zero = 0;
	const std::uint32_t stale = d_stale();
	d.acquire_all(&ref0, nullptr, 1);
	value(d, query_index(d, ref0)); value(d, static_cast<std::uint32_t>(d.free_count()));
	record_step(d, "acquire q0 (all characters)");
	void* q0 = ref0;
	vslot<set_op_fn>(q0, 0x30)(q0, nullptr, 1, &h[5]); record_step(d, "q0 remove handle 5");
	vslot<set_op_fn>(q0, 0x30)(q0, nullptr, 0, &h[5]); record_step(d, "q0 add handle 5 (clears flags)");
	vslot<set_op_fn>(q0, 0x30)(q0, nullptr, 2, &h[31]); record_step(d, "q0 mode 2 handle 31");
	vslot<set_op_fn>(q0, 0x30)(q0, nullptr, 1, &stale); record_step(d, "q0 remove stale-generation handle (no change)");
	vslot<set_op_fn>(q0, 0x30)(q0, nullptr, 1, &zero); record_step(d, "q0 remove null handle (no change)");
	vslot<mode_u32_fn>(q0, 0x40)(q0, nullptr, 1, 7); record_step(d, "q0 subtract property 7 (flags_b |= 1)");
	vslot<mode_u32_fn>(q0, 0x40)(q0, nullptr, 2, 0x1a); record_step(d, "q0 intersect property 0x1a (flags_a |= 0x100)");
	vslot<mode_u32_fn>(q0, 0x40)(q0, nullptr, 2, 0xe); record_step(d, "q0 intersect property 0xe (flags_a |= 0x80)");
	vslot<mode_u32_fn>(q0, 0x40)(q0, nullptr, 0, 9); record_step(d, "q0 union property 9 (flags cleared)");
	vslot<set_op_fn>(q0, 0x3c)(q0, nullptr, 0, &h[2]); record_step(d, "q0 union named set ent2");
	vslot<set_op_fn>(q0, 0x3c)(q0, nullptr, 1, &h[1]); record_step(d, "q0 subtract named set ent1 (lookup fails, no change)");
	vslot<set_op_fn>(q0, 0x3c)(q0, nullptr, 2, &h[1]); record_step(d, "q0 intersect named set ent1 (lookup fails, cleared)");
	vslot<byte_fn>(q0, 0x10)(q0, nullptr, 1); record_step(d, "q0 reset all (slot 0x10)");
	const std::uint32_t pair_b = 33;
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, const std::uint32_t*)>(q0, 0x44)(q0, nullptr, 2, 2, &pair_b); record_step(d, "q0 intersect pair set (2, 33)");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, const std::uint32_t*)>(q0, 0x44)(q0, nullptr, 0, 7, &pair_b); record_step(d, "q0 union pair set (7, 33) fails (no change)");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, const std::uint32_t*)>(q0, 0x44)(q0, nullptr, 2, 7, &pair_b); record_step(d, "q0 intersect pair set (7, 33) fails (cleared)");
	vslot<byte_fn>(q0, 0x10)(q0, nullptr, 1); record_step(d, "q0 reset all again");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, const char*)>(q0, 0x48)(q0, nullptr, 1, "alpha"); record_step(d, "q0 slot 0x48 subtract class by name");
	vslot<mode_ptr_fn>(q0, 0x4c)(q0, nullptr, 2, &records[3]); record_step(d, "q0 slot 0x4c intersect class record");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, const char*)>(q0, 0x50)(q0, nullptr, 0, "beta"); record_step(d, "q0 slot 0x50 union type by name");
	vslot<mode_ptr_fn>(q0, 0x54)(q0, nullptr, 1, &records[6]); record_step(d, "q0 slot 0x54 subtract type record");
	d.acquire_excluding(&ref1, nullptr, &h[100]);
	value(d, query_index(d, ref1)); value(d, static_cast<std::uint32_t>(d.free_count()));
	record_step(d, "acquire q1 excluding handle 100");
	void* q1 = ref1;
	vslot<mode_u32_fn>(q1, 0x40)(q1, nullptr, 2, 0xc); vslot<mode_u32_fn>(q1, 0x40)(q1, nullptr, 1, 8); record_step(d, "q1 flags via properties 0xc / 8");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, void**)>(q0, 0x34)(q0, nullptr, 0, &ref1); record_step(d, "q0 union q1");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, void**)>(q0, 0x34)(q0, nullptr, 2, &ref1); record_step(d, "q0 intersect q1 (flags merged)");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, void**)>(q0, 0x34)(q0, nullptr, 1, &ref1); record_step(d, "q0 subtract q1 (empty)");
	vslot<byte_fn>(q0, 0x10)(q0, nullptr, 1); record_step(d, "q0 reset all (3)");
	vslot<mode_u32_fn>(q0, 0x38)(q0, nullptr, 2, 5); record_step(d, "q0 intersect grid cell 5");
	vslot<void (__fastcall*)(void*, void*)>(q0, 0x58)(q0, nullptr); record_step(d, "q0 invert (slot 0x58)");
	vslot<void (__fastcall*)(void*, void*, const void*)>(q0, 0x5c)(q0, nullptr, &records[2]); record_step(d, "q0 invert with record (slot 0x5c)");
	{
		EntitySet out{};
		fill_pattern(out.words.data(), 77);
		vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t*)>(q0, 0x8c)(q0, nullptr, 0, out.words.data()); value_set(d, out.words.data());
		vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t*)>(q0, 0x8c)(q0, nullptr, 2, out.words.data()); value_set(d, out.words.data());
		vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t*)>(q0, 0x8c)(q0, nullptr, 1, out.words.data()); value_set(d, out.words.data());
		record_step(d, "q0 export (slot 0x8c) modes 0/2/1");
	}
	value(d, dense_of_entity(vslot<void* (__fastcall*)(void*, void*)>(q0, 0x14)(q0, nullptr))); record_step(d, "q0 first entity (slot 0x14)");
	vslot<byte_fn>(q0, 0x10)(q0, nullptr, 1);
	{
		const float origin[3] = {10.0f, -1.0f, 2.0f};
		value(d, dense_of_entity(vslot<void* (__fastcall*)(void*, void*, const float*, std::uint32_t)>(q0, 0x18)(q0, nullptr, origin, 7)));
		value(d, dense_of_entity(vslot<void* (__fastcall*)(void*, void*, const float*, std::uint32_t)>(q0, 0x1c)(q0, nullptr, origin, 3)));
		record_step(d, "q0 nearest (slots 0x18/0x1c): LegacyEmptyDenseDisposition");
		const float dir[3] = {1.0f, 0.2f, 0.0f};
		vslot<void (__fastcall*)(void*, void*, const float*, const float*, float, float, std::uint32_t)>(q0, 0x60)(q0, nullptr, origin, dir, 0.98f, 0.3f, 7); record_step(d, "q0 cone filter (slot 0x60)");
		vslot<byte_fn>(q0, 0x10)(q0, nullptr, 1);
		const float angles[3] = {0.0f, 0.25f, 1.0f};
		vslot<void (__fastcall*)(void*, void*, const float*, const float*, float, float, std::uint32_t)>(q0, 0x64)(q0, nullptr, origin, angles, 0.9f, -0.9f, 5); record_step(d, "q0 cone from angles (slot 0x64)");
	}
	vslot<byte_fn>(q0, 0x80)(q0, nullptr, 0); vslot<byte_fn>(q0, 0x84)(q0, nullptr, 7); vslot<byte_fn>(q0, 0x88)(q0, nullptr, 3); record_step(d, "q0 flag bytes (slots 0x80/0x84/0x88)");
	vslot<byte_fn>(q0, 0x10)(q0, nullptr, 1);
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t)>(q0, 0x68)(q0, nullptr, 11, 22, 33, 0); record_step(d, "q0 physics slot 0x68");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)>(q0, 0x6c)(q0, nullptr, 44, 55, 1, 1); record_step(d, "q0 physics slot 0x6c simple");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)>(q0, 0x6c)(q0, nullptr, 44, 55, 0, 0); record_step(d, "q0 physics slot 0x6c inverse");
	vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)>(q0, 0x7c)(q0, nullptr, 1, 2, 3, 4); record_step(d, "q0 physics slot 0x7c (player trace inserted)");
	{
		const float p[3] = {1.0f, 2.0f, 3.0f};
		vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t)>(q0, 0x74)(q0, nullptr, 66, 77, va_of(p)); value(d, u32at(q0, L.q_result)); record_step(d, "q0 trace (slot 0x74)");
		const float base[3] = {4.0f, 5.0f, 6.0f}, angles[3] = {0.0f, 0.5f, 2.0f};
		vslot<void (__fastcall*)(void*, void*, const float*, const float*, float, std::uint32_t)>(q0, 0x70)(q0, nullptr, base, angles, 3.25f, 9); value(d, u32at(q0, L.q_result)); record_step(d, "q0 trace along angles (slot 0x70)");
		vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, const std::uint32_t*)>(q0, 0x78)(q0, nullptr, 88, 99, &h[33]); value(d, u32at(q0, L.q_result)); record_step(d, "q0 trace from entity 33 (slot 0x78)");
		vslot<void (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, const std::uint32_t*)>(q0, 0x78)(q0, nullptr, 88, 99, &stale); value(d, u32at(q0, L.q_result)); record_step(d, "q0 trace from stale handle (no call)");
	}
	std::uint32_t out = 0;
	vslot<std::uint32_t* (__fastcall*)(void*, void*, std::uint32_t*)>(q0, 0x20)(q0, nullptr, &out); value(d, pool_index(reinterpret_cast<void*>(static_cast<std::uintptr_t>(out))));
	value(d, vslot<std::uint8_t (__fastcall*)(void*, void*)>(q0, 0x24)(q0, nullptr));
	value(d, vslot<bool (__fastcall*)(void*, void*)>(q0, 0x28)(q0, nullptr) ? 1u : 0u);
	vslot<void (__fastcall*)(void*, void*, std::uint32_t*)>(q0, 0x2c)(q0, nullptr, &out); value(d, pool_index(reinterpret_cast<void*>(static_cast<std::uintptr_t>(out))));
	record_step(d, "q0 result/player trace lookups (slots 0x20/0x24/0x28/0x2c)");
	// fill the trace records as physics would, then score through the native policy
	{
		auto* c = static_cast<std::uint8_t*>(q0) + L.q_collisions;
		const auto count = u32at(c, L.c_slot_count);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			auto* rec = at(eq::native_trace_pool) + c[L.c_slots + i] * eq::trace_record_bytes;
			for (std::uint32_t f = 0; f < 15; ++f)
			{
				if (f == 3) continue; // +0xc keeps the owning entity's token
				const float v = static_cast<float>(i + 1) * 0.5f + static_cast<float>(f);
				std::memcpy(rec + f * 4, &v, 4);
			}
			const std::uint32_t material = 0x33 + i;
			std::memcpy(rec + 0x2c, &material, 4);
			const float score = (i == 1) ? 0.0f : 4.0f - static_cast<float>(i) * 1.5f;
			std::memcpy(rec + 0x3c, &score, 4);
		}
		record_step(d, "trace records filled by the test");
	}
	void* policy[2] = {nullptr, nullptr};
	value(d, d.policy_construct(policy, nullptr, &ref0) == policy ? 1u : 0u); value(d, va_of(policy[0])); value(d, query_index(d, policy[1]));
	void* order0 = d.order_fill(q0, policy); value(d, order_index(d, order0));
	record_step(d, "order fill with the trace policy (0x4a9730 / 0x4a9a80)");
	{
		value(d, vslot<std::uint32_t (__fastcall*)(void*, void*)>(order0, 0x0)(order0, nullptr));
		for (std::uint32_t i = 0; i < 3; ++i)
		{
			value(d, dense_of_entity(vslot<void* (__fastcall*)(void*, void*, std::uint32_t)>(order0, 0x4)(order0, nullptr, i)));
			vslot<void (__fastcall*)(void*, void*, std::uint32_t*, std::uint32_t)>(order0, 0x8)(order0, nullptr, &out, i);
			value(d, pool_index(reinterpret_cast<void*>(static_cast<std::uintptr_t>(out))));
		}
		record_step(d, "order slots 0/4/8");
	}
	const void* dense_policy = dense_policy_vtable; const void* zero_policy = zero_policy_vtable;
	void* order1 = d.order_fill(q0, &dense_policy); value(d, order_index(d, order1)); record_step(d, "order fill with a dense-scoring policy (sorted)");
	void* order2 = d.order_fill(q1, &zero_policy); value(d, order_index(d, order2)); record_step(d, "order fill with a zero policy (unsorted)");
	value(d, order_index(d, d.order_fill(q0, &dense_policy))); record_step(d, "fourth order fill refused");
	d.order_release(order1); d.order_release(nullptr); record_step(d, "release order 1 and null");
	value(d, order_index(d, d.order_fill(q1, &dense_policy))); record_step(d, "order 1 reused");
	d.order_release(order0); d.order_release(order1); d.order_release(order2); record_step(d, "all orders released");
	{
		vslot<std::uint32_t* (__fastcall*)(void*, void*, std::uint32_t*)>(q0, 0x20)(q0, nullptr, &out);
		void* rec = reinterpret_cast<void*>(static_cast<std::uintptr_t>(out));
		void* none = nullptr;
		for (void** ref : {&rec, &none})
		{
			value(d, d.has_record(ref, nullptr) ? 1u : 0u);
			value(d, dense_of_entity(d.record_entity(ref, nullptr)));
			value(d, d.record_is_player(ref, nullptr) ? 1u : 0u);
			value(d, va_of(d.record_v0(ref, nullptr)) - (ref == &rec ? va_of(rec) : va_of(zero_vector_data)));
			value(d, va_of(d.record_v10(ref, nullptr)) - (ref == &rec ? va_of(rec) : va_of(zero_vector_data)));
			value(d, va_of(d.record_v20(ref, nullptr)) - (ref == &rec ? va_of(rec) : va_of(zero_vector_data)));
			value(d, va_of(d.record_v30(ref, nullptr)) - (ref == &rec ? va_of(rec) : va_of(zero_vector_data)));
			value_bits(d, d.record_score(ref, nullptr));
			const auto* material = d.record_material(ref, nullptr);
			value(d, va_of(material) - eq::global_material_byte); value(d, *material); value(d, g32(eq::global_material_init));
		}
		// records owned by entities carrying the class flag (dense % 4 == 0): entity 0 answers material 0x10 (falls
		// through to the record's +0x2c byte, 0 here); entity 100 answers its own material byte 0x24
		std::uint8_t spare[eq::trace_record_bytes]{};
		std::memcpy(spare + 0xc, &h[0], 4);
		void* spare_ref = spare;
		value(d, *d.record_material(&spare_ref, nullptr));
		std::memcpy(spare + 0xc, &h[100], 4); value(d, *d.record_material(&spare_ref, nullptr));
		record_step(d, "trace record accessors (0x4a9580..0x4a9630, 0x4a97a0)");
	}
	{
		std::uint32_t list[0x38 / 4]; std::memset(list, 0x5a, sizeof list);
		value(d, va_of(d.list_construct(list, nullptr)) - va_of(list)); value(d, list[0x34 / 4]); value(d, list[0]);
		record_step(d, "list constructor 0x4a98b0");
	}
	value(d, static_cast<std::uint32_t>(d.free_count()));
	d.acquire_record(&ref2, nullptr, &records[1], &h[5]); value(d, query_index(d, ref2)); value(d, static_cast<std::uint32_t>(d.free_count()));
	record_step(d, "acquire q2 from record (0x4ab6d0) excluding handle 5");
	d.release(&ref2, nullptr); value(d, query_index(d, ref2)); value(d, static_cast<std::uint32_t>(d.free_count())); record_step(d, "release q2");
	d.acquire_copy(&ref2, nullptr, &ref0); value(d, query_index(d, ref2)); record_step(d, "acquire q2 as a copy of q0 (0x4ab610)");
	d.release(&ref2, nullptr); record_step(d, "release q2 again");
	{
		const float pos[3] = {1.0f, 1.0f, 1.0f}, delta[3] = {0.5f, -0.5f, 2.0f};
		value(d, d.movement_trace(h[1], h[2], pos, delta, &out, &ref0) ? 1u : 0u); value(d, out); record_step(d, "movement trace helper 0x4a9650 on q0");
		value(d, d.movement_trace(stale, h[2], pos, delta, &out, &ref0) ? 1u : 0u); value(d, out); record_step(d, "movement trace helper with a stale handle");
		value(d, d.trace_copy(h[3], h[0], pos, delta, &out, &ref0) ? 1u : 0u); value(d, out); value(d, static_cast<std::uint32_t>(d.free_count())); record_step(d, "0x4ab7f0 scoped copy trace");
		value(d, d.trace_player(h[3], 0, pos, delta, &out, 0) ? 1u : 0u); value(d, out); record_step(d, "0x4ab890 excluding the player");
		value(d, d.trace_player(h[3], 0, pos, delta, &out, 1) ? 1u : 0u); value(d, out); record_step(d, "0x4ab890 keeping the player");
		value(d, d.trace_simple(h[5], 7, 8, va_of(pos), &out) ? 1u : 0u); value(d, out); record_step(d, "0x4ab960");
		value(d, d.trace_simple(0, 7, 8, va_of(pos), &out) ? 1u : 0u); value(d, out); record_step(d, "0x4ab960 without exclusion");
		value(d, d.trace_classes(h[2], 7, 8, va_of(pos), &out) ? 1u : 0u); value(d, out); record_step(d, "0x4aba30");
	}
	d.release(&ref0, nullptr); d.release(&ref1, nullptr); value(d, static_cast<std::uint32_t>(d.free_count())); record_step(d, "release q0 and q1 (pool restored)");
	current = nullptr;
}

driver make_driver(mode m)
{
	driver d{};
	d.m = m;
	if (m == mode::native)
	{
		d.has_record = native<decltype(d.has_record)>(0x4a9580); d.record_entity = native<decltype(d.record_entity)>(0x4a9590);
		d.record_is_player = native<decltype(d.record_is_player)>(0x4a95c0); d.record_v0 = native<decltype(d.record_v0)>(0x4a95f0);
		d.record_v10 = native<decltype(d.record_v10)>(0x4a9600); d.record_v20 = native<decltype(d.record_v20)>(0x4a9610);
		d.record_v30 = native<decltype(d.record_v30)>(0x4a9620); d.record_score = native<decltype(d.record_score)>(0x4a9630);
		d.record_material = native<decltype(d.record_material)>(0x4a97a0); d.policy_construct = native<decltype(d.policy_construct)>(0x4a9730);
		d.list_construct = native<decltype(d.list_construct)>(0x4a98b0); d.movement_trace = native<decltype(d.movement_trace)>(0x4a9650);
		d.acquire_all = native<decltype(d.acquire_all)>(0x4ab5b0); d.acquire_copy = native<decltype(d.acquire_copy)>(0x4ab610);
		d.acquire_excluding = native<decltype(d.acquire_excluding)>(0x4ab670); d.acquire_record = native<decltype(d.acquire_record)>(0x4ab6d0);
		d.release = native<decltype(d.release)>(0x4ab720); d.free_count = native<decltype(d.free_count)>(0x4ab740);
		d.order_fill = native<decltype(d.order_fill)>(0x4ab770); d.order_release = native<decltype(d.order_release)>(0x4ab7d0);
		d.trace_copy = native<decltype(d.trace_copy)>(0x4ab7f0); d.trace_player = native<decltype(d.trace_player)>(0x4ab890);
		d.trace_simple = native<decltype(d.trace_simple)>(0x4ab960); d.trace_classes = native<decltype(d.trace_classes)>(0x4aba30);
	}
	else
	{
		d.has_record = &eq::QueryApi_004a9580; d.record_entity = &eq::QueryApi_004a9590; d.record_is_player = &eq::QueryApi_004a95c0;
		d.record_v0 = &eq::QueryApi_004a95f0; d.record_v10 = &eq::QueryApi_004a9600; d.record_v20 = &eq::QueryApi_004a9610; d.record_v30 = &eq::QueryApi_004a9620;
		d.record_score = &eq::QueryApi_004a9630; d.record_material = &eq::QueryApi_004a97a0; d.policy_construct = &eq::QueryApi_004a9730;
		d.list_construct = &eq::QueryApi_004a98b0; d.movement_trace = &eq::QueryApi_004a9650;
		d.acquire_all = &eq::QueryApi_004ab5b0; d.acquire_copy = &eq::QueryApi_004ab610; d.acquire_excluding = &eq::QueryApi_004ab670; d.acquire_record = &eq::QueryApi_004ab6d0;
		d.release = &eq::QueryApi_004ab720; d.free_count = &eq::QueryApi_004ab740; d.order_fill = &eq::QueryApi_004ab770; d.order_release = &eq::QueryApi_004ab7d0;
		d.trace_copy = &eq::QueryApi_004ab7f0; d.trace_player = &eq::QueryApi_004ab890; d.trace_simple = &eq::QueryApi_004ab960; d.trace_classes = &eq::QueryApi_004aba30;
	}
	return d;
}

// ---- comparisons and expectations --------------------------------------------------------------------------------------
bool compare_runs(const driver& a, const driver& b)
{
	if (a.steps.size() != b.steps.size()) { std::printf("step counts differ: %zu vs %zu\n", a.steps.size(), b.steps.size()); return false; }
	bool same = true;
	for (std::size_t i = 0; i < a.steps.size(); ++i)
	{
		const auto& x = a.steps[i];
		const auto& y = b.steps[i];
		bool ok = x.label == y.label && x.manager == y.manager && x.pool == y.pool && x.values == y.values && x.collide == y.collide;
		if (!ok)
		{
			same = false;
			std::printf("  step %zu '%s' differs:", i, x.label.c_str());
			if (x.manager != y.manager)
			{
				std::size_t first = 0;
				while (first < x.manager.size() && x.manager[first] == y.manager[first]) ++first;
				std::printf(" manager@+%zx (%02x vs %02x)", first, x.manager[first], y.manager[first]);
			}
			if (x.pool != y.pool)
			{
				std::size_t first = 0;
				while (first < x.pool.size() && x.pool[first] == y.pool[first]) ++first;
				std::printf(" pool@+%zx", first);
			}
			if (x.values != y.values)
			{
				std::printf(" values[");
				for (auto v : x.values) std::printf(" %x", v);
				std::printf(" ] vs [");
				for (auto v : y.values) std::printf(" %x", v);
				std::printf(" ]");
			}
			if (x.collide != y.collide)
			{
				std::printf(" collide[");
				for (auto v : x.collide) std::printf(" %x", v);
				std::printf(" ] vs [");
				for (auto v : y.collide) std::printf(" %x", v);
				std::printf(" ]");
			}
			std::printf("\n");
		}
	}
	return same;
}
// Mode-independent expectations about the recorded steps (checked on every run, both capacities).
void expect_script(const driver& d, const char* tag)
{
	auto step = [&](const char* label) -> const snapshot* { for (const auto& s : d.steps) if (s.label == label) return &s; return nullptr; };
	auto manager_u32 = [&](const snapshot& s, std::uint32_t off) { return u32at(s.manager.data(), off); };
	const auto& L = fx::L;
	char buf[160];
	const auto* s = step("acquire q0 (all characters)");
	std::snprintf(buf, sizeof buf, "%s: first acquisition takes query 0 and leaves two free", tag);
	check(s && s->values.size() == 2 && s->values[0] == 0 && s->values[1] == 2, buf);
	std::snprintf(buf, sizeof buf, "%s: reset sets the three flag bytes 1/0/1 and clears flags", tag);
	check(s && s->manager[L.q_b0] == 1 && s->manager[L.q_b1] == 0 && s->manager[L.q_b2] == 1 && manager_u32(*s, L.q_flags_a) == 0 && manager_u32(*s, L.q_flags_b) == 0 && s->manager[L.q_busy] == 1, buf);
	s = step("q0 subtract property 7 (flags_b |= 1)");
	std::snprintf(buf, sizeof buf, "%s: property mode 1 records flag bit in flags_b", tag);
	check(s && manager_u32(*s, L.q_flags_b) == 1, buf);
	s = step("q0 intersect property 0xe (flags_a |= 0x80)");
	std::snprintf(buf, sizeof buf, "%s: property mode 2 accumulates flags_a 0x180", tag);
	check(s && manager_u32(*s, L.q_flags_a) == 0x180, buf);
	s = step("q0 union property 9 (flags cleared)");
	std::snprintf(buf, sizeof buf, "%s: union clears both flag words", tag);
	check(s && manager_u32(*s, L.q_flags_a) == 0 && manager_u32(*s, L.q_flags_b) == 0, buf);
	s = step("q0 nearest (slots 0x18/0x1c): LegacyEmptyDenseDisposition");
	std::snprintf(buf, sizeof buf, "%s: slots 0x18/0x1c resolve nothing (dense index passed as a token)", tag);
	check(s && s->values.size() == 2 && s->values[0] == 0xffffffff && s->values[1] == 0xffffffff, buf);
	s = step("q0 physics slot 0x7c (player trace inserted)");
	std::snprintf(buf, sizeof buf, "%s: physics slot 0xc receives the set and the collisions at the layout offset", tag);
	check(s && s->collide.size() == 8 && s->collide[0] == 0xc && s->collide[6] == L.q_collisions - L.q_bitmap && s->collide[7] == 0, buf);
	std::snprintf(buf, sizeof buf, "%s: the player's trace maps dense 5 to pool slot 0", tag);
	check(s && s->manager[L.q_collisions + L.c_map + 5] == 0 && manager_u32(*s, L.q_collisions + L.c_key_count) == 1 && manager_u32(*s, L.q_collisions + L.c_slot_count) == 1
	      && *reinterpret_cast<const std::uint16_t*>(s->manager.data() + L.q_collisions + L.c_keys) == 5 && u32at(s->pool.data(), 0xd64) == 1, buf);
	s = step("q0 trace (slot 0x74)");
	std::snprintf(buf, sizeof buf, "%s: slot 0x74 stores the handle physics returned and two more traces", tag);
	check(s && s->values.size() == 1 && s->values[0] == fx::handles[0] && manager_u32(*s, L.q_collisions + L.c_slot_count) == 3 && u32at(s->pool.data(), 0xd64) == 3, buf);
	s = step("q0 result/player trace lookups (slots 0x20/0x24/0x28/0x2c)");
	std::snprintf(buf, sizeof buf, "%s: result trace, not-empty, player trace found", tag);
	check(s && s->values.size() == 4 && s->values[0] != 0xffffffff && s->values[1] == 0 && s->values[2] == 1 && s->values[3] == 0, buf);
	s = step("order fill with the trace policy (0x4a9730 / 0x4a9a80)");
	std::snprintf(buf, sizeof buf, "%s: policy carries the retail vtable and query 0; order 0 filled", tag);
	check(s && s->values.size() == 4 && s->values[0] == 1 && s->values[1] == eq::native_policy_vtable && s->values[2] == 0 && s->values[3] == 0, buf);
	if (s)
	{
		// ascending scores
		const auto count = manager_u32(*s, L.m_orders + L.o_count);
		bool ascending = count > 0;
		for (std::uint32_t i = 1; i < count; ++i)
		{
			float a, b;
			std::memcpy(&a, s->manager.data() + L.m_orders + L.o_pairs + (i - 1) * 8 + 4, 4);
			std::memcpy(&b, s->manager.data() + L.m_orders + L.o_pairs + i * 8 + 4, 4);
			if (b < a) ascending = false;
		}
		std::snprintf(buf, sizeof buf, "%s: trace-policy order is sorted ascending by score (%u pairs)", tag, count);
		check(ascending, buf);
	}
	s = step("fourth order fill refused");
	std::snprintf(buf, sizeof buf, "%s: fourth concurrent order fill returns null", tag);
	check(s && s->values.size() == 1 && s->values[0] == 0xffffffff, buf);
	s = step("order 1 reused");
	std::snprintf(buf, sizeof buf, "%s: released order 1 is reused", tag);
	check(s && s->values.size() == 1 && s->values[0] == 1, buf);
	s = step("release q0 and q1 (pool restored)");
	std::snprintf(buf, sizeof buf, "%s: releasing every query returns every trace and frees all three queries", tag);
	check(s && s->values.size() == 1 && s->values[0] == 3 && u32at(s->pool.data(), 0xd64) == 0 && u32at(s->pool.data(), 0xd58) == 50, buf);
	s = step("trace record accessors (0x4a9580..0x4a9630, 0x4a97a0)");
	std::snprintf(buf, sizeof buf, "%s: record accessors answer offsets 0/0x10/0x20/0x30 and the zero vector for null", tag);
	check(s && s->values.size() == 24 && s->values[0] == 1 && s->values[3] == 0 && s->values[4] == 0x10 && s->values[5] == 0x20 && s->values[6] == 0x30 && s->values[11] == 0 && s->values[14] == 0 && s->values[15] == 0 && s->values[16] == 0 && s->values[17] == 0, buf);
	std::snprintf(buf, sizeof buf, "%s: the result record resolves entity 0 (not the player); flagged entity 100 answers its material 0x24", tag);
	check(s && s->values.size() == 24 && s->values[1] == 0 && s->values[2] == 0 && s->values[22] == 0 && s->values[23] == 0x24, buf);
}
// Boundary checks of the port alone at a capacity (set bits 0, N-1, N-2, 257 when wide, duplicates, full, recycle).
void boundary_tests(const descriptor& desc)
{
	using namespace fx;
	populate(desc);
	reset_trace_pool();
	driver drv = make_driver(mode::port);
	current = &drv;
	drv.manager = static_cast<std::uint8_t*>(eq::create_manager(desc));
	char buf[160];
	const char* tag = desc.world == 256 ? "stock" : "raised";
	std::snprintf(buf, sizeof buf, "%s: manager created with size %x", tag, static_cast<unsigned>(eq::manager_size(desc)));
	check(drv.manager && eq::manager_size(desc) == L.m_size, buf);
	void* ref = nullptr;
	drv.acquire_all(&ref, nullptr, 0);
	void* q = ref;
	auto* set = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(q) + L.q_bitmap);
	auto empty = [&] { for (std::uint32_t w = 0; w < L.set_words; ++w) if (set[w]) return false; return true; };
	std::snprintf(buf, sizeof buf, "%s: reset with 0 empties the set; first entity is null", tag);
	check(empty() && vslot<void* (__fastcall*)(void*, void*)>(q, 0x14)(q, nullptr) == nullptr, buf);
	const std::uint32_t last = desc.world - 1;
	for (auto i : {0u, last, 255u, 254u})
	{
		vslot<set_op_fn>(q, 0x30)(q, nullptr, 0, &handles[i]);
	}
	std::snprintf(buf, sizeof buf, "%s: bits 0, 254, 255 and %u set through handles", tag, last);
	check(((set[0] & 1u) != 0) && ((set[last >> 5] >> (last & 31)) & 1u) && ((set[7] >> 31) & 1u) && ((set[7] >> 30) & 1u), buf);
	std::snprintf(buf, sizeof buf, "%s: first entity is dense 0, highest is %u", tag, last);
	check(dense_of_entity(vslot<void* (__fastcall*)(void*, void*)>(q, 0x14)(q, nullptr)) == 0 && first_live_in(set, 1) == 254 && first_live_in(set, 256) == (desc.world > 256 ? last : L.world), buf);
	vslot<set_op_fn>(q, 0x30)(q, nullptr, 1, &handles[0]); vslot<set_op_fn>(q, 0x30)(q, nullptr, 1, &handles[0]);
	std::snprintf(buf, sizeof buf, "%s: removing a handle twice is idempotent", tag);
	check((set[0] & 1u) == 0 && dense_of_entity(vslot<void* (__fastcall*)(void*, void*)>(q, 0x14)(q, nullptr)) == 254, buf);
	// a token of the wrong generation for a live slot is refused by the World and changes nothing
	const std::uint32_t wrong = desc.tokens.token(3, DenseIndex{254}).value;
	vslot<set_op_fn>(q, 0x30)(q, nullptr, 1, &wrong);
	std::snprintf(buf, sizeof buf, "%s: a token with another generation does not touch its slot", tag);
	check(((set[7] >> 30) & 1u) != 0, buf);
	if (desc.world > 256)
	{
		// dense 257 (token 0x501 at generation 1... here its minted generation) exists and is found by dense lookup
		vslot<byte_fn>(q, 0x10)(q, nullptr, 0);
		vslot<set_op_fn>(q, 0x30)(q, nullptr, 0, &handles[257]);
		const auto first = vslot<void* (__fastcall*)(void*, void*)>(q, 0x14)(q, nullptr);
		check(dense_of_entity(first) == 257 && ((set[8] >> 1) & 1u), "raised: handle 257 sets bit 257 and slot 0x14 resolves entity 257 by dense index");
		const float origin[3] = {entities[257].pos[0], entities[257].pos[1], entities[257].pos[2]};
		const auto nearest = vslot<void* (__fastcall*)(void*, void*, const float*, std::uint32_t)>(q, 0x18)(q, nullptr, origin, 7);
		const auto nearest_v = vslot<void* (__fastcall*)(void*, void*, const float*, std::uint32_t)>(q, 0x1c)(q, nullptr, origin, 7);
		check(nearest == nullptr && nearest_v == nullptr, "raised: slots 0x18/0x1c do NOT resolve dense 257 (LegacyEmptyDenseDisposition: 257 is not token 257)");
		check(world_find_token(&world, nullptr, 257) == nullptr && world_find_token(&world, nullptr, handles[257]) == &entities[257] && desc.tokens.index({handles[257]}).value == 257,
		      "raised: token 257 is null while the minted token of slot 257 resolves (fixture World agrees with the plan's domains)");
		// traces keyed by dense index above 255: map/keys hold the full dense index, not the low byte
		auto* c = static_cast<std::uint8_t*>(q) + L.q_collisions;
		auto* rec257 = insert_trace(c, 257);
		auto* rec1 = insert_trace(c, 1);
		const auto key0 = *reinterpret_cast<std::uint16_t*>(c + L.c_keys);
		check(rec257 && rec1 && c[L.c_map + 257] == 0 && c[L.c_map + 1] == 1 && c[L.c_map + 0] == 0xff && key0 == 257 && u32at(c, L.c_key_count) == 2 && u32at(c, L.c_slot_count) == 2,
		      "raised: trace insert maps dense 257 (not 1) to pool slot 0; dense 1 to slot 1; key is 257");
		std::uint32_t out = 0;
		vslot<set_op_fn>(q, 0x30)(q, nullptr, 0, &handles[1]);
		// order fill: slot 8 finds the trace of entity 257 through the wide map
		const void* policy = dense_policy_vtable;
		void* order = drv.order_fill(q, &policy);
		vslot<void (__fastcall*)(void*, void*, std::uint32_t*, std::uint32_t)>(order, 0x8)(order, nullptr, &out, 0);
		const auto entity0 = vslot<void* (__fastcall*)(void*, void*, std::uint32_t)>(order, 0x4)(order, nullptr, 0);
		check(order && vslot<std::uint32_t (__fastcall*)(void*, void*)>(order, 0x0)(order, nullptr) == 2 && out == va_of(dense_of_entity(entity0) == 257 ? rec257 : rec1),
		      "raised: order slot 8 resolves the trace of a > 255 entity through the wide map");
		// the trace policy scores entity 257 from its own record, not from dense 1's
		float score = 7.5f; std::memcpy(rec257 + 0x3c, &score, 4); score = -2.0f; std::memcpy(rec1 + 0x3c, &score, 4);
		void* trace_policy[2];
		check(eq::QueryApi_004a9730(trace_policy, nullptr, &ref) == trace_policy, "0x4a9730 returns the policy it constructed (EAX = this; 0x441950 pushes it as 0x4ab770's policy)");
		check(eq::QueryApi_004ab7d0(nullptr) == nullptr && eq::QueryApi_004ab7d0(order) == order, "0x4ab7d0 returns its argument (EAX = [esp+4])");
		check(eq::QueryApi_004a9a80(trace_policy, nullptr, reinterpret_cast<const std::uint8_t*>(&entities[257])) == 7.5 && eq::QueryApi_004a9a80(trace_policy, nullptr, reinterpret_cast<const std::uint8_t*>(&entities[1])) == -2.0
		      && eq::QueryApi_004a9a80(trace_policy, nullptr, reinterpret_cast<const std::uint8_t*>(&entities[1023])) == 0.0, "raised: 0x4a9a80 scores 257 and 1 from their own records, unmapped 1023 as 0");
		drv.order_release(order);
		// 1024 pairs: a full set sorts
		vslot<byte_fn>(q, 0x10)(q, nullptr, 0);
		for (std::uint32_t i = 0; i < 1024; ++i) { live[i] = true; handles[i] = desc.tokens.token(1, DenseIndex{i}).value; entities[i].vtable = entity_vtable; entities[i].token = handles[i]; entities[i].dense = i; set[i >> 5] |= 1u << (i & 31); }
		order = drv.order_fill(q, &policy);
		bool ascending = true;
		for (std::uint32_t i = 1; i < 1024; ++i)
		{
			float a, b;
			std::memcpy(&a, static_cast<std::uint8_t*>(order) + L.o_pairs + (i - 1) * 8 + 4, 4);
			std::memcpy(&b, static_cast<std::uint8_t*>(order) + L.o_pairs + i * 8 + 4, 4);
			if (b < a) ascending = false;
		}
		check(order && vslot<std::uint32_t (__fastcall*)(void*, void*)>(order, 0x0)(order, nullptr) == 1024 && ascending && dense_of_entity(vslot<void* (__fastcall*)(void*, void*, std::uint32_t)>(order, 0x4)(order, nullptr, 1023)) < 1024,
		      "raised: a full 1024-entity set fills 1024 sorted pairs");
		drv.order_release(order);
		populate(desc); // restore the boundary population
		vslot<byte_fn>(q, 0x10)(q, nullptr, 0); // releases the two traces above
		check(u32at(c, L.c_slot_count) == 0 && u32at(at(eq::native_trace_pool), 0xd64) == 0, "raised: reset releases the query's traces");
		// duplicate insert of one handle: two traces, the map keeps the latest, release returns both
		auto* first_rec = insert_trace(c, 1023);
		auto* second_rec = insert_trace(c, 1023);
		check(first_rec && second_rec && first_rec != second_rec && c[L.c_map + 1023] == pool_index(second_rec) && u32at(c, L.c_slot_count) == 2 && u32at(c, L.c_key_count) == 2
		      && u32at(at(eq::native_trace_pool), 0xd64) == 2, "raised: duplicate insert keeps both pool slots, the map points at the latest");
		// pool exhaustion: inserts beyond 50 return null and change nothing
		std::uint32_t inserted = 2;
		while (inserted < 50) { if (!insert_trace(c, inserted % 1024)) break; ++inserted; }
		const auto before_count = u32at(c, L.c_slot_count);
		check(inserted == 50 && insert_trace(c, 300) == nullptr && u32at(c, L.c_slot_count) == before_count && u32at(at(eq::native_trace_pool), 0xd64) == 50,
		      "raised: the 51st trace is refused (the 50-record pool is not widened)");
		const std::uint8_t first_returned = c[L.c_slots];
		drv.release(&ref, nullptr);
		check(u32at(at(eq::native_trace_pool), 0xd64) == 0 && u32at(at(eq::native_trace_pool), 0xd58) == 50 && c[L.c_map + 1023] == 0xff && c[L.c_map + 257] == 0xff && u32at(c, L.c_key_count) == 0,
		      "raised: release returns all 50 traces and clears every used map entry");
		// recycle: the ring hands slots back FIFO (the first slot returned is the first handed out again)
		drv.acquire_all(&ref, nullptr, 0);
		auto* again = insert_trace(c, 2);
		check(pool_index(again) == first_returned, "raised: after a full release the first slot handed out is the first one returned (FIFO ring)");
	}
	drv.release(&ref, nullptr);
	check(drv.free_count() == 3 && eq::owns_query(q) && !eq::owns_query(drv.manager + L.m_tail) && eq::owns_order(drv.manager + L.m_orders) && !eq::owns_order(q), "ownership predicates distinguish the manager's queries and orders");
	eq::destroy_manager(drv.manager);
	check(eq::installed_manager() == nullptr, "manager destroyed");
	current = nullptr;
}

LONG WINAPI report_crash(EXCEPTION_POINTERS* info)
{
	const auto& r = *info->ExceptionRecord;
	const auto& c = *info->ContextRecord;
	std::printf("CRASH exception %08lx at %08lx (eax %08lx ecx %08lx edx %08lx esp %08lx ebp %08lx)\n", r.ExceptionCode, va_of(r.ExceptionAddress), c.Eax, c.Ecx, c.Edx, c.Esp, c.Ebp);
	if (r.NumberParameters >= 2) std::printf("  access %s at %08lx\n", r.ExceptionInformation[0] ? "write" : "read", static_cast<unsigned long>(r.ExceptionInformation[1]));
	const auto* stack = reinterpret_cast<const std::uint32_t*>(c.Esp);
	std::printf("  stack:");
	for (int i = 0; i < 16; ++i) std::printf(" %08lx", static_cast<unsigned long>(stack[i]));
	std::printf("\n");
	return EXCEPTION_EXECUTE_HANDLER;
}
extern "C" __declspec(dllexport) int __cdecl run_entity_query_test()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	SetUnhandledExceptionFilter(&report_crash);
	if (va_of(GetModuleHandleW(nullptr)) != 0x400000) { std::puts("FAIL test image is not based at 0x400000"); return 2; }
	if (!find_fixture_section()) { std::puts("FAIL the launcher has no .peerfix section"); return 2; }
	int argc = 0;
	wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::string exe_path;
	if (argc > 1) { char narrow[MAX_PATH * 2]{}; WideCharToMultiByte(CP_ACP, 0, wargv[1], -1, narrow, sizeof narrow, nullptr, nullptr); exe_path = narrow; }
	LocalFree(wargv);
	fx::build_tables();
	eq::set_manager_getter(&manager_select);
	const bool covers = fixture_covers(0x419c90, 0x779000);
	std::printf("fixture %x..%x %s\n", va_of(fixture_begin), va_of(fixture_begin) + static_cast<std::uint32_t>(fixture_size), covers ? "covers the retail query addresses" : "does NOT cover the retail query addresses");
	check(covers, "fixture section covers 0x419c90..0x779000");
	if (!covers) return 2;
	if (!writable(va_of(fixture_begin), fixture_size)) { std::puts("FAIL cannot make the fixture section writable/executable"); return 2; }
	if (!exe_path.empty())
	{
		std::ifstream file(exe_path, std::ios::binary);
		binary.assign(std::istreambuf_iterator<char>(file), {});
		if (binary.empty()) { std::printf("cannot read %s\n", exe_path.c_str()); return 2; }
	}
	// ---- differential: native query code vs the port, same script, stock capacity ----
	if (!binary.empty() && install_native_image())
	{
		// Twice: at the process default x87 precision (53-bit) and at the 24-bit precision a Direct3D 8 device sets
		// unless FPU_PRESERVE is requested. The port's x87 sequences are the native ones, so both must agree.
		for (const auto precision : {_PC_53, _PC_24})
		{
			_controlfp_s(nullptr, precision, _MCW_PC);
			const char* tag = precision == _PC_53 ? "53-bit" : "24-bit";
			std::printf("native image installed; constructing the retail manager (x87 precision %s)\n", tag);
			native_bytes(0x4a9a80, 0x10); // the retail trace-score callback for the native run
			fx::populate(stock_descriptor);
			reset_trace_pool();
			driver native_run = make_driver(mode::native);
			std::memset(at(0x756b10), 0, 0x1dec);
			native<void* (__fastcall*)(void*, void*)>(0x4ab4e0)(at(0x756b10), nullptr); // the retail manager constructor on the retail static
			native_run.manager = at(0x756b10);
			std::puts("retail manager constructed; running the native script");
			run_script(native_run);
			expect_script(native_run, "native");
			jump(0x4a9a80, reinterpret_cast<const void*>(&eq::QueryApi_004a9a80)); // the integrator's entry jump, after the native run
			fx::populate(stock_descriptor);
			reset_trace_pool();
			driver port_run = make_driver(mode::port);
			port_run.manager = static_cast<std::uint8_t*>(eq::create_manager(stock_descriptor));
			check(port_run.manager != nullptr, "port manager created at stock capacity");
			run_script(port_run);
			expect_script(port_run, "port@256");
			char message[200];
			std::snprintf(message, sizeof message, "differential (x87 %s): every step of the port equals the retail query code (manager bytes, trace pool, results, physics calls)", tag);
			check(compare_runs(native_run, port_run), message);
			std::printf("differential steps compared: %zu\n", native_run.steps.size());
			eq::destroy_manager(port_run.manager);
		}
		_controlfp_s(nullptr, _PC_53, _MCW_PC);
		// the stock-layout fallback of 0x4a9a80 for a query that is not the DLL's: the retail static at 0x756b10
		{
			void* native_query = at(0x756b10);
			void* policy[2] = {nullptr, native_query};
			auto* c = at(0x756b10) + 0x38;
			std::memset(c + 4, 0xff, 256);
			c[4 + 5] = 7;
			float score = 2.5f; std::memcpy(at(eq::native_trace_pool) + 7 * 0x40 + 0x3c, &score, 4);
			check(eq::QueryApi_004a9a80(policy, nullptr, reinterpret_cast<const std::uint8_t*>(&fx::entities[5])) == 2.5, "0x4a9a80 reads the retail +0x38 collision layout for a query that is not the DLL's");
		}
	}
	else
	{
		std::puts("SKIPPED differential: no retail exe given (or the fixture could not be placed)");
		binary.clear();
		install_synthetic_stubs();
		install_helper_stubs();
	}
	// ---- synthetic: the port alone at both capacities (retail helpers when the exe is loaded, stubs otherwise) ----
	for (const descriptor& desc : {stock_descriptor, raised_descriptor_for(0, 0, 0, 0)})
	{
		fx::populate(desc);
		reset_trace_pool();
		driver run = make_driver(mode::port);
		run.manager = static_cast<std::uint8_t*>(eq::create_manager(desc));
		char buf[80];
		std::snprintf(buf, sizeof buf, "synthetic@%u: manager created", desc.world);
		check(run.manager != nullptr, buf);
		run_script(run);
		expect_script(run, desc.world == 256 ? "synthetic@256" : "synthetic@1024");
		eq::destroy_manager(run.manager);
		boundary_tests(desc);
	}
	std::printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
