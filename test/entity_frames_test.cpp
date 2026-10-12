// Phase A entity capacity: the widened caller frames (entity_frames.cpp) against scripted World/Grid/Query/
// entity/model fixtures, with no game process and no game content.
//  1. Synthetic, at world 256 and 1024: every frame, sets of 128 bytes end to end, loops over 1024, outputs at the
//     dense indices 0/255/256/257/1023, the 0x44ea40 clear widening, canaries around every caller-provided set.
//  2. Differential, at 256: the retail frame code copied from a private executable given on the command line
//     (argv[1] or XML2_FIX_TEST_EXE) into this image's reserved range, with the in-region helpers it calls, run on
//     the same scripted sequences as the port; return values, sets, every vtable call (order and arguments), the
//     trace-pool records and the fixture objects' state must match. Without the executable it reports SKIPPED.
//  3. The exhaustive caller check is offline (notes); its dispositions are in the report.
#include <Windows.h>
#include "entity_frames.hpp"
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace entity_capacity_layout;
using namespace entity_frames;
namespace ec = entity_capacity;

// The retail image range 0x401000..0x801000 is this image's own: the CMake block generates a 4 MB block of
// code-class zeros to .text$a, which the linker places before the compiler's .text$mn (protection is raised to
// read/write/execute at runtime).
extern "C" std::uint8_t fixture_space[0x400000];

int failures = 0;
bool verbose = std::getenv("XML2_FRAMES_VERBOSE") != nullptr;
std::string last_dump; // the most recent scenario's snapshot, printed under a failing check
void check(bool value, const char* message)
{
	std::printf("%s %s\n", value ? "PASS" : "FAIL", message);
	failures += !value;
	if (!value && !last_dump.empty()) std::printf("%s", last_dump.c_str());
}
#define MARK if (verbose) std::printf("-> %s\n", __func__)
#define FIN(snapshot) remember(snapshot, __func__)
template <typename T> T& at(std::uint32_t va) { return *reinterpret_cast<T*>(static_cast<std::uintptr_t>(va)); }
void* at_ptr(std::uint32_t va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }
void jump(std::uint32_t site, const void* target)
{
	auto* code = static_cast<std::uint8_t*>(at_ptr(site));
	code[0] = 0xe9;
	const auto delta = std::int32_t(reinterpret_cast<std::uintptr_t>(target) - site - 5);
	std::memcpy(code + 1, &delta, 4);
}
std::vector<std::string> call_log;
void logf(const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof line, format, args);
	va_end(args);
	call_log.emplace_back(line);
}
std::string v3(const float* v) { char t[96]; std::snprintf(t, sizeof t, "(%g,%g,%g)", v[0], v[1], v[2]); return t; }

// ---- the descriptor under test ----------------------------------------------------------------------------------
descriptor active = stock_descriptor;
const descriptor raised = raised_descriptor_for(0, 0, 0, 0);
std::uint32_t cap() { return active.world; }
std::uint32_t token_of(std::uint32_t dense) { return active.tokens.token(1, DenseIndex{dense}).value; }
std::uint32_t dense_of(std::uint32_t token) { return active.tokens.index(WorldToken{token}).value; }
void use(const descriptor& d) { active = d; set_descriptor(&active); }

// ---- entities --------------------------------------------------------------------------------------------------
struct FixEntity
{
	void** vt;                     // +0
	std::uint32_t flags4;          // +4
	std::uint8_t pad8[0x14];       // +8
	std::uint32_t token;           // +0x1c
	float position[3];             // +0x20
	std::uint8_t pad2c[0xc];       // +0x2c
	float aabb[6];                 // +0x38 (slot 0x4c)
	float nav_box[6];              // +0x50 (0x491445)
	std::uint8_t pad68[0x120 - 0x68];
	float velocity[3];             // +0x120
	std::uint8_t pad12c[0x158 - 0x12c];
	float bounds[6];               // +0x158 (slot 0x38, CActor)
	std::uint8_t pad170[0x258 - 0x170];
	std::uint8_t byte258;          // +0x258
	std::uint8_t pad259[0x284 - 0x259];
	std::uint32_t field284;        // +0x284
	std::uint8_t pad288[0x31c - 0x288];
	std::uint8_t byte31c;          // +0x31c
	std::uint8_t tail[0x400 - 0x31d];
	// scripting
	int id;
	std::uint8_t class_descriptor[0x40]; // bitmap at +0x14
	std::uint32_t kind;
	void* model;
	bool has_aabb;
	float half[3];
};
static_assert(offsetof(FixEntity, token) == 0x1c && offsetof(FixEntity, aabb) == 0x38 && offsetof(FixEntity, nav_box) == 0x50 && offsetof(FixEntity, velocity) == 0x120 &&
              offsetof(FixEntity, bounds) == 0x158 && offsetof(FixEntity, byte258) == 0x258 && offsetof(FixEntity, field284) == 0x284 && offsetof(FixEntity, byte31c) == 0x31c);
