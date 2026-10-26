// [Engine] ModelPools, the pools' lifecycle executed: the retail .text/.rdata/.data of XMen2.exe copied to their own
// addresses (the fixture section at 0x401000), the x2 and x4 plans applied to it exactly as the DLL does, and the game's
// own aggregate constructor (0x5754c0), allocators (ring pops), frees, pool destructors, zone walk (0x5852a0) and
// aggregate destructor (0x575560) run on a raised aggregate of the test's. Every item is a fake whose vtable slot 0 (the
// call the destructors make) counts the slot: no slot may be destroyed twice, nor one that isn't live.
//
//  - each pool destructor alone, with bits at and around the retail size, the last slot, and an empty pool; the bytes
//    after bitmap A (the ring) filled with a pattern that must survive;
//  - every pool full to its raised capacity, some freed through the game's own free, then the zone walk, then a second
//    full allocate/free/zone-walk cycle and a third at shutdown (0x575560), with live bits at and above 288 (attributes)
//    and 224 (procedural geometry) going into every reset;
//  - the plan of f4ce771 (no findNext copy, retail-size attribute and procedural-geometry tails) as the negative
//    control: it must fail the same checks.
//
// usage: xml2_model_pools_lifecycle_test XMen2.exe   (or XML2_FIX_TEST_EXE)

#include "model_pools_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace model_pools_rules;

extern "C" std::uint8_t fixture_space[0x400000];

namespace
{
	int checks = 0, failures = 0;
	bool quiet = false; // the negative control counts its own failures
	int control_failures = 0;
	void check(const bool ok, const std::string& what)
	{
		++checks;
		if (ok) return;
		if (quiet)
		{
			if (++control_failures <= 40) std::printf("  control fails, as expected: %s\n", what.c_str());
			return;
		}
		++failures;
		std::printf("FAIL %s\n", what.c_str());
	}

	std::vector<std::uint8_t> exe;
	struct section { DWORD va, size, raw; };
	std::vector<section> sections;

	bool load_exe(const char* path)
	{
		std::ifstream f(path, std::ios::binary);
		exe.assign(std::istreambuf_iterator<char>(f), {});
		if (exe.size() < 0x400) return false;
		const auto u32 = [](std::size_t o) { std::uint32_t v; std::memcpy(&v, &exe[o], 4); return v; };
		const auto u16 = [](std::size_t o) { std::uint16_t v; std::memcpy(&v, &exe[o], 2); return v; };
		const auto pe = u32(0x3c);
		if (u32(pe) != 0x4550 || u32(pe + 0x34) != image_base) return false;
		for (std::size_t i = 0; i < u16(pe + 6); ++i)
		{
			const auto h = pe + 0x18 + u16(pe + 0x14) + 40 * i;
			const char* n = reinterpret_cast<const char*>(&exe[h]);
			if (std::strncmp(n, ".text", 8) && std::strncmp(n, ".rdata", 8) && std::strncmp(n, ".data", 8)) continue;
			sections.push_back({image_base + u32(h + 12), std::min(u32(h + 16), u32(h + 8)), u32(h + 20)});
		}
		return sections.size() == 3;
	}

