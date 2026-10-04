#include "geometry_sharing.hpp"
#include "geometry_sharing_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <atomic>
#include <cstring>

namespace geometry_sharing
{
	namespace
	{
		using namespace geometry_sharing_rules;

		layout types{};

		// What status() reports.
		std::atomic<unsigned> checked{0};
		std::atomic<unsigned> rejected{0};
		std::atomic<unsigned> fallback{0};
		std::atomic<int> state{0}; // 1 on (the key set); -1 off (the key absent or 0); 0 unavailable (the key set, but a guard or the hook failed)

		// No C++ objects in here: the read is guarded, in case the address isn't readable.
		struct memory
		{
			bool read(address a, void* out, std::size_t n)
			{
				__try
				{
					if (a < 0x10000)
					{
						return false;
					}
					std::memcpy(out, reinterpret_cast<const void*>(a), n);
					return true;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					return false;
				}
			}
		};

		bool matches(address a, std::string_view hex)
		{
			memory m;
			std::uint8_t bytes[32]{};
			return hex.size() / 2 <= sizeof bytes && m.read(a, bytes, hex.size() / 2) && limits_rules::matches(bytes, hex);
		}

		template <typename Guards>
		bool guards_match(const Guards& guards, address base, const char* module)
		{
			if (const auto* g = first_mismatch(guards, [&](address va, std::string_view hex) { return matches(base + va, hex); }))
			{
				logger::write("geometry sharing: %s code guard failed at 0x%08X (%s) - unavailable, retail comparison", module, base + g->va, g->what);
				return false;
			}
			return true;
		}

		// Integer return, not bool: the authored bridge tests the complete EAX.
		std::uint32_t __cdecl keep_candidate(address incoming, address candidate)
		{
			memory m;
			const auto result = compare(m, incoming, candidate, types);
			checked.fetch_add(1, std::memory_order_relaxed);
			if (result == comparison::different)
			{
				rejected.fetch_add(1, std::memory_order_relaxed);
				return 0;
			}
			if (result == comparison::unsupported)
			{
				fallback.fetch_add(1, std::memory_order_relaxed);
			}
			return 1;
		}
	}

	void install(HMODULE game)
	{
		if (!ini::flag(L"Game", L"GeometrySharingBlendIndices", enabled_by_default))
		{
			state = -1;
			logger::write("geometry sharing: off (no [Game] GeometrySharingBlendIndices=1 in xml2-fix.ini) - retail comparison");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("geometry sharing: not the retail executable at 0x400000 - unavailable, retail comparison");
			return;
		}
		const auto gfx = static_cast<address>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"libIGGfx.dll")));
		const auto attrs = static_cast<address>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"libIGAttrs.dll")));
		if (!gfx || !attrs)
		{
			logger::write("geometry sharing: engine modules not loaded - unavailable, retail comparison");
			return;
		}
		if (!guards_match(exe_guards, 0, "EXE") || !guards_match(gfx_guards, gfx, "Gfx") || !guards_match(attrs_guards, attrs, "Attrs"))
		{
			return;
		}

		// The vtables that identify the legacy classes, and the slots the guarded functions sit in.
		memory m;
		types = {attrs + geometry_vtable_rva, gfx + vertex_vtable_rva};
		if (word(m, types.geometry_vtable + 0x54) != attrs + 0x17000 || word(m, types.vertex_vtable + 0x5c) != gfx + 0x4010 ||
		    word(m, types.vertex_vtable + 0x70) != gfx + 0x46040)
		{
			logger::write("geometry sharing: legacy vtable guard failed - unavailable, retail comparison");
			return;
		}

		// The bridge goes in a block of its own, made executable once it is written; then the jump to it.
		const auto code = bridge(static_cast<address>(reinterpret_cast<std::uintptr_t>(&keep_candidate)), continue_va, next_candidate_va);
		auto* stub = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!stub)
		{
			logger::write("geometry sharing: bridge allocation failed (error %lu) - unavailable, retail comparison", GetLastError());
			return;
		}
		std::memcpy(stub, code.data(), code.size());
		DWORD old = 0;
		if (!VirtualProtect(stub, code.size(), PAGE_EXECUTE_READ, &old))
		{
			logger::write("geometry sharing: bridge protection failed (error %lu) - unavailable, retail comparison", GetLastError());
			VirtualFree(stub, 0, MEM_RELEASE);
			return;
		}
		const auto patch = jump(hook_va, static_cast<address>(reinterpret_cast<std::uintptr_t>(stub)));
		auto* site = reinterpret_cast<void*>(hook_va);
		if (!VirtualProtect(site, patch.size(), PAGE_EXECUTE_READWRITE, &old))
		{
			logger::write("geometry sharing: hook protection failed (error %lu) - unavailable, retail comparison", GetLastError());
			VirtualFree(stub, 0, MEM_RELEASE);
			return;
		}
		std::memcpy(site, patch.data(), patch.size());
		DWORD ignored = 0;
		VirtualProtect(site, patch.size(), old, &ignored);
		FlushInstructionCache(GetCurrentProcess(), stub, code.size());
		FlushInstructionCache(GetCurrentProcess(), site, patch.size());
		state = 1;
		logger::write("geometry sharing: on ([Game] GeometrySharingBlendIndices=1) - packed blend indices checked before candidate reuse; guarded legacy arrays, other layouts stay retail");
	}

	std::string status()
	{
		return std::string("geometry sharing ") + (state == 1 ? "on" : state == -1 ? "off" : "unavailable") + "; geometry comparisons " +
		       std::to_string(checked.load()) + "; geometry rejected " + std::to_string(rejected.load()) + "; geometry fallback " + std::to_string(fallback.load());
	}
}