void* entity_vtable[0x1a8 / 4];
const std::uint8_t* __fastcall e_class(FixEntity* self, void*) { return self->class_descriptor; }
float* __fastcall e_position(FixEntity* self, void*, float* out) { std::memcpy(out, self->position, 12); logf("e24(%d)", self->id); return out; }
float* __fastcall e_half(FixEntity* self, void*, float* out) { std::memcpy(out, self->half, 12); logf("e28(%d)", self->id); return out; }
float* __fastcall e_bounds(FixEntity* self, void*) { logf("e38(%d)", self->id); return self->bounds; }
float* __fastcall e_aabb(FixEntity* self, void*) { return self->has_aabb ? self->aabb : nullptr; }
std::uint32_t __fastcall e_kind(FixEntity* self, void*) { return self->kind; }
void* __fastcall e_model(FixEntity* self, void*) { return self->model; }
float* __fastcall e_matrix(FixEntity* self, void*, float* out)
{
	for (int i = 0; i < 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	out[12] = self->position[0]; out[13] = self->position[1]; out[14] = self->position[2];
	logf("e58(%d)", self->id);
	return out;
}
void __fastcall e_notify(FixEntity* self, void*, int a, int b, int c) { logf("ebc(%d,%d,%d,%d)", self->id, a, b, c); }
void __fastcall e_scale(FixEntity* self, void*, std::uint32_t bits) { float f; std::memcpy(&f, &bits, 4); logf("e1a4(%d,%g)", self->id, f); }
void set_class(FixEntity& e, int id, bool on)
{
	const std::uint32_t bit = std::uint32_t(id) + 0x24;
	auto* word = reinterpret_cast<std::uint32_t*>(e.class_descriptor + 0x14 + (bit >> 5) * 4);
	if (on) *word |= 1u << (bit & 31); else *word &= ~(1u << (bit & 31));
}

// ---- collision models, the physics world and the trace filter -------------------------------------------------------
struct FixModel { void** vt; int id; bool ok10, ok14; float hit[3]; float t = 0.5f; }; // t: the hit distance the model writes at hit+0x3c (record+0x74)
void* model_vtable[8];
// A model writes the whole 64-byte hit block at record+0x38: point, normal (+0x20), distance (+0x3c).
void write_hit(const FixModel* self, float* hit) { std::memset(hit, 0, 0x40); std::memcpy(hit, self->hit, 12); hit[8] = 0; hit[9] = 1; hit[10] = 0; hit[15] = self->t; }
std::uint8_t __fastcall m_sweep9(FixModel* self, void*, const float* a, const float* b, const float* c, std::uint32_t d, std::uint32_t e, std::uint32_t f, std::uint32_t g, float* hit, const float* m)
{
	logf("m10(%d,%s,%s,%s,%u,%u,%u,%u,%g,%s)", self->id, v3(a).c_str(), v3(b).c_str(), v3(c).c_str(), d, e, f, g, m[0], v3(m + 12).c_str());
	write_hit(self, hit);
	return self->ok10;
}
std::uint8_t __fastcall m_sweep10(FixModel* self, void*, const float* dir, const float* b, const float* a, std::uint32_t r2c, std::uint32_t r30, std::uint32_t b34, std::uint32_t b35, float* hit, const float* m1, const float* m2)
{
	logf("m14(%d,%s,%s,%s,%u,%u,%u,%u,%g,%g)", self->id, v3(dir).c_str(), v3(b).c_str(), v3(a).c_str(), r2c, r30, b34, b35, m1[12], m2[12]);
	write_hit(self, hit);
	return self->ok14;
}
FixModel physics_world{model_vtable, 90, true, true, {7, 8, 9}, 0.5f};
void* __cdecl stub_physics_world() { logf("56c7f0"); return &physics_world; }

struct FixFilter { void** vt; };
void* filter_vtable[2];
alignas(16) std::uint8_t trace_pool[50][64];
int pool_used = 0;
std::uint8_t* __fastcall f_take(FixFilter*, void*, std::uint32_t* token)
{
	logf("filter(%u)", *token);
	if (pool_used >= 50) return nullptr;
	return trace_pool[pool_used++];
}
FixFilter filter{filter_vtable};

// ---- the World and the Grid --------------------------------------------------------------------------------------------
struct FixWorld
{
	void** vt;
	std::array<void*, 1024> entities{};
	std::array<std::uint32_t, 1024> handles{};
	std::uint32_t player = 0;
	std::map<std::string, std::vector<std::uint32_t>> named;
};
void* world_vtable[0x68 / 4];
FixWorld world;
void* __fastcall w_entity_of_token(FixWorld* self, void*, std::uint32_t* handle)
{
	const auto dense = dense_of(*handle);
	logf("w0c(%u)", *handle);
	return dense < cap() && self->handles[dense] == *handle ? self->entities[dense] : nullptr;
}
void* __fastcall w_entity_of_dense(FixWorld* self, void*, std::uint32_t dense) { return dense < cap() ? self->entities[dense] : nullptr; }
std::uint8_t __fastcall w_valid(FixWorld* self, void*, std::uint32_t* handle)
{
	const auto dense = dense_of(*handle);
	logf("w14(%u)", *handle);
	return dense < cap() && self->handles[dense] == *handle && self->entities[dense];
}
std::uint32_t* __fastcall w_handle_of_dense(FixWorld* self, void*, std::uint32_t* out, std::uint32_t dense) { logf("w18(%u)", dense); *out = self->handles[dense]; return out; }
std::uint32_t* __fastcall w_player(FixWorld* self, void*, std::uint32_t* out) { logf("w24"); *out = self->player; return out; }
std::uint8_t __fastcall w_find_named(FixWorld* self, void*, const char* name, EntitySet* set)
{
	logf("w34(%s)", name);
	for (std::uint32_t w = 0; w < cap() / 32; ++w) set->words[w] = 0; // the native World slot fills eight dwords
	const auto it = self->named.find(name);
	if (it == self->named.end()) return 0;
	for (auto dense : it->second) set->set(dense);
	return 1;
}
void* __cdecl stub_world_getter() { return &world; }
void* __cdecl stub_entity_of_dense(std::uint32_t dense) { return w_entity_of_dense(&world, nullptr, dense); }
std::uint32_t* __cdecl stub_player_handle(std::uint32_t* out) { return w_player(&world, nullptr, out); }
void* __fastcall stub_entity_of_handle(std::uint32_t* handle, void*) { return w_entity_of_token(&world, nullptr, handle); }
std::uint8_t __fastcall stub_handle_valid(std::uint32_t* handle, void*) { return w_valid(&world, nullptr, handle); }

struct FixGrid { void** vt; EntitySet candidates; bool ok; };
void* grid_vtable[0x44 / 4];
FixGrid grid;
std::uint8_t __fastcall g_cells(FixGrid* self, void*, const float* lo, const float* hi, EntitySet* out)
{
	logf("g30(%s,%s)", v3(lo).c_str(), v3(hi).c_str());
	for (std::uint32_t w = 0; w < cap() / 32; ++w) out->words[w] = self->candidates.words[w]; // the native Grid writes world/32 dwords
	return self->ok;
}
void* __cdecl stub_grid_getter() { return &grid; }

// ---- box and sphere tests (external to the ported frames: 0x563160 / 0x5631d0 are stubbed geometrically) ----------------
std::uint8_t __fastcall stub_sphere(const float* sphere, void*, const float* bounds)
{
	bool hit = true;
	for (int k = 0; k < 3; ++k) hit = hit && sphere[k] + sphere[3] >= bounds[k] && sphere[k] - sphere[3] <= bounds[k + 3];
	logf("sphere(%g,%s)", sphere[3], v3(bounds).c_str());
	return hit;
}
// synthetic stand-ins for 0x44d010 / 0x44d070 (the native bytes replace them in differential mode)
std::uint8_t __fastcall standin_box_overlaps(const float* b, void*, const float* box)
{
	for (int k = 0; k < 3; ++k) { if (b[k] > box[k + 3]) return 0; if (b[k + 3] < box[k]) return 0; }
	return 1;
}
std::uint8_t __fastcall standin_box_inside(const float* b, void*, const float* box)
{
	for (int k = 0; k < 3; ++k) { if (!(b[k + 3] <= box[k + 3])) return 0; if (!(b[k] >= box[k])) return 0; }
	return 1;
}

// ---- capsule sweeps (0x600dc0 / 0x601280), scripted ----------------------------------------------------------------
struct CapsuleScript { std::uint8_t f144, f145, f146, f148; float t; float hit[3], add[3], normal[3]; } capsule_script{};
void __cdecl stub_capsule(void* capsule)
{
	auto* ctx = static_cast<std::uint8_t*>(capsule) - 0xf0;
	logf("600dc0(%s,%g,%g)", v3(reinterpret_cast<float*>(ctx + 0x110)).c_str(), *reinterpret_cast<float*>(ctx + 0x11c), *reinterpret_cast<float*>(ctx + 0x120));
	ctx[0x144] = capsule_script.f144; ctx[0x145] = capsule_script.f145; ctx[0x146] = capsule_script.f146; ctx[0x148] = capsule_script.f148;
	std::memcpy(ctx + 0x13c, &capsule_script.t, 4);
	std::memcpy(ctx + 0x158, capsule_script.hit, 12); std::memcpy(ctx + 0x164, capsule_script.add, 12); std::memcpy(ctx + 0x170, capsule_script.normal, 12);
}
void __cdecl stub_capsule_ratio(void* capsule) { logf("601280"); stub_capsule(capsule); }

// ---- navigation ------------------------------------------------------------------------------------------------
struct FixComponent { void** vt; std::uint32_t flags; float position[3]; int id; };
void* component_vtable[0x28 / 4];
float* __fastcall c_position(FixComponent* self, void*, float* out) { std::memcpy(out, self->position, 12); logf("c24(%d)", self->id); return out; }
std::map<const void*, FixComponent*> nav_components, nav_components_b;
std::map<const void*, bool> nav_reachable;
void* __cdecl stub_nav_component(void* entity) { const auto it = nav_components.find(entity); return it == nav_components.end() ? nullptr : it->second; }
void* __cdecl stub_nav_component_b(void* entity) { const auto it = nav_components_b.find(entity); return it == nav_components_b.end() ? nullptr : it->second; }
std::uint8_t __fastcall stub_nav_reachable(FixComponent* self, void*, const float* point) { logf("4c2590(%d,%s)", self->id, v3(point).c_str()); return nav_reachable[self]; }

struct FixPathGrid
{
	std::uint8_t raw[0x20c00];
	std::uint8_t cells[16][16][8];
	float lo[3], hi[3], center[3];
};
FixPathGrid path_grid;
void __fastcall stub_pathgrid_box(FixPathGrid* self, void*, std::uint8_t* cell, float* lo, float* hi) { logf("490a80(%u,%u,%u)", cell[0], cell[1], cell[2]); std::memcpy(lo, self->lo, 12); std::memcpy(hi, self->hi, 12); }
void __fastcall stub_pathgrid_center(FixPathGrid* self, void*, std::uint8_t* cell, float* out) { logf("490980(%u)", cell[0]); std::memcpy(out, self->center, 12); }
std::uint8_t* __fastcall stub_pathgrid_cell(FixPathGrid* self, void*, std::uint32_t x, std::uint32_t y) { logf("492450(%u,%u)", x, y); return self->cells[x & 15][y & 15]; }
std::uint8_t* __fastcall stub_pathgrid_cell_b(FixPathGrid* self, void*, std::uint32_t x, std::uint32_t y) { logf("493290(%u,%u)", x, y); return self->cells[x & 15][y & 15]; }

// ---- names, formatting and the order helpers ----------------------------------------------------------------------
std::uint8_t name_table[0x9000];
std::uint32_t name_table_used = 0;
std::uint32_t add_name(const char* text)
{
	const std::uint32_t token = ++name_table_used; // tokens 1.. (0 means none)
	std::uint32_t next_string = 0;
	for (std::uint32_t t = 1; t < token; ++t) { std::uint32_t o; std::memcpy(&o, name_table + 4 + t * 4, 4); next_string = o + std::uint32_t(std::strlen(reinterpret_cast<char*>(name_table + 0x8008 + o)) + 1); }
	std::memcpy(name_table + 0x8008 + next_string, text, std::strlen(text) + 1);
	std::memcpy(name_table + 4 + token * 4, &next_string, 4);
	return token;
}
std::uint8_t* __cdecl stub_name_table() { return name_table; }
int __cdecl stub_snprintf(char* out, int size, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const int n = std::vsnprintf(out, std::size_t(size), format, args);
	va_end(args);
	return n;
}
struct FixGame { void** vt; int difficulty; };
void* game_vtable[0x270 / 4];
FixGame game{game_vtable, 1};
int __fastcall game_difficulty(FixGame* self, void*) { logf("difficulty"); return self->difficulty; }
void* __cdecl stub_game() { return &game; }
void __fastcall stub_set_state(FixEntity* self, void*, int state) { logf("4749a0(%d,%d)", self ? self->id : -1, state); }
void __fastcall stub_timing(FixEntity* self, void*, float a, float b) { logf("4198d0(%d,%g,%g)", self ? self->id : -1, a, b); }
void __fastcall stub_order(FixEntity* self, void*, int order) { logf("41d3e0(%d,%d)", self ? self->id : -1, order); }
void __fastcall stub_flag(FixEntity* self, void*, int bit) { logf("41a1f0(%d,%d)", self ? self->id : -1, bit); }
void __fastcall stub_noop(void*, void*) {}

// ---- libIGMath imports -------------------------------------------------------------------------------------------
void __fastcall vec_set3(float* v, void*, float x, float y, float z) { v[0] = x; v[1] = y; v[2] = z; }
void __fastcall vec_set(float* v, void*, const float* s) { std::memcpy(v, s, 12); }
float __fastcall vec_length(const float* v, void*) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float __fastcall vec_length2(const float* v, void*) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; }
float __fastcall vec_normalize(float* v, void*) { const float l = vec_length(v, nullptr); if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; } return l; }
void __fastcall vec_make_min(float* v, void*, const float* s) { for (int k = 0; k < 3; ++k) if (s[k] < v[k]) v[k] = s[k]; }
void __fastcall vec_make_max(float* v, void*, const float* s) { for (int k = 0; k < 3; ++k) if (s[k] > v[k]) v[k] = s[k]; }
float* __fastcall vec_scale(const float* v, void*, float* out, const float* s) { for (int k = 0; k < 3; ++k) out[k] = v[k] * *s; return out; }
float* __fastcall vec_sub(const float* v, void*, float* out, const float* s) { for (int k = 0; k < 3; ++k) out[k] = v[k] - s[k]; return out; }
float* __fastcall vec_assign(float* v, void*, const float* s) { std::memcpy(v, s, 12); return v; }
float* __fastcall vec_index(float* v, void*, int i) { return v + i; }
float* __fastcall vec_ctor(float* v, void*) { v[0] = v[1] = v[2] = 0; return v; }
void __fastcall vec_transform(float* out, void*, const float* v, const float* m)
{
	for (int k = 0; k < 3; ++k) out[k] = v[0] * m[k] + v[1] * m[4 + k] + v[2] * m[8 + k];
}
void __fastcall mat_identity(float* m, void*) { for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f; }
void __fastcall mat_pre_scale(float* m, void*, const float* s) { for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) m[r * 4 + c] *= s[r]; }
void __fastcall mat_translation(float* m, void*, const float* t) { std::memcpy(m + 12, t, 12); }
float zero_vector[3]{};

