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

		// "module+offset" for an address (a fault outside every module reads "?+address").
		void where_of(const std::uint32_t at, char (&out)[96])
		{
			char name[64];
			std::uint32_t base = 0;
			module_of(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), name, sizeof(name), base);
			wsprintfA(out, "%s+%#lx", name[0] ? name : "?", static_cast<unsigned long>(at - base));
		}

		bool readable(const std::uint32_t at, const std::uint32_t size)
		{
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), &info, sizeof(info)) != sizeof(info)) return false;
			if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
			return static_cast<std::uint64_t>(at) + size <= static_cast<std::uint64_t>(address_of(info.BaseAddress)) + info.RegionSize;
		}

		// A stack value is a return address when it lands in a module and the bytes before it are a call.
		bool return_address(const std::uint32_t value)
		{
			char name[64];
			std::uint32_t base = 0;
			module_of(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(value)), name, sizeof(name), base);
			if (!base || value < base + 0x1000 || !readable(value - 7, 7)) return false;
			const auto* b = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(value));
			return b[-5] == 0xe8 || (b[-2] == 0xff && (b[-1] & 0x38) == 0x10) || (b[-3] == 0xff && (b[-2] & 0x38) == 0x10) ||
			       (b[-6] == 0xff && (b[-5] & 0x38) == 0x10) || (b[-7] == 0xff && (b[-6] & 0x38) == 0x10);
		}

		// The crash context (0.6 preview fix G): registers, the faulting region, the EBP chain and the return addresses on
		// the stack, so a fault outside every module (a call through a stale pointer) still names its caller chain.
		void write_crash_context(const CONTEXT* c)
		{
			if (!c) return;
			char w[96];
			logger::write("crash: registers eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx eip %08lx",
			              c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp, c->Eip);
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(c->Eip)), &info, sizeof(info)) == sizeof(info))
				logger::write("crash: eip region base %08lx allocation %08lx size %#lx state %#lx protect %#lx type %#lx", address_of(info.BaseAddress),
				              address_of(info.AllocationBase), static_cast<unsigned long>(info.RegionSize), info.State, info.Protect, info.Type);
			if (readable(c->Eip - 16, 32))
			{
				const auto* b = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(c->Eip - 16));
				char hex[100]{};
				for (int i = 0; i < 32; ++i) wsprintfA(hex + 2 * i + (i >= 16 ? 1 : 0), i == 16 ? " %02x" : "%02x", b[i]);
				logger::write("crash: bytes eip-16..eip+16 %s", hex);
			}
			std::uint32_t frame = c->Ebp;
			for (int depth = 0; depth < 16 && frame && readable(frame, 8); ++depth)
			{
				const auto* f = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(frame));
				where_of(f[1], w);
				logger::write("crash: ebp frame %d %08lx return %08lx (%s)", depth, frame, f[1], w);
				if (f[0] <= frame) break;
				frame = f[0];
			}
			int found = 0;
			for (std::uint32_t at = c->Esp & ~3u; found < 40 && at < c->Esp + 0x2000 && readable(at, 4); at += 4)
			{
				const auto value = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(at));
				if (!return_address(value)) continue;
				where_of(value, w);
				logger::write("crash: stack esp+%#lx %08lx (%s)", static_cast<unsigned long>(at - c->Esp), value, w);
				++found;
			}
		}

		// XML2FIX_CRASHTRACE=1 in the environment (agent live repro only; no ini key): every first-chance access violation /
		// privileged / illegal instruction gets one line with its caller, also the ones a __try block swallows.
		std::atomic<int> traced{0};
		LONG WINAPI first_chance(EXCEPTION_POINTERS* info)
		{
			if (!info || !info->ExceptionRecord || !info->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
			const auto code = info->ExceptionRecord->ExceptionCode;
			if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_ILLEGAL_INSTRUCTION)
				return EXCEPTION_CONTINUE_SEARCH;
			if (traced.fetch_add(1) >= 60) return EXCEPTION_CONTINUE_SEARCH;
			__try
			{
				char w[96], r[96];
				const auto* c = info->ContextRecord;
				where_of(c->Eip, w);
				std::uint32_t ret = 0;
				if (readable(c->Esp, 4)) ret = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(c->Esp));
				where_of(ret, r);
				const auto target = info->ExceptionRecord->NumberParameters >= 2 ? static_cast<std::uint32_t>(info->ExceptionRecord->ExceptionInformation[1]) : 0u;
				logger::write("first chance: %#010lx at %08lx (%s) address %08lx; [esp] %08lx (%s); ecx %08lx esi %08lx thread %lu", code, c->Eip, w,
				              target, ret, r, c->Ecx, c->Esi, GetCurrentThreadId());
				if (traced.load() <= 8) write_crash_context(c);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// Then the crash goes on as it always did (the previous filter, else Windows' error reporting).
		LONG WINAPI on_crash(EXCEPTION_POINTERS* info)
		{
			if (!in_filter.exchange(true) && info && info->ExceptionRecord)
			{
				__try
				{
					write_crash_line(info->ExceptionRecord);
					write_crash_context(info->ContextRecord);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
			return previous_filter ? previous_filter(info) : EXCEPTION_CONTINUE_SEARCH;
		}

		// 0.6 preview fix J: a pure virtual call (VC runtime R6025) never reaches the exception filter - MSVCR71's _purecall
		// calls its handler, then _amsg_exit(25) shows the box and exits. XMen2.exe and the Alchemy DLLs import _purecall
		// from MSVCR71.dll, whose _set_purecall_handler (exported since VC7.1) takes this one: the crash line and context
		// (the caller of the dead virtual is on the stack), then the runtime goes on exactly as before (the box, exit 255).
		using purecall_handler = void(__cdecl*)();
		purecall_handler previous_purecall = nullptr;
		std::atomic<bool> in_purecall{false};
		void write_purecall()
		{
			CONTEXT c{};
			RtlCaptureContext(&c);
			const auto u = measure();
			logger::write("crash: pure virtual function call (VC runtime R6025) on thread %lu; laa %s; above 2 GB %s; private %s; highest %s; 2 GB guard %s",
			              GetCurrentThreadId(), laa_state, megabytes(u.committed_high).c_str(), megabytes(u.committed_private).c_str(),
			              hex32(u.highest_committed).c_str(), guard_state);
			write_crash_context(&c);
		}

		void __cdecl on_purecall()
		{
			if (!in_purecall.exchange(true))
			{
				__try
				{
					write_purecall();
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
			if (previous_purecall) previous_purecall();
		}

		const char* install_purecall()
		{
			const HMODULE runtime = GetModuleHandleA("msvcr71.dll");
			if (!runtime) return "unavailable (no msvcr71.dll)";
			using setter = purecall_handler(__cdecl*)(purecall_handler);
			const auto set = reinterpret_cast<setter>(GetProcAddress(runtime, "_set_purecall_handler"));
			if (!set) return "unavailable (no _set_purecall_handler)";
			previous_purecall = set(on_purecall);
			_set_purecall_handler(on_purecall); // this DLL's own runtime too
			return "on";
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
		logger::write("address space: pure virtual call trace %s", install_purecall());
		char trace[4]{};
		if (GetEnvironmentVariableA("XML2FIX_CRASHTRACE", trace, sizeof(trace)) == 1 && trace[0] == '1')
		{
			AddVectoredExceptionHandler(1, first_chance);
			logger::write("address space: first-chance fault trace on (XML2FIX_CRASHTRACE=1)");
		}
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