	// The retail image back in the fixture (every run starts from it).
	bool map_image()
	{
		const auto lo = reinterpret_cast<std::uintptr_t>(fixture_space), hi = lo + sizeof fixture_space;
		for (const auto& s : sections)
		{
			if (s.va < lo || s.va + s.size > hi || s.raw + s.size > exe.size()) return false;
			std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(s.va)), &exe[s.raw], s.size);
		}
		FlushInstructionCache(GetCurrentProcess(), fixture_space, sizeof fixture_space);
		return true;
	}

	// ---- the pools: the game's own functions per pool --------------------------------------------------------
	struct pool_code
	{
		kind k;
		DWORD destructor, reset, pop, free_by_pointer; // reset: the zone walk's per-pool reset; free 0: none used
	};
	constexpr pool_code pools[] = {
		{kind::transform, 0x571300, 0x585420, 0x572a50, 0x574ff0},
		{kind::attributes, 0x5713a0, 0x585450, 0x572ac0, 0x575060},
		{kind::shader_fx, 0x571460, 0x585480, 0x572b30, 0x5750e0},
		{kind::igb, 0x571500, 0x5854b0, 0x572ba0, 0x575150},
		{kind::actor, 0x5715a0, 0x5854e0, 0x572c10, 0x5751d0},
		{kind::procedural_geometry, 0x5716a0, 0x585510, 0x572c70, 0x575250},
		{kind::particle_cloud, 0x571750, 0x5853c0, 0x5719d0, 0x574ac0},
		{kind::model, 0x571930, 0x5753f0, 0x572ce0, 0},
	};

	aggregate_layout layout{};
	std::uint8_t* block = nullptr;
	std::uint8_t* pool_base(const kind k) { return block + pool_start(layout, k); }
	std::uint32_t* bitmap(const kind k, const bool b) { return reinterpret_cast<std::uint32_t*>(pool_base(k) + (b ? layout.pool[index(k)].bitmap_b : layout.pool[index(k)].bitmap_a)); }
	std::uint32_t& field32(const kind k, const DWORD offset) { return *reinterpret_cast<std::uint32_t*>(pool_base(k) + offset); }

	// ---- the fake items -------------------------------------------------------------------------------------
	std::vector<std::vector<int>> destroyed; // [pool][slot] destroy count
	std::vector<std::vector<bool>> live;     // [pool][slot] as the test allocated it
	int violations = 0, calls = 0, call_budget = 0;
	std::string first_violation;

	void on_destroy(const std::uintptr_t item)
	{
		++calls;
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			const auto k = pools[p].k;
			const auto base = reinterpret_cast<std::uintptr_t>(pool_base(k));
			const auto size = item_size[index(k)];
			const auto cap = static_cast<std::uintptr_t>(layout.pool[index(k)].capacity);
			if (item < base || item >= base + cap * size) continue;
			const auto slot = (item - base) / size;
			if ((item - base) % size || !live[p][slot] || destroyed[p][slot]++)
			{
				if (!violations++)
					first_violation = std::string(kind_name[index(k)]) + " slot " + std::to_string(slot) + (live[p][slot] ? " destroyed twice" : " destroyed while not live");
			}
			if (calls > call_budget) RaiseException(0xe0000ff1, EXCEPTION_NONCONTINUABLE, 0, nullptr); // a walk that never ends
			return;
		}
		if (!violations++) first_violation = "a destroy outside every pool";
	}
	struct fake_item
	{
		virtual void __thiscall destroy(int);
	};
	void __thiscall fake_item::destroy(int) { on_destroy(reinterpret_cast<std::uintptr_t>(this)); }
	void* fake_vtable = nullptr;

	using thiscall0 = int(__thiscall*)(void*);
	using thiscall1 = int(__thiscall*)(void*, void*);
	template <class F> F fn(const DWORD va) { return reinterpret_cast<F>(static_cast<std::uintptr_t>(va)); }

	// The game's code, guarded: false when it faults or runs away (the fake raises after the budget).
	bool run0(const DWORD va, void* self, int* result = nullptr)
	{
		__try
		{
			const int r = fn<thiscall0>(va)(self);
			if (result) *result = r;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	bool run1(const DWORD va, void* self, void* arg)
	{
		__try
		{
			fn<thiscall1>(va)(self, arg);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void reset_counts()
	{
		destroyed.assign(std::size(pools), {});
		live.assign(std::size(pools), {});
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			destroyed[p].assign(layout.pool[index(pools[p].k)].capacity, 0);
			live[p].assign(layout.pool[index(pools[p].k)].capacity, false);
		}
		violations = calls = 0;
		first_violation.clear();
	}

	// The plan, applied as the DLL applies it; `control`: f4ce771's plan (without the lifecycle fixes).
	std::uint8_t* caves = nullptr;
	std::uint8_t* systems = nullptr;
	bool install(const capacities& caps, const bool control)
	{
		if (!map_image()) return false;
		layout = aggregate_layout_for(caps);
		if (block) VirtualFree(block, 0, MEM_RELEASE);
		if (systems) VirtualFree(systems, 0, MEM_RELEASE);
		if (caves) VirtualFree(caves, 0, MEM_RELEASE);
		// one guard page after the aggregate: a write past its end faults
		block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, layout.size + 0x2000, MEM_RESERVE, PAGE_NOACCESS));
		if (!block || !VirtualAlloc(block, layout.size, MEM_COMMIT, PAGE_READWRITE)) return false;
		systems = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, layout.psys.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		caves = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		const auto at = [](const void* p) { return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p)); };
		const auto p = plan_for(layout, blocks{at(block), at(systems)}, at(caves));
		if (!p || p->cave_code.size() > 0x1000) return false;
		std::memcpy(caves, p->cave_code.data(), p->cave_code.size());
		for (const auto& e : p->expects)
			if (std::memcmp(reinterpret_cast<void*>(static_cast<std::uintptr_t>(e.va)), e.before.data(), e.before.size())) return false;
		for (const auto& w : p->writes)
		{
			auto* to = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(w.va));
			if (std::memcmp(to, w.before.data(), w.before.size())) return false;
			if (control && (w.va == 0x571724 || w.va == 0x571435 || w.va == 0x571735)) continue;
			std::memcpy(to, w.bytes.data(), w.bytes.size());
		}
		FlushInstructionCache(GetCurrentProcess(), fixture_space, sizeof fixture_space);
		FlushInstructionCache(GetCurrentProcess(), caves, 0x1000);
		return true;
	}

	// ---- one pool destructor alone ----------------------------------------------------------------------------
	void destructor_case(const std::size_t p, const std::vector<int>& slots, const std::string& name)
	{
		const auto k = pools[p].k;
		const auto& pl = layout.pool[index(k)];
		std::memset(block, 0, layout.size);
		reset_counts();
		call_budget = 4 * pl.capacity + 8;
		for (DWORD i = pl.ring; i < pl.ring + 64 && i < pl.size; ++i) pool_base(k)[i] = static_cast<std::uint8_t>(0xa5 ^ i);
		for (const int s : slots)
		{
			bitmap(k, false)[s / 32] |= 1u << (s % 32);
			*reinterpret_cast<void**>(pool_base(k) + s * item_size[index(k)]) = fake_vtable;
			live[p][s] = true;
		}
		const bool returned = run0(pools[p].destructor, pool_base(k));
		const std::string what = std::string(kind_name[index(k)]) + " destructor, " + name;
		check(returned, what + ": returns (no fault, no endless walk)");
		check(!violations, what + ": " + first_violation);
		int missed = 0;
		for (const int s : slots) missed += destroyed[p][s] != 1;
		check(!missed, what + ": every live slot destroyed once");
		int left = 0;
		for (DWORD w = 0; w < pl.words; ++w) left += bitmap(k, false)[w] != 0;
		check(!left, what + ": bitmap A clear to its last dword (" + std::to_string(left) + " dwords left)");
		int guard = 0;
		for (DWORD i = pl.ring; i < pl.ring + 64 && i < pl.size; ++i) guard += pool_base(k)[i] != static_cast<std::uint8_t>(0xa5 ^ i);
		check(!guard, what + ": the bytes after bitmap A untouched");
	}

	// ---- a whole lifecycle -----------------------------------------------------------------------------------
	int allocate(const std::size_t p)
	{
		const auto k = pools[p].k;
		int slot = -1;
		if (!run0(pools[p].pop, pool_base(k), &slot)) return -1;
		if (slot < 0 || slot >= layout.pool[index(k)].capacity) return -2;
		// as the typed allocators (0x574620 and its siblings) do after the pop: bitmap A, then the item
		bitmap(k, false)[slot / 32] |= 1u << (slot % 32);
		*reinterpret_cast<void**>(pool_base(k) + slot * item_size[index(k)]) = fake_vtable;
		if (live[p][slot]) return -3;
		live[p][slot] = true;
		destroyed[p][slot] = 0;
		return slot;
	}

	void fill(const std::string& when)
	{
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			const auto k = pools[p].k;
			const int cap = layout.pool[index(k)].capacity;
			int bad = 0;
			for (int i = 0; i < cap; ++i) bad += allocate(p) < 0;
			check(!bad, when + ": " + kind_name[index(k)] + " allocates all " + std::to_string(cap) + " slots");
			check(field32(k, layout.pool[index(k)].live) == static_cast<std::uint32_t>(cap), when + ": " + kind_name[index(k)] + " live count full");
		}
	}

	// Frees every third slot (and slot 1) through the game's own free; returns how many.
	void free_some(const std::string& when)
	{
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			if (!pools[p].free_by_pointer) continue;
			const auto k = pools[p].k;
			const int cap = layout.pool[index(k)].capacity;
			int bad = 0, freed = 0;
			for (int s = 1; s < cap - 1; s += 3)
			{
				if (!run1(pools[p].free_by_pointer, pool_base(k), pool_base(k) + s * item_size[index(k)]) || destroyed[p][s] != 1) ++bad;
				live[p][s] = false;
				++freed;
			}
			check(!bad && !violations, when + ": " + kind_name[index(k)] + " frees through the game " + first_violation);
			check(field32(k, layout.pool[index(k)].live) == static_cast<std::uint32_t>(cap - freed), when + ": " + kind_name[index(k)] + " live count after the frees");
		}
	}

	// After a zone walk or the destructor: every slot live before it destroyed exactly once, bitmaps clear, counts 0.
	void after_teardown(const std::vector<std::vector<bool>>& before, const std::string& when, const bool zone_walk)
	{
		check(!violations, when + ": " + first_violation);
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			const auto k = pools[p].k;
			const auto& pl = layout.pool[index(k)];
			int missed = 0;
			for (int s = 0; s < pl.capacity; ++s)
				if (before[p][s]) missed += destroyed[p][s] != 1;
			check(!missed, when + ": " + kind_name[index(k)] + " every live slot destroyed once (" + std::to_string(missed) + " not)");
			int left = 0;
			for (DWORD w = 0; w < pl.words; ++w) left += bitmap(k, false)[w] != 0;
			check(!left, when + ": " + kind_name[index(k)] + " bitmap A clear (" + std::to_string(left) + " dwords left)");
			if (!zone_walk) continue;
			int left_b = 0;
			for (DWORD w = 0; w < pl.words; ++w) left_b += bitmap(k, true)[w] != 0;
			check(!left_b, when + ": " + kind_name[index(k)] + " bitmap B clear");
			check(field32(k, pl.live) == 0, when + ": " + kind_name[index(k)] + " live count 0");
			// the ring rebuilt: every slot exactly once
			std::vector<int> seen(pl.capacity, 0);
			int bad = 0;
			for (int i = 0; i < pl.capacity; ++i)
			{
				const auto v = field32(k, pl.ring + 4 * i);
				if (v >= static_cast<std::uint32_t>(pl.capacity) || seen[v]++) ++bad;
			}
			check(!bad, when + ": " + kind_name[index(k)] + " ring holds every slot once");
		}
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			live[p].assign(live[p].size(), false);
			destroyed[p].assign(destroyed[p].size(), 0);
		}
		violations = calls = 0;
		first_violation.clear();
	}

	void lifecycle(const char* name, const bool shutdown)
	{
		std::memset(block, 0, layout.size);
		reset_counts();
		call_budget = 1 << 20;
		check(run0(0x5754c0, block), std::string(name) + ": the aggregate constructor runs");
		for (int cycle = 1; cycle <= 2; ++cycle)
		{
			const std::string when = std::string(name) + " cycle " + std::to_string(cycle);
			fill(when);
			free_some(when);
			for (auto& d : destroyed) d.assign(d.size(), 0);
			const auto before = live;
			// live bits at and above 288 / 224 go into the reset
			check(live[1][288] && live[1][layout.pool[index(kind::attributes)].capacity - 1] && live[5][224] && live[5][221], when + ": high slots live");
			check(run0(0x5852a0, block), when + ": the zone walk (0x5852a0) returns");
			after_teardown(before, when + " zone walk", true);
		}
		if (!shutdown) return; // the control: 0x575560's SEH frame would unwind into the runaway walk
		// shutdown with a few live slots, the last one and some above the retail size among them
		const std::string when = std::string(name) + " shutdown";
		for (std::size_t p = 0; p < std::size(pools); ++p)
			for (int i = 0; i < layout.pool[index(pools[p].k)].capacity; ++i) allocate(p);
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			if (!pools[p].free_by_pointer) continue;
			const auto k = pools[p].k;
			const int cap = layout.pool[index(k)].capacity;
			for (int s = 0; s < cap - 1; ++s)
				if (s != 0 && s != retail_capacities[index(k)] - 1 && s != retail_capacities[index(k)] && s != cap / 2 + 1)
				{
					run1(pools[p].free_by_pointer, pool_base(k), pool_base(k) + s * item_size[index(k)]);
					live[p][s] = false;
				}
		}
		for (auto& d : destroyed) d.assign(d.size(), 0);
		const auto before = live;
		check(run0(0x575560, block), when + ": the aggregate destructor (0x575560) returns");
		after_teardown(before, when, false);
		// and once more on the now empty aggregate: nothing destroyed
		check(run0(0x575560, block) && calls == 0 && !violations, when + ": the aggregate destructor on empty pools destroys nothing");
	}

	void run_config(const capacities& caps, const char* name, const bool control)
	{
		std::printf("%s%s\n", name, control ? " (negative control: the f4ce771 plan)" : "");
		if (!install(caps, control))
		{
			check(false, std::string(name) + ": the plan installs on the fixture");
			return;
		}
		for (std::size_t p = 0; p < std::size(pools); ++p)
		{
			const int cap = layout.pool[index(pools[p].k)].capacity, retail = retail_capacities[index(pools[p].k)];
			destructor_case(p, {}, "empty");
			destructor_case(p, {cap - 1}, "only the last slot");
			destructor_case(p, {0}, "only slot 0");
			std::vector<int> around{0, retail - 1, retail, retail + 1, cap / 2 + 1, cap - 2, cap - 1};
			if (pools[p].k == kind::attributes) around.insert(around.end(), {287, 288, 300});
			if (pools[p].k == kind::procedural_geometry) around.insert(around.end(), {219, 221, 223, 224, 500});
			std::sort(around.begin(), around.end());
			around.erase(std::unique(around.begin(), around.end()), around.end());
			destructor_case(p, around, "slots around the retail size and the last");
			std::vector<int> all;
			for (int s = 0; s < cap; ++s) all.push_back(s);
			destructor_case(p, all, "full");
		}
		lifecycle(name, !control);
	}
}

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* path = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	if (!path || !load_exe(path))
	{
		std::puts("usage: xml2_model_pools_lifecycle_test XMen2.exe (a retail exe at 0x400000)");
		return 2;
	}
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(image_base) || reinterpret_cast<std::uintptr_t>(fixture_space) > 0x401000)
	{
		std::puts("the fixture must start at 0x401000 in an image based at 0x400000");
		return 2;
	}
	DWORD old = 0;
	if (!VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old)) return 2;
	static fake_item prototype;
	fake_vtable = *reinterpret_cast<void**>(&prototype);

	run_config(x2_capacities, "x2", false);
	run_config(x4_capacities, "x4", false);
	// The negative control: f4ce771's plan must fail these checks (the procedural-geometry walk runs away or destroys
	// slot 220; the attribute and procedural-geometry bits above the retail tails survive).
	const int real = failures;
	quiet = true;
	run_config(x4_capacities, "x4", true);
	quiet = false;
	std::printf("negative control: %d checks failed (the f4ce771 plan)\n", control_failures);
	check(control_failures > 0, "the negative control fails");
	check(failures == real, "the control's failures stay its own");
	std::printf("model pools lifecycle: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