// ---- synthetic stand-ins for the in-region helpers the port calls by address -------------------------------------------
// (0x44d940 0x44d8f0 0x44d780 0x44d720 0x44d6c0 0x44de70; replaced by the retail bytes in differential mode.)
std::uint32_t fold_flags(std::uint32_t f) // 0x44d880
{
	return ((((((((f >> 26 & 1) << 1 | f >> 14 & 1) << 1 | f >> 13 & 1) << 1 | f >> 12 & 1) << 1 | f >> 11 & 1) << 1 | f >> 10 & 1) << 1 | f >> 9 & 1) << 1 | f >> 8 & 1) << 1 | f >> 7 & 1;
}
template <typename T> T& fld(void* b, std::uint32_t o) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(b) + o); }
float* flt(void* b, std::uint32_t o) { return reinterpret_cast<float*>(static_cast<std::uint8_t*>(b) + o); }
void* __fastcall standin_record_ctor(void* record, void*)
{
	std::memset(record, 0, 0x34); fld<std::uint8_t>(record, 0x34) = 1; fld<std::uint8_t>(record, 0x35) = 0; fld<std::uint32_t>(record, 0x78) = 0;
	mat_identity(flt(record, 0x7c), nullptr);
	return record;
}
std::uint8_t __fastcall standin_record_fill(void* record, void*, FixEntity* entity)
{
	if (!entity) { fld<void*>(record, 0) = nullptr; return 0; }
	fld<void*>(record, 0) = entity;
	float tmp[3];
	std::memcpy(flt(record, 0x4), e_half(entity, nullptr, tmp), 12);
	std::memcpy(flt(record, 0x10), e_position(entity, nullptr, tmp), 12);
	std::memcpy(flt(record, 0x1c), entity->position, 12);
	fld<std::uint32_t>(record, 0x28) = entity->kind;
	fld<void*>(record, 0x78) = entity->model;
	if (entity->model) e_matrix(entity, nullptr, flt(record, 0x7c));
	fld<std::uint32_t>(record, 0x54) = fold_flags(entity->flags4);
	const auto kind = fld<std::uint32_t>(record, 0x28);
	if ((kind == 1 || kind == 4) && !fld<void*>(record, 0x78)) { fld<std::uint32_t>(record, 0x28) = 2; return 1; }
	if (kind == 0)
	{
		auto& counter = at<std::int32_t>(0x717998);
		if (counter < 100) ++counter; else if (counter == 100) counter = 0x65;
		fld<std::uint32_t>(record, 0x28) = 3; fld<void*>(record, 0x78) = nullptr;
		return 0;
	}
	return 1;
}
void* __fastcall standin_context_ctor(void* ctx, void*, FixEntity* entity, const float* start, const float* end, const float* extents, EntitySet* set)
{
	fld<void*>(ctx, 0) = nullptr; vec_set3(flt(ctx, 4), nullptr, 0, 0, 0); vec_set3(flt(ctx, 0x10), nullptr, 0, 0, 0); vec_set3(flt(ctx, 0x1c), nullptr, 0, 0, 0);
	fld<std::uint32_t>(ctx, 0x28) = fld<std::uint32_t>(ctx, 0x2c) = fld<std::uint32_t>(ctx, 0x30) = 0; fld<std::uint8_t>(ctx, 0x34) = 1; fld<std::uint8_t>(ctx, 0x35) = 0;
	fld<void*>(ctx, 0x78) = nullptr; mat_identity(flt(ctx, 0x7c), nullptr);
	fld<EntitySet*>(ctx, 0xd4) = set; fld<float>(ctx, 0xd8) = 1.0f; fld<std::uint32_t>(ctx, 0xe0) = 0;
	auto fail = [&]()
	{
		for (int w = 0; w < 8; ++w) set->words[w] = 0; // the native eight-dword clear
		std::memcpy(flt(ctx, 0xbc), zero_vector, 12); fld<float>(ctx, 0xd8) = 0; fld<std::uint32_t>(ctx, 0xe0) = 0;
	};
	if (!entity) { fld<std::uint32_t>(ctx, 0x28) = 3; fld<void*>(ctx, 0) = nullptr; fld<void*>(ctx, 0x78) = nullptr; }
	else
	{
		if (!standin_record_fill(ctx, nullptr, entity)) { fail(); auto& c = at<std::int32_t>(0x71799c); if (c < 100) ++c; else if (c == 100) c = 0x65; return ctx; }
		const auto kind = fld<std::uint32_t>(ctx, 0x28);
		if (kind == 1 || kind == 4)
		{
			float p[3]; e_position(entity, nullptr, p);
			for (int k = 0; k < 3; ++k) flt(ctx, 0xe4)[k] = start[k] - p[k];
			for (int k = 0; k < 3; ++k) flt(ctx, 0xac)[k] = flt(ctx, 0xac)[k] + flt(ctx, 0xe4)[k];
		}
	}
	std::memcpy(flt(ctx, 4), extents, 12); std::memcpy(flt(ctx, 0x10), start, 12);
	float d[3]; vec_set3(d, nullptr, end[0] - start[0], end[1] - start[1], end[2] - start[2]);
	std::memcpy(flt(ctx, 0xc8), d, 12);
	const float len = vec_length(flt(ctx, 0xc8), nullptr);
	fld<float>(ctx, 0xdc) = len;
	if (!(len > at<float>(0x6819cc)) && len == len) { fail(); std::memcpy(flt(ctx, 0xc8), zero_vector, 12); return ctx; }
	const float f = (1.0f + len) / len;
	for (int k = 0; k < 3; ++k) flt(ctx, 0xbc)[k] = f * flt(ctx, 0xc8)[k];
	std::memcpy(flt(ctx, 0xf0), flt(ctx, 0x10), 12);
	const float ex = flt(ctx, 4)[0], ey = flt(ctx, 4)[1];
	fld<float>(ctx, 0xfc) = ex > ey ? ex : ey; fld<float>(ctx, 0x100) = flt(ctx, 4)[2];
	std::memcpy(flt(ctx, 0x130), flt(ctx, 0xbc), 12);
	return ctx;
}
float* __fastcall standin_context_max(void* ctx, void*, float* out)
{
	auto* e = fld<void*>(ctx, 0); const auto kind = fld<std::uint32_t>(ctx, 0x28);
	if ((e && kind == 1) || kind == 4) for (int k = 0; k < 3; ++k) out[k] = flt(ctx, 0xe4)[k] + flt(e, 0x44)[k];
	else for (int k = 0; k < 3; ++k) out[k] = flt(ctx, 4)[k] + flt(ctx, 0x10)[k];
	return out;
}
float* __fastcall standin_context_min(void* ctx, void*, float* out)
{
	auto* e = fld<void*>(ctx, 0); const auto kind = fld<std::uint32_t>(ctx, 0x28);
	if ((e && kind == 1) || kind == 4) for (int k = 0; k < 3; ++k) out[k] = flt(ctx, 0xe4)[k] + flt(e, 0x38)[k];
	else for (int k = 0; k < 3; ++k) out[k] = flt(ctx, 0x10)[k] - flt(ctx, 4)[k];
	return out;
}
void __fastcall standin_commit(void* ctx, void*, void* record, FixFilter* f, std::uint32_t)
{
	auto* entity = fld<FixEntity*>(record, 0);
	if (!entity) return;
	std::uint32_t token = entity->token;
	std::uint8_t* pool = f_take(f, nullptr, &token);
	if (!pool) return;
	fld<std::uint32_t>(record, 0x44) = token;
	if (fld<float>(record, 0x74) < fld<float>(ctx, 0xd8)) { fld<float>(ctx, 0xd8) = fld<float>(record, 0x74); fld<std::uint32_t>(ctx, 0xe0) = token; }
	std::memcpy(pool, flt(record, 0x38), 0x40);
}

// ---- fixture installation ------------------------------------------------------------------------------------------
std::vector<std::uint8_t> binary;
std::size_t file_offset(std::uint32_t va)
{
	auto u16 = [](std::size_t o) { std::uint16_t n; std::memcpy(&n, binary.data() + o, 2); return n; };
	auto u32 = [](std::size_t o) { std::uint32_t n; std::memcpy(&n, binary.data() + o, 4); return n; };
	const auto pe = u32(0x3c); const auto sections = pe + 24 + u16(pe + 20);
	for (unsigned i = 0; i < u16(pe + 6); ++i)
	{
		const auto row = sections + i * 40; const auto rva = u32(row + 12);
		if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(row + 16)) return u32(row + 20) + va - 0x400000 - rva;
	}
	return binary.size();
}
bool native_bytes(std::uint32_t va, std::size_t n)
{
	const auto offset = file_offset(va);
	if (offset + n > binary.size()) return false;
	std::memcpy(at_ptr(va), binary.data() + offset, n);
	return true;
}
bool in_fixture(std::uint32_t va) { const auto b = reinterpret_cast<std::uintptr_t>(fixture_space); return va >= b && va + 16 <= b + sizeof fixture_space; }

