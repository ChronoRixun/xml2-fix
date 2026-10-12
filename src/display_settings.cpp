#include "display_settings.hpp"
#include "display_settings_rules.hpp"
#include "game_version.hpp"
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

		// The log stub the wipe guard's rerouted call lands on (cdecl, no arguments, no result).
		void wipe_log_stub()
		{
			logger::write("display settings: the display device failed - the player's Resolution/FSAA registry settings are left untouched");
		}

		std::vector<std::uint8_t> unhex(const char* hex)
		{
			std::vector<std::uint8_t> bytes;
			for (const auto* p = hex; p[0] && p[1]; p += 2)
			{
				const auto nibble = [](const char c) { return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
				bytes.push_back(static_cast<std::uint8_t>(nibble(p[0]) << 4 | nibble(p[1])));
			}
			return bytes;
		}
	}

	void install(const HMODULE game)
	{
		if (reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("display settings: not the retail XMen2.exe - the boot FSAA switch and the settings wipe are left as they are");
			return;
		}

		// The private 5-entry jump table and the index-4 case's code, in one executable page.
		// The table is read by the game's indexed jump, the case by the table's 5th entry.
		constexpr std::size_t table_size = 5 * 4;
		auto* const page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!page)
		{
			logger::write("display settings: can't allocate the FSAA jump table (error %lu) - left as the game has it", GetLastError());
			return;
		}
		const auto table = reinterpret_cast<std::uint32_t>(page);
		const auto stub = table + table_size;
		const auto table_bytes = fsaa_table_bytes(stub);
		const auto stub_bytes = fsaa_case_8x_bytes(support_byte_8x, engine_param_cell, multi_sample_type_name, fsaa_switch_join, stub);
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

		std::vector<xml1_hook_transaction::patch> writes;
		writes.reserve(std::size(guards) + 6);
		for (const auto& guard : guards)
		{
			const auto bytes = unhex(guard.hex); // verified by the transaction, written back unchanged
			writes.push_back({guard.va, bytes, bytes});
		}
		auto fsaa = fsaa_switch_writes(table);
		writes.insert(writes.end(), fsaa.begin(), fsaa.end());
		auto wipe = wipe_guard_writes(reinterpret_cast<std::uint32_t>(reinterpret_cast<void*>(&wipe_log_stub)));
		writes.insert(writes.end(), wipe.begin(), wipe.end());

		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			logger::write("display settings: the boot FSAA switch knows the panel's 8x option (index 4 -> 8 samples, 6 where the adapter has no 8x), and a failed display device no longer resets Resolution/FSAA");
			return;
		}
		if (safe)
		{
			logger::write("display settings: XMen2.exe doesn't have the expected code (not the retail build?) - the boot FSAA switch and the settings wipe are left as they are");
			VirtualFree(page, 0, MEM_RELEASE);
			return;
		}
		logger::write("display settings: the rollback could not be verified - refusing startup rather than run half-patched");
		game_version::refuse_startup();
	}
}
