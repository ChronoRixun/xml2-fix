#include "address_space.hpp"
#include "address_space_rules.hpp"
#include "log.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

namespace address_space
{
	namespace
	{
		using namespace address_space_rules;

		const char* laa_state = "unknown";
		const char* guard_state = "off";        // "on" / "off" (flag off) / "unavailable" (the block was in use)
		std::uint64_t test_reserved = 0;
		std::mutex test_mutex;
		LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
		std::atomic<bool> in_filter{false};

		std::uint32_t address_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

		std::uint32_t top_address()
		{
			SYSTEM_INFO system{};
			GetSystemInfo(&system);
			return address_of(system.lpMaximumApplicationAddress);
		}

		usage measure()
		{
			usage u;
			std::uint64_t at = 0x10000;
			const std::uint64_t top = static_cast<std::uint64_t>(top_address()) + 1;
			MEMORY_BASIC_INFORMATION info{};
			while (at < top && VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), &info, sizeof(info)) == sizeof(info))
			{
				const auto base = address_of(info.BaseAddress);
				const auto size = static_cast<std::uint32_t>(info.RegionSize);
				if (size == 0) break;
				add_region(u, base, size, info.State, info.Type, address_of(info.AllocationBase));
				at = static_cast<std::uint64_t>(base) + size;
			}
			return u;
		}

		const char* read_laa(const HMODULE game)
		{
			__try
			{
				const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const std::uint8_t*>(game) + dos->e_lfanew);
				if (dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE)
					return large_address_aware(nt->FileHeader.Characteristics) ? "on" : "off";
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
			return "unknown";
		}

		// The 2 GB guard (address_space_rules.hpp): with the flag on, nothing may straddle 0x80000000.
		void reserve_guard()
		{
			void* at = reinterpret_cast<void*>(static_cast<std::uintptr_t>(guard_base));
			if (VirtualAlloc(at, guard_size, MEM_RESERVE, PAGE_NOACCESS) == at)
			{
				guard_state = "on";
				return;
			}
			const auto error = GetLastError();
			MEMORY_BASIC_INFORMATION info{};
			const bool known = VirtualQuery(at, &info, sizeof(info)) == sizeof(info);
			guard_state = "unavailable";
			logger::write("address space: the 2 GB guard block 0x7FFF0000 could not be reserved (error %lu; state %#lx, allocation base %s) - "
			              "a block may straddle 2 GB", error, known ? info.State : 0, known ? hex32(address_of(info.AllocationBase)).c_str() : "?");
		}

		void module_of(const void* address, char* name, const DWORD size, std::uint32_t& base)
		{
			name[0] = 0;
			base = 0;
			HMODULE module = nullptr;
			if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                        static_cast<LPCSTR>(address), &module) || !module)
				return;
			char path[MAX_PATH]{};
			if (!GetModuleFileNameA(module, path, MAX_PATH)) return;
			const char* leaf = path;
			for (const char* p = path; *p; ++p)
				if (*p == '\\' || *p == '/') leaf = p + 1;
			lstrcpynA(name, leaf, static_cast<int>(size));
			base = address_of(module);
		}

		// The crash line: what the fault was, where, and the address space at that moment.
		void write_crash_line(const EXCEPTION_RECORD* record)
		{
			char name[64];
			std::uint32_t module_base = 0;
			module_of(record->ExceptionAddress, name, sizeof(name), module_base);
			const auto at = address_of(record->ExceptionAddress);
			const bool access = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2;
			const char* how = !access ? "" : record->ExceptionInformation[0] == 1 ? ", writing " : record->ExceptionInformation[0] == 8 ? ", executing " : ", reading ";
			const std::string target = access ? hex32(static_cast<std::uint32_t>(record->ExceptionInformation[1])) : std::string();
			const auto u = measure();
			logger::write("crash: exception %#010lx at %s (%s+%#lx)%s%s; laa %s; above 2 GB %s; private %s; highest %s; 2 GB guard %s",
			              record->ExceptionCode, hex32(at).c_str(), name[0] ? name : "?", static_cast<unsigned long>(at - module_base), how,
			              target.c_str(), laa_state, megabytes(u.committed_high).c_str(), megabytes(u.committed_private).c_str(),
			              hex32(u.highest_committed).c_str(), guard_state);
		}

		// Then the crash goes on as it always did (the previous filter, else Windows' error reporting).
		LONG WINAPI on_crash(EXCEPTION_POINTERS* info)
		{
			if (!in_filter.exchange(true) && info && info->ExceptionRecord)
			{
				__try
				{
					write_crash_line(info->ExceptionRecord);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
			return previous_filter ? previous_filter(info) : EXCEPTION_CONTINUE_SEARCH;
		}

		std::string high_address_test(const std::uint32_t keep_mb)
		{
			if (std::string(laa_state) != "on") return "error the exe is not large-address-aware: nothing above 2 GB to test";
			std::lock_guard lock(test_mutex);
			const auto before = measure();
			const std::uint64_t keep = static_cast<std::uint64_t>(keep_mb) * mb;
			std::uint64_t excess = before.free_low > keep ? before.free_low - keep : 0;
			std::uint64_t reserved = 0;
			unsigned blocks = 0;
			std::uint64_t at = 0x10000;
			MEMORY_BASIC_INFORMATION info{};
			while (excess >= granularity && at < two_gb && VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), &info, sizeof(info)) == sizeof(info))
			{
				const auto base = address_of(info.BaseAddress);
				const auto size = static_cast<std::uint32_t>(info.RegionSize);
				if (size == 0) break;
				at = static_cast<std::uint64_t>(base) + size;
				if (info.State != MEM_FREE) continue;
				const auto take = reservable(base, size, excess);
				if (!take) continue;
				void* want = reinterpret_cast<void*>(static_cast<std::uintptr_t>(reservable_start(base)));
				if (VirtualAlloc(want, take, MEM_RESERVE, PAGE_NOACCESS) == want)
				{
					reserved += take;
					excess -= take;
					++blocks;
				}
			}
			test_reserved += reserved;
			const auto after = measure();
			logger::write("address space: high-address test - reserved %s in %u blocks below 2 GB (%s of it was free; %s left free for the game); "
			              "the game's next allocations land above 2 GB", megabytes(reserved).c_str(), blocks, megabytes(before.free_low).c_str(),
			              megabytes(after.free_low).c_str());
			return "ok reserved " + megabytes(reserved) + " in " + std::to_string(blocks) + " blocks; " + megabytes(after.free_low) + " left free below 2 GB";
		}
	}

	void install(const HMODULE game)
	{
		laa_state = game ? read_laa(game) : "unknown";
		if (std::string(laa_state) == "on")
			reserve_guard();
		const auto u = measure();
		logger::write("address space: laa %s; 2 GB guard %s; top %s; %s free below 2 GB%s", laa_state, guard_state, hex32(top_address()).c_str(),
		              megabytes(u.free_low).c_str(), u.straddling ? "; a block crosses 2 GB" : "");
		previous_filter = SetUnhandledExceptionFilter(on_crash);
	}

	const char* laa()
	{
		return laa_state;
	}

	std::string status()
	{
		return describe(laa_state, top_address(), measure(), guard_state, test_reserved);
	}

	std::string pipe(const std::string& arguments)
	{
		const auto first = arguments.find_first_not_of(" \t");
		const auto text = first == std::string::npos ? std::string() : arguments.substr(first);
		if (text.empty() || text.rfind("status", 0) == 0) return status();
		if (text.rfind("high", 0) == 0)
		{
			const char* number = text.c_str() + 4;
			while (*number == ' ' || *number == '\t') ++number;
			char* end = nullptr;
			const auto keep = std::strtoul(number, &end, 10);
			if (end == number || keep > 2048) return "error memory high takes the MB to leave free below 2 GB (0..2048)";
			return high_address_test(static_cast<std::uint32_t>(keep));
		}
		return "error memory takes status or high <MB>";
	}
}