void install_fixture()
{
	DWORD old = 0;
	VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old);
	// vtables
	entity_vtable[0] = &e_class; entity_vtable[0x24 / 4] = &e_position; entity_vtable[0x28 / 4] = &e_half; entity_vtable[0x38 / 4] = &e_bounds;
	entity_vtable[0x4c / 4] = &e_aabb; entity_vtable[0x50 / 4] = &e_kind; entity_vtable[0x54 / 4] = &e_model; entity_vtable[0x58 / 4] = &e_matrix;
	entity_vtable[0xbc / 4] = &e_notify; entity_vtable[0x1a4 / 4] = &e_scale;
	model_vtable[0x10 / 4] = &m_sweep9; model_vtable[0x14 / 4] = &m_sweep10;
	filter_vtable[0] = &f_take;
	world_vtable[0xc / 4] = &w_entity_of_token; world_vtable[0x10 / 4] = &w_entity_of_dense; world_vtable[0x14 / 4] = &w_valid;
	world_vtable[0x18 / 4] = &w_handle_of_dense; world_vtable[0x24 / 4] = &w_player; world_vtable[0x34 / 4] = &w_find_named;
	world.vt = world_vtable;
	grid_vtable[0x30 / 4] = &g_cells; grid.vt = grid_vtable;
	component_vtable[0x24 / 4] = &c_position;
	game_vtable[0x268 / 4] = &game_difficulty;
	// getters and external helpers
	jump(native_world_getter, &stub_world_getter); jump(native_grid_getter, &stub_grid_getter);
	jump(native_entity_of_dense, &stub_entity_of_dense); jump(native_player_handle, &stub_player_handle);
	jump(native_entity_of_handle, &stub_entity_of_handle); jump(native_handle_valid, &stub_handle_valid);
	jump(native_sphere_hits_box, &stub_sphere); jump(native_sphere_hits_box_exact, &stub_sphere);
	jump(native_capsule_sweep, &stub_capsule); jump(native_capsule_sweep_ratio, &stub_capsule_ratio);
	jump(native_physics_world, &stub_physics_world);
	jump(native_nav_component, &stub_nav_component); jump(native_nav_component_b, &stub_nav_component_b); jump(native_nav_reachable, &stub_nav_reachable);
	jump(native_pathgrid_box, &stub_pathgrid_box); jump(native_pathgrid_center, &stub_pathgrid_center);
	jump(native_pathgrid_cell, &stub_pathgrid_cell); jump(native_pathgrid_cell_b, &stub_pathgrid_cell_b);
	jump(native_name_table, &stub_name_table); jump(native_snprintf, &stub_snprintf); jump(native_game_object, &stub_game);
	jump(native_actor_set_state, &stub_set_state); jump(native_actor_timing, &stub_timing); jump(native_actor_order, &stub_order); jump(native_actor_flag, &stub_flag);
	jump(0x672161, &stub_noop); // __security_check_cookie (native 0x4cd350 epilogue)
	jump(0x672170, reinterpret_cast<const void*>(&std::strncpy)); // the CRT strncpy thunk (JMP [0x67f148]) the native 0x4cd350 calls
	// imports
	at<void*>(iat_vec_set3) = &vec_set3; at<void*>(iat_vec_set) = &vec_set; at<void*>(iat_vec_length) = &vec_length; at<void*>(iat_vec_length2) = &vec_length2;
	at<void*>(iat_vec_make_min) = &vec_make_min; at<void*>(iat_vec_make_max) = &vec_make_max; at<void*>(iat_vec_scale) = &vec_scale; at<void*>(iat_vec_sub) = &vec_sub;
	at<void*>(iat_vec_assign) = &vec_assign; at<void*>(iat_vec_index) = &vec_index; at<void*>(iat_vec_ctor) = &vec_ctor; at<void*>(iat_vec_transform) = &vec_transform;
	at<void*>(iat_mat_identity) = &mat_identity; at<void*>(iat_mat_pre_scale) = &mat_pre_scale; at<void*>(iat_mat_translation) = &mat_translation;
	at<void*>(0x67fa5c) = &vec_normalize; at<void*>(0x67fa54) = zero_vector; at<void*>(iat_strncpy) = reinterpret_cast<void*>(&std::strncpy);
	// constants, globals, strings
	at<float>(const_zero) = 0.0f; at<float>(const_one) = 1.0f; at<float>(const_ratio_threshold) = 0.09f; at<float>(const_epsilon) = 1e-8f; at<float>(const_half) = 0.5f;
	at<float>(const_quarter) = 0.25f; at<float>(const_nav_speed2) = 5e-7f; at<float>(const_nav_distance2) = 800.0f; at<float>(const_four) = 4.0f;
	at<std::int32_t>(class_id_a) = 0; at<std::int32_t>(class_id_b) = 1; at<std::int32_t>(class_id_c) = 2; at<std::int32_t>(class_id_d) = 3; at<std::int32_t>(class_id_e) = 4;
	at<std::uint8_t>(danger_room_flag) = 0xfe;
	std::memcpy(at_ptr(format_name_index), "%s%d", 5); std::memcpy(at_ptr(format_name_zero_index), "%s0%d", 6); at<char>(string_empty) = 0;
	at<float>(0x685674) = 0.995f; // 0x44de70's hit-distance scale
	at<std::uint32_t>(0x6f38f8) = 0x12345678; at<void*>(0x718430) = &world;
	jump(0x4241f0, &stub_noop); // 0x44d940's record member constructor (MOV EAX,ECX; RET); the retail bytes replace it
	// synthetic stand-ins for the in-region helpers
	jump(native_box_overlaps, &standin_box_overlaps); jump(native_box_inside, &standin_box_inside);
	jump(native_context_ctor, &standin_context_ctor); jump(native_record_ctor, &standin_record_ctor); jump(native_record_fill, &standin_record_fill);
	jump(native_context_max, &standin_context_max); jump(native_context_min, &standin_context_min); jump(native_commit_hit, &standin_commit);
	FlushInstructionCache(GetCurrentProcess(), fixture_space, sizeof fixture_space);
}
bool install_native()
{
	bool ok = true;
	ok = native_bytes(0x44cd70, 0x44eaf6 - 0x44cd70) && ok;   // CCollide frames, 0x44ce30, box tests, the region, 0x44e7d0, 0x44ea40
	ok = native_bytes(0x491210, 0x4915a7 - 0x491210) && ok;   // navigation
	ok = native_bytes(0x493330, 0x4935d7 - 0x493330) && ok;
	ok = native_bytes(0x4cd350, 0x4cd879 - 0x4cd350) && ok;   // named query caller
	ok = native_bytes(0x570130, 0x570153 - 0x570130) && ok;   // bit clear used by 0x491210
	ok = native_bytes(0x4241f0, 3) && ok;                     // MOV EAX,ECX; RET (0x44d940)
	FlushInstructionCache(GetCurrentProcess(), fixture_space, sizeof fixture_space);
	return ok;
}

