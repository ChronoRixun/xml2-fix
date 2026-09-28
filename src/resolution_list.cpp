#include "resolution_list.hpp"
#include "log.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace resolution_list
{
	namespace
	{
		using namespace resolution_rules;

		// The table the game gets: 64 slots of 12 bytes, as the game's own but bigger. The game
		// fills it (FUN_00619ac0) whenever the Advanced Options panel opens; the shipped defaults
		// are copied in first so it is never emptier than the game's.
		alignas(16) char table[slots * slot_bytes]{};

		std::size_t slots_in_use = stock_slots;
		bool moved = false;

		// No C++ objects here: the reads are guarded, in case this isn't XMen2.exe at all.
		bool bytes_match(const std::uint8_t* base, const table_site& site)
		{
			__try
			{
				return matches(site, base + site.rva);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool copy_stock_table(const std::uint8_t* base)
		{
			__try
			{
				std::memcpy(table, base + stock_table_rva, stock_slots * slot_bytes);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
	}

	std::size_t install(const HMODULE game, const display_rules::resolution_list setting)
	{
		slots_in_use = stock_slots;
		moved = false;
		if (setting == display_rules::resolution_list::game)
		{
			logger::write("resolution list: the game's own %zu-slot table ([Display] ResolutionList=game); the Video options list is trimmed to it, so it can't overflow", stock_slots);
			return slots_in_use;
		}

		auto* base = reinterpret_cast<std::uint8_t*>(game);
		for (const auto& site : table_sites)
		{
			if (!bytes_match(base, site))
			{
				logger::write("resolution list: XMen2.exe doesn't have the expected code for %s (not the retail build?) - the game's own %zu-slot table stays and the Video options list is trimmed to it",
				              site.what, stock_slots);
				return slots_in_use;
			}
		}
		if (!copy_stock_table(base))
		{
			logger::write("resolution list: can't read the game's resolution table - its own %zu-slot table stays and the Video options list is trimmed to it", stock_slots);
			return slots_in_use;
		}

		// All seven unprotected first, so a refusal leaves the game's code untouched.
		std::array<DWORD, table_sites.size()> protection{};
		for (std::size_t i = 0; i < table_sites.size(); ++i)
		{
			if (!VirtualProtect(base + table_sites[i].rva, table_sites[i].expected.size(), PAGE_EXECUTE_READWRITE, &protection[i]))
			{
				logger::write("resolution list: can't unprotect %s (error %lu) - the game's own %zu-slot table stays and the Video options list is trimmed to it", table_sites[i].what,
				              GetLastError(), stock_slots);
				for (std::size_t j = 0; j < i; ++j)
				{
					VirtualProtect(base + table_sites[j].rva, table_sites[j].expected.size(), protection[j], &protection[j]);
				}
				return slots_in_use;
			}
		}
		const auto address = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(table));
		for (std::size_t i = 0; i < table_sites.size(); ++i)
		{
			std::uint8_t* at = base + table_sites[i].rva;
			std::memcpy(at + table_sites[i].imm_offset, &address, sizeof(address));
			FlushInstructionCache(GetCurrentProcess(), at, table_sites[i].expected.size());
			VirtualProtect(at, table_sites[i].expected.size(), protection[i], &protection[i]);
		}
		slots_in_use = slots;
		moved = true;
		logger::write("resolution list: the game's %zu-slot resolution table (0x%lX) is replaced by one of %zu slots at %p (%zu references patched); the Video options list can hold %zu sizes",
		              stock_slots, stock_table_va, slots, static_cast<void*>(table), table_sites.size(), slots);
		return slots_in_use;
	}

	std::size_t capacity()
	{
		return slots_in_use;
	}

	bool relocated()
	{
		return moved;
	}
}
