#pragma once

// The model component pools' decisions, kept apart from the patching so the native test can check them
// against a copy of XMen2.exe: what [Engine] ModelPools asks for, the layout of the model aggregate for a
// set of capacities, what every row of model_pools_sites.hpp becomes under a layout, and the code of the
// caves that hold the numbers an imm8 can't.
//
// The aggregate (SPEC-runtime-limits 2.4): a static object at 0x7bf6d0 (0x1b698 bytes, its init guard at
// 0x7dad68, getter 0x575620, constructor 0x5754c0, atexit thunk 0x67e1a0). Every model the game shows is a
// CModel handle from CSafeResMgrI<IModel, 692, 0x2c> plus components from seven pools inside it: transforms
// (692), attributes (260), shader effects (432), IGB geometry (400), animated actors (32), procedural
// geometry (220) and particle clouds (40), and one movie slot. When any pool a model needs is full,
// 0x575c90 creates nothing and says nothing (its can-allocate test, 0x5756e0): an effect, a prop or a HUD
// head does not appear - the beta 1 crash was a team-combo popup whose second head the full IGB pool (400)
// refused. Every pool has the same template layout (pool_layout_for, verified against each constructor):
//
//   items[N] | bitmap A (N bits) | ring[N] | ring end | write | read | free | bitmap B | live | ids[N] | mask | shift
//
// The pools sit back to back (transforms first, the IModel manager - a vtable then its pool - last), so a
// bigger pool moves every field behind it. The raise moves the whole aggregate into a zero-filled block
// of the DLL that the game's own constructor builds at the first getter call and its own destructor tears
// down at exit, with every displacement, capacity and bitmap size of the model code patched (the rows of
// model_pools_sites.hpp, regenerated from the exe with capstone). Thirty-one of those numbers are imm8
// (`cmp r, 0x20` / `cmp [m], 0x28` / `push 0x58`) and can't hold the new value: each of those
// instructions, with the ones after it up to five bytes, moves into a cave that does the same with an
// imm32 and jumps back. Four destructor tails (actors, particle clouds, attributes, procedural geometry) and the
// particle-system records clear clear their bitmap at its retail size; they move into caves that clear the whole
// bitmap. The procedural-geometry destructor's findNext (0x403d40) is shared with FX templates: the pool gets a copy
// of it with its own capacity.