// ---- scenarios ---------------------------------------------------------------------------------------------------
struct Guarded
{
	std::array<std::uint32_t, 8> before{};
	EntitySet set{};
	std::array<std::uint32_t, 8> after{};
	Guarded() { before.fill(0xdeadbeef); after.fill(0xdeadbeef); }
	bool intact() const { for (auto v : before) if (v != 0xdeadbeef) return false; for (auto v : after) if (v != 0xdeadbeef) return false; return true; }
};
struct Snapshot
{
	std::uint32_t result = 0;
	std::array<std::uint32_t, 32> set{};
	std::vector<std::string> log;
	std::vector<std::uint32_t> extra;
	std::vector<std::uint8_t> pool;
	bool guards = true;
};
void remember(const Snapshot& s, const char* what)
{
	char line[256];
	std::snprintf(line, sizeof line, "   %s: result %u set{", what, s.result); last_dump = line;
	for (std::uint32_t i = 0; i < 1024; ++i) if ((s.set[i >> 5] >> (i & 31)) & 1) { std::snprintf(line, sizeof line, " %u", i); last_dump += line; }
	last_dump += " } extra{"; for (auto e : s.extra) { std::snprintf(line, sizeof line, " %u", e); last_dump += line; }
	std::snprintf(line, sizeof line, " } guards %d\n", s.guards); last_dump += line;
	for (auto& l : s.log) last_dump += "      " + l + "\n";
	if (verbose) std::printf("%s", last_dump.c_str());
}
std::array<FixEntity, 70> entities;
std::array<FixModel, 8> models;
std::array<FixComponent, 8> components;
FixEntity self_entity;
std::vector<std::uint32_t> sample; // dense indices under test
void reset_fixture()
{
	call_log.clear(); pool_used = 0; std::memset(trace_pool, 0, sizeof trace_pool);
	world.entities.fill(nullptr); world.handles.fill(0); world.named.clear(); world.player = 0;
	grid.candidates = EntitySet{}; grid.ok = true;
	nav_components.clear(); nav_components_b.clear(); nav_reachable.clear();
	at<std::int32_t>(0x717998) = at<std::int32_t>(0x71799c) = at<std::int32_t>(0x7179a0) = 0;
	at<std::uint8_t>(danger_room_flag) = 0xfe; std::memset(at_ptr(danger_room_courses), 0, 0x78 * 4);
	game.difficulty = 1; capsule_script = CapsuleScript{};
	for (std::size_t i = 0; i < entities.size(); ++i)
	{
		auto& e = entities[i]; std::memset(&e, 0, sizeof e); e.vt = entity_vtable; e.id = int(i); e.kind = 1; e.has_aabb = true;
		const float base = float(i) * 4.0f;
		e.position[0] = base; e.position[1] = 1; e.position[2] = 2;
		for (int k = 0; k < 3; ++k) { e.aabb[k] = e.position[k] - 1; e.aabb[k + 3] = e.position[k] + 1; e.nav_box[k] = e.aabb[k]; e.nav_box[k + 3] = e.aabb[k + 3]; e.bounds[k] = e.aabb[k] - 0.5f; e.bounds[k + 3] = e.aabb[k + 3] + 0.5f; }
		e.half[0] = 1; e.half[1] = 1.5f; e.half[2] = 2; e.flags4 = 0x80 | 0x200;
		set_class(e, 0, true);
	}
	for (std::size_t i = 0; i < models.size(); ++i) { models[i] = FixModel{model_vtable, int(i), true, true, {float(i), 2, 3}, 0.5f}; }
	for (std::size_t i = 0; i < components.size(); ++i) { components[i] = FixComponent{component_vtable, 0, {0, 0, 0}, int(i)}; }
	std::memset(&self_entity, 0, sizeof self_entity); self_entity.vt = entity_vtable; self_entity.id = 99; self_entity.kind = 1; self_entity.has_aabb = true; self_entity.model = &models[7];
	self_entity.position[0] = 100; self_entity.position[1] = 100; self_entity.position[2] = 100; self_entity.half[0] = self_entity.half[1] = self_entity.half[2] = 3;
	for (int k = 0; k < 3; ++k) { self_entity.aabb[k] = self_entity.position[k] - 3; self_entity.aabb[k + 3] = self_entity.position[k] + 3; self_entity.bounds[k] = self_entity.aabb[k]; self_entity.bounds[k + 3] = self_entity.aabb[k + 3]; }
	sample = cap() == 256 ? std::vector<std::uint32_t>{0, 1, 5, 100, 255} : std::vector<std::uint32_t>{0, 1, 255, 256, 257, 1023};
	for (std::size_t i = 0; i < sample.size(); ++i) { entities[i].token = token_of(sample[i]); world.entities[sample[i]] = &entities[i]; world.handles[sample[i]] = entities[i].token; }
}
void capture(Snapshot& s, const EntitySet& set) { for (std::uint32_t w = 0; w < 32; ++w) s.set[w] = set.words[w]; s.log = call_log; s.pool.assign(&trace_pool[0][0], &trace_pool[0][0] + sizeof trace_pool); }
bool same_prefix(const Snapshot& a, const Snapshot& b, std::uint32_t words) { for (std::uint32_t w = 0; w < words; ++w) if (a.set[w] != b.set[w]) return false; return true; }
void compare(const Snapshot& native, const Snapshot& port, const char* what)
{
	char message[256];
	std::snprintf(message, sizeof message, "differential %s: result %u vs %u", what, native.result, port.result); check(native.result == port.result, message);
	std::snprintf(message, sizeof message, "differential %s: eight set words agree", what); check(same_prefix(native, port, 8), message);
	bool logs = native.log.size() == port.log.size();
	for (std::size_t i = 0; logs && i < native.log.size(); ++i) logs = native.log[i] == port.log[i];
	std::snprintf(message, sizeof message, "differential %s: call sequence agrees (%zu entries)", what, native.log.size()); check(logs, message);
	if (!logs) for (std::size_t i = 0; i < native.log.size() || i < port.log.size(); ++i) if (i >= native.log.size() || i >= port.log.size() || native.log[i] != port.log[i]) { std::printf("   first difference at %zu: native '%s' port '%s'\n", i, i < native.log.size() ? native.log[i].c_str() : "-", i < port.log.size() ? port.log[i].c_str() : "-"); break; }
	std::snprintf(message, sizeof message, "differential %s: extra state agrees", what); check(native.extra == port.extra, message);
	// Pool records are 0x44de70/0x44e0d0 copies of record+0x38..+0x78; the dword at record+0x64 (pool +0x2c) is never
	// written by any path here (stack leftovers natively too), so it is masked.
	auto masked = [](std::vector<std::uint8_t> p) { for (std::size_t r = 0; r + 64 <= p.size(); r += 64) std::memset(p.data() + r + 0x2c, 0, 4); return p; };
	const auto np = masked(native.pool), pp = masked(port.pool);
	std::snprintf(message, sizeof message, "differential %s: trace pool agrees", what); check(np == pp, message);
	if (np != pp) { int shown = 0; for (std::size_t i = 0; i < np.size() && shown < 8; ++i) if (np[i] != pp[i]) { std::printf("   pool record %zu +0x%02zx: native %02x port %02x\n", i / 64, i % 64, np[i], pp[i]); ++shown; } }
}
using cd70_fn = std::uint32_t(__fastcall*)(void*, void*, const float*, const float*, EntitySet*);
using ce70_fn = std::uint32_t(__fastcall*)(void*, void*, const float*, const float*, EntitySet*, std::uint8_t);
using d170_fn = std::uint32_t(__fastcall*)(void*, void*, const float*, float, EntitySet*, std::uint8_t);
using d360_fn = std::uint32_t* (__fastcall*)(void*, void*, std::uint32_t*, const float*, const float*, const float*, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t, EntitySet*, void*, std::uint8_t, std::uint8_t, void*);
using ea40_fn = std::uint32_t(__fastcall*)(void*, void*, const float*, const float*, const float*, EntitySet*, void*, void*);
using e7d0_fn = std::uint32_t(__fastcall*)(void*, void*, const float*, const float*, EntitySet*, const float*);
using n1210_fn = std::uint32_t(__fastcall*)(void*, void*, const std::uint8_t*, std::uint32_t, std::uint32_t, std::uint32_t, EntitySet*, std::uint8_t*);
using n3330_fn = std::uint32_t(__fastcall*)(void*, void*, std::int32_t, std::int32_t, std::uint8_t);
using cd350_fn = void(__fastcall*)(void*, void*, std::uint32_t*, const std::uint32_t*, std::uint8_t, std::int32_t, std::uint8_t);
const float lo_box[3] = {-10, -10, -10}, hi_box[3] = {1000, 10, 10};

