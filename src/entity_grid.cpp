// CEntityGrid replacement owner. Every function names the native routine it reproduces; the comments describe the
// native behaviour as decompiled (XMen2.exe sha1 7d95cdb4..., 0x463c00-0x4654aa), including its defects.
//
// Floating point: the native code is x87, so every intermediate rounds to the live x87 precision control: 24 bits
// under a Direct3D 8 device created without D3DCREATE_FPU_PRESERVE (the D3D default), 53 bits otherwise (the
// retail CRT startup at 0x6729f6 sets _PC_53). The slots read the control word on entry (_controlfp) and evaluate
// the native expression chains in `float` or `double` accordingly, which reproduces the per-operation x87 rounding
// of either mode exactly (the operands are floats, each +,-,*,/ rounds once to the chosen width, the final store
// rounds to float). Int*float products that the native code forms with FILD/FMUL are exact in double and are
// rounded once. _ftol (0x67217c) is reproduced bit for bit, including its out-of-range behaviour (see
// native_ftol). The differential test runs the retail code and this port under both precision controls.

#include "entity_grid.hpp"
#include <Windows.h>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace entity_grid
{
	namespace
	{
		using namespace entity_capacity_layout;

		// ---- object layout ----------------------------------------------------------------------------------
		constexpr std::uint32_t ring_words = block_count;                  // u32[128]
		constexpr std::uint32_t bitmap_bytes = block_count / 8;            // u32[4]
		constexpr std::uint32_t prefix_bytes = 0x1000;                     // our private header precedes the object
		constexpr std::uint32_t prefix_magic = 0x44495247;                 // 'GRID'
		struct prefix { std::uint32_t magic; layout L; };
		static_assert(sizeof(prefix) <= prefix_bytes);

		const prefix* prefix_of(const void* object)
		{
			if (!object) return nullptr;
			const auto* p = reinterpret_cast<const prefix*>(static_cast<const std::uint8_t*>(object) - prefix_bytes);
			return p->magic == prefix_magic ? p : nullptr;
		}

		// ---- native _ftol (0x67217c) -----------------------------------------------------------------------
		// FLD st0; FST float [s]; FISTP qword [q] (current rounding mode, nearest); FILD qword; EDX = float bits;
		// EAX = low dword of q. EAX == 0 && (EDX & 0x7fffffff) == 0 -> 0. Else diff = value - q as float; for a
		// negative value EAX += (diff > 0); for a positive one EAX -= (diff < 0). In range this is truncation.
		std::int32_t native_ftol(double v)
		{
			std::int64_t q;
			if (!(std::fabs(v) < 9223372036854775808.0)) q = INT64_MIN; // FISTP indefinite (also NaN)
			else q = static_cast<std::int64_t>(std::nearbyint(v));
			const float f = static_cast<float>(v);
			std::uint32_t fb; std::memcpy(&fb, &f, 4);
			auto eax = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(q)));
			if (eax == 0 && (fb & 0x7fffffffu) == 0) return 0;
			const float diff = static_cast<float>(v - static_cast<double>(q));
			std::uint32_t db; std::memcpy(&db, &diff, 4);
			if (fb & 0x80000000u) { if ((db ^ 0x80000000u) > 0x80000000u) eax += 1; }
			else if (db > 0x80000000u) eax -= 1;
			return eax;
		}
		// FILD int; FMUL float: exact product rounded once to float (equal under x87 single or double precision).
		float int_times(std::int32_t i, float f) { return static_cast<float>(static_cast<double>(i) * static_cast<double>(f)); }
		std::int32_t clamp_low0(std::int32_t v) { return v & ((v < 0) - 1); } // native max(0, v): v & ((v<0)-1)
		// The live x87 precision control: single (D3D default) evaluates the native chains in float, otherwise double.
		bool single_precision() { return (_controlfp(0, 0) & _MCW_PC) == _PC_24; }
		// FILD i; FMUL f under precision T: the exact product rounded to T.
		template <typename T> T int_times_t(std::int32_t i, float f) { return static_cast<T>(static_cast<double>(i) * static_cast<double>(f)); }

		// ---- accessors ----------------------------------------------------------------------------------------
		struct grid
		{
			std::uint8_t* p; const layout& L;
			explicit grid(void* self) : p(static_cast<std::uint8_t*>(self)), L(prefix_of(self)->L) {}
			template <typename T> T& at(std::uint32_t off) { return *reinterpret_cast<T*>(p + off); }
			vec3& min() { return at<vec3>(off_min); }
			vec3& max() { return at<vec3>(off_max); }
			vec3& cell() { return at<vec3>(off_cell); }
			vec3& half() { return at<vec3>(off_half); }
			vec3& inverse() { return at<vec3>(off_inverse); }
			std::int32_t& width() { return at<std::int32_t>(off_width); }
			std::int32_t& height() { return at<std::int32_t>(off_height); }
			std::uint8_t* coarse() { return p + off_coarse; }
			std::uint32_t* block_bitmap() { return &at<std::uint32_t>(L.block_bitmap); }
			std::uint32_t* ring() { return &at<std::uint32_t>(L.ring); }
			std::uint32_t& ring_enqueue() { return at<std::uint32_t>(L.ring_enqueue); }
			std::uint32_t& ring_dequeue() { return at<std::uint32_t>(L.ring_dequeue); }
			std::uint32_t& ring_count() { return at<std::uint32_t>(L.ring_count); }
			std::uint32_t* alloc_bitmap() { return &at<std::uint32_t>(L.alloc_bitmap); }
			std::uint32_t& alloc_count() { return at<std::uint32_t>(L.alloc_count); }
			std::uint8_t* extent(std::uint32_t index) { return p + L.extents + index * 4; }
			std::uint8_t* cache(std::uint32_t i) { return p + L.caches + i * L.cache_stride; }
			std::uint32_t* cache_set_words(std::uint32_t i) { return reinterpret_cast<std::uint32_t*>(cache(i) + cache_off_set); }
			std::uint32_t index_of(std::uint32_t token) const { return token & L.index_mask; }

			// Cells. The count and the indices are bytes at 256 and u16 at 1024; index k lives at cell_indices + k *
			// cell_field with no bounds check, exactly as the native byte arithmetic (so at 256 "index 31" is the next
			// cell's count byte: the native overflow defect, see cell_insert).
			std::uint32_t cell_count(const std::uint8_t* c) const { return L.cell_field == 1 ? c[0] : *reinterpret_cast<const std::uint16_t*>(c); }
			void set_cell_count(std::uint8_t* c, std::uint32_t v) const { if (L.cell_field == 1) c[0] = static_cast<std::uint8_t>(v); else *reinterpret_cast<std::uint16_t*>(c) = static_cast<std::uint16_t>(v); }
			std::uint32_t cell_index(const std::uint8_t* c, std::uint32_t k) const { const auto* e = c + L.cell_indices + k * L.cell_field; return L.cell_field == 1 ? *e : *reinterpret_cast<const std::uint16_t*>(e); }
			void set_cell_index(std::uint8_t* c, std::uint32_t k, std::uint32_t v) const { auto* e = c + L.cell_indices + k * L.cell_field; if (L.cell_field == 1) *e = static_cast<std::uint8_t>(v); else *reinterpret_cast<std::uint16_t*>(e) = static_cast<std::uint16_t>(v); }
			std::uint8_t* cell_at(std::uint32_t block, std::uint32_t a, std::uint32_t b) { return p + L.pool + block * L.block_stride + ((a & 3) * 4 + (b & 3)) * L.cell_stride; }
			std::uint8_t& coarse_entry(std::uint32_t a, std::uint32_t b) { return coarse()[(a >> 2) * coarse_dim + (b >> 2)]; }

			// 0x463c00: walks the set bits of the block bitmap (no per-block work survives in the retail build) and
			// clears it.
			void clear_block_bitmap() { std::memset(block_bitmap(), 0, bitmap_bytes); }
			// 0x463ce0 push loop body and 0x464f20's push: FIFO append with the native wrap (enqueue 0x7f -> 0).
			void ring_push(std::uint32_t value)
			{
				auto slot = ring_enqueue();
				ring_enqueue() = slot + 1;
				ring_count() += 1;
				if (slot + 1 > 0x7f) { ring_enqueue() = 0; slot = 0x7f; }
				ring()[slot] = value;
			}
			// 0x463ce0: enqueue = dequeue = count = 0, then push 0..127.
			void ring_init()
			{
				ring_enqueue() = ring_dequeue() = ring_count() = 0;
				for (std::uint32_t i = 0; i < block_count; ++i) ring_push(i);
			}
			// 0x464e10 (+ 0x403f20): pop the head block id, mark it allocated.
			std::uint32_t ring_pop()
			{
				const auto id = ring()[ring_dequeue()];
				alloc_bitmap()[id >> 5] |= 1u << (id & 31);
				const auto next = ring_dequeue() + 1;
				ring_dequeue() = next >= block_count ? 0 : next;
				ring_count() -= 1;
				alloc_count() += 1;
				return id;
			}
			// 0x464d70 / 0x464db0 / 0x401600 / 0x464e00: construct the 16 cells (count = 0 only; the indices keep
			// whatever they held), set the block bitmap bit.
			void block_construct(std::uint32_t id)
			{
				for (std::uint32_t k = 0; k < cells_per_block; ++k) set_cell_count(p + L.pool + id * L.block_stride + k * L.cell_stride, 0);
				block_bitmap()[id >> 5] |= 1u << (id & 31);
			}
			// 0x464f20 / 0x464fa0: release a block to the ring.
			void block_free(std::uint32_t id)
			{
				block_bitmap()[id >> 5] &= ~(1u << (id & 31));
				alloc_bitmap()[id >> 5] &= ~(1u << (id & 31));
				ring_push(id);
				alloc_count() -= 1;
			}
			// 0x464680: the cell of (a, b) or nullptr when its block is not mapped.
			std::uint8_t* cell_lookup(std::uint32_t a, std::uint32_t b)
			{
				const auto id = coarse_entry(a, b);
				return id == no_block ? nullptr : cell_at(id, a, b);
			}
			// 0x464ce0: the cell of (a, b), mapping a block from the ring when needed; nullptr when all 128 are out.
			std::uint8_t* cell_lookup_alloc(std::uint32_t a, std::uint32_t b)
			{
				auto& entry = coarse_entry(a, b);
				std::uint32_t id = entry;
				if (id == no_block)
				{
					if (alloc_count() == block_count) return nullptr;
					id = ring_pop();
					block_construct(id);
					entry = static_cast<std::uint8_t>(id);
				}
				return cell_at(id, a, b);
			}
			// 0x464e60: unmap every block whose 16 cells are all empty (coarse map order, row by row).
			void collect_empty_blocks()
			{
				for (std::uint32_t r = 0; r < coarse_dim; ++r)
					for (std::uint32_t c = 0; c < coarse_dim; ++c)
					{
						auto& entry = coarse()[r * coarse_dim + c];
						if (entry >= block_count) continue;
						bool empty = true;
						for (std::uint32_t k = 0; k < cells_per_block && empty; ++k) empty = cell_count(p + L.pool + entry * L.block_stride + k * L.cell_stride) == 0;
						if (!empty) continue;
						block_free(entry);
						entry = no_block;
					}
			}
			// 0x464ca0: lookup for removal; a failed mapping collects empty blocks and retries once.
			std::uint8_t* cell_lookup_collect(std::uint32_t a, std::uint32_t b)
			{
				auto* c = cell_lookup_alloc(a, b);
				if (!c) { collect_empty_blocks(); c = cell_lookup_alloc(a, b); }
				return c;
			}
			// 0x464c40: remove the token's index from cell (a, b): first match, last entry swapped into its place.
			void cell_remove(std::uint32_t token, std::uint32_t a, std::uint32_t b)
			{
				auto* c = cell_lookup_collect(a, b);
				if (!c) return;
				auto n = cell_count(c);
				if (n == 0) return;
				const auto index = index_of(token);
				std::uint32_t k = 0;
				while (cell_index(c, k) != index) { ++k; if (cell_count(c) <= k) return; }
				n = cell_count(c) - 1;
				set_cell_count(c, n);
				if (k < n) set_cell_index(c, k, cell_index(c, n));
			}
			// 0x465400: append the token's index to cell (a, b). Native appends while count < 0x20 at the retail cell
			// size of 0x20 bytes (count byte + 31 indices): the 32nd entry is written to byte 32, the next cell's count
			// byte, and count becomes 32. Reproduced at 256 (cell_append_limit 0x20); the wide cell holds 1024
			// indices so the raised limit of 1024 never leaves the cell.
			void cell_insert(std::uint32_t token, std::uint32_t a, std::uint32_t b)
			{
				auto* c = cell_lookup_alloc(a, b);
				if (!c) { collect_empty_blocks(); c = cell_lookup_alloc(a, b); if (!c) return; }
				const auto n = cell_count(c);
				if (n < L.cell_append_limit) { set_cell_index(c, n, index_of(token)); set_cell_count(c, n + 1); }
			}
			// 0x4645b0: OR the indices of cells a0..a1 x b0..b1 into `set`; with a flag the set is cleared at the first
			// entry found and the flag set. Rows (b) outer, columns (a) inner, signed bounds as native.
			void gather(std::int32_t a0, std::int32_t a1, std::int32_t b0, std::int32_t b1, std::uint32_t* set, std::uint8_t* flag)
			{
				for (auto b = b0; b <= b1; ++b)
					for (auto a = a0; a <= a1; ++a)
					{
						const auto* c = cell_lookup(static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b));
						if (!c || cell_count(c) == 0) continue;
						for (std::uint32_t k = 0; k < cell_count(c); ++k)
						{
							if (flag && *flag == 0) { std::memset(set, 0, L.set_words * 4); *flag = 1; }
							const auto index = cell_index(c, k);
							set[index >> 5] |= 1u << (index & 31);
						}
					}
			}
			// 0x464a10: clear the indices of cells a0..a1 x b0..b1 from `set`.
			void scatter(std::int32_t a0, std::int32_t a1, std::int32_t b0, std::int32_t b1, std::uint32_t* set)
			{
				for (auto b = b0; b <= b1; ++b)
					for (auto a = a0; a <= a1; ++a)
					{
						const auto* c = cell_lookup(static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b));
						if (!c || cell_count(c) == 0) continue;
						for (std::uint32_t k = 0; k < cell_count(c); ++k)
						{
							const auto index = cell_index(c, k);
							set[index >> 5] &= ~(1u << (index & 31));
						}
					}
			}
			// 0x464480: the cell rectangle of a world-space box, clamped to the grid; false when the box misses it.
			template <typename T> bool cell_range_t(const vec3* lo, const vec3* hi, std::int32_t& x0, std::int32_t& x1, std::int32_t& y0, std::int32_t& y1)
			{
				if (lo->x > max().x) return false; // FCOMP tests: unordered passes, as the native flag tests do
				if (lo->y > max().y) return false;
				if (hi->x < min().x) return false;
				if (hi->y < min().y) return false;
				x0 = clamp_low0(native_ftol(static_cast<double>(((T(lo->x) + T(0.5f)) - T(min().x)) * T(inverse().x))));
				x1 = native_ftol(static_cast<double>(((T(hi->x) - T(0.5f)) - T(min().x)) * T(inverse().x)));
				if (width() - 1 < x1) x1 = width() - 1;
				y0 = clamp_low0(native_ftol(static_cast<double>(((T(lo->y) + T(0.5f)) - T(min().y)) * T(inverse().y))));
				y1 = native_ftol(static_cast<double>(((T(hi->y) - T(0.5f)) - T(min().y)) * T(inverse().y)));
				if (height() - 1 < y1) y1 = height() - 1;
				x1 = clamp_low0(x1);
				if (x0 >= x1) x0 = x1;
				y1 = clamp_low0(y1);
				if (y0 >= y1) y0 = y1;
				return true;
			}
			bool cell_range(const vec3* lo, const vec3* hi, std::int32_t& x0, std::int32_t& x1, std::int32_t& y0, std::int32_t& y1)
			{
				return single_precision() ? cell_range_t<float>(lo, hi, x0, x1, y0, y1) : cell_range_t<double>(lo, hi, x0, x1, y0, y1);
			}
			// 0x4653c0: entity rectangle e against cache rectangle c (bytes), false for an invalid cache.
			static bool overlap(const std::uint8_t* e, const std::uint8_t* c)
			{
				return c[0] <= c[1] && e[0] <= c[1] && c[0] <= e[1] && e[2] <= c[3] && c[2] <= e[3];
			}
			void store_extent(std::uint8_t* e, std::int32_t x0, std::int32_t x1, std::int32_t y0, std::int32_t y1)
			{
				e[0] = static_cast<std::uint8_t>(x0); e[1] = static_cast<std::uint8_t>(x1); e[2] = static_cast<std::uint8_t>(y0); e[3] = static_cast<std::uint8_t>(y1);
			}
			static void absent_extent(std::uint8_t* e) { e[0] = 0xff; e[1] = 0; e[2] = 0xff; e[3] = 0; }
			// 0x463c90 (constructor) and 0x464330 (slot 0x38): invalid rectangle, empty set; the bounds are untouched.
			void cache_clear(std::uint32_t i)
			{
				auto* c = cache(i);
				c[1] = 0; c[3] = 0; c[0] = 0xff; c[2] = 0xff;
				std::memset(c + cache_off_set, 0, L.set_words * 4);
			}
			// virtual dispatch through the object's own vptr, as the native code does for slots 0x00/0x24/0x38
			template <typename Fn> Fn slot(std::uint32_t index) { return reinterpret_cast<Fn>(at<const void* const*>(0)[index]); }
		};

		bounds* __fastcall native_bounds_pair_ctor(bounds* self, void* edx, int zero)
		{
			return native<bounds_pair_ctor_fn>(0x419af0)(self, edx, zero);
		}
	}

	bounds_pair_ctor_fn bounds_pair_ctor = &native_bounds_pair_ctor;

	layout layout_for(std::uint32_t world)
	{
		layout L;
		if (world != stock_world && world != raised_world) return L;
		L.world = world;
		L.index_mask = world - 1;
		L.cell_field = world == stock_world ? 1 : 2;
		L.cell_indices = world == stock_world ? 1 : 4;
		L.cell_stride = world == stock_world ? 0x20 : 4 + world * 2;
		L.cell_append_limit = world == stock_world ? 0x20 : world;
		L.block_stride = cells_per_block * L.cell_stride;
		L.pool = off_coarse + coarse_bytes + 3; // 0x204: the coarse map padded to a dword
		L.cells_bytes = block_count * L.block_stride;
		L.block_bitmap = L.pool + L.cells_bytes;
		L.ring = L.block_bitmap + bitmap_bytes;
		L.ring_enqueue = L.ring + ring_words * 4 + 4; // the dword after the ring is unused in the retail object
		L.ring_dequeue = L.ring_enqueue + 4;
		L.ring_count = L.ring_dequeue + 4;
		L.alloc_bitmap = L.ring_count + 4;
		L.alloc_count = L.alloc_bitmap + bitmap_bytes;
		L.extents = L.alloc_count + 4;
		L.set_words = world / 32;
		L.caches = L.extents + world * 4 + 4; // one unused dword between the extents and the caches
		L.cache_stride = cache_off_set + L.set_words * 4;
		L.size = L.caches + cache_count * L.cache_stride;
		return L;
	}

	std::size_t object_size(const descriptor& d) { return layout_for(d.world).size; }

	const layout* layout_of(const void* object) { const auto* p = prefix_of(object); return p ? &p->L : nullptr; }

	// ---- the vtable -----------------------------------------------------------------------------------------------
	const vtable_image vtable{
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(native_rtti_locator)),
		{reinterpret_cast<const void*>(&reset), reinterpret_cast<const void*>(&init), reinterpret_cast<const void*>(&cell_size),
		 reinterpret_cast<const void*>(&snap), reinterpret_cast<const void*>(&cell_center), reinterpret_cast<const void*>(&width),
		 reinterpret_cast<const void*>(&height), reinterpret_cast<const void*>(&get_min), reinterpret_cast<const void*>(&get_max),
		 reinterpret_cast<const void*>(&remove), reinterpret_cast<const void*>(&update), reinterpret_cast<const void*>(&present),
		 reinterpret_cast<const void*>(&query), reinterpret_cast<const void*>(&cache_set_ptr), reinterpret_cast<const void*>(&cache_reset),
		 reinterpret_cast<const void*>(&cache_bounds), reinterpret_cast<const void*>(&cache_update)}};

	// ---- construction / destruction -----------------------------------------------------------------------------
	void* create(const descriptor& d)
	{
		const auto L = layout_for(d.world);
		if (!L.valid()) return nullptr;
		auto* base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, prefix_bytes + L.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!base) return nullptr;
		auto* head = reinterpret_cast<prefix*>(base);
		head->magic = prefix_magic; head->L = L;
		auto* self = base + prefix_bytes;
		grid g(self);
		// 0x463de0
		g.at<const void*>(0) = vtable.slots;
		// 0x463d50 on the storage sub-object (+0x48)
		std::memset(g.block_bitmap(), 0, bitmap_bytes);
		g.ring_enqueue() = g.ring_dequeue() = g.ring_count() = 0;
		std::memset(g.alloc_bitmap(), 0, bitmap_bytes);
		g.alloc_count() = 0;
		g.ring_init();
		g.alloc_count() = 0;
		std::memset(g.alloc_bitmap(), 0, bitmap_bytes);
		g.clear_block_bitmap();
		g.ring_init();
		std::memset(g.coarse(), 0xff, coarse_bytes);
		// 0x463de0: every entity absent
		for (std::uint32_t i = 0; i < L.world; ++i) grid::absent_extent(g.extent(i));
		// 0x463c90: both caches invalid and empty
		for (std::uint32_t i = 0; i < cache_count; ++i) g.cache_clear(i);
		return self;
	}

	void destroy(void* object)
	{
		if (!prefix_of(object)) return;
		grid g(object);
		// 0x465450
		g.at<const void*>(0) = vtable.slots;
		g.clear_block_bitmap();
		VirtualFree(static_cast<std::uint8_t*>(object) - prefix_bytes, 0, MEM_RELEASE);
	}

	// ---- slots --------------------------------------------------------------------------------------------------
	// 0x464aa0: forget every block and entity; the cells themselves keep their stale contents (native).
	void __fastcall reset(void* self, void*)
	{
		grid g(self);
		g.alloc_count() = 0;
		std::memset(g.alloc_bitmap(), 0, bitmap_bytes);
		g.clear_block_bitmap();
		g.ring_init();
		std::memset(g.coarse(), 0xff, coarse_bytes);
		for (std::uint32_t i = 0; i < g.L.world; ++i) grid::absent_extent(g.extent(i));
		for (int i = 0; i < static_cast<int>(cache_count); ++i) g.slot<void(__fastcall*)(void*, void*, int)>(0x38 / 4)(self, nullptr, i);
	}

	// 0x463f30: spatial initialization. Cell-aligned min/max; a second rounding when the grid exceeds 84 cells;
	// then raw bounds and ceil'd cell counts when it still does. Ends with the virtual reset (slot 0). T is the
	// x87 precision the chains round to.
	template <typename T> void init_t(void* self, const vec3* pmin, const vec3* pmax, const vec3* pcell)
	{
		grid g(self);
		auto& cell = g.cell(); auto& half = g.half(); auto& inv = g.inverse(); auto& mn = g.min(); auto& mx = g.max();
		const auto st = [](T v) { return static_cast<float>(v); };
		cell = *pcell;
		half = {st(T(cell.x) * T(0.5f)), st(T(cell.y) * T(0.5f)), st(T(cell.z) * T(0.5f))};
		inv = {st(T(1.0f) / T(cell.x)), st(T(1.0f) / T(cell.y)), st(T(1.0f) / T(cell.z))};
		mn = *pmin; mx = *pmax;
		mn.x = int_times(native_ftol(std::floor(static_cast<double>(T(inv.x) * T(mn.x)))), cell.x);
		mn.y = int_times(native_ftol(std::floor(static_cast<double>(T(mn.y) * T(inv.y)))), cell.y);
		mn.z = int_times(native_ftol(std::floor(static_cast<double>(T(mn.z) * T(inv.z)))), cell.z);
		mx.x = int_times(native_ftol(std::ceil(static_cast<double>((T(mx.x) + T(half.x)) * T(inv.x)))), cell.x);
		mx.y = int_times(native_ftol(std::ceil(static_cast<double>((T(mx.y) + T(half.y)) * T(inv.y)))), cell.y);
		mx.z = int_times(native_ftol(std::ceil(static_cast<double>((T(half.z) + T(mx.z)) * T(inv.z)))), cell.z);
		vec3 ext{st(T(mx.x) - T(mn.x)), st(T(mx.y) - T(mn.y)), st(T(mx.z) - T(mn.z))};
		g.width() = native_ftol(static_cast<double>(T(ext.x) / T(cell.x)));
		g.height() = native_ftol(static_cast<double>(T(ext.y) / T(cell.y)));
		if (g.width() > static_cast<std::int32_t>(max_cells) || g.height() > static_cast<std::int32_t>(max_cells))
		{
			mn = *pmin; mx = *pmax;
			mn.x = int_times(native_ftol(static_cast<double>(T(std::ceil(static_cast<double>(mn.x))) * T(inv.x))), cell.x);
			mn.y = int_times(native_ftol(static_cast<double>(T(std::ceil(static_cast<double>(mn.y))) * T(inv.y))), cell.y);
			mn.z = int_times(native_ftol(static_cast<double>(T(std::ceil(static_cast<double>(mn.z))) * T(inv.z))), cell.z);
			mx.x = int_times(native_ftol(static_cast<double>((T(std::floor(static_cast<double>(mx.x))) + T(half.x)) * T(inv.x))), cell.x);
			mx.y = int_times(native_ftol(static_cast<double>((T(std::floor(static_cast<double>(mx.y))) + T(half.y)) * T(inv.y))), cell.y);
			mx.z = int_times(native_ftol(static_cast<double>((T(std::floor(static_cast<double>(mx.z))) + T(half.z)) * T(inv.z))), cell.z);
			ext = {st(T(mx.x) - T(mn.x)), st(T(mx.y) - T(mn.y)), st(T(mx.z) - T(mn.z))};
			g.width() = native_ftol(static_cast<double>(T(ext.x) / T(cell.x)));
			g.height() = native_ftol(static_cast<double>(T(ext.y) / T(cell.y)));
		}
		if (g.width() > static_cast<std::int32_t>(max_cells) || g.height() > static_cast<std::int32_t>(max_cells))
		{
			mn = *pmin; mx = *pmax;
			g.width() = native_ftol(std::ceil(static_cast<double>((T(pmax->x) - T(pmin->x)) / T(cell.x))));
			g.height() = native_ftol(std::ceil(static_cast<double>((T(pmax->y) - T(pmin->y)) / T(cell.y))));
		}
		g.slot<void(__fastcall*)(void*, void*)>(0)(self, nullptr);
	}
	void __fastcall init(void* self, void*, const vec3* pmin, const vec3* pmax, const vec3* pcell)
	{
		if (single_precision()) init_t<float>(self, pmin, pmax, pcell); else init_t<double>(self, pmin, pmax, pcell);
	}

	// 0x614280 (shared accessor): the cell size vector.
	const vec3* __fastcall cell_size(void* self, void*) { return &grid(self).cell(); }

	// 0x463e30: snap a position to the nearest cell origin: out = min + cell * ftol((in - min) / cell + 0.5).
	template <typename T> void snap_t(void* self, vec3* out, const vec3* in)
	{
		grid g(self);
		const float* mn = &g.min().x; const float* cell = &g.cell().x; const float* src = &in->x;
		float r[3];
		for (int i = 0; i < 3; ++i)
		{
			const auto k = native_ftol(static_cast<double>((T(src[i]) - T(mn[i])) / T(cell[i]) + T(0.5f)));
			r[i] = static_cast<float>(int_times_t<T>(k, cell[i]) + T(mn[i]));
		}
		out->x = r[0]; out->y = r[1]; out->z = r[2];
	}
	vec3* __fastcall snap(void* self, void*, vec3* out, const vec3* in)
	{
		if (single_precision()) snap_t<float>(self, out, in); else snap_t<double>(self, out, in);
		return out;
	}

	// 0x463ea0: the centre of cell (x, y) at the grid's min z.
	template <typename T> void cell_center_t(void* self, vec3* out, int x, int y)
	{
		grid g(self);
		const float ox = static_cast<float>(int_times_t<T>(x, g.cell().x) + T(g.half().x) + T(g.min().x));
		const float oy = static_cast<float>(int_times_t<T>(y, g.cell().y) + T(g.half().y) + T(g.min().y));
		out->x = ox; out->y = oy; out->z = g.min().z;
	}
	vec3* __fastcall cell_center(void* self, void*, vec3* out, int x, int y)
	{
		if (single_precision()) cell_center_t<float>(self, out, x, y); else cell_center_t<double>(self, out, x, y);
		return out;
	}

	int __fastcall width(void* self, void*) { return grid(self).width(); }  // 0x463ed0
	int __fastcall height(void* self, void*) { return grid(self).height(); } // 0x463ee0
	vec3* __fastcall get_min(void* self, void*, vec3* out) { *out = grid(self).min(); return out; } // 0x463ef0
	vec3* __fastcall get_max(void* self, void*, vec3* out) { *out = grid(self).max(); return out; } // 0x463f10

	// 0x464b20: take the entity out of every cell of its rectangle (clamped to the grid), mark it absent, clear
	// its bit in both caches. Nothing happens for an absent entity.
	void __fastcall remove(void* self, void*, std::uint32_t token)
	{
		grid g(self);
		const auto index = g.index_of(token);
		auto* e = g.extent(index);
		if (e[0] > e[1]) return;
		const std::int32_t a0 = clamp_low0(e[0]);
		std::int32_t a1 = e[1]; if (g.width() - 1 <= a1) a1 = g.width() - 1;
		const std::int32_t b0 = clamp_low0(e[2]);
		std::int32_t b1 = e[3]; if (g.height() - 1 <= b1) b1 = g.height() - 1;
		for (auto b = b0; b <= b1; ++b)
			for (auto a = a0; a <= a1; ++a) g.cell_remove(token, static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b));
		grid::absent_extent(e);
		g.cache_set_words(0)[index >> 5] &= ~(1u << (index & 31));
		g.cache_set_words(1)[index >> 5] &= ~(1u << (index & 31));
	}

	// 0x464fd0: place the entity at a world-space box. Null box or a box off the grid -> virtual remove (slot
	// 0x24). An absent entity is appended to every cell of the new rectangle; an unchanged rectangle returns
	// without touching the caches; a disjoint move removes then inserts; an overlapping move walks the union of
	// rows: new-only rows insert, old-only rows remove, shared rows adjust the left and right edges. Then the
	// rectangle is stored and the entity's bit in each cache follows the rectangle overlap.
	void __fastcall update(void* self, void*, std::uint32_t token, const bounds* b)
	{
		grid g(self);
		const auto do_remove = g.slot<void(__fastcall*)(void*, void*, std::uint32_t)>(0x24 / 4);
		if (!b) { do_remove(self, nullptr, token); return; }
		std::int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		if (!g.cell_range(&b->min, &b->max, x0, x1, y0, y1)) { do_remove(self, nullptr, token); return; }
		const auto index = g.index_of(token);
		auto* e = g.extent(index);
		if (e[0] > e[1])
		{
			for (auto y = y0; y <= y1; ++y)
				for (auto x = x0; x <= x1; ++x) g.cell_insert(token, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
		}
		else
		{
			const std::int32_t ox0 = e[0], ox1 = e[1], oy0 = e[2], oy1 = e[3];
			if (x0 == ox0 && x1 == ox1 && y0 == oy0 && y1 == oy1) return;
			if (ox0 > x1 || ox1 < x0 || oy0 > y1 || oy1 < y0)
			{
				for (auto y = oy0; y <= oy1; ++y)
					for (auto x = ox0; x <= ox1; ++x) g.cell_remove(token, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
				for (auto y = y0; y <= y1; ++y)
					for (auto x = x0; x <= x1; ++x) g.cell_insert(token, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
			}
			else
			{
				const auto ys = y0 < oy0 ? y0 : oy0, ye = y1 > oy1 ? y1 : oy1;
				for (auto y = ys; y <= ye; ++y)
				{
					const auto uy = static_cast<std::uint32_t>(y);
					if (y < oy0 || y > oy1)
					{
						for (auto x = x0; x <= x1; ++x) g.cell_insert(token, static_cast<std::uint32_t>(x), uy);
					}
					else if (y < y0 || y > y1)
					{
						for (auto x = ox0; x <= ox1; ++x) g.cell_remove(token, static_cast<std::uint32_t>(x), uy);
					}
					else
					{
						if (x0 != ox0)
						{
							if (ox0 < x0) { for (auto x = ox0; x <= x0 - 1; ++x) g.cell_remove(token, static_cast<std::uint32_t>(x), uy); }
							else { for (auto x = x0; x <= ox0 - 1; ++x) g.cell_insert(token, static_cast<std::uint32_t>(x), uy); }
						}
						if (x1 != ox1)
						{
							if (ox1 > x1) { for (auto x = x1 + 1; x <= ox1; ++x) g.cell_remove(token, static_cast<std::uint32_t>(x), uy); }
							else { for (auto x = ox1 + 1; x <= x1; ++x) g.cell_insert(token, static_cast<std::uint32_t>(x), uy); }
						}
					}
				}
			}
		}
		g.store_extent(e, x0, x1, y0, y1);
		for (std::uint32_t c = 0; c < cache_count; ++c)
		{
			auto* word = &g.cache_set_words(c)[index >> 5];
			if (grid::overlap(e, g.cache(c) + cache_off_extents)) *word |= 1u << (index & 31);
			else *word &= ~(1u << (index & 31));
		}
	}

	// 0x4642e0: 1 when the entity has a rectangle.
	int __fastcall present(void* self, void*, std::uint32_t token)
	{
		grid g(self);
		const auto* e = g.extent(g.index_of(token));
		return e[0] <= e[1] ? 1 : 0;
	}

	// 0x464400: entities of every cell touched by the box, into the caller's set. The set is cleared only when the
	// first entity is found; the return value says whether any was (0 leaves the set untouched).
	unsigned char __fastcall query(void* self, void*, const vec3* min, const vec3* max, std::uint32_t* set)
	{
		grid g(self);
		std::int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		std::uint8_t found = 0;
		if (g.cell_range(min, max, x0, x1, y0, y1)) g.gather(x0, x1, y0, y1, set, &found);
		return found;
	}

	std::uint32_t* __fastcall cache_set_ptr(void* self, void*, int cache) { return grid(self).cache_set_words(static_cast<std::uint32_t>(cache)); } // 0x464310
	void __fastcall cache_reset(void* self, void*, int cache) { grid(self).cache_clear(static_cast<std::uint32_t>(cache)); }               // 0x464330

	// 0x464370: the cache's query bounds, 1; or a zero pair from the native helper, 0, when the cache is invalid.
	unsigned char __fastcall cache_bounds(void* self, void*, int cache, bounds* out)
	{
		grid g(self);
		const auto* c = g.cache(static_cast<std::uint32_t>(cache));
		if (c[0] > c[1])
		{
			bounds pair{};
			bounds_pair_ctor(&pair, nullptr, 1);
			std::memcpy(out, &pair, sizeof pair);
			return 0;
		}
		std::memcpy(out, c + cache_off_bounds, sizeof(bounds));
		return 1;
	}

	// 0x4646d0: move the cache to a new box: bounds stored first; a box off the grid resets the cache (slot 0x38);
	// an invalid or disjoint cache is rebuilt; an unchanged rectangle returns; otherwise the four edges are
	// adjusted in the native order (rows below, rows above, columns left, columns right), re-adding the boundary
	// row/column after each shrink as the native code does.
	void __fastcall cache_update(void* self, void*, int cache, const bounds* b)
	{
		grid g(self);
		auto* c = g.cache(static_cast<std::uint32_t>(cache));
		std::memcpy(c + cache_off_bounds, b, sizeof(bounds));
		std::int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		if (!g.cell_range(&b->min, &b->max, x0, x1, y0, y1)) { g.slot<void(__fastcall*)(void*, void*, int)>(0x38 / 4)(self, nullptr, cache); return; }
		auto* set = reinterpret_cast<std::uint32_t*>(c + cache_off_set);
		if (c[0] > c[1])
		{
			std::memset(set, 0, g.L.set_words * 4);
			g.gather(x0, x1, y0, y1, set, nullptr);
		}
		else
		{
			const std::int32_t ox0 = c[0], ox1 = c[1], oy0 = c[2], oy1 = c[3];
			if (x0 == ox0 && x1 == ox1 && y0 == oy0 && y1 == oy1) return;
			if (x1 < ox0 || x0 > ox1 || y1 < oy0 || y0 > oy1)
			{
				std::memset(set, 0, g.L.set_words * 4);
				g.gather(x0, x1, y0, y1, set, nullptr);
			}
			else
			{
				const auto ix0 = ox0 > x0 ? ox0 : x0, ix1 = ox1 >= x1 ? x1 : ox1;
				const auto iy0 = oy0 > y0 ? oy0 : y0, iy1 = oy1 < y1 ? oy1 : y1;
				if (y0 < oy0) g.gather(ix0, ix1, y0, oy0 - 1, set, nullptr);
				else if (oy0 < y0) { g.scatter(ix0, ix1, oy0, y0 - 1, set); g.gather(ix0, ix1, y0, y0, set, nullptr); }
				if (oy1 < y1) g.gather(ix0, ix1, oy1 + 1, y1, set, nullptr);
				else if (y1 < oy1) { g.scatter(ix0, ix1, y1 + 1, oy1, set); g.gather(ix0, ix1, y1, y1, set, nullptr); }
				if (x0 < ox0) g.gather(x0, ox0 - 1, y0, y1, set, nullptr);
				else if (ox0 < x0) { g.scatter(ox0, x0 - 1, oy0, oy1, set); g.gather(x0, x0, iy0, iy1, set, nullptr); }
				if (x1 > ox1) g.gather(ox1 + 1, x1, y0, y1, set, nullptr);
				else if (ox1 > x1) { g.scatter(x1 + 1, ox1, oy0, oy1, set); g.gather(x1, x1, iy0, iy1, set, nullptr); }
			}
		}
		g.store_extent(c + cache_off_extents, x0, x1, y0, y1);
	}
}
