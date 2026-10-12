// CWorld replacement owner. Every function names the native routine it reproduces; the comments describe the native
// behaviour as decompiled (XMen2.exe sha1 7d95cdb4..., 0x4c6120-0x4c7f7c), including its quirks. The three ratl
// containers (the two ordered maps and the node pools) are reimplemented with identical external semantics on the
// native field layout: lookup by key, insert, erase, full-at-capacity refusal, clear on reset; the map is an unbalanced
// binary search tree on the native {parent, left, right, key} nodes instead of the retail red-black tree (nothing
// observable depends on the tree shape: no slot iterates a map).

#include "entity_world.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace entity_world
{
	namespace
	{
		using namespace entity_capacity_layout;

		// ---- private header --------------------------------------------------------------------------------------
		constexpr std::uint32_t prefix_bytes = 0x1000; // precedes the object
		constexpr std::uint32_t prefix_magic = 0x444c5257; // 'WRLD'
		constexpr std::uint32_t sidecar_bytes = 272;       // layouts.json world_lifecycle_sidecar
		constexpr std::uint32_t sidecar_bitmap_bytes = 128;
		struct sidecar // offsets as layouts.json: retired 0, destroying 128, live 256, free 260, retired 264, depth/flags 268
		{
			std::uint32_t retired_bits[sidecar_bitmap_bytes / 4];
			std::uint32_t destroying_bits[sidecar_bitmap_bytes / 4];
			std::uint32_t live_count, free_count, retired_count;
			std::uint8_t reset_depth, flags, pad[2];
		};
		static_assert(sizeof(sidecar) == sidecar_bytes);
		constexpr std::uint8_t flag_reset_failure = 1, flag_delete_seen = 2;
		struct telemetry
		{
			std::uint32_t max_generation, full_refusals, allocator_null_refusals, reset_time_attempts, reentrant_release_refusals, resets;
			std::uint32_t retired_by_release, retired_by_reset, retired_by_test, name_map_full_refusals, name_node_exhaustion_refusals;
			std::uint32_t hierarchy_full_refusals, relation_table_refusals;
		};
		struct prefix { std::uint32_t magic; layout L; sidecar S; telemetry T; };
		static_assert(sizeof(prefix) <= prefix_bytes);

		prefix* prefix_of(void* object)
		{
			if (!object) return nullptr;
			auto* p = reinterpret_cast<prefix*>(static_cast<std::uint8_t*>(object) - prefix_bytes);
			return p->magic == prefix_magic ? p : nullptr;
		}
		const prefix* prefix_of(const void* object) { return prefix_of(const_cast<void*>(object)); }

		refusal_log_fn logger_fn = nullptr;

		// ---- the native container formulas (see the header) -----------------------------------------------------
		// A pool of n elements of E bytes at `base`: elements, a pad dword, the ring (n entries + pad), enqueue,
		// dequeue, count, the bitmap (n/32 words), the active count. Retail: 0x838 (E=4), 0xc38 (E=8), 0x1438 (E=16).
		pool_layout pool_for(std::uint32_t base, std::uint32_t n, std::uint32_t E)
		{
			pool_layout p;
			p.elems = base; p.elem_size = E;
			p.ring = base + n * E + 4;
			p.enqueue = p.ring + n * 4 + 4;
			p.dequeue = p.enqueue + 4;
			p.count = p.dequeue + 4;
			p.bitmap = p.count + 4;
			p.active = p.bitmap + n / 8;
			p.size = p.active + 4 - base;
			return p;
		}
		genpool_layout genpool_for(std::uint32_t base, std::uint32_t n, std::uint32_t E)
		{
			genpool_layout g;
			g.pool = pool_for(base, n, E);
			g.gens = base + g.pool.size;
			g.mask = g.gens + n * 4;
			g.shift = g.mask + 4;
			g.size = g.shift + 4 - base;
			return g;
		}
		map_layout map_for(std::uint32_t base, std::uint32_t n, std::uint32_t V)
		{
			map_layout m;
			m.base = base; m.root = base + 4; m.last = base + 8;
			m.nodes = pool_for(base + 0xc, n, 16);
			m.values = base + 0xc + m.nodes.size; m.value_size = V;
			m.size = 0xc + m.nodes.size + n * V + 4;
			return m;
		}
		relation_layout relation_for(std::uint32_t base, std::uint32_t n)
		{
			relation_layout r;
			r.base = base; r.heads = base;
			r.nodes = genpool_for(base + n * 4 + 4, n, 8);
			r.values = base + n * 4 + 4 + r.nodes.size;
			r.current = r.values + n * 4 + 4;
			r.cursor = r.current + 4;
			r.dirty = r.cursor + 4;
			r.size = r.dirty + 4 - base;
			return r;
		}
	}

	layout layout_for(std::uint32_t world)
	{
		layout L;
		if (world != stock_world && world != raised_world) return L;
		L.world = world;
		L.index_bits = world == stock_world ? stock_index_bits : raised_index_bits;
		L.index_mask = (1u << L.index_bits) - 1;
		L.set_words = world / 32; L.set_bytes = world / 8;
		L.core = world_core_for(world);
		L.core_live_bits_words = world / 32;
		const auto core_end = L.core.shift + 4;
		L.names = map_for(core_end, world, 4);
		L.name_nodes = genpool_for(core_end + L.names.size, world, 8);
		L.hierarchy = map_for(core_end + L.names.size + L.name_nodes.size, world, hierarchy_record_words * 4);
		L.hierarchy_version = L.hierarchy.base + L.hierarchy.size;
		L.relations = relation_for(L.hierarchy_version + 4, world);
		L.properties = L.relations.base + L.relations.size;
		L.reserved = L.properties + world * property_record_bytes;
		L.sets = L.reserved + 4;
		L.size = L.sets + property_count * L.set_bytes;
		return L;
	}

	namespace
	{
		// Retail offsets, checked once (the differential test also compares them against the native constructor's
		// writes); the raised ones are the plan's layouts.json "world" entries.
		struct layout_checks
		{
			layout_checks()
			{
				const auto s = layout_for(256), r = layout_for(1024);
				checks_ok = s.names.base == 0xc44 && s.names.root == 0xc48 && s.names.nodes.elems == 0xc50 && s.names.nodes.ring == 0x1c54 &&
				            s.names.nodes.active == 0x2084 && s.names.values == 0x2088 && s.name_nodes.pool.elems == 0x248c && s.name_nodes.pool.ring == 0x2c90 &&
				            s.name_nodes.pool.enqueue == 0x3094 && s.name_nodes.pool.active == 0x30c0 && s.name_nodes.gens == 0x30c4 && s.name_nodes.mask == 0x34c4 &&
				            s.hierarchy.base == 0x34cc && s.hierarchy.root == 0x34d0 && s.hierarchy.nodes.active == 0x490c && s.hierarchy.values == 0x4910 &&
				            s.hierarchy_version == 0x5d14 && s.relations.base == 0x5d18 && s.relations.nodes.pool.elems == 0x5d18 + 0x404 &&
				            s.relations.nodes.pool.ring == 0x5d18 + 0xc08 && s.relations.nodes.pool.bitmap == 0x5d18 + 0x1018 && s.relations.nodes.pool.active == 0x5d18 + 0x1038 &&
				            s.relations.nodes.gens == 0x5d18 + 0x103c && s.relations.nodes.mask == 0x5d18 + 0x143c && s.relations.values == 0x5d18 + 0x1444 &&
				            s.relations.current == 0x5d18 + 0x1848 && s.relations.cursor == 0x5d18 + 0x184c && s.relations.dirty == 0x5d18 + 0x1850 &&
				            s.relations.size == 0x1854 && s.properties == 0x756c && s.reserved == 0x856c && s.sets == 0x8570 && s.size == 0x9370 &&
				            r.names.base == 0x30a4 && r.name_nodes.pool.elems == 0x914c && r.hierarchy.base == 0xd1ec && r.hierarchy_version == 0x17294 &&
				            r.relations.base == 0x17298 && r.relations.size == 0x60b4 && r.properties == 0x1d34c && r.reserved == 0x2134c && r.sets == 0x21350 && r.size == 0x24b50;
			}
			bool checks_ok = false;
		};
		const layout_checks layout_checked;

		// ---- accessors ----------------------------------------------------------------------------------------------
		struct world
		{
			std::uint8_t* p; prefix& P; const layout& L;
			explicit world(void* self) : p(static_cast<std::uint8_t*>(self)), P(*prefix_of(self)), L(P.L) {}
			template <typename T> T& at(std::uint32_t off) { return *reinterpret_cast<T*>(p + off); }
			template <typename T> T* ptr(std::uint32_t off) { return reinterpret_cast<T*>(p + off); }
			const void* const* vt() const { return *reinterpret_cast<const void* const* const*>(p); }
			template <typename Fn> Fn slot(std::uint32_t s) const { return reinterpret_cast<Fn>(vt()[s / 4]); }

			// handle core
			void*& entity(std::uint32_t i) { return ptr<void*>(L.core.entities)[i]; }
			std::uint32_t* ring() { return ptr<std::uint32_t>(L.core.free_ring); }
			std::uint32_t& enqueue() { return at<std::uint32_t>(L.core.enqueue); }
			std::uint32_t& dequeue() { return at<std::uint32_t>(L.core.dequeue); }
			std::uint32_t& free_count() { return at<std::uint32_t>(L.core.free_count); }
			std::uint32_t* live_bits() { return ptr<std::uint32_t>(L.core.live_bits); }
			std::uint32_t& live_count() { return at<std::uint32_t>(L.core.live_count); }
			std::uint32_t* handles() { return ptr<std::uint32_t>(L.core.handles); }
			std::uint32_t& mask() { return at<std::uint32_t>(L.core.mask); }
			std::uint32_t& shift() { return at<std::uint32_t>(L.core.shift); }
			bool live(std::uint32_t i) { return (live_bits()[i >> 5] >> (i & 31)) & 1u; }
			void set_live(std::uint32_t i) { live_bits()[i >> 5] |= 1u << (i & 31); }
			void clear_live(std::uint32_t i) { live_bits()[i >> 5] &= ~(1u << (i & 31)); }
			bool retired(std::uint32_t i) const { return (P.S.retired_bits[i >> 5] >> (i & 31)) & 1u; }
			void set_retired(std::uint32_t i) { P.S.retired_bits[i >> 5] |= 1u << (i & 31); }
			bool destroying(std::uint32_t i) const { return (P.S.destroying_bits[i >> 5] >> (i & 31)) & 1u; }
			void set_destroying(std::uint32_t i, bool v) { if (v) P.S.destroying_bits[i >> 5] |= 1u << (i & 31); else P.S.destroying_bits[i >> 5] &= ~(1u << (i & 31)); }
			bool resetting() const { return P.S.reset_depth != 0; }
			std::uint32_t index(std::uint32_t token) const { return token & L.index_mask; }
			std::uint32_t generation(std::uint32_t token) const { return token >> L.index_bits; }

			// property records / sets
			std::uint32_t* record(std::uint32_t i) { return ptr<std::uint32_t>(L.properties + i * property_record_bytes); }
			std::uint32_t* members(std::uint32_t prop) { return ptr<std::uint32_t>(L.sets + prop * L.set_bytes); }

			// ---- the generic pool (0x483080/0x482ab0 ring ctor, 0x4c62c0-style ring init, 0x483410 pop, pushes) -------
			std::uint32_t* pool_ring(const pool_layout& Q) { return ptr<std::uint32_t>(Q.ring); }
			void pool_ring_push(const pool_layout& Q, std::uint32_t value)
			{
				auto& enq = at<std::uint32_t>(Q.enqueue); auto& cnt = at<std::uint32_t>(Q.count);
				auto s = enq; enq = s + 1; cnt += 1;
				if (s + 1 >= L.world) { enq = 0; s = L.world - 1; }
				pool_ring(Q)[s] = value;
			}
			// Ring init (0x4c62c0 / 0x4c6370 / 0x4c61f0): enqueue = dequeue = count = 0 then push 0..n-1.
			void pool_ring_init(const pool_layout& Q)
			{
				at<std::uint32_t>(Q.enqueue) = at<std::uint32_t>(Q.dequeue) = at<std::uint32_t>(Q.count) = 0;
				for (std::uint32_t i = 0; i < L.world; ++i) pool_ring_push(Q, i);
			}
			// Pool reset (0x4c7800 / 0x4c7830 / 0x4c7860): active = 0, bitmap cleared, ring re-initialised.
			void pool_reset(const pool_layout& Q)
			{
				at<std::uint32_t>(Q.active) = 0;
				std::memset(p + Q.bitmap, 0, L.world / 8);
				pool_ring_init(Q);
			}
			// Pool alloc (0x4c7360 / 0x4c7bb0 / 0x4c7050): the ring's front entry, bitmap bit set, dequeue advanced,
			// count down, active up. Native reads ring[dequeue] even when the ring is empty (a stale entry); this port
			// answers pool_none instead and the caller refuses.
			std::uint32_t pool_alloc(const pool_layout& Q)
			{
				auto& cnt = at<std::uint32_t>(Q.count); auto& deq = at<std::uint32_t>(Q.dequeue);
				if (cnt == 0) return pool_none;
				const auto i = pool_ring(Q)[deq];
				ptr<std::uint32_t>(Q.bitmap)[i >> 5] |= 1u << (i & 31);
				deq += 1; if (deq >= L.world) deq = 0;
				cnt -= 1;
				at<std::uint32_t>(Q.active) += 1;
				return i;
			}
			// Pool free (0x4c71c0 / 0x4c6630 / 0x4c7590): bitmap bit cleared, index pushed, active down.
			void pool_free(const pool_layout& Q, std::uint32_t i)
			{
				ptr<std::uint32_t>(Q.bitmap)[i >> 5] &= ~(1u << (i & 31));
				pool_ring_push(Q, i);
				at<std::uint32_t>(Q.active) -= 1;
			}
			// Generation pool seeding (0x4c6440 / 0x4c64b0): mask/shift from n-1, every token (1 << shift) | i.
			void genpool_seed(const genpool_layout& G)
			{
				std::uint32_t m = 0, sh = 0;
				for (std::uint32_t v = L.world - 1; v != 0; v >>= 1) { m = (m << 1) | 1; ++sh; }
				at<std::uint32_t>(G.mask) = m; at<std::uint32_t>(G.shift) = sh;
				for (std::uint32_t i = 0; i < L.world; ++i) ptr<std::uint32_t>(G.gens)[i] = (1u << sh) | i;
			}
			// Internal node-token advance (0x4c6150): += 1 << shift; a negative result reseeds to generation 1. (Internal
			// node namespaces only; the World handle table has its own bounded policy below.)
			void genpool_advance(const genpool_layout& G, std::uint32_t i)
			{
				auto& g = ptr<std::uint32_t>(G.gens)[i];
				const auto sh = at<std::uint32_t>(G.shift);
				g += 1u << sh;
				if (static_cast<std::int32_t>(g) < 0) g = (1u << sh) | i;
			}
			std::uint32_t node_index(const genpool_layout& G, std::uint32_t token) { return token & at<std::uint32_t>(G.mask); }
			std::uint32_t* node8(const genpool_layout& G, std::uint32_t i) { return ptr<std::uint32_t>(G.pool.elems + i * 8); } // {value, next}

			// ---- the ordered map (0x5ab7d0 find; 0x4c7b30/0x4c7e00 insert; 0x601e50 erase) on the native nodes ----
			std::uint32_t* map_node(const map_layout& M, std::uint32_t i) { return ptr<std::uint32_t>(M.nodes.elems + i * 16); } // {parent, left, right, key}
			std::uint32_t& map_root(const map_layout& M) { return at<std::uint32_t>(M.root); }
			std::uint32_t map_active(const map_layout& M) { return at<std::uint32_t>(M.nodes.active); }
			std::uint8_t* map_value(const map_layout& M, std::uint32_t i) { return p + M.values + i * M.value_size; }
			// 0x5ab7d0: signed comparison walk from the root; map_null when absent.
			std::uint32_t map_find(const map_layout& M, std::uint32_t key)
			{
				auto i = map_root(M);
				while (i != map_null)
				{
					const auto* n = map_node(M, i);
					const auto k = static_cast<std::int32_t>(n[3]);
					if (k > static_cast<std::int32_t>(key)) i = n[1];
					else if (k < static_cast<std::int32_t>(key)) i = n[2];
					else return i;
				}
				return map_null;
			}
			// Insert a key (the caller has checked the capacity, as the native callers do); returns the node index or
			// map_null when the pool is exhausted (unreachable through the checked callers). Duplicate keys are never
			// inserted by the callers (they find first).
			std::uint32_t map_insert(const map_layout& M, std::uint32_t key)
			{
				const auto i = pool_alloc(M.nodes);
				if (i == pool_none) return map_null;
				auto* n = map_node(M, i);
				n[3] = key; n[1] = n[2] = map_null; n[0] = map_null;
				at<std::uint32_t>(M.last) = i;
				auto& root = map_root(M);
				if (root == map_null) { root = i; return i; }
				auto c = root;
				for (;;)
				{
					auto* cn = map_node(M, c);
					const bool left = static_cast<std::int32_t>(cn[3]) > static_cast<std::int32_t>(key);
					auto& link = left ? cn[1] : cn[2];
					if (link == map_null) { link = i; n[0] = c; return i; }
					c = link;
				}
			}
			void map_replace_child(const map_layout& M, std::uint32_t parent, std::uint32_t old, std::uint32_t with)
			{
				if (parent == map_null) map_root(M) = with;
				else { auto* pn = map_node(M, parent); if (pn[1] == old) pn[1] = with; else pn[2] = with; }
				if (with != map_null) map_node(M, with)[0] = parent;
			}
			// Erase node i (binary search tree deletion); the node returns to the pool (0x601e50 -> slot 0x10 = 0x4c6630).
			void map_erase(const map_layout& M, std::uint32_t i)
			{
				auto* n = map_node(M, i);
				if (n[1] != map_null && n[2] != map_null)
				{
					// in-order successor: leftmost of the right subtree
					auto s = n[2];
					while (map_node(M, s)[1] != map_null) s = map_node(M, s)[1];
					auto* sn = map_node(M, s);
					if (sn[0] != i) { map_replace_child(M, sn[0], s, sn[2]); sn[2] = n[2]; map_node(M, sn[2])[0] = s; }
					map_replace_child(M, n[0], i, s);
					sn[1] = n[1]; map_node(M, sn[1])[0] = s;
				}
				else map_replace_child(M, n[0], i, n[1] != map_null ? n[1] : n[2]);
				n[0] = n[1] = n[2] = map_null;
				pool_free(M.nodes, i);
			}
			// Map clear (0x4c7740: root = null then 0x4c7800 on the node pool). `last` and the values stay as native.
			void map_clear(const map_layout& M) { map_root(M) = map_null; pool_reset(M.nodes); }

			// ---- names ---------------------------------------------------------------------------------------------
			// The native methods copy the name into a 100-byte stack buffer with no bound (a longer name would smash the
			// frame and trip the security cookie); this port copies at most 255 characters.
			std::uint32_t folded_hash(const char* name, char* folded, std::size_t folded_size)
			{
				std::size_t i = 0;
				for (; name[i] != 0 && i + 1 < folded_size; ++i) folded[i] = name[i];
				folded[i] = 0;
				native_hooks.fold_case(folded);
				std::uint32_t h = 0; native_hooks.name_hash(&h, nullptr, folded);
				return h;
			}
			std::uint32_t hash_of(const char* text) { std::uint32_t h = 0; native_hooks.name_hash(&h, nullptr, text); return h; }

			// ---- lifecycle --------------------------------------------------------------------------------------------
			void sync_sidecar()
			{
				P.S.live_count = live_count(); P.S.free_count = free_count();
			}
			// Advance the slot's generation, or retire the slot at generation_max (plan section 3). Decided BEFORE any
			// free-ring publication.
			void advance_or_retire(std::uint32_t i, std::uint32_t& retired_counter)
			{
				auto& h = handles()[i];
				const auto g = generation(h);
				if (g < generation_max)
				{
					h += 1u << L.index_bits;
					if (g + 1 > P.T.max_generation) P.T.max_generation = g + 1;
				}
				else if (!retired(i)) { set_retired(i); P.S.retired_count += 1; retired_counter += 1; }
			}
			void core_ring_push(std::uint32_t i)
			{
				auto s = enqueue(); enqueue() = s + 1; free_count() += 1;
				if (s + 1 >= L.world) { enqueue() = 0; s = L.world - 1; }
				ring()[s] = i;
			}
			// Rebuild the free ring ascending with every reusable slot exactly once (reset, and the test retirement).
			void core_ring_rebuild()
			{
				enqueue() = dequeue() = free_count() = 0;
				for (std::uint32_t i = 0; i < L.world; ++i) if (!retired(i) && !live(i)) core_ring_push(i);
				P.S.free_count = free_count();
			}
			status snapshot() const;
			void refuse(refusal_kind kind, std::uint32_t size, std::uint32_t token) const
			{
				if (logger_fn) { refusal r{kind, size, token, snapshot()}; logger_fn(r); }
			}
		};

		status world::snapshot() const
		{
			status s;
			s.available = true; s.capacity = L.world;
			s.live = P.S.live_count; s.free_slots = P.S.free_count; s.retired = P.S.retired_count;
			s.max_generation = P.T.max_generation;
			s.full_refusals = P.T.full_refusals; s.allocator_null_refusals = P.T.allocator_null_refusals; s.reset_time_attempts = P.T.reset_time_attempts;
			s.refusals = s.full_refusals + s.allocator_null_refusals + s.reset_time_attempts;
			s.reentrant_release_refusals = P.T.reentrant_release_refusals; s.resets = P.T.resets;
			s.retired_by_release = P.T.retired_by_release; s.retired_by_reset = P.T.retired_by_reset; s.retired_by_test = P.T.retired_by_test;
			s.name_map_full_refusals = P.T.name_map_full_refusals; s.name_node_exhaustion_refusals = P.T.name_node_exhaustion_refusals;
			s.hierarchy_full_refusals = P.T.hierarchy_full_refusals; s.relation_table_refusals = P.T.relation_table_refusals;
			s.resetting = P.S.reset_depth != 0; s.reset_failure_latched = (P.S.flags & flag_reset_failure) != 0; s.delete_flag_seen = (P.S.flags & flag_delete_seen) != 0;
			return s;
		}

		// ---- native ABI shapes of the objects the World calls ----------------------------------------------------
		using fn_obj = void*(__fastcall*)(void*, void*);                      // entity slot 0 (object getter), slot 0x44 (bounds pointer)
		using fn_void = void(__fastcall*)(void*, void*);                      // Grid slot 0, the entity-object slot 4
		using fn_int = void(__fastcall*)(void*, void*, std::uint32_t);        // entity slot 4 (0), Grid slot 0x24 (token), allocator slot 0x10 (ptr), entity slot 0x6c (0)
		using fn_ptr = void(__fastcall*)(void*, void*, const void*);          // entity slot 0x68 (&name id)
		using fn_alloc = void*(__fastcall*)(void*, void*, std::uint32_t);     // allocator slot 0xc (size)
		using fn_grid_update = void(__fastcall*)(void*, void*, std::uint32_t, const void*); // Grid slot 0x28 (token, bounds)
		using fn_by_name = void*(__fastcall*)(void*, void*, const char*);     // World slot 8
		using fn_by_token = void*(__fastcall*)(void*, void*, WorldToken);     // World slot 0xc
		using fn_valid = bool(__fastcall*)(void*, void*, WorldToken);         // World slot 0x14
		using fn_handle = void(__fastcall*)(void*, void*, std::uint32_t*, DenseIndex); // World slot 0x18
		using fn_unregister = void(__fastcall*)(void*, void*, const char*, std::uint32_t); // World slot 0x30
		using fn_release = void(__fastcall*)(void*, void*, std::uint32_t);    // World slot 0x64
		template <typename Fn> Fn vslot(void* object, std::uint32_t s) { return reinterpret_cast<Fn>((*static_cast<const void* const* const*>(object))[s / 4]); }

		// 0x4654d0: World getter -> slot 0x14 (the getter returns this object, so a self call through the vtable).
		bool valid_token(world& W, std::uint32_t token) { return W.slot<fn_valid>(0x14)(W.p, nullptr, WorldToken{token}); }

		// ---- the relationship table (one table; 0x5d18 + table * 0x1854 natively) -----------------------------------
		// Native indexes the table by `table * 0x1854` without a check; every caller in the retail image passes 0 (the
		// object holds one table). Any other value is refused and counted (the arithmetic would leave the object).
		bool relation_table_ok(world& W, int table) { if (table == 0) return true; W.P.T.relation_table_refusals += 1; W.refuse(refusal_kind::relation_table_index, 0, 0); return false; }
		std::uint32_t& rel_head(world& W, std::uint32_t i) { return W.ptr<std::uint32_t>(W.L.relations.heads)[i]; }
		std::uint32_t& rel_value(world& W, std::uint32_t i) { return W.ptr<std::uint32_t>(W.L.relations.values)[i]; }
		std::uint32_t& rel_current(world& W) { return W.at<std::uint32_t>(W.L.relations.current); }
		std::uint32_t& rel_cursor(world& W) { return W.at<std::uint32_t>(W.L.relations.cursor); }
		std::uint8_t& rel_dirty(world& W) { return W.at<std::uint8_t>(W.L.relations.dirty); }

		// 0x4c7320: a node from the relation pool holding {value, next}; returns its packed token.
		std::uint32_t rel_node_new(world& W, std::uint32_t value, std::uint32_t next)
		{
			const auto& G = W.L.relations.nodes;
			const auto i = W.pool_alloc(G.pool);
			if (i == pool_none) return 0; // unreachable: at most one node per entity (0x4c7270 removes before adding)
			const auto tok = W.ptr<std::uint32_t>(G.gens)[i];
			auto* n = W.node8(G, W.node_index(G, tok));
			n[1] = next; n[0] = value;
			return tok;
		}
		// 0x4c73d0: unlink `token` from the list of `owner`'s members; zero values[token] only when found (native).
		void rel_unlink(world& W, std::uint32_t token, std::uint32_t owner)
		{
			const auto& G = W.L.relations.nodes;
			auto* link = &rel_head(W, W.index(owner));
			if (*link == 0) return;
			std::uint32_t* n;
			for (;;)
			{
				n = W.node8(G, W.node_index(G, *link));
				if (n[0] == token) break;
				link = &n[1];
				if (*link == 0) return;
			}
			const auto next = n[1];
			const auto ni = W.node_index(G, *link);
			W.pool_free(G.pool, ni);
			W.genpool_advance(G, ni);
			*link = next;
			rel_value(W, W.index(token)) = 0;
		}
		// 0x4c72e0: values[token] = value; when value != 0, push token onto value's member list.
		void rel_link(world& W, std::uint32_t token, std::uint32_t value)
		{
			rel_value(W, W.index(token)) = value;
			if (value == 0) return;
			auto& head = rel_head(W, W.index(value));
			head = rel_node_new(W, token, head);
		}
		// 0x4c7270: dirty = 1; for a valid token whose value differs: unlink the old (when any), link the new.
		void rel_set(world& W, std::uint32_t token, std::uint32_t value)
		{
			rel_dirty(W) = 1;
			if (token == 0) return;
			if (!valid_token(W, token)) return;
			const auto cur = rel_value(W, W.index(token));
			if (cur == value) return;
			if (cur != 0) rel_unlink(W, token, cur);
			rel_link(W, token, value);
		}
		// 0x4c6d00: begin the enumeration of `token`'s members; writes the first member (0 when none).
		void rel_begin(world& W, std::uint32_t* out, std::uint32_t token)
		{
			const auto& G = W.L.relations.nodes;
			std::uint32_t first = 0;
			rel_current(W) = token;
			if (token != 0 && valid_token(W, token))
			{
				const auto head = rel_head(W, W.index(token));
				rel_cursor(W) = head;
				if (head != 0)
				{
					const auto* n = W.node8(G, W.node_index(G, head));
					first = n[0];
					rel_cursor(W) = n[1];
				}
			}
			rel_dirty(W) = 0;
			*out = first;
		}
		// 0x4c6d70 / 0x4c6dd0: the next member (0 at the end).
		void rel_next(world& W, std::uint32_t* out)
		{
			const auto& G = W.L.relations.nodes;
			std::uint32_t v = 0;
			if (rel_cursor(W) != 0)
			{
				const auto* n = W.node8(G, W.node_index(G, rel_cursor(W)));
				v = n[0];
				rel_cursor(W) = n[1];
			}
			*out = v;
		}
		// 0x4c7610 (release): dirty = 1; 0x4c7640 unlinks the entity from its value's list; 0x4c7690 clears the value
		// of every member of the entity, frees their nodes and clears the head.
		void rel_release(world& W, std::uint32_t token)
		{
			const auto& G = W.L.relations.nodes;
			rel_dirty(W) = 1;
			// 0x4c7640
			rel_dirty(W) = 1;
			if (token != 0 && valid_token(W, token) && rel_value(W, W.index(token)) != 0) rel_unlink(W, token, rel_value(W, W.index(token)));
			// 0x4c7690
			rel_dirty(W) = 1;
			if (token != 0 && valid_token(W, token))
			{
				auto cur = rel_head(W, W.index(token));
				while (cur != 0)
				{
					const auto ni = W.node_index(G, cur);
					auto* n = W.node8(G, ni);
					rel_value(W, W.index(n[0])) = 0;
					const auto next = n[1];
					W.pool_free(G.pool, ni);
					W.genpool_advance(G, ni);
					cur = next;
				}
				rel_head(W, W.index(token)) = 0;
			}
		}

		// ---- hierarchy records ------------------------------------------------------------------------------------------
		// 0x4c7db0: add `id` to the record's four child names unless present; the first empty (== hash("")) slot takes it.
		void record_add_child(world& W, std::uint32_t* record, std::uint32_t id)
		{
			for (std::uint32_t i = 0; i < 4; ++i)
			{
				if (record[1 + i] == id) return;
				const auto empty = W.hash_of("");
				if (record[1 + i] == empty) { record[1 + i] = id; return; }
			}
		}
		// 0x4c7a10: after a registration, apply the name's hierarchy record: the parent id (when not empty) to the entity
		// (slot 0x68 with a pointer INTO the record, then slot 0x6c(0)); then each child name's entity (slot 8 by name)
		// gets this entity's name id (slot 0x68 with a pointer to a local, slot 0x6c(0)); the child loop stops at the
		// first empty id.
		void post_register(world& W, const char* folded, std::uint32_t token)
		{
			auto* entity = W.slot<fn_by_token>(0xc)(W.p, nullptr, WorldToken{token}); // 0x4654b0
			if (!entity) return;
			const auto id = W.hash_of(folded);
			const auto idx = W.map_find(W.L.hierarchy, id);
			if (idx == map_null) return;
			auto* record = reinterpret_cast<std::uint32_t*>(W.map_value(W.L.hierarchy, idx));
			const auto empty = W.hash_of("");
			if (record[0] != empty)
			{
				vslot<fn_ptr>(entity, 0x68)(entity, nullptr, &record[0]);
				vslot<fn_int>(entity, 0x6c)(entity, nullptr, 0);
			}
			for (std::uint32_t i = 0; i < 4; ++i)
			{
				const auto empty_i = W.hash_of("");
				const auto child = record[1 + i];
				if (child == empty_i) return;
				const char* child_name = child == 0 ? "" : native_hooks.name_of_id(&record[1 + i], nullptr); // the inline 0x602280 lookup == 0x41aa80
				auto* other = W.slot<fn_by_name>(0x8)(W.p, nullptr, child_name);
				if (other)
				{
					const auto* name_id = reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(entity) + 0x18);
					std::uint32_t h = W.hash_of(native_hooks.name_of_id(name_id, nullptr));
					vslot<fn_ptr>(other, 0x68)(other, nullptr, &h);
					vslot<fn_int>(other, 0x6c)(other, nullptr, 0);
				}
			}
		}
	}

	std::size_t object_size(const descriptor& d) { return layout_for(d.world).size; }
	const layout* layout_of(const void* object) { const auto* p = prefix_of(object); return p ? &p->L : nullptr; }

	hooks native_hooks = retail_hooks();
	hooks retail_hooks()
	{
		hooks h;
		h.grid_getter = native<void*(__cdecl*)()>(native_grid_getter);
		h.allocator_getter = native<void*(__cdecl*)()>(native_allocator_getter);
		h.name_of_id = native<const char*(__fastcall*)(const std::uint32_t*, void*)>(native_name_of_handle);
		h.name_hash = native<void(__fastcall*)(std::uint32_t*, void*, const char*)>(native_name_hash);
		h.fold_case = native<void(__cdecl*)(char*)>(native_fold_case);
		return h;
	}
	void set_refusal_logger(refusal_log_fn fn) { logger_fn = fn; }
	refusal_log_fn refusal_logger() { return logger_fn; }

	// ---- the vtable ------------------------------------------------------------------------------------------------
	const vtable_image vtable{
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(native_rtti_locator)),
		{reinterpret_cast<const void*>(&scalar_deleting_destructor), reinterpret_cast<const void*>(&reset), reinterpret_cast<const void*>(&entity_by_name),
		 reinterpret_cast<const void*>(&entity_by_token), reinterpret_cast<const void*>(&entity_by_dense), reinterpret_cast<const void*>(&token_valid),
		 reinterpret_cast<const void*>(&handle_by_dense), reinterpret_cast<const void*>(&index_of), reinterpret_cast<const void*>(&entity_of),
		 reinterpret_cast<const void*>(&hierarchy_version_get), reinterpret_cast<const void*>(&hierarchy_version_set), reinterpret_cast<const void*>(&register_name),
		 reinterpret_cast<const void*>(&unregister_name), reinterpret_cast<const void*>(&entities_named), reinterpret_cast<const void*>(&hierarchy_relate),
		 reinterpret_cast<const void*>(&property_update), reinterpret_cast<const void*>(&property_members), reinterpret_cast<const void*>(&property_record),
		 reinterpret_cast<const void*>(&grid_update), reinterpret_cast<const void*>(&relation_set), reinterpret_cast<const void*>(&relation_members),
		 reinterpret_cast<const void*>(&relation_get), reinterpret_cast<const void*>(&relation_begin), reinterpret_cast<const void*>(&relation_next),
		 reinterpret_cast<const void*>(&allocate), reinterpret_cast<const void*>(&release)}};

	// ---- construction / destruction ---------------------------------------------------------------------------------
	// 0x4c6730: vptr; handle core (0x4c6440: pointer pool ctor, ring 0..n-1, mask/shift, tokens at generation 1); name map
	// (root null, last -1, node pool ctor with keys zeroed, ring 0..n-1); name node pool (0x4c64b0: tokens at generation
	// 1); hierarchy map (same, 20-byte values zeroed by 0x4c6190); hierarchy version 0; the relationship table (0x4c6700:
	// node tokens seeded then advanced once by 0x4c66c0, heads/values/current/cursor/dirty zero); properties and the 112
	// membership sets zeroed.
	void* create(const descriptor& d)
	{
		const auto L = layout_for(d.world);
		if (!L.valid() || !layout_checked.checks_ok) return nullptr;
		auto* base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, prefix_bytes + L.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!base) return nullptr;
		auto* P = reinterpret_cast<prefix*>(base);
		P->magic = prefix_magic; P->L = L;
		auto* object = base + prefix_bytes;
		*reinterpret_cast<const void**>(object) = vtable.slots;
		world W(object);
		// handle core
		W.pool_ring_init(pool_layout{L.core.entities, 4, L.core.free_ring, L.core.enqueue, L.core.dequeue, L.core.free_count, L.core.live_bits, L.core.live_count, 0});
		{
			std::uint32_t m = 0, sh = 0;
			for (std::uint32_t v = L.world - 1; v != 0; v >>= 1) { m = (m << 1) | 1; ++sh; }
			W.mask() = m; W.shift() = sh;
			for (std::uint32_t i = 0; i < L.world; ++i) W.handles()[i] = d.tokens.seed(DenseIndex{i}).value;
		}
		// name map, name nodes, hierarchy map
		W.map_root(L.names) = map_null; W.at<std::uint32_t>(L.names.last) = 0xffffffffu; W.pool_ring_init(L.names.nodes);
		W.genpool_seed(L.name_nodes); W.pool_ring_init(L.name_nodes.pool);
		W.map_root(L.hierarchy) = map_null; W.at<std::uint32_t>(L.hierarchy.last) = 0xffffffffu; W.pool_ring_init(L.hierarchy.nodes);
		W.at<std::uint32_t>(L.hierarchy_version) = 0;
		// the relationship table: 0x4c6520 -> 0x4c64b0 (seed), then 0x4c66c0 -> 0x4c6530 (pool reset + advance all)
		W.genpool_seed(L.relations.nodes); W.pool_ring_init(L.relations.nodes.pool);
		for (std::uint32_t i = 0; i < L.world; ++i) W.genpool_advance(L.relations.nodes, i);
		// sidecar
		P->S.live_count = 0; P->S.free_count = W.free_count(); P->S.retired_count = 0; P->S.reset_depth = 0; P->S.flags = 0;
		P->T.max_generation = generation_min;
		return object;
	}

	void destroy(void* object)
	{
		auto* P = prefix_of(object);
		if (!P) return;
		P->magic = 0;
		VirtualFree(P, 0, MEM_RELEASE);
	}

	status counters(const void* object)
	{
		if (!prefix_of(object)) return {};
		world W(const_cast<void*>(object));
		return W.snapshot();
	}

	bool retire_slot(void* object, std::uint32_t index)
	{
		if (!prefix_of(object)) return false;
		world W(object);
		if (index >= W.L.world || W.live(index) || W.retired(index) || W.resetting()) return false;
		W.set_retired(index); W.P.S.retired_count += 1; W.P.T.retired_by_test += 1;
		// rebuild the ring in its current FIFO order without the retired slot
		std::uint32_t order[raised_world]; std::uint32_t n = 0;
		for (std::uint32_t k = 0, d = W.dequeue(); k < W.free_count(); ++k, d = (d + 1) % W.L.world) if (W.ring()[d] != index) order[n++] = W.ring()[d];
		W.enqueue() = W.dequeue() = W.free_count() = 0;
		for (std::uint32_t k = 0; k < n; ++k) W.core_ring_push(order[k]);
		W.sync_sidecar();
		return true;
	}

	bool check_invariants(const void* object, char* why, std::size_t why_size)
	{
		auto fail = [&](const char* text) { if (why && why_size) std::snprintf(why, why_size, "%s", text); return false; };
		if (!prefix_of(object)) return fail("not a World object");
		world W(const_cast<void*>(object));
		const auto n = W.L.world;
		std::uint32_t live = 0, retired = 0;
		for (std::uint32_t i = 0; i < n; ++i)
		{
			const bool l = W.live(i), r = W.retired(i);
			if (l && r) return fail("a slot is both live and retired");
			if (l && W.entity(i) == nullptr) return fail("a live slot has no entity pointer");
			if (!l && W.entity(i) != nullptr) return fail("a free slot keeps an entity pointer");
			const auto g = W.generation(W.handles()[i]);
			if (W.index(W.handles()[i]) != i) return fail("a stored token's index differs from its slot");
			if (g < generation_min || g > generation_max) return fail("a stored generation is outside 1..0x3fff");
			live += l; retired += r;
		}
		if (live != W.live_count()) return fail("live bitmap and live count disagree");
		if (retired != W.P.S.retired_count) return fail("retired bitmap and retired count disagree");
		if (live + W.free_count() + retired != n) return fail("live + free + retired != capacity");
		if (W.free_count() > n || W.enqueue() >= n || W.dequeue() >= n) return fail("ring words out of range");
		if ((W.dequeue() + W.free_count()) % n != W.enqueue() && !(W.free_count() == n && W.dequeue() == W.enqueue())) return fail("ring enqueue/dequeue/count inconsistent");
		std::uint32_t seen[raised_world / 32] = {};
		for (std::uint32_t k = 0, d = W.dequeue(); k < W.free_count(); ++k, d = (d + 1) % n)
		{
			const auto s = W.ring()[d];
			if (s >= n) return fail("ring entry out of range");
			if (W.live(s) || W.retired(s)) return fail("ring entry is live or retired");
			if ((seen[s >> 5] >> (s & 31)) & 1u) return fail("ring entry duplicated");
			seen[s >> 5] |= 1u << (s & 31);
		}
		for (std::uint32_t i = 0; i < n; ++i) if (!W.live(i) && !W.retired(i) && !((seen[i >> 5] >> (i & 31)) & 1u) && !W.resetting()) return fail("a reusable free slot is missing from the ring");
		if (W.P.S.live_count != W.live_count() || W.P.S.free_count != W.free_count()) return fail("sidecar counts differ from the core");
		return true;
	}

	// ---- slots ------------------------------------------------------------------------------------------------------------

	// 0x4c7f00: destructor (0x4c7ea0: vptr rewrite, empty per-table destructor, base vptr) then operator delete when
	// bit 0 of the flag is set. The DLL object's backing is the DLL's (never the game's heap): the flag is recorded and
	// ignored; nothing else is observable.
	void* __fastcall scalar_deleting_destructor(void* self, void*, std::uint32_t flags)
	{
		if (!prefix_of(self)) return self;
		world W(self);
		if (flags & 1) { W.P.S.flags |= flag_delete_seen; W.refuse(refusal_kind::delete_flag, 0, 0); }
		return self;
	}

	// 0x4c7740 (slot 4 thunk 0x4c7730): destroy every live slot (slot 0x18 then slot 0x64), Grid slot 0, hierarchy map
	// cleared, the handle core rebuilt, the name map cleared, the name node pool reset and every node token advanced.
	// Lifecycle per plan section 3: resetting state, no free-ring publication during destruction, occupied slots
	// advance through destruction and every non-retired slot advances once more, then an ascending FIFO of every
	// reusable slot is rebuilt.
	void __fastcall reset(void* self, void*)
	{
		if (!prefix_of(self)) return;
		world W(self);
		W.P.S.reset_depth += 1;
		W.P.T.resets += 1;
		for (std::uint32_t i = 0; i < W.L.world; ++i)
		{
			if (!W.live(i)) continue;
			std::uint32_t token = 0;
			W.slot<fn_handle>(0x18)(self, nullptr, &token, DenseIndex{i});
			W.slot<fn_release>(0x64)(self, nullptr, token);
		}
		if (auto* grid = native_hooks.grid_getter()) vslot<fn_void>(grid, 0x0)(grid, nullptr);
		W.map_clear(W.L.hierarchy);
		// the handle core: 0x4c7830 (live count 0, bitmap cleared, ring 0..n-1) then 0x4c7550 on every slot
		std::memset(W.live_bits(), 0, W.L.world / 8);
		for (std::uint32_t i = 0; i < W.L.world; ++i) { W.entity(i) = nullptr; W.set_destroying(i, false); if (!W.retired(i)) W.advance_or_retire(i, W.P.T.retired_by_reset); }
		W.live_count() = 0;
		W.core_ring_rebuild();
		W.map_clear(W.L.names);
		W.pool_reset(W.L.name_nodes.pool);
		for (std::uint32_t i = 0; i < W.L.world; ++i) W.genpool_advance(W.L.name_nodes, i);
		W.sync_sidecar();
		W.P.S.reset_depth -= 1;
	}

	// 0x4c6f20: fold + intern the name, find it; the first name node's handle through slot 0xc; null when unknown.
	void* __fastcall entity_by_name(void* self, void*, const char* name)
	{
		if (!prefix_of(self)) return nullptr;
		world W(self);
		char folded[256];
		const auto id = W.folded_hash(name, folded, sizeof folded);
		const auto idx = W.map_find(W.L.names, id);
		if (idx == map_null) return nullptr;
		const auto head = *reinterpret_cast<const std::uint32_t*>(W.map_value(W.L.names, idx));
		const auto* node = W.node8(W.L.name_nodes, W.node_index(W.L.name_nodes, head));
		return W.slot<fn_by_token>(0xc)(self, nullptr, WorldToken{node[0]});
	}

	// 0x4c6c20: 0 for the null token; the slot's pointer when slot 0x14 validates the token (FindToken).
	void* __fastcall entity_by_token(void* self, void*, WorldToken token)
	{
		if (!prefix_of(self)) return nullptr;
		world W(self);
		if (token.value == 0) return nullptr;
		if (!W.slot<fn_valid>(0x14)(self, nullptr, token)) return nullptr; // 0x4c6bd0 (inlined call)
		return W.entity(W.index(token.value));
	}

	// 0x4c68d0: a DENSE index (FindDense): the slot's handle through slot 0xc when its live bit is set, else 0. Native
	// reads the bitmap without a bound; an index beyond the capacity answers 0 here.
	void* __fastcall entity_by_dense(void* self, void*, DenseIndex index)
	{
		if (!prefix_of(self)) return nullptr;
		world W(self);
		if (index.value >= W.L.world || !W.live(index.value)) return nullptr;
		return W.slot<fn_by_token>(0xc)(self, nullptr, WorldToken{W.handles()[index.value]});
	}

	// 0x4c6bd0 (slot 0x14 thunk 0x4c6bc0): index = token & mask; the stored token must equal the whole token; the
	// result is the live bit. Phase A adds the operational generation bound (1..0x3fff) and the retired check.
	bool __fastcall token_valid(void* self, void*, WorldToken token)
	{
		if (!prefix_of(self)) return false;
		world W(self);
		const auto i = W.index(token.value);
		if (i >= W.L.world) return false;
		if (W.handles()[i] != token.value) return false;
		const auto g = W.generation(token.value);
		if (g < generation_min || g > generation_max) return false;
		if (W.retired(i)) return false;
		return W.live(i);
	}

	// 0x4c6910: *out = the slot's handle when live, else 0 (DENSE index; bounded here as slot 0x10).
	// Returns `out` (native mov eax,[esp+4] on both paths; 0x466742 and 0x4cd525 read [eax]).
	std::uint32_t* __fastcall handle_by_dense(void* self, void*, std::uint32_t* out, DenseIndex index)
	{
		if (!prefix_of(self)) { *out = 0; return out; }
		world W(self);
		*out = (index.value < W.L.world && W.live(index.value)) ? W.handles()[index.value] : 0;
		return out;
	}

	// 0x4c6840: mask & token.
	std::uint32_t __fastcall index_of(void* self, void*, std::uint32_t token)
	{
		if (!prefix_of(self)) return 0;
		world W(self);
		return W.mask() & token;
	}
	// 0x4c6850 / 0x4c6860: the hierarchy version word.
	// Native EAX: `out` / `in` (mov eax,[esp+4]); 0x50a659 compares [eax] after slot 0x24.
	std::uint32_t* __fastcall hierarchy_version_get(void* self, void*, std::uint32_t* out) { if (!prefix_of(self)) { *out = 0; return out; } world W(self); *out = W.at<std::uint32_t>(W.L.hierarchy_version); return out; }
	const std::uint32_t* __fastcall hierarchy_version_set(void* self, void*, const std::uint32_t* in) { if (!prefix_of(self)) return in; world W(self); W.at<std::uint32_t>(W.L.hierarchy_version) = *in; return in; }
	// 0x4c6870: slot 0xc through the vtable.
	void* __fastcall entity_of(void* self, void*, WorldToken token) { if (!prefix_of(self)) return nullptr; world W(self); return W.slot<fn_by_token>(0xc)(self, nullptr, token); }

	// 0x4c7890: register (name, token). Known name: a no-op when the token is already listed, else a new name node is
	// pushed at the head. Unknown name: a name node is taken first, then the map insert only when the map is not full
	// (the taken node stays consumed when it is: the native leak is preserved). Then the hierarchy post-registration,
	// which the "already listed" early return skips.
	void __fastcall register_name(void* self, void*, const char* name, std::uint32_t token)
	{
		if (!prefix_of(self)) return;
		world W(self);
		const auto& G = W.L.name_nodes;
		char folded[256];
		const auto id = W.folded_hash(name, folded, sizeof folded);
		const auto idx = W.map_find(W.L.names, id);
		if (idx != map_null)
		{
			auto& head = *reinterpret_cast<std::uint32_t*>(W.map_value(W.L.names, idx));
			for (auto cur = head; cur != 0;)
			{
				const auto* n = W.node8(G, W.node_index(G, cur));
				if (n[0] == token) return;
				cur = n[1];
			}
			const auto ni = W.pool_alloc(G.pool);
			if (ni == pool_none) { W.P.T.name_node_exhaustion_refusals += 1; W.refuse(refusal_kind::name_nodes_exhausted, 0, token); return; }
			const auto ntok = W.ptr<std::uint32_t>(G.gens)[ni];
			auto* n = W.node8(G, W.node_index(G, ntok));
			n[0] = token; n[1] = head;
			head = ntok;
		}
		else
		{
			const auto ni = W.pool_alloc(G.pool);
			if (ni == pool_none) { W.P.T.name_node_exhaustion_refusals += 1; W.refuse(refusal_kind::name_nodes_exhausted, 0, token); return; }
			const auto ntok = W.ptr<std::uint32_t>(G.gens)[ni];
			auto* n = W.node8(G, W.node_index(G, ntok));
			n[0] = token; n[1] = 0;
			if (W.map_active(W.L.names) != W.L.world)
			{
				const auto id2 = W.hash_of(folded);
				const auto mi = W.map_insert(W.L.names, id2);
				if (mi != map_null) *reinterpret_cast<std::uint32_t*>(W.map_value(W.L.names, mi)) = ntok;
			}
			else { W.P.T.name_map_full_refusals += 1; W.refuse(refusal_kind::name_map_full, 0, token); }
		}
		post_register(W, folded, token);
	}

	// 0x4c70a0: unregister (name, token): find the name; unlink the first node holding the token (freeing it and
	// advancing its node generation); erase the name when its list is empty.
	void __fastcall unregister_name(void* self, void*, const char* name, std::uint32_t token)
	{
		if (!prefix_of(self)) return;
		world W(self);
		const auto& G = W.L.name_nodes;
		char folded[256];
		const auto id = W.folded_hash(name, folded, sizeof folded);
		const auto idx = W.map_find(W.L.names, id);
		if (idx == map_null) return;
		auto* head = reinterpret_cast<std::uint32_t*>(W.map_value(W.L.names, idx));
		auto* link = head;
		if (*link != 0)
		{
			std::uint32_t* n = W.node8(G, W.node_index(G, *link));
			bool found = n[0] == token;
			while (!found)
			{
				link = &n[1];
				if (*link == 0) break;
				n = W.node8(G, W.node_index(G, *link));
				found = n[0] == token;
			}
			if (found)
			{
				const auto next = n[1];
				const auto ni = W.node_index(G, *link);
				W.pool_free(G.pool, ni);
				W.genpool_advance(G, ni);
				*link = next;
			}
		}
		if (*head == 0) W.map_erase(W.L.names, idx);
	}

	// 0x4c6e10: the set of entities registered under the name (cleared first); AL = 1 when the name exists, else 0.
	bool __fastcall entities_named(void* self, void*, const char* name, std::uint32_t* set)
	{
		if (!prefix_of(self)) return false;
		world W(self);
		const auto& G = W.L.name_nodes;
		char folded[256];
		const auto id = W.folded_hash(name, folded, sizeof folded);
		const auto idx = W.map_find(W.L.names, id);
		if (idx == map_null) return false;
		std::memset(set, 0, W.L.set_bytes);
		for (auto cur = *reinterpret_cast<const std::uint32_t*>(W.map_value(W.L.names, idx)); cur != 0;)
		{
			const auto* n = W.node8(G, W.node_index(G, cur));
			const auto e = W.index(n[0]);
			set[e >> 5] |= 1u << (e & 31);
			cur = n[1];
		}
		return true;
	}

	// 0x4c7c20: hierarchy (parent name, child name): refused when the map cannot take two more records; the parent's
	// record gets the child id (new record when absent); the child's record gets the parent id (new record when absent).
	void __fastcall hierarchy_relate(void* self, void*, const char* parent, const char* child)
	{
		if (!prefix_of(self)) return;
		world W(self);
		const auto& H = W.L.hierarchy;
		if (W.map_active(H) + 2 > W.L.world) { W.P.T.hierarchy_full_refusals += 1; W.refuse(refusal_kind::hierarchy_full, 0, 0); return; }
		char folded[256];
		const auto hp = W.folded_hash(parent, folded, sizeof folded);
		const auto hc = W.folded_hash(child, folded, sizeof folded);
		const auto ip = W.map_find(H, hp);
		if (ip != map_null) record_add_child(W, reinterpret_cast<std::uint32_t*>(W.map_value(H, ip)), hc);
		else
		{
			std::uint32_t record[hierarchy_record_words] = {};
			record_add_child(W, record, hc);
			const auto ni = W.map_insert(H, hp);
			if (ni != map_null) std::memcpy(W.map_value(H, ni), record, sizeof record);
		}
		const auto ic = W.map_find(H, hc);
		if (ic != map_null) { reinterpret_cast<std::uint32_t*>(W.map_value(H, ic))[0] = hp; return; }
		std::uint32_t record[hierarchy_record_words] = {hp, 0, 0, 0, 0};
		const auto ni = W.map_insert(H, hc);
		if (ni != map_null) std::memcpy(W.map_value(H, ni), record, sizeof record);
	}

	// 0x4c6990: for each of the 112 properties whose bit differs between the new record and the entity's stored one,
	// set or clear the entity's bit in that property's membership set (four-way unrolled natively; each property's set
	// is independent, so the order is immaterial); then store the 16-byte record. The entity is the token's index.
	void __fastcall property_update(void* self, void*, std::uint32_t token, const std::uint32_t* record)
	{
		if (!prefix_of(self)) return;
		world W(self);
		const auto e = W.index(token);
		const auto* old = W.record(e);
		for (std::uint32_t prop = 0; prop < property_count; ++prop)
		{
			const auto bit = 1u << (prop & 31);
			const bool now = (record[prop >> 5] & bit) != 0, was = (old[prop >> 5] & bit) != 0;
			if (now == was) continue;
			auto& word = W.members(prop)[e >> 5];
			if (now) word |= 1u << (e & 31); else word &= ~(1u << (e & 31));
		}
		std::memcpy(W.record(e), record, property_record_bytes);
	}
	// 0x4c6950 / 0x4c6970: pointers into the membership sets / property records (unchecked, as native).
	std::uint32_t* __fastcall property_members(void* self, void*, std::uint32_t property) { if (!prefix_of(self)) return nullptr; world W(self); return W.members(property); }
	std::uint32_t* __fastcall property_record(void* self, void*, std::uint32_t index) { if (!prefix_of(self)) return nullptr; world W(self); return W.record(index); }

	// 0x4c6890: the entity (slot 0xc); its slot 0x44 result handed to Grid slot 0x28 with the token.
	void __fastcall grid_update(void* self, void*, std::uint32_t token)
	{
		if (!prefix_of(self)) return;
		world W(self);
		auto* entity = W.slot<fn_by_token>(0xc)(self, nullptr, WorldToken{token});
		if (!entity) return;
		auto* grid = native_hooks.grid_getter();
		const void* bounds = vslot<fn_obj>(entity, 0x44)(entity, nullptr);
		vslot<fn_grid_update>(grid, 0x28)(grid, nullptr, token, bounds);
	}

	// 0x4c7240 -> 0x4c7270.
	void __fastcall relation_set(void* self, void*, int table, std::uint32_t token, std::uint32_t value)
	{
		if (!prefix_of(self)) return;
		world W(self);
		if (!relation_table_ok(W, table)) return;
		rel_set(W, token, value);
	}
	// 0x4c6c60: begin the enumeration; AL = 0 when there is no first member (the set is NOT cleared then, as native);
	// else the set is cleared and every member's index set; AL = 1.
	bool __fastcall relation_members(void* self, void*, int table, std::uint32_t token, std::uint32_t* set)
	{
		if (!prefix_of(self)) return false;
		world W(self);
		if (!relation_table_ok(W, table)) return false;
		std::uint32_t cur = 0;
		rel_begin(W, &cur, token);
		if (cur == 0) return false;
		std::memset(set, 0, W.L.set_bytes);
		do
		{
			const auto e = W.index(cur);
			set[e >> 5] |= 1u << (e & 31);
			rel_next(W, &cur);
		} while (cur != 0);
		return true;
	}
	// 0x4c6b60: *out = the token's related value (0 for the null or an invalid token).
	// Returns `out` (native mov eax,[esp+8] after its push, both paths).
	std::uint32_t* __fastcall relation_get(void* self, void*, std::uint32_t* out, int table, std::uint32_t token)
	{
		if (!prefix_of(self)) { *out = 0; return out; }
		world W(self);
		if (!relation_table_ok(W, table)) { *out = 0; return out; }
		*out = (token != 0 && valid_token(W, token)) ? rel_value(W, W.index(token)) : 0;
		return out;
	}
	// 0x4c6da0: begin; returns `out`.
	std::uint32_t* __fastcall relation_begin(void* self, void*, std::uint32_t* out, int table, std::uint32_t token)
	{
		if (!prefix_of(self)) { *out = 0; return out; }
		world W(self);
		if (!relation_table_ok(W, table)) { *out = 0; return out; }
		rel_begin(W, out, token);
		return out;
	}
	// 0x4c6dd0: next; returns `out` (the third argument is unused natively).
	std::uint32_t* __fastcall relation_next(void* self, void*, std::uint32_t* out, int table, std::uint32_t)
	{
		if (!prefix_of(self)) { *out = 0; return out; }
		world W(self);
		if (!relation_table_ok(W, table)) { *out = 0; return out; }
		rel_next(W, out);
		return out;
	}

	// 0x4c6fd0: refuse when full (native: live == capacity; here: no reusable free slot, and never while resetting);
	// allocator getter -> slot 0xc(size); null stays a refusal with the world unchanged; else 0x4c7020/0x4c7050: pop
	// the ring's front, set its live bit, store the pointer, bump live, hand back the slot's token through *out.
	void* __fastcall allocate(void* self, void*, std::uint32_t* out_token, std::uint32_t size)
	{
		if (!prefix_of(self)) return nullptr;
		world W(self);
		if (W.resetting())
		{
			W.P.T.reset_time_attempts += 1; W.P.S.flags |= flag_reset_failure;
			W.refuse(refusal_kind::allocation_during_reset, size, 0);
			return nullptr;
		}
		if (W.free_count() == 0) { W.P.T.full_refusals += 1; W.refuse(refusal_kind::allocation_full, size, 0); return nullptr; }
		auto* allocator = native_hooks.allocator_getter();
		auto* entity = vslot<fn_alloc>(allocator, 0xc)(allocator, nullptr, size);
		if (!entity) { W.P.T.allocator_null_refusals += 1; W.refuse(refusal_kind::allocator_null, size, 0); return nullptr; }
		const auto i = W.ring()[W.dequeue()];
		W.set_live(i);
		W.dequeue() = (W.dequeue() + 1 >= W.L.world) ? 0 : W.dequeue() + 1; // 0x483410
		W.free_count() -= 1;
		W.live_count() += 1;
		W.entity(i) = entity;
		*out_token = W.handles()[i];
		if (W.generation(W.handles()[i]) > W.P.T.max_generation) W.P.T.max_generation = W.generation(W.handles()[i]);
		W.sync_sidecar();
		return entity;
	}

	// 0x4c7460: release (token). Native order kept: entity (slot 0xc; a null ends it), entity slot 0 -> that object's
	// slot 4, name unregister (slot 0x30 with 0x41aa80's string of the entity's name id at +0x18), Grid slot 0x24,
	// entity slot 4(0), the property record and its 112 membership bits cleared, the relationship cleanup (0x4c7610),
	// allocator slot 0x10, the pointer cleared. Then the lifecycle: live bit cleared and live decremented once, the
	// generation advanced or the slot retired, the free-ring entry published only for a non-retired slot outside a
	// reset. A destroying bit makes a reentrant release of the same token a counted no-op.
	void __fastcall release(void* self, void*, std::uint32_t token)
	{
		if (!prefix_of(self)) return;
		world W(self);
		auto* entity = W.slot<fn_by_token>(0xc)(self, nullptr, WorldToken{token});
		if (!entity) return;
		const auto i = W.index(token);
		if (W.destroying(i)) { W.P.T.reentrant_release_refusals += 1; W.refuse(refusal_kind::reentrant_release, 0, token); return; }
		W.set_destroying(i, true);
		{
			auto* object = vslot<fn_obj>(entity, 0x0)(entity, nullptr);
			vslot<fn_void>(object, 0x4)(object, nullptr);
		}
		{
			const auto* name_id = reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(entity) + 0x18);
			const char* name = native_hooks.name_of_id(name_id, nullptr);
			W.slot<fn_unregister>(0x30)(self, nullptr, name, token);
		}
		{
			auto* grid = native_hooks.grid_getter();
			vslot<fn_int>(grid, 0x24)(grid, nullptr, token);
		}
		vslot<fn_int>(entity, 0x4)(entity, nullptr, 0);
		std::memset(W.record(i), 0, property_record_bytes);
		for (std::uint32_t prop = 0; prop < property_count; ++prop) W.members(prop)[i >> 5] &= ~(1u << (i & 31));
		rel_release(W, token);
		{
			auto* allocator = native_hooks.allocator_getter();
			vslot<fn_int>(allocator, 0x10)(allocator, nullptr, reinterpret_cast<std::uint32_t>(entity));
		}
		W.entity(i) = nullptr;
		// lifecycle
		W.clear_live(i);
		W.live_count() -= 1;
		W.advance_or_retire(i, W.P.T.retired_by_release);
		if (!W.retired(i) && !W.resetting()) W.core_ring_push(i);
		W.set_destroying(i, false);
		W.sync_sidecar();
	}
}