// --- 0x44cd70: candidates & set ---
Snapshot run_cd70(bool native, bool grid_ok)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) grid.candidates.set(d); grid.candidates.set(2); grid.ok = grid_ok;
	g.set.set(sample[0]); g.set.set(sample[2]); g.set.set(sample.back()); g.set.set(3);
	const auto fn = native ? entity_capacity_layout::native<cd70_fn>(0x44cd70) : &ec::Physics_0044cd70;
	s.result = fn(at_ptr(native_collide_object), nullptr, lo_box, hi_box, &g.set) & 0xff;
	capture(s, g.set); s.guards = g.intact(); FIN(s); return s;
}
// --- 0x44ce70: filtered by overlap / inside ---
Snapshot run_ce70(bool native, bool overlap)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	entities[2].aabb[3] = 5000; // not inside the box, still overlapping
	entities[3].has_aabb = false; // no collision bounds
	entities[4].aabb[0] = 2000; entities[4].aabb[3] = 2001; // outside
	const auto fn = native ? entity_capacity_layout::native<ce70_fn>(0x44ce70) : &ec::Physics_0044ce70;
	s.result = fn(at_ptr(native_collide_object), nullptr, lo_box, hi_box, &g.set, overlap);
	capture(s, g.set); s.guards = g.intact(); FIN(s); return s;
}
// --- 0x44d170: sphere ---
Snapshot run_d170(bool native, float radius, std::uint8_t flag)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	entities[2].has_aabb = false;
	const float center[3] = {2, 1, 2};
	const auto fn = native ? entity_capacity_layout::native<d170_fn>(0x44d170) : &ec::Physics_0044d170;
	s.result = fn(at_ptr(native_collide_object), nullptr, center, radius, &g.set, flag);
	capture(s, g.set); s.guards = g.intact(); FIN(s); return s;
}
// --- 0x44e7d0: swept box ---
Snapshot run_e7d0(bool native, bool grid_ok)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); } grid.ok = grid_ok;
	entities[3].has_aabb = false; entities[4].aabb[0] = 3000; entities[4].aabb[3] = 3001;
	const float lo[3] = {-2, -2, -2}, hi[3] = {2, 2, 2}, sweep[3] = {20, 0, 0};
	const auto fn = native ? entity_capacity_layout::native<e7d0_fn>(0x44e7d0) : &ec::Physics_0044e7d0;
	s.result = fn(nullptr, nullptr, lo, hi, &g.set, sweep);
	capture(s, g.set); s.guards = g.intact(); FIN(s); return s;
}
// --- 0x44d360 A: context kind 1 (self entity with a model), player test from the set ---
Snapshot run_d360_a(bool native)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	entities[0].kind = 1; entities[0].model = &models[1];                  // 0x44e650 -> 0x44e590/0x44e6a0, hit
	entities[1].kind = 1; entities[1].model = &models[2];                  // player (kind 1; kind 2 is skipped natively): 0x44e590 hit; loop: 0x44e650 -> 0x44e590 hit
	entities[2].kind = 3;                                                  // 0x44e4d0 hit
	entities[3].kind = 0;                                                  // record fill fails -> cleared
	entities[4].kind = 4; entities[4].model = &models[4]; models[4].ok14 = false; // 0x44e650 miss -> cleared
	if (sample.size() > 5) { entities[5].kind = 1; entities[5].model = &models[5]; models[5].ok14 = false; }
	world.player = entities[1].token;
	std::uint32_t result = 0xffffffff;
	const float start[3] = {0, 1, 2}, end[3] = {40, 1, 2}, extents[3] = {1, 1, 1};
	const auto fn = native ? entity_capacity_layout::native<d360_fn>(0x44d360) : &ec::Physics_0044d360;
	fn(at_ptr(native_collide_object), nullptr, &result, start, end, extents, 0x11, 0x22, 3, 4, &g.set, &filter, 0, 1, &self_entity);
	s.result = result; capture(s, g.set); s.guards = g.intact();
	s.extra = {at<std::uint32_t>(0x7179a0), std::uint32_t(pool_used)};
	FIN(s); return s;
}
// --- 0x44d360 B: no entity (context kind 3), forced player test, unscaled direction ---
Snapshot run_d360_b(bool native)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	entities[0].kind = 3;                                                   // 0x44e210 (ratio sweep) hit
	entities[1].kind = 2; entities[1].half[0] = entities[1].half[1] = entities[1].half[2] = 0; // 0x44dca0 non-positive radius: counter, cleared
	entities[2].kind = 5;                                                   // 0x44e140 physics world hit
	entities[3].kind = 1; entities[3].model = &models[3]; models[3].ok10 = false; // player and loop: 0x44e440 miss -> cleared
	entities[4].kind = 4; entities[4].model = &models[4];                   // 0x44e440 hit
	if (sample.size() > 5) { entities[5].kind = 2; }                        // 0x44dca0 hit
	world.player = entities[3].token;
	capsule_script = CapsuleScript{1, 0, 0, 0, 0.25f, {1, 2, 3}, {0.5f, 0.5f, 0.5f}, {0, 1, 0}};
	std::uint32_t result = 0xffffffff;
	const float start[3] = {0, 1, 2}, end[3] = {40, 1, 2}, extents[3] = {1.0f, 1.5f, 2.0f};
	const auto fn = native ? entity_capacity_layout::native<d360_fn>(0x44d360) : &ec::Physics_0044d360;
	fn(at_ptr(native_collide_object), nullptr, &result, start, end, extents, 7, 8, 1, 0, &g.set, &filter, 1, 0, nullptr);
	s.result = result; capture(s, g.set); s.guards = g.intact();
	s.extra = {at<std::uint32_t>(0x7179a0), std::uint32_t(pool_used)};
	FIN(s); return s;
}
// --- 0x44d360 C: zero-length sweep (context constructor failure) ---
Snapshot run_d360_c(bool native)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	std::uint32_t result = 0xffffffff;
	const float start[3] = {0, 1, 2}, extents[3] = {1, 1, 1};
	const auto fn = native ? entity_capacity_layout::native<d360_fn>(0x44d360) : &ec::Physics_0044d360;
	fn(at_ptr(native_collide_object), nullptr, &result, start, start, extents, 0, 0, 0, 0, &g.set, &filter, 1, 0, &self_entity);
	s.result = result; capture(s, g.set); s.guards = g.intact();
	s.extra = {at<std::uint32_t>(0x71799c)};
	FIN(s); return s;
}
// --- 0x44ea40: constructor failure (entity kind 0) and the normal candidate query ---
Snapshot run_ea40(bool native, bool fail)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	entities[4].aabb[0] = 3000; entities[4].aabb[3] = 3001;
	self_entity.kind = fail ? 0 : 1;
	const float start[3] = {0, 1, 2}, end[3] = {40, 1, 2}, extents[3] = {1, 1, 1};
	const auto fn = native ? entity_capacity_layout::native<ea40_fn>(0x44ea40) : &ec::Physics_0044ea40;
	s.result = fn(at_ptr(native_collide_object), nullptr, start, end, extents, &g.set, &self_entity, &filter);
	capture(s, g.set); s.guards = g.intact();
	s.extra = {at<std::uint32_t>(0x717998)};
	FIN(s); return s;
}
// --- 0x491210 / 0x493330: navigation ---
void prepare_navigation()
{
	for (auto d : sample) grid.candidates.set(d);
	std::memset(&path_grid, 0, sizeof path_grid);
	path_grid.lo[0] = -10; path_grid.lo[1] = -10; path_grid.lo[2] = -10; path_grid.hi[0] = 1000; path_grid.hi[1] = 10; path_grid.hi[2] = 10;
	path_grid.center[0] = 5; path_grid.center[1] = 1; path_grid.center[2] = 2;
	fld<float>(path_grid.raw, 0x20ad8) = 4.0f; fld<std::uint16_t>(path_grid.raw, 0x20b08) = 41;
	path_grid.raw[0x20b10] = 16; path_grid.raw[0x20b11] = 16;
	for (int x = 0; x < 16; ++x) for (int y = 0; y < 16; ++y) { auto* c = path_grid.cells[x][y]; c[0] = std::uint8_t(10 + x); c[1] = std::uint8_t(x == 3 ? 0xff : 30 + x); c[4] = 0x40; c[5] = 0x41; }
	for (std::uint32_t i = 0x20b20; i < 0x20ba0; ++i) path_grid.raw[i] = std::uint8_t(0xa0 + (i & 0xf)); // the retail scratch stays untouched by the port
	world.entities[sample[0]] = nullptr;                                    // null entity -> cleared
	set_class(entities[1], 0, false);                                       // not class a -> cleared
	components[2] = FixComponent{component_vtable, 1u << 18, {6, 1, 2}, 2}; nav_components[&entities[2]] = &components[2]; // bit 18 set, bit 22 clear -> removed
	components[3] = FixComponent{component_vtable, (1u << 18) | (1u << 22), {6, 1, 2}, 3}; nav_components[&entities[3]] = &components[3]; // output byte, counted
	components[4] = FixComponent{component_vtable, 0, {6, 1, 2}, 4}; nav_components_b[&entities[3]] = &components[4]; nav_reachable[&components[4]] = true;
	entities[4].nav_box[0] = 5000; entities[4].nav_box[3] = 5001;          // box test fails -> cleared
	if (sample.size() > 5) { nav_components_b[&entities[5]] = &components[5]; nav_reachable[&components[5]] = false; } // unreachable -> cleared
}
Snapshot run_1210(bool native)
{
	MARK; reset_fixture(); prepare_navigation(); Guarded g; Snapshot s; std::uint8_t output = 7;
	std::uint8_t cell[8] = {3, 9, 0, 0, 0, 0, 0, 0};
	const auto fn = native ? entity_capacity_layout::native<n1210_fn>(0x491210) : &ec::EntitySetCaller_00491210;
	s.result = fn(path_grid.raw, nullptr, cell, 5, 6, 1, &g.set, &output);
	capture(s, g.set); s.guards = g.intact();
	s.extra = {output, path_grid.raw[0x20b1a], path_grid.raw[0x20b1b], path_grid.raw[0x20b1c]};
	FIN(s); return s;
}
Snapshot run_3330(bool native, bool blocking, std::uint8_t height)
{
	MARK; reset_fixture(); prepare_navigation(); Snapshot s;
	if (!blocking) grid.ok = false; // no candidates: the neighbour classification runs
	const auto fn = native ? entity_capacity_layout::native<n3330_fn>(0x493330) : &ec::EntitySetCaller_00493330;
	s.result = fn(path_grid.raw, nullptr, 2, 3, height) & 0xff;
	// the result set and output byte: the object's own scratch natively, the owner-keyed scratch in the port
	const auto& scratch = navigation_scratch(path_grid.raw);
	if (native) { for (int w = 0; w < 8; ++w) std::memcpy(&s.set[w], path_grid.raw + 0x20b20 + w * 4, 4); s.extra.push_back(path_grid.raw[0x20b40]); }
	else { for (int w = 0; w < 32; ++w) s.set[w] = scratch.set.words[w]; s.extra.push_back(scratch.output); }
	s.log = call_log;
	for (auto o : {0x20b18, 0x20b19, 0x20b1d}) s.extra.push_back(path_grid.raw[o]);
	for (auto o : {0x20b44, 0x20b48}) s.extra.push_back(fld<std::uint32_t>(path_grid.raw, o));
	for (int i = 0; i < 8; ++i) s.extra.push_back(path_grid.cells[2][3][i]);
	s.extra.push_back(fld<std::uint16_t>(path_grid.cells[2][3], 2));
	for (int x = 1; x <= 3; ++x) for (int y = 2; y <= 4; ++y) s.extra.push_back(path_grid.cells[x][y][4] | (path_grid.cells[x][y][5] << 8));
	FIN(s); return s;
}
// --- 0x4cd350: named query ---
struct NamedSetup { std::uint32_t token; std::array<std::uint32_t, 0xd0 / 4> output{}; std::uint8_t* self; };
std::uint8_t named_self[0x2200];
NamedSetup prepare_named(bool many)
{
	NamedSetup n{};
	std::memset(name_table, 0, sizeof name_table); name_table_used = 0;
	n.token = add_name("door");
	world.named["door"] = {sample[0]}; world.named["door3"] = {sample[1], sample[4]}; world.named["door07"] = {sample[2]}; world.named["door14"] = {sample[3]};
	if (many) { std::vector<std::uint32_t> all; for (std::uint32_t d = 0; d < 60; ++d) { all.push_back(d + 2); world.entities[d + 2] = &entities[d + 6]; world.handles[d + 2] = entities[d + 6].token = token_of(d + 2); set_class(entities[d + 6], 3, true); } world.named["door1"] = all; }
	for (std::size_t i = 0; i < sample.size(); ++i) { set_class(entities[i], 3, true); set_class(entities[i], 4, true); }
	set_class(entities[1], 0, false); set_class(entities[1], 4, false); // class d only
	set_class(entities[4], 4, false);                                  // class d + a
	std::memset(named_self, 0, sizeof named_self); fld<float>(named_self, 0x2158) = 2.5f; n.self = named_self;
	n.output.fill(0x77777777);
	return n;
}
Snapshot run_cd350(bool native, std::uint8_t apply, std::int32_t order, std::uint8_t set_state, bool many, bool danger)
{
	MARK; reset_fixture(); auto n = prepare_named(many); Snapshot s;
	if (danger) { at<std::uint8_t>(danger_room_flag) = 2; at<std::uint8_t>(danger_room_courses + 2 * 0x78) = 1; game.difficulty = 1; }
	const auto fn = native ? entity_capacity_layout::native<cd350_fn>(0x4cd350) : &ec::EntitySetCaller_004cd350;
	fn(n.self, nullptr, n.output.data(), &n.token, apply, order, set_state);
	s.log = call_log;
	s.result = n.output[0x33];
	for (int i = 0; i < 0x34; ++i) s.extra.push_back(n.output[i]);
	for (std::size_t i = 0; i < sample.size(); ++i) { s.extra.push_back(entities[i].field284); s.extra.push_back(entities[i].byte31c); }
	for (int k = 0; k < 3; ++k) s.extra.push_back(fld<std::uint32_t>(named_self, 0x20d0 + k * 4));
	FIN(s); return s;
}

