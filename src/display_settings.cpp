#include "display_settings.hpp"
#include "display_settings_rules.hpp"
#include "game_version.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <cstring>
#include <vector>

namespace display_settings
{
	namespace
	{
		using namespace display_settings_rules;

		// The log stub the wipe guard's rerouted call lands on: FUN_0061bef0 takes nothing and its
		// result is unused (call; call; mov esi,[esp+80h] at 0x5faf56..0x5faf60), so a cdecl function
		// with no arguments fits; it keeps ebx/esi/edi/ebp like any C++ function.
		void wipe_log_stub()
		{
			logger::write("display settings: the display device failed - the game quits with its own message; the player's Resolution/FSAA registry "
			              "settings are left untouched");
		}

	}

	void install(const HMODULE game, const bool guard_wipe)
	{
		if (reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("display settings: not the retail XMen2.exe - the boot FSAA switch and the settings wipe are left as they are");
			return;
		}
		// The guards, read-only, before anything is allocated or written (they may cover the patch
		// sites; the transaction checks each write's own original bytes again).
		for (const auto& guard : guards)
		{
			if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(guard.va)), guard.hex))
			{
				logger::write("display settings: XMen2.exe doesn't have the expected code at %08X (%s) - the boot FSAA switch and the settings wipe are left as they are",
				              guard.va, guard.what);
				return;
			}
		}

		// The private 5-entry jump table and the index-4 case's code, in one page that lives as long
		// as the process once the patch is in (the game's indexed jump reads the table, the table's
		// 5th entry runs the case). Executable and read-only: nothing writes it after this.
		constexpr std::size_t table_size = 5 * 4;
		auto* const page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!page)
		{
			logger::write("display settings: can't allocate the FSAA jump table (error %lu) - left as the game has it", GetLastError());
			return;
		}
		const auto table = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(page));
		const auto stub = table + table_size;
		const auto table_bytes = fsaa_table_bytes(stub);
		const auto stub_bytes = fsaa_case_8x_bytes(engine_param_cell, multi_sample_type_name, fsaa_switch_join, stub);
		std::memcpy(page, table_bytes.data(), table_bytes.size());
		std::memcpy(page + table_size, stub_bytes.data(), stub_bytes.size());
		{
			DWORD ignored = 0;
			if (!VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &ignored))
			{
				logger::write("display settings: can't protect the FSAA jump table (error %lu) - left as the game has it", GetLastError());
				VirtualFree(page, 0, MEM_RELEASE);
				return;
			}
		}
		FlushInstructionCache(GetCurrentProcess(), page, 4096);

		auto writes = fsaa_switch_writes(table);
		if (guard_wipe)
		{
			const auto wipe = wipe_guard_writes(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&wipe_log_stub)));
			writes.insert(writes.end(), wipe.begin(), wipe.end());
		}

		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			logger::write("display settings: the boot FSAA switch knows the panel's 8x option (index 4 -> 8 samples, clamped to what the adapter offers when the "
			              "device is made)%s",
			              guard_wipe ? ", and a failed display device no longer resets Resolution/FSAA"
			                         : "; a failed display device still resets Resolution/FSAA (the game's own recovery, kept in its own display mode)");
			return;
		}
		if (safe)
		{
			logger::write("display settings: the patch didn't take (rolled back, every byte verified) - the boot FSAA switch and the settings wipe are left as they are");
			VirtualFree(page, 0, MEM_RELEASE);
			return;
		}
		// The table and stub stay allocated: a byte that couldn't be restored may still point at them.
		logger::write("display settings: the rollback could not be verified - refusing startup rather than run half-patched");
		game_version::refuse_startup();
	}
}