#include "limits_rules.hpp"
#include "model_pools_sites.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace model_pools_rules
{
	using model_pools_sites::kind;
	using model_pools_sites::field;
	using model_pools_sites::frame;
	using model_pools_sites::site;

	constexpr DWORD image_base = 0x400000;
	constexpr DWORD retail_aggregate = 0x7bf6d0;
	constexpr DWORD retail_aggregate_size = 0x1b698;
	constexpr DWORD init_guard = 0x7dad68; // stays where it is: only the object moves
	constexpr DWORD movie_size = 0x34;     // one movie slot (N = 1, 8-byte items), unchanged
	constexpr std::size_t kinds = 9;

	constexpr std::size_t index(const kind k) { return static_cast<std::size_t>(k); }

	// Item size of each pool, in aggregate order (the movie's is not used: it keeps its 0x34 bytes).
	constexpr std::array<DWORD, kinds> item_size{12, 44, 8, 56, 184, 16, 92, 8, 0x2c};
	constexpr std::array<const char*, kinds> kind_name{"transforms", "attributes", "shader effects", "IGB models", "actors", "procedural geometry", "particle clouds", "movie", "models"};

	using capacities = std::array<int, kinds>;
	constexpr capacities retail_capacities{692, 260, 432, 400, 32, 220, 40, 1, 692};
	// SPEC-runtime-limits 2.4's target: x4 with headroom (Actor 128: 64 NPCs + heroes + HUD and menu actors). Particle
	// clouds: each CModelParticleCloud takes one of the particle systems of a second static manager (0x7dad78, guard
	// 0x7dc868, getter 0x578cc0; its constructor 0x579bc0 pops a free list and stores NULL when it is empty, and the
	// component's vt+8 (0x578df0) then reads through that NULL - found in game: 256 clouds over 40 systems crashed at
	// 0x578df3). The manager moves and grows with the clouds (one system per cloud). Its record address is built with
	// an 8-bit displacement (0x579c04: lea ecx,[edx+edx*2+records/8]), so the records must start within 1016 bytes:
	// at most 252 systems, and an even count (the records start on 8 bytes).
	constexpr int max_particle_systems = 252;
	constexpr capacities x4_capacities{4096, 1024, 2048, 2048, 128, 1024, 128, 1, 4096};
	constexpr capacities x2_capacities{2048, 512, 1024, 1024, 64, 512, 80, 1, 2048};

	// ---- [Engine] ModelPools in xml2-fix.ini -----------------------------------------------------------

	struct setting
	{
		std::optional<capacities> raise; // nullopt: the game's own pools
		bool valid = true;               // false: a value the rule doesn't know (the pools stay the game's)
		const char* name = "off";
	};

	// "x4" / "x2" raise; "off", "x1", "0", "no", "false", "stock" and an absent key keep the game's own.
	inline setting parse_setting(const std::optional<std::string>& text)
	{
		if (!text) return {};
		std::string v;
		for (const char c : limits_rules::value_text(*text)) v += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
		if (v == "x4" || v == "4") return {x4_capacities, true, "x4"};
		if (v == "x2" || v == "2") return {x2_capacities, true, "x2"};
		if (v.empty() || v == "off" || v == "x1" || v == "1" || v == "0" || v == "no" || v == "false" || v == "stock") return {};
		return {std::nullopt, false, "off"};
	}

	// ---- Co-op: gameplay-agreement rule 28 (ModelPools) -----------------------------------------------------
	// A full pool refuses the model and with it the spawn (the negative control in docs/model-pools.md: 45 spawns on
	// full stock pools made no enemy), so peers with other capacities can simulate different games under load. The
	// rule registers only when the key asks for a raise (or holds a value the rule doesn't know): absent or off adds
	// no entry and keeps the released record. Values: {installed ? 1 : 0, model handles, IGB models, actors} as
	// installed ({0,0,0,0} with the game's own pools); a requested raise that didn't install is unverified, which
	// closes host/join rather than letting a stock-pool peer join raised ones under the raised peer's name.
	constexpr std::uint16_t agreement_revision = 1;
	struct agreement
	{
		bool requested = false;
		bool verified = true;
		std::array<std::uint32_t, 4> values{};
	};

	inline agreement agreement_for(const setting& wanted, const std::optional<capacities>& installed)
	{
		agreement a;
		a.requested = wanted.raise.has_value() || !wanted.valid;
		if (!a.requested) return a;
		a.verified = wanted.valid && installed.has_value() && *installed == *wanted.raise;
		if (installed)
			a.values = {1u, static_cast<std::uint32_t>((*installed)[index(kind::model)]), static_cast<std::uint32_t>((*installed)[index(kind::igb)]),
			            static_cast<std::uint32_t>((*installed)[index(kind::actor)])};
		return a;
	}

	// ---- Layouts ------------------------------------------------------------------------------------------

	struct pool_layout
	{
		int capacity = 0;
		DWORD bitmap_a = 0, ring = 0, ring_end = 0, ring_write = 0, ring_read = 0, ring_free = 0, bitmap_b = 0, live = 0, ids = 0, id_mask = 0, id_shift = 0, size = 0;
		DWORD words = 0; // dwords per bitmap
	};

	constexpr pool_layout pool_layout_for(const int n, const DWORD item)
	{
		pool_layout p;
		p.capacity = n;
		p.words = (static_cast<DWORD>(n) + 31) / 32;
		p.bitmap_a = static_cast<DWORD>(n) * item;
		p.ring = p.bitmap_a + 4 * p.words;
		p.ring_end = p.ring + 4 * static_cast<DWORD>(n);
		p.ring_write = p.ring_end + 4;
		p.ring_read = p.ring_write + 4;
		p.ring_free = p.ring_read + 4;
		p.bitmap_b = p.ring_free + 4;
		p.live = p.bitmap_b + 4 * p.words;
		p.ids = p.live + 4;
		p.id_mask = p.ids + 4 * static_cast<DWORD>(n);
		p.id_shift = p.id_mask + 4;
		p.size = p.id_shift + 4;
		return p;
	}

	// The particle-system manager (static 0x7dad78, retail 40 systems): free list[N] | (N) | free count | records[N] x 24 |
	// bitmap (N bits) | 40 emitter objects x 0x90 (indexed by emitter, not by system: they stay 40).
	constexpr DWORD retail_particle_manager = 0x7dad78;
	constexpr DWORD particle_manager_guard = 0x7dc868;
	struct psys_layout
	{
		int capacity = 0;
		DWORD count = 0, records = 0, bitmap = 0, objects = 0, size = 0, words = 0;
	};
	constexpr psys_layout psys_layout_for(const int n)
	{
		psys_layout p;
		p.capacity = n;
		p.words = (static_cast<DWORD>(n) + 31) / 32;
		p.count = 4 * static_cast<DWORD>(n) + 4;
		p.records = p.count + 4;
		p.bitmap = p.records + 24 * static_cast<DWORD>(n);
		p.objects = p.bitmap + 4 * p.words;
		p.size = p.objects + 40 * 0x90;
		return p;
	}

	struct aggregate_layout
	{
		psys_layout psys{};
		capacities capacity{};
		std::array<DWORD, kinds> offset{};     // where each pool starts (the model manager: its vtable)
		std::array<pool_layout, kinds> pool{}; // pool-relative fields (the model pool sits at offset + 4)
		DWORD size = 0;
	};

	constexpr aggregate_layout aggregate_layout_for(const capacities& caps)
	{
		aggregate_layout a;
		a.capacity = caps;
		a.psys = psys_layout_for(caps[index(kind::particle_cloud)]);
		DWORD at = 0;
		for (std::size_t k = 0; k < kinds; ++k)
		{
			a.offset[k] = at;
			if (k == index(kind::movie))
			{
				at += movie_size;
				continue;
			}
			a.pool[k] = pool_layout_for(caps[k], item_size[k]);
			at += a.pool[k].size + (k == index(kind::model) ? 4 : 0);
		}
		a.size = at;
		return a;
	}

	// Where pool k's own fields start in the aggregate (the model manager keeps its vtable in front).
	constexpr DWORD pool_start(const aggregate_layout& a, const kind k)
	{
		return a.offset[index(k)] + (k == kind::model ? 4 : 0);
	}

	constexpr std::optional<DWORD> pool_field(const pool_layout& p, const field f)
	{
		switch (f)
		{
		case field::bitmap_a: return p.bitmap_a;
		case field::ring: return p.ring;
		case field::ring_end: return p.ring_end;
		case field::ring_write: return p.ring_write;
		case field::ring_read: return p.ring_read;
		case field::ring_free: return p.ring_free;
		case field::bitmap_b: return p.bitmap_b;
		case field::live: return p.live;
		case field::ids: return p.ids;
		case field::id_mask: return p.id_mask;
		case field::id_shift: return p.id_shift;
		default: return std::nullopt;
		}
	}

	// The aggregate-relative value of (kind, field) - base and model_pool included.
	constexpr std::optional<DWORD> aggregate_field(const aggregate_layout& a, const kind k, const field f, const DWORD extra)
	{
		if (f == field::base) return a.offset[index(k)] + extra;
		if (f == field::model_pool) return pool_start(a, kind::model);
		if (k == kind::movie) return std::nullopt;
		const auto v = pool_field(a.pool[index(k)], f);
		if (!v) return std::nullopt;
		return pool_start(a, k) + *v;
	}

	// What a row holds under layout `a` with the aggregate at `block`; nullopt when the row makes no sense.
	constexpr std::optional<DWORD> psys_field(const psys_layout& p, const field f)
	{
		switch (f)
		{
		case field::psys_count: return p.count;
		case field::psys_records: return p.records;
		case field::psys_records_div8: return p.records / 8;
		case field::psys_bitmap: return p.bitmap;
		case field::psys_bitmap_hi: return p.bitmap + 4;
		case field::psys_objects: return p.objects;
		default: return std::nullopt;
		}
	}

	// Where the aggregate and the particle-system manager are.
	struct blocks
	{
		DWORD aggregate = retail_aggregate;
		DWORD particle_manager = retail_particle_manager;
	};

	constexpr std::optional<std::uint32_t> value_of(const site& s, const aggregate_layout& a, const blocks& at)
	{
		const DWORD block = at.aggregate;
		const auto& p = a.pool[index(s.pool)];
		switch (s.from)
		{
		case frame::psys: return psys_field(a.psys, s.what);
		case frame::psys_records:
			if (const auto v = psys_field(a.psys, s.what)) return *v - a.psys.records;
			return std::nullopt;
		case frame::psys_absolute: return s.what == field::base ? std::optional<std::uint32_t>(at.particle_manager) : std::nullopt;
		case frame::pool: return pool_field(p, s.what);
		case frame::manager:
			if (s.pool != kind::model) return std::nullopt;
			if (const auto v = pool_field(p, s.what)) return 4 + *v;
			return std::nullopt;
		case frame::aggregate: return aggregate_field(a, s.pool, s.what, s.extra);
		case frame::absolute:
			if (const auto v = aggregate_field(a, s.pool, s.what, s.extra)) return block + *v;
			return std::nullopt;
		case frame::immediate:
		case frame::find_next:
			switch (s.what)
			{
			case field::capacity: return static_cast<std::uint32_t>(p.capacity);
			case field::last_index: return static_cast<std::uint32_t>(p.capacity - 1);
			case field::bitmap_words: return p.words;
			case field::bitmap_bytes: return 4 * p.words;
			default: return std::nullopt;
			}
		}
		return std::nullopt;
	}

	// The model pool and the transform pool share bitset<692>::findNext (0x56fcc0): their capacities move together.
	constexpr bool capacities_valid(const capacities& c)
	{
		if (c[index(kind::movie)] != 1 || c[index(kind::model)] != c[index(kind::transform)]) return false;
		const int clouds = c[index(kind::particle_cloud)];
		if (clouds > max_particle_systems || clouds % 2) return false; // see x4_capacities
		for (std::size_t k = 0; k < kinds; ++k)
			if (c[k] < retail_capacities[k] || c[k] > 65536) return false;
		return true;
	}

	// ---- Bytes ------------------------------------------------------------------------------------------

	inline std::uint32_t operand(const std::string_view hex, const std::size_t offset, const std::size_t size)
	{
		std::uint32_t v = 0;
		for (std::size_t i = 0; i < size; ++i) v |= static_cast<std::uint32_t>(limits_rules::hex_byte(hex, offset + i)) << (8 * i);
		if (size == 1 && (v & 0x80)) v |= 0xffffff00; // imm8 are sign-extended
		return v;
	}

	inline std::vector<std::uint8_t> bytes_of(const std::string_view hex)
	{
		std::vector<std::uint8_t> b(limits_rules::hex_size(hex));
		for (std::size_t i = 0; i < b.size(); ++i) b[i] = limits_rules::hex_byte(hex, i);
		return b;
	}

	inline void put32(std::vector<std::uint8_t>& b, const std::size_t at, const std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
	}

	inline void push32(std::vector<std::uint8_t>& b, const std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
	}

	// The destructor tails that clear an A bitmap a dword at a time (edi = the bitmap): replaced by a whole-bitmap
	// clear in a cave, then back to `resume`. The zone reset (0x5852a0 -> 0x585450 / 0x585510 ...) and the aggregate
	// destructor (0x575560) run these destructors; nothing else clears bitmap A, so a tail that stops at the retail
	// size leaves the bits of every slot above it set across the reset, and the next destructor walk calls those dead
	// slots again. The attributes (0x571433: xor eax,eax then 9 stores) and procedural-geometry (0x571733: 7 stores)
	// tails keep their xor (EAX stays 0 and EDX is untouched, as in the retail tails) and move the stores.
	struct bitmap_clear
	{
		DWORD va;
		std::string_view hex;
		kind pool;
		DWORD resume;
		bool zero_edx;         // the tail leaves edx = 0
		bool particle_systems; // the particle-system manager's records bitmap (else the pool's bitmap A)
	};
	inline constexpr std::array<bitmap_clear, 5> bitmap_clears{{
		{0x571691, "c70700000000", kind::actor, 0x571697, false, false},          // actor pool destructor: mov dword [edi], 0
		{0x571845, "33d28917895704", kind::particle_cloud, 0x57184c, true, false}, // particle cloud destructor: xor edx,edx; mov [edi],edx; mov [edi+4],edx
		{0x578657, "33d28917895704", kind::particle_cloud, 0x57865e, true, true},  // particle-system records clear (0x578570): the same tail
		{0x571435, "890789470489470889470c89471089471489471889471c894720", kind::attributes, 0x57144f, false, false}, // attributes destructor: mov [edi+0..0x20], eax
		{0x571735, "890789470489470889470c894710894714894718", kind::procedural_geometry, 0x571749, false, false},     // procedural geometry destructor: mov [edi+0..0x18], eax
	}};

	// A findNext the raised pool shares with code outside the aggregate. The procedural-geometry destructor (0x5716a0)
	// walks its A bitmap with bitset<220>::findNext (0x403d40, called at 0x571724), which the FX-template destructor
	// (0x405340, call at 0x4053c4) shares for a bitset<220> of its own: raising it in place would break that one. Its
	// five 220s are both bound and end sentinel; under the raised compare after the call (0x57172b) the sentinel 220 reads
	// as a live slot (the destructor then calls slot 220, and again for every later search from 221). The helper is
	// position independent (short jumps inside it only): it is copied into the caves with the pool's capacity in those
	// five numbers and only the procedural-geometry call moves to the copy. Its bytes stay guarded (read only).
	struct helper_clone
	{
		DWORD helper;              // the shared helper
		std::string_view hex;      // its retail bytes, through its last ret
		std::array<std::uint8_t, 5> capacity_at; // offsets of the imm32 capacity/sentinel numbers in it
		kind pool;
		DWORD call;                // the one call that moves to the copy
		std::string_view call_hex; // its retail bytes (call helper)
	};
	inline constexpr std::array<helper_clone, 1> helper_clones{{
		{0x403d40,
		 "8b4424043ddc000000568bf17c09b8dc0000005ec20800538a5c24108bc8c1f90584db8b148e7502f7d28bc883e11fd3ea85d2751f83e0e083c0203ddc0000007d4d"
		 "8bd0c1fa0584db8b14967502f7d285d274e1f7c2ffff0000750683c010c1ea1084d2750683c008c1ea08f6c20f750683c004c1ea04f6c201750c8d642400d1ea40"
		 "f6c20174f83ddc0000007c05b8dc0000005b5ec20800",
		 {0x05, 0x0f, 0x3c, 0x89, 0x90}, kind::procedural_geometry, 0x571724, "e81726e9ff"},
	}};

	// The particle-system manager's constructor runs its emitter-object init loop with the system count (mov ebx, ebp at
	// 0x578c29, ebp = N): it becomes mov ebx, 40 (the 40 emitter objects stay 40).
	struct fixed_write
	{
		DWORD va;
		std::string_view before;
		std::string_view after;
		const char* what;
	};
	inline constexpr std::array<fixed_write, 1> fixed_writes{{
		{0x578c29, "8bddeb038d4900", "bb280000009090", "particle-system manager constructor: emitter objects stay 40"},
	}};

	// One write of the raise: `bytes` at `va` (the retail bytes there are in `before`).
	struct write
	{
		DWORD va;
		std::vector<std::uint8_t> bytes;
		std::vector<std::uint8_t> before;
		std::string what;
	};

	struct plan
	{
		std::vector<write> writes;     // in place, in the exe
		std::vector<write> expects;    // read only: bytes the caves copy (bytes == before)
		std::vector<std::uint8_t> cave_code; // to be put at the cave block's address
		std::size_t fields = 0, caves = 0;
	};

	// The rows of an instruction, applied to a copy of its bytes.
	inline std::optional<std::vector<std::uint8_t>> patched_instruction(const DWORD va, const std::string_view hex, const aggregate_layout& a, const blocks& block, std::size_t* fields = nullptr)
	{
		auto b = bytes_of(hex);
		for (const auto& s : model_pools_sites::sites)
		{
			if (s.va != va) continue;
			const auto v = value_of(s, a, block);
			if (!v || s.hex != hex) return std::nullopt;
			if (s.size == 4)
				put32(b, s.offset, *v);
			else if (s.size == 1)
			{
				if (static_cast<std::int32_t>(*v) < -128 || static_cast<std::int32_t>(*v) > 127)
				{
					if (s.in_cave) continue; // a cave widens it
					return std::nullopt;     // an 8-bit number that can't hold the value and has no cave
				}
				b[s.offset] = static_cast<std::uint8_t>(*v);
			}
			else
				return std::nullopt;
			if (fields) ++*fields;
		}
		return b;
	}

	// Every write and the cave code for layout `a`, the aggregate at `block` and the caves at `cave_base`.
	inline std::optional<plan> plan_for(const aggregate_layout& a, const blocks& block, const DWORD cave_base)
	{
		plan out;
		std::vector<DWORD> done;
		const auto in_cave = [](const DWORD va)
		{
			for (const auto& c : model_pools_sites::caves)
			{
				const auto& first = model_pools_sites::span_instructions[c.first];
				if (va >= first.va && va < c.resume) return true;
			}
			return false;
		};
		// In place: every instruction with rows outside the cave spans.
		for (const auto& s : model_pools_sites::sites)
		{
			if (std::find(done.begin(), done.end(), s.va) != done.end()) continue;
			done.push_back(s.va);
			if (s.in_cave != in_cave(s.va)) return std::nullopt;
			if (s.in_cave) continue;
			auto b = patched_instruction(s.va, s.hex, a, block, &out.fields);
			if (!b) return std::nullopt;
			out.writes.push_back({s.va, std::move(*b), bytes_of(s.hex), "model pools field"});
		}
		auto& code = out.cave_code;
		const auto here = [&] { return cave_base + static_cast<DWORD>(code.size()); };
		const auto jump_to = [&](const DWORD target)
		{
			code.push_back(0xe9);
			push32(code, target - (here() + 4));
		};
		for (const auto& f : fixed_writes)
			out.writes.push_back({f.va, bytes_of(f.after), bytes_of(f.before), f.what});
		// The imm8 caves.
		for (const auto& c : model_pools_sites::caves)
		{
			const DWORD entry = here();
			std::vector<std::uint8_t> before;
			for (std::size_t i = c.first; i < static_cast<std::size_t>(c.first) + c.count; ++i)
			{
				const auto& in = model_pools_sites::span_instructions[i];
				const auto original = bytes_of(in.hex);
				before.insert(before.end(), original.begin(), original.end());
				using model_pools_sites::op;
				if (in.how == op::branch)
				{
					code.push_back(0x0f);
					code.push_back(static_cast<std::uint8_t>(0x80 | in.condition));
					push32(code, in.target - (here() + 4));
					continue;
				}
				auto b = patched_instruction(in.va, in.hex, a, block, &out.fields);
				if (!b) return std::nullopt;
				if (in.how == op::copy)
				{
					code.insert(code.end(), b->begin(), b->end());
					continue;
				}
				// widen: the imm8 row of this instruction -> imm32
				const site* row = nullptr;
				for (const auto& s : model_pools_sites::sites)
					if (s.va == in.va && s.size == 1) row = &s;
				if (!row) return std::nullopt;
				const auto v = value_of(*row, a, block);
				if (!v) return std::nullopt;
				if (in.how == op::widen_push)
				{
					if (b->size() != 2 || (*b)[0] != 0x6a) return std::nullopt;
					code.push_back(0x68);
					push32(code, *v);
				}
				else
				{
					if ((*b)[0] != 0x83 || row->offset + 1u != b->size()) return std::nullopt;
					code.push_back(0x81);
					code.insert(code.end(), b->begin() + 1, b->begin() + row->offset);
					push32(code, *v);
				}
				++out.fields;
			}
			jump_to(c.resume);
			const DWORD span = c.resume - c.va;
			if (span < 5 || span != before.size()) return std::nullopt;
			std::vector<std::uint8_t> jmp(span, 0x90);
			jmp[0] = 0xe9;
			put32(jmp, 1, entry - (c.va + 5));
			out.writes.push_back({c.va, std::move(jmp), std::move(before), "model pools imm8 cave"});
			++out.caves;
		}
		// The bitmap clears: push eax; push ecx; push edi; xor eax,eax; mov ecx,words; rep stosd; pop edi; pop ecx; pop eax
		for (const auto& c : bitmap_clears)
		{
			const DWORD entry = here();
			if (c.zero_edx) code.insert(code.end(), {0x33, 0xd2});
			code.insert(code.end(), {0x50, 0x51, 0x57, 0x33, 0xc0, 0xb9});
			push32(code, c.particle_systems ? a.psys.words : a.pool[index(c.pool)].words);
			code.insert(code.end(), {0xf3, 0xab, 0x5f, 0x59, 0x58});
			jump_to(c.resume);
			const DWORD span = c.resume - c.va;
			std::vector<std::uint8_t> jmp(span, 0x90);
			jmp[0] = 0xe9;
			put32(jmp, 1, entry - (c.va + 5));
			out.writes.push_back({c.va, std::move(jmp), bytes_of(c.hex), "model pools bitmap clear cave"});
			++out.caves;
		}
		// The shared findNext helpers: a copy with the pool's capacity, and the one call moved to it.
		for (const auto& h : helper_clones)
		{
			while (code.size() % 16) code.push_back(0xcc);
			const DWORD entry = here();
			auto b = bytes_of(h.hex);
			const auto retail = static_cast<std::uint32_t>(retail_capacities[index(h.pool)]);
			for (const auto at : h.capacity_at)
			{
				if (at + 4u > b.size() || operand(h.hex, at, 4) != retail) return std::nullopt;
				put32(b, at, static_cast<std::uint32_t>(a.pool[index(h.pool)].capacity));
			}
			code.insert(code.end(), b.begin(), b.end());
			const auto call = bytes_of(h.call_hex);
			if (call.size() != 5 || call[0] != 0xe8 || operand(h.call_hex, 1, 4) != h.helper - (h.call + 5)) return std::nullopt;
			std::vector<std::uint8_t> moved(5, 0xe8);
			put32(moved, 1, entry - (h.call + 5));
			out.writes.push_back({h.call, std::move(moved), call, "model pools findNext copy call"});
			out.expects.push_back({h.helper, bytes_of(h.hex), bytes_of(h.hex), "model pools findNext copied (read only)"});
			++out.caves;
		}
		return out;
	}
}