// --- 0x44d360 A2: the player is not in the set (a11 = 0), so no player test ---
Snapshot run_d360_a2(bool native)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	entities[0].kind = 1; entities[0].model = &models[1];
	world.entities[2] = &entities[8]; entities[8].token = token_of(2); world.handles[2] = entities[8].token; world.player = entities[8].token; // dense 2 is never in `sample`
	std::uint32_t result = 0xffffffff;
	const float start[3] = {0, 1, 2}, end[3] = {40, 1, 2}, extents[3] = {1, 1, 1};
	const auto fn = native ? entity_capacity_layout::native<d360_fn>(0x44d360) : &ec::Physics_0044d360;
	fn(at_ptr(native_collide_object), nullptr, &result, start, end, extents, 1, 2, 0, 0, &g.set, &filter, 0, 1, &self_entity);
	s.result = result; capture(s, g.set); s.guards = g.intact(); s.extra = {std::uint32_t(pool_used)};
	FIN(s); return s;
}
// --- 0x44d360 B2: the 0x44e210 clear paths (zero capsule without a counter; contact flagged +0x148), the 0x44e140
// early return (zero extents keep the entity without a commit), and 0x44dca0 ignoring +0x148 ---
Snapshot run_d360_b2(bool native)
{
	MARK; reset_fixture(); Guarded g; Snapshot s;
	for (auto d : sample) { grid.candidates.set(d); g.set.set(d); }
	entities[0].kind = 3; entities[0].half[0] = entities[0].half[1] = entities[0].half[2] = 0; // 0x44e210: non-positive -> cleared, no counter
	entities[1].kind = 2;                                                   // 0x44dca0: commits despite +0x148
	entities[2].kind = 3; entities[2].half[0] = 1; entities[2].half[1] = 1; entities[2].half[2] = 4; // 0x44e210: ratio 1.33 -> 0x600dc0; +0x144 with +0x148 -> cleared
	entities[3].kind = 5; entities[3].half[0] = entities[3].half[1] = entities[3].half[2] = 0;  // 0x44e140: length2 0 <= eps -> return, bit kept
	entities[4].kind = 4; entities[4].model = &models[4];                   // 0x44e440 hit
	if (sample.size() > 5) { entities[5].kind = 3; entities[5].half[0] = 1; entities[5].half[1] = 1.5f; entities[5].half[2] = 2; } // ratio 0 -> 0x601280; cleared by +0x148
	capsule_script = CapsuleScript{1, 0, 0, 1, 0.25f, {1, 2, 3}, {0.5f, 0.5f, 0.5f}, {0, 1, 0}};
	std::uint32_t result = 0xffffffff;
	const float start[3] = {0, 1, 2}, end[3] = {40, 1, 2}, extents[3] = {1.0f, 1.5f, 2.0f};
	const auto fn = native ? entity_capacity_layout::native<d360_fn>(0x44d360) : &ec::Physics_0044d360;
	fn(at_ptr(native_collide_object), nullptr, &result, start, end, extents, 7, 8, 1, 0, &g.set, &filter, 1, 0, nullptr);
	s.result = result; capture(s, g.set); s.guards = g.intact();
	s.extra = {at<std::uint32_t>(0x7179a0), std::uint32_t(pool_used)};
	FIN(s); return s;
}
// --- 0x491210 N2: the velocity, +0x258, flag-bit-1 and class b/c filters, and the counted entity without components ---
Snapshot run_1210_b(bool native)
{
	MARK; reset_fixture(); prepare_navigation(); Guarded g; Snapshot s; std::uint8_t output = 7;
	world.entities[sample[0]] = &entities[0]; set_class(entities[1], 0, true); nav_components.clear(); nav_components_b.clear(); entities[4].nav_box[0] = 15; entities[4].nav_box[3] = 17;
	entities[0].velocity[0] = 1.0f;         // too fast -> cleared
	entities[1].byte258 = 8;                // flagged -> cleared
	entities[2].flags4 |= 2;                // bit 1 -> cleared
	set_class(entities[3], 1, true);        // class b -> cleared
	set_class(entities[4], 2, true);        // class c -> cleared
	// a sixth entity (1024 only) has no components at all -> counted
	std::uint8_t cell[8] = {3, 9, 0, 0, 0, 0, 0, 0};
	const auto fn = native ? entity_capacity_layout::native<n1210_fn>(0x491210) : &ec::EntitySetCaller_00491210;
	s.result = fn(path_grid.raw, nullptr, cell, 5, 6, 0, &g.set, &output);
	capture(s, g.set); s.guards = g.intact(); s.extra = {output, path_grid.raw[0x20b1c]};
	FIN(s); return s;
}
// --- 0x493330: a missing neighbour makes fewer than five level cells -> flag 4 ---
Snapshot run_3330_sparse(bool native)
{
	MARK; reset_fixture(); prepare_navigation(); Snapshot s;
	grid.ok = false; path_grid.cells[1][3][4] = 0;
	const auto fn = native ? entity_capacity_layout::native<n3330_fn>(0x493330) : &ec::EntitySetCaller_00493330;
	s.result = fn(path_grid.raw, nullptr, 2, 3, 30) & 0xff;
	s.log = call_log; s.extra = {path_grid.raw[0x20b1d], path_grid.cells[2][3][5]};
	FIN(s); return s;
}
// --- 0x4cd350: a zero token returns at once; a name nobody carries tries all 31 spellings and returns before the order ---
Snapshot run_cd350_empty(bool native, bool zero_token)
{
	MARK; reset_fixture(); auto n = prepare_named(false); Snapshot s;
	world.named.clear();
	std::uint32_t token = zero_token ? 0 : n.token;
	const auto fn = native ? entity_capacity_layout::native<cd350_fn>(0x4cd350) : &ec::EntitySetCaller_004cd350;
	fn(n.self, nullptr, n.output.data(), &token, 1, 0x1e, 1);
	s.log = call_log; s.result = n.output[0x33]; s.extra = {std::uint32_t(call_log.size())};
	FIN(s); return s;
}

// ---- synthetic expectations --------------------------------------------------------------------------------------
std::string tag(const char* what) { static char t[128]; std::snprintf(t, sizeof t, "%s @%u", what, cap()); return t; }
bool only_bits(const Snapshot& s, std::vector<std::uint32_t> bits)
{
	EntitySet expected; for (auto b : bits) expected.set(b);
	for (std::uint32_t w = 0; w < cap() / 32; ++w) if (s.set[w] != expected.words[w]) return false;
	return true;
}
void synthetic(const char* label)
{
	std::printf("-- synthetic (%s) at %u --\n", label, cap());
	reset_fixture(); // populates `sample` for this capacity
	const auto n = sample.size();
	{
		auto s = run_cd70(false, true);
		check(s.result == 1 && s.guards && only_bits(s, {sample[0], sample[2], sample.back()}), tag("44cd70 keeps set & cells over the whole set").c_str());
		auto f = run_cd70(false, false);
		check(f.result == 0 && f.guards && only_bits(f, {}), tag("44cd70 clears the whole set when the grid refuses").c_str());
	}
	{
		auto s = run_ce70(false, true);
		check(s.result == n - 2 && s.guards && only_bits(s, n == 5 ? std::vector<std::uint32_t>{sample[0], sample[1], sample[2]} : std::vector<std::uint32_t>{sample[0], sample[1], sample[2], sample[5]}), tag("44ce70 overlap keeps 0/255/256 (+1023), drops the unbounded and the outside entity").c_str());
		auto i = run_ce70(false, false);
		check(i.result == n - 3 && only_bits(i, n == 5 ? std::vector<std::uint32_t>{sample[0], sample[1]} : std::vector<std::uint32_t>{sample[0], sample[1], sample[5]}), tag("44ce70 inside drops the half-outside entity too").c_str());
	}
	{
		auto s = run_d170(false, 1.0f, 1);
		check(s.result == 2 && s.guards && only_bits(s, {sample[0], sample[1]}), tag("44d170 radius 1 reaches entities 0 and 1, skips the unbounded one").c_str());
		auto w = run_d170(false, 0.1f, 0);
		check(w.result == 2 && only_bits(w, {sample[0], sample[1]}), tag("44d170 radius floor 1.0 keeps the same hits").c_str());
		bool logged = false; for (auto& l : w.log) if (l.rfind("sphere(1,", 0) == 0) logged = true;
		check(logged, tag("44d170 passes the floored radius to the sphere test").c_str());
	}
	{
		auto s = run_e7d0(false, true);
		check(s.result == n - 2 && s.guards && only_bits(s, n == 5 ? std::vector<std::uint32_t>{sample[0], sample[1], sample[2]} : std::vector<std::uint32_t>{sample[0], sample[1], sample[2], sample[5]}), tag("44e7d0 swept box keeps the overlapping bounded entities").c_str());
		bool box = false; for (auto& l : s.log) if (l == "g30((-3,-3,-3),(23,3,3))") box = true;
		check(box, tag("44e7d0 asks the grid for the swept box inflated by one unit").c_str());
		auto f = run_e7d0(false, false); check(f.result == 0 && only_bits(f, {}), tag("44e7d0 clears everything when the grid refuses").c_str());
	}
	{
		auto s = run_d360_a(false);
		check(s.guards && only_bits(s, {sample[0], sample[1], sample[2]}), tag("44d360 A keeps the hits (0, player 1, 2) and clears the failed fill (3) and the missed model (4, 1023)").c_str());
		check(s.extra[1] == 4 && s.result == entities[1].token, tag("44d360 A commits four trace records; the first (the player) stays the best hit").c_str());
		std::size_t player_tests = 0; for (auto& l : s.log) if (l == "w24") ++player_tests;
		check(player_tests == 2, tag("44d360 A reads the player handle twice (membership test, then fill)").c_str());
		auto b = run_d360_b(false);
		check(b.guards && only_bits(b, n == 5 ? std::vector<std::uint32_t>{sample[0], sample[2], sample[4]} : std::vector<std::uint32_t>{sample[0], sample[2], sample[4], sample[5]}), tag("44d360 B keeps the capsule/physics/model hits and clears the zero capsule and the missed model").c_str());
		check(b.extra[0] == 1, tag("44d360 B counts the non-positive capsule once").c_str());
		bool ratio = false; for (auto& l : b.log) if (l == "601280") ratio = true;
		check(ratio, tag("44d360 B selects the ratio sweep for the matching capsule").c_str());
		auto c = run_d360_c(false);
		check(c.result == 0 && c.guards && only_bits(c, {}), tag("44d360 C zero-length sweep clears the whole wide set and returns a null hit").c_str());
		auto a2 = run_d360_a2(false);
		std::size_t fills = 0; for (auto& l : a2.log) if (l == "w0c(" + std::to_string(entities[8].token) + ")") ++fills;
		check(fills == 0 && a2.result == entities[0].token && a2.extra[0] == n, tag("44d360 A2 skips the player test when the player is outside the set; every default entity commits").c_str());
		auto b2 = run_d360_b2(false);
		check(b2.guards && only_bits(b2, {sample[1], sample[3], sample[4]}), tag("44d360 B2 clears the zero capsule and the +0x148 contacts, keeps the +0x148-blind 0x44dca0 hit and the untouched zero-extent 0x44e140 entity").c_str());
		check(b2.extra[0] == 0 && b2.extra[1] == 2, tag("44d360 B2 counts nothing (0x44e210 has no counter) and commits two records").c_str());
	}
	{
		auto f = run_ea40(false, true);
		check(f.result == 0 && f.guards && only_bits(f, {}), tag("44ea40 constructor failure clears all 32 words (the plan gap)").c_str());
		check(f.extra[0] == 1, tag("44ea40 failure counts the unsupported kind once").c_str());
		auto s = run_ea40(false, false);
		check(s.result == 0 && only_bits(s, n == 5 ? std::vector<std::uint32_t>{sample[0], sample[1], sample[2], sample[3]} : std::vector<std::uint32_t>{sample[0], sample[1], sample[2], sample[3], sample[5]}), tag("44ea40 success leaves the swept-box candidates").c_str());
	}
	{
		auto s = run_1210(false);
		check(s.result == 1 && s.guards && only_bits(s, {sample[3]}), tag("491210 keeps the counted blocker (256) and prunes 0/1/255/257/1023").c_str());
		check(s.extra[0] == 1 && s.extra[1] == 5 && s.extra[2] == 6 && s.extra[3] == 9, tag("491210 output byte and the cell bytes stored in the grid").c_str());
		auto b = run_3330(false, true, 12);
		check(b.result == 0x48 && b.extra[0] == 1 && b.extra[1] == 0, tag("493330 blocking: flag 0x40|8 (output set), layer 0").c_str());
		bool untouched = true; for (std::uint32_t i = 0x20b20; i < 0x20ba0; ++i) untouched = untouched && path_grid.raw[i] == std::uint8_t(0xa0 + (i & 0xf));
		check(untouched, tag("493330 never writes the CPathGrid scratch at +0x20b20..+0x20ba0").c_str());
		auto c = run_3330(false, false, 30);
		check(c.result == 0x41 && c.extra[1] == 1 && c.extra[2] == 0x41 && c.extra[3] == 5, tag("493330 non-blocking height 30: layer 1, five level neighbours, no flag change").c_str());
		auto d = run_3330(false, false, 12);
		check(d.result == 0x42 && d.extra[1] == 0 && d.extra[3] == 8, tag("493330 non-blocking height 12: layer 0, eight level neighbours set flag 2").c_str());
		auto e = run_3330_sparse(false);
		check(e.result == 0x45 && e.extra[0] == 4, tag("493330 four level neighbours set flag 4").c_str());
		auto f = run_1210_b(false);
		check(f.result == (n == 5 ? 0u : 1u) && f.guards && only_bits(f, n == 5 ? std::vector<std::uint32_t>{} : std::vector<std::uint32_t>{sample[5]}) && f.extra[0] == 0 && f.extra[1] == 3, tag("491210 velocity/+0x258/bit-1/class-b/class-c filters prune; the component-less entity counts").c_str());
	}
	{
		auto s = run_cd350(false, 1, 0x1e, 1, false, false);
		check(s.result == 5 && s.extra[0] == entities[0].token && s.extra[1] == entities[1].token && s.extra[2] == entities[4].token && s.extra[3] == entities[2].token && s.extra[4] == entities[3].token, tag("4cd350 collects door, door3 (two), door07, door14 in order").c_str());
		float scale = 10.0f / 5.0f; std::uint32_t bits; std::memcpy(&bits, &scale, 4);
		check(entities[0].field284 == bits && entities[0].byte31c == 0 && entities[1].field284 == 0, tag("4cd350 applies the divided scale to the actor-class entities only").c_str());
		auto m = run_cd350(false, 1, 0x1d, 0, true, false);
		check(m.result == 50, tag("4cd350 output stays capped at 50 entries").c_str());
		bool flagged = false; for (auto& l : m.log) if (l == "41a1f0(0,34)") flagged = true;
		check(flagged, tag("4cd350 order 0x1d sets flag 0x22 and keeps the undivided scale").c_str());
		auto d = run_cd350(false, 1, 0x1e, 1, false, true);
		scale = 8.0f; std::memcpy(&bits, &scale, 4);
		check(entities[0].field284 == bits, tag("4cd350 danger-room course 1 fixes the scale at 8").c_str());
		auto r = run_cd350(false, 0, 0x1e, 0, false, false);
		check(r.result == 0 && r.log.back() == "ebc(3,0,0,0)", tag("4cd350 apply=0 notifies every entity and resets the count").c_str());
		auto f = run_cd350(false, 1, 0x1f, 0, false, false);
		check(fld<float>(named_self, 0x20d0) == entities[3].position[0] && entities[0].byte31c == 2 && entities[1].byte31c == 0, tag("4cd350 order 0x1f copies the last character position and marks the actors").c_str());
		auto z = run_cd350_empty(false, true);
		check(z.result == 0 && z.extra[0] == 0, tag("4cd350 zero token returns with an empty output and no lookups").c_str());
		auto e = run_cd350_empty(false, false);
		check(e.result == 0 && e.extra[0] == 31, tag("4cd350 unknown name tries the 31 spellings and returns before the order").c_str());
	}
}
void differential()
{
	std::printf("-- differential at 256 --\n");
	use(stock_descriptor);
	compare(run_cd70(true, true), run_cd70(false, true), "44cd70");
	compare(run_cd70(true, false), run_cd70(false, false), "44cd70 refused");
	compare(run_ce70(true, true), run_ce70(false, true), "44ce70 overlap");
	compare(run_ce70(true, false), run_ce70(false, false), "44ce70 inside");
	compare(run_d170(true, 1.0f, 1), run_d170(false, 1.0f, 1), "44d170");
	compare(run_d170(true, 0.1f, 0), run_d170(false, 0.1f, 0), "44d170 floored");
	compare(run_e7d0(true, true), run_e7d0(false, true), "44e7d0");
	compare(run_e7d0(true, false), run_e7d0(false, false), "44e7d0 refused");
	compare(run_d360_a(true), run_d360_a(false), "44d360 A");
	compare(run_d360_b(true), run_d360_b(false), "44d360 B");
	compare(run_d360_c(true), run_d360_c(false), "44d360 C");
	compare(run_d360_a2(true), run_d360_a2(false), "44d360 A2");
	compare(run_d360_b2(true), run_d360_b2(false), "44d360 B2");
	compare(run_1210_b(true), run_1210_b(false), "491210 filters");
	compare(run_3330_sparse(true), run_3330_sparse(false), "493330 sparse");
	compare(run_cd350_empty(true, true), run_cd350_empty(false, true), "4cd350 zero token");
	compare(run_cd350_empty(true, false), run_cd350_empty(false, false), "4cd350 unknown name");
	compare(run_ea40(true, true), run_ea40(false, true), "44ea40 failure");
	compare(run_ea40(true, false), run_ea40(false, false), "44ea40");
	compare(run_1210(true), run_1210(false), "491210");
	compare(run_3330(true, true, 12), run_3330(false, true, 12), "493330 blocking");
	compare(run_3330(true, false, 30), run_3330(false, false, 30), "493330 neighbours");
	compare(run_3330(true, false, 12), run_3330(false, false, 12), "493330 neighbours layer 0");
	compare(run_cd350(true, 1, 0x1e, 1, false, false), run_cd350(false, 1, 0x1e, 1, false, false), "4cd350");
	compare(run_cd350(true, 1, 0x1d, 0, true, false), run_cd350(false, 1, 0x1d, 0, true, false), "4cd350 capped");
	compare(run_cd350(true, 1, 0x1e, 1, false, true), run_cd350(false, 1, 0x1e, 1, false, true), "4cd350 danger room");
	compare(run_cd350(true, 0, 0x1e, 0, false, false), run_cd350(false, 0, 0x1e, 0, false, false), "4cd350 reset");
	compare(run_cd350(true, 1, 0x1f, 0, false, false), run_cd350(false, 1, 0x1f, 0, false, false), "4cd350 order 0x1f");
}
int main(int argc, char** argv)
{
	std::setbuf(stdout, nullptr);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || !in_fixture(0x419000) || !in_fixture(0x785000)) { std::puts("fixture must cover 0x419000..0x785000 at image base 0x400000"); return 2; }
	install_fixture();
	// the FPU helpers alone
	check(capsule_ratio_below_threshold(1.0f, 2.0f, 1.0f, 2.0f, 0.09f) && !capsule_ratio_below_threshold(1.0f, 2.0f, 2.0f, 2.0f, 0.09f) && !capsule_ratio_below_threshold(1.0f, 2.0f, 1.0f, 0.0f, 0.09f), "ratio threshold: equal ratios below, distant above, infinite/NaN above");
	float t = 3.0f; add_quarter_scaled(&t, 4.0f); subtract_quarter_scaled(&t, 2.0f); check(t == 3.5f, "quarter-scaled add/subtract");
	use(stock_descriptor); synthetic("stand-in helpers");
	use(raised); synthetic("stand-in helpers");
	{
		NavigationScratch& a = navigation_scratch(&world); NavigationScratch& b = navigation_scratch(&grid);
		check(&a != &b && &navigation_scratch(&world) == &a, "navigation scratch is keyed by owner and stable");
		for (int i = 0; i < 40; ++i) navigation_scratch(&entities[i]);
		check(navigation_scratch(&entities[39]).owner == &entities[39], "navigation scratch recycles when full");
	}
	const char* path = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	if (!path) { std::printf("SKIPPED differential: no executable given\nEntity frames: %d failure(s)\n", failures); return failures ? 1 : 0; }
	std::ifstream file(path, std::ios::binary); binary.assign(std::istreambuf_iterator<char>(file), {});
	if (binary.size() < 0x1000 || !install_native()) { std::printf("SKIPPED differential: executable unreadable\nEntity frames: %d failure(s)\n", failures); return failures ? 1 : 0; }
	use(stock_descriptor); synthetic("retail helpers");
	use(raised); synthetic("retail helpers");
	differential();
	std::printf("Entity frames: %d failure(s)\n", failures);
	return failures ? 1 : 0;
}
