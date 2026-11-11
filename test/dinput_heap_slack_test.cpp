// Windows' DirectInput 7 HID list walk reads one entry past its list (dinput_heap_slack.hpp). Three parts:
//  1. the padded LocalAlloc / LocalReAlloc: extra zeroed bytes, data kept, flags-only reallocs passed through;
//  2. a walk written the way dinput.dll's is (field +0x44 tested before the count), over a list grown the way
//     dinput.dll grows it, with each block ending at a no-access page (a "page heap" placed under the hooks):
//     unpadded it faults exactly when the count equals the capacity, padded it never does;
//  3. this PC's own SysWOW64\dinput.dll, in child processes: DirectInputCreateEx under the page-end placement,
//     without and with the production hook. When the system walk has the read-before-count shape, the children
//     also stand in for a PC with exactly 16 / 32 HID devices: the device interface enumeration that dinput.dll
//     resolves with GetProcAddress ends after the first N interfaces, N chosen so the list is exactly full. Then
//     the unhooked child faults at the walk's compare (the boot crash, every time) and the hooked one does not.
//     This PC's own count is reported too (it is the crash's condition only when it equals the capacity).
#define DIRECTINPUT_VERSION 0x0700
#include "dinput_heap_slack.hpp"
#include "iat_hook.hpp"

#include <Windows.h>
#include <dinput.h>
#include <setupapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace
{
	int failures = 0, checks = 0;
	void expect(const bool ok, const char* what)
	{
		++checks;
		if (!ok)
		{
			++failures;
			std::printf("FAIL %s\n", what);
		}
	}

	// ---- page-end placement: each fixed block ends (8-byte rounded) right before a PAGE_NOACCESS page ----
	struct placed
	{
		void* base;
		SIZE_T bytes;
	};
	std::map<void*, placed> blocks;
	CRITICAL_SECTION blocks_lock;
	decltype(&LocalAlloc) system_alloc = &LocalAlloc;
	decltype(&LocalReAlloc) system_realloc = &LocalReAlloc;
	decltype(&LocalFree) system_free = &LocalFree;

	void* place(const SIZE_T bytes, const bool zero)
	{
		const SIZE_T rounded = (bytes + 7) & ~SIZE_T{7};
		const SIZE_T pages = (rounded + 0xFFF) / 0x1000;
		auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, (pages + 1) * 0x1000, MEM_RESERVE, PAGE_NOACCESS));
		if (!base || !VirtualAlloc(base, pages * 0x1000, MEM_COMMIT, PAGE_READWRITE))
		{
			return nullptr;
		}
		unsigned char* block = base + pages * 0x1000 - rounded;
		std::memset(base, zero ? 0 : 0xA5, pages * 0x1000);
		EnterCriticalSection(&blocks_lock);
		blocks[block] = {base, bytes};
		LeaveCriticalSection(&blocks_lock);
		return block;
	}

	bool find(void* block, placed& out)
	{
		EnterCriticalSection(&blocks_lock);
		const auto it = blocks.find(block);
		const bool found = it != blocks.end();
		if (found)
		{
			out = it->second;
		}
		LeaveCriticalSection(&blocks_lock);
		return found;
	}

	void unplace(void* block)
	{
		placed p{};
		EnterCriticalSection(&blocks_lock);
		const auto it = blocks.find(block);
		if (it != blocks.end())
		{
			p = it->second;
			blocks.erase(it);
		}
		LeaveCriticalSection(&blocks_lock);
		if (p.base)
		{
			VirtualFree(p.base, 0, MEM_RELEASE);
		}
	}

	HLOCAL WINAPI end_alloc(const UINT flags, const SIZE_T bytes)
	{
		if (flags & LMEM_MOVEABLE)
		{
			return system_alloc(flags, bytes);
		}
		return place(bytes, (flags & LMEM_ZEROINIT) != 0);
	}

	HLOCAL WINAPI end_realloc(const HLOCAL memory, const SIZE_T bytes, const UINT flags)
	{
		placed old{};
		if ((flags & LMEM_MODIFY) || !find(memory, old))
		{
			return system_realloc(memory, bytes, flags);
		}
		auto* block = static_cast<unsigned char*>(place(bytes, false));
		if (!block)
		{
			return nullptr;
		}
		const SIZE_T kept = old.bytes < bytes ? old.bytes : bytes;
		std::memcpy(block, memory, kept);
		if ((flags & LMEM_ZEROINIT) && bytes > kept)
		{
			std::memset(block + kept, 0, bytes - kept);
		}
		unplace(memory);
		return block;
	}

	HLOCAL WINAPI end_free(const HLOCAL memory)
	{
		placed p{};
		if (memory && find(memory, p))
		{
			unplace(memory);
			return nullptr;
		}
		return system_free(memory);
	}

	// ---- the walk, as dinput.dll 10.0.26100.8115 writes it: header {count, capacity, ?}, 0x48-byte entries ----
	struct list_header
	{
		int count;
		int capacity;
		int unused;
	};
	constexpr SIZE_T entry = 0x48;

	using alloc_t = HLOCAL(WINAPI*)(UINT, SIZE_T);
	using realloc_t = HLOCAL(WINAPI*)(HLOCAL, SIZE_T, UINT);

	// Builds a list of `devices` entries (every field +0x44 nonzero, as an opened HID device's is) the way
	// dinput.dll does: 16 entries first, doubled when full.
	list_header* build(const int devices, const alloc_t alloc, const realloc_t realloc)
	{
		auto* list = static_cast<list_header*>(alloc(LMEM_ZEROINIT, sizeof(list_header) + 16 * entry));
		if (!list)
		{
			return nullptr;
		}
		list->capacity = 16;
		for (int i = 0; i < devices; ++i)
		{
			if (list->count >= list->capacity)
			{
				auto* grown = static_cast<list_header*>(realloc(list, sizeof(list_header) + list->capacity * 2 * entry, LMEM_MOVEABLE | LMEM_ZEROINIT));
				if (!grown)
				{
					return nullptr;
				}
				list = grown;
				list->capacity *= 2;
			}
			auto* slot = reinterpret_cast<unsigned char*>(list + 1) + list->count * entry;
			*reinterpret_cast<int*>(slot + 0x44) = 1;
			++list->count;
		}
		return list;
	}

	// Returns the entries walked, or -1 if the walk faulted.
	__declspec(noinline) int walk(const list_header* list)
	{
		int walked = -1;
		__try
		{
			int i = 0;
			for (auto* slot = reinterpret_cast<const unsigned char*>(list + 1);; slot += entry, ++i)
			{
				if (*reinterpret_cast<const volatile int*>(slot + 0x44) == 0 || i >= list->count) // field first, as dinput.dll
				{
					break;
				}
			}
			walked = i;
		}
		__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
		{
			walked = -1;
		}
		return walked;
	}

	// ---- a PC with fewer HID devices: dinput.dll's SetupDiEnumDeviceInterfaces ends after `interface_limit` ----
	long interface_limit = -1;
	using enum_interfaces_t = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
	enum_interfaces_t real_enum_interfaces = nullptr;

	BOOL WINAPI limited_enum_interfaces(HDEVINFO set, PSP_DEVINFO_DATA device, const GUID* guid, const DWORD index, PSP_DEVICE_INTERFACE_DATA data)
	{
		if (interface_limit >= 0 && index >= static_cast<DWORD>(interface_limit))
		{
			SetLastError(ERROR_NO_MORE_ITEMS);
			return FALSE;
		}
		return real_enum_interfaces(set, device, guid, index, data);
	}

	FARPROC WINAPI limiting_get_proc_address(HMODULE module, LPCSTR name)
	{
		const FARPROC found = GetProcAddress(module, name);
		if (found && reinterpret_cast<ULONG_PTR>(name) > 0xFFFF && std::strcmp(name, "SetupDiEnumDeviceInterfaces") == 0)
		{
			real_enum_interfaces = reinterpret_cast<enum_interfaces_t>(found);
			return reinterpret_cast<FARPROC>(&limited_enum_interfaces);
		}
		return found;
	}

	std::wstring system_dinput_path()
	{
		wchar_t folder[MAX_PATH]{};
		const UINT length = GetSystemDirectoryW(folder, MAX_PATH);
		return std::wstring(folder, length) + L"\\dinput.dll";
	}

	// Child: DirectInputCreateEx from SysWOW64\dinput.dll under the page-end placement, with or without the fix.
	// Prints "hr count capacity shape-rva" and exits 0; an unhandled fault ends it with the exception code.
	int child(const bool fixed, const long limit)
	{
		interface_limit = limit;
		const HMODULE module = LoadLibraryW(system_dinput_path().c_str());
		if (!module)
		{
			std::printf("no system dinput.dll\n");
			return 3;
		}
		iat_hook::hook(module, "KERNEL32.dll", "LocalAlloc", 0, reinterpret_cast<void*>(&end_alloc));
		iat_hook::hook(module, "KERNEL32.dll", "LocalReAlloc", 0, reinterpret_cast<void*>(&end_realloc));
		iat_hook::hook(module, "KERNEL32.dll", "LocalFree", 0, reinterpret_cast<void*>(&end_free));
		const bool limiting = iat_hook::hook(module, "KERNEL32.dll", "GetProcAddress", 0, reinterpret_cast<void*>(&limiting_get_proc_address)) != nullptr;
		// An access violation in the system dinput.dll is reported as "fault rva <offset> reading <address>" before the
		// child ends with it.
		static HMODULE watched = nullptr;
		watched = module;
		AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* e) -> LONG
		{
			const auto at = reinterpret_cast<ULONG_PTR>(e->ExceptionRecord->ExceptionAddress);
			const auto base = reinterpret_cast<ULONG_PTR>(watched);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
			if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && at >= base && at < base + nt->OptionalHeader.SizeOfImage)
			{
				std::printf("fault rva %lx reading %08lx\n", static_cast<unsigned long>(at - base), static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]));
				std::fflush(stdout);
			}
			return EXCEPTION_CONTINUE_SEARCH;
		});
		if (fixed)
		{
			const auto installed = dinput_heap_slack::install(module);
			if (!installed.alloc || !installed.realloc)
			{
				std::printf("hook not installed\n");
				return 4;
			}
		}
		using create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
		const auto create = reinterpret_cast<create_t>(GetProcAddress(module, "DirectInputCreateEx"));
		void* input = nullptr;
		const HRESULT hr = create ? create(GetModuleHandleW(nullptr), 0x0700, IID_IDirectInput7A, &input, nullptr) : E_FAIL;
		const auto list = dinput_heap_slack::hid_list(module);
		std::printf("hr %08lx count %d capacity %d shape-rva %lx limited %d\n", static_cast<unsigned long>(hr), list.count, list.capacity,
			static_cast<unsigned long>(list.compare_rva), limiting && real_enum_interfaces ? 1 : 0);
		if (input)
		{
			static_cast<IUnknown*>(input)->Release();
		}
		std::fflush(stdout);
		return SUCCEEDED(hr) ? 0 : 5;
	}

	struct child_result
	{
		DWORD exit_code = 0;
		std::string output;
	};

	child_result run_child(const wchar_t* flag, const long limit = -1)
	{
		child_result result;
		wchar_t self[MAX_PATH]{};
		GetModuleFileNameW(nullptr, self, MAX_PATH);
		std::wstring command = L"\"" + std::wstring(self) + L"\" " + flag + L" " + std::to_wstring(limit);
		SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
		HANDLE read = nullptr, write = nullptr;
		CreatePipe(&read, &write, &inherit, 0);
		SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW startup{sizeof(startup)};
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = write;
		startup.hStdError = write;
		PROCESS_INFORMATION process{};
		// SEM_NOGPFAULTERRORBOX (inherited): the expected fault ends the child without a Windows error dialog.
		const UINT previous_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
		SetErrorMode(previous_mode);
		CloseHandle(write);
		if (!started)
		{
			CloseHandle(read);
			result.exit_code = 0xFFFFFFFF;
			return result;
		}
		char buffer[512];
		DWORD got = 0;
		while (ReadFile(read, buffer, sizeof(buffer), &got, nullptr) && got)
		{
			result.output.append(buffer, got);
		}
		CloseHandle(read);
		WaitForSingleObject(process.hProcess, 60000);
		GetExitCodeProcess(process.hProcess, &result.exit_code);
		CloseHandle(process.hProcess);
		CloseHandle(process.hThread);
		while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r'))
		{
			result.output.pop_back();
		}
		return result;
	}
}

int main(int argc, char** argv)
{
	InitializeCriticalSection(&blocks_lock);
	const long limit = argc > 2 ? std::strtol(argv[2], nullptr, 10) : -1;
	if (argc > 1 && std::strcmp(argv[1], "--child-unfixed") == 0)
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		return child(false, limit);
	}
	if (argc > 1 && std::strcmp(argv[1], "--child-fixed") == 0)
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		return child(true, limit);
	}

	// 1. The padded functions (before any install they call kernel32's own).
	{
		using dinput_heap_slack::slack;
		auto* dirty = static_cast<unsigned char*>(LocalAlloc(LMEM_FIXED, 0x200));
		std::memset(dirty, 0xCC, 0x200);
		LocalFree(dirty); // a likely reuse below without LMEM_ZEROINIT: the tail must still be zero

		auto* block = static_cast<unsigned char*>(dinput_heap_slack::padded_local_alloc(LMEM_FIXED, 0x40));
		expect(block != nullptr, "padded LocalAlloc returns a block");
		expect(LocalSize(block) >= 0x40 + slack, "the block has the slack");
		bool zero = block != nullptr;
		for (SIZE_T i = 0; zero && i < slack; ++i) zero = block[0x40 + i] == 0;
		expect(zero, "the slack is zero without LMEM_ZEROINIT");
		std::memset(block, 0x5A, 0x40);

		SetLastError(1234);
		auto* grown = static_cast<unsigned char*>(dinput_heap_slack::padded_local_realloc(block, 0x400, LMEM_MOVEABLE | LMEM_ZEROINIT));
		expect(GetLastError() == 1234, "the caller's last error is kept");
		expect(grown != nullptr && LocalSize(grown) >= 0x400 + slack, "a grown block has the slack");
		bool kept = grown != nullptr;
		for (SIZE_T i = 0; kept && i < 0x40; ++i) kept = grown[i] == 0x5A;
		expect(kept, "growing keeps the data");
		zero = grown != nullptr;
		for (SIZE_T i = 0x40; zero && i < 0x400 + slack; ++i) zero = grown[i] == 0;
		expect(zero, "growing with LMEM_ZEROINIT zeroes the new bytes and the slack");

		std::memset(grown, 0x77, 0x400);
		auto* shrunk = static_cast<unsigned char*>(dinput_heap_slack::padded_local_realloc(grown, 0x20, LMEM_MOVEABLE));
		expect(shrunk != nullptr, "a shrink returns a block");
		kept = shrunk != nullptr;
		for (SIZE_T i = 0; kept && i < 0x20; ++i) kept = shrunk[i] == 0x77;
		expect(kept, "shrinking keeps the data");
		zero = shrunk != nullptr;
		for (SIZE_T i = 0x20; zero && i < 0x20 + slack; ++i) zero = shrunk[i] == 0;
		expect(zero, "shrinking zeroes the old data under the new slack");
		LocalFree(shrunk);

		const HLOCAL moveable = dinput_heap_slack::padded_local_alloc(LMEM_MOVEABLE, 0x10);
		expect(moveable != nullptr && LocalSize(moveable) >= 0x10 + slack, "a moveable block has the slack");
		auto* locked = static_cast<unsigned char*>(LocalLock(moveable));
		zero = locked != nullptr;
		for (SIZE_T i = 0x10; zero && i < 0x10 + slack; ++i) zero = locked[i] == 0;
		expect(zero, "a moveable block's slack is zero");
		LocalUnlock(moveable);
		expect((LocalFlags(moveable) & LMEM_LOCKCOUNT) == 0, "the moveable block is left unlocked");
		const SIZE_T before = LocalSize(moveable);
		const HLOCAL modified = dinput_heap_slack::padded_local_realloc(moveable, 0, LMEM_MODIFY | LMEM_MOVEABLE);
		expect(modified == moveable && LocalSize(moveable) == before, "an LMEM_MODIFY call is passed through unchanged");
		LocalFree(moveable);
	}

	// 2. The walk over page-end blocks: the fault is the count == capacity case, and the slack removes it.
	{
		int unpadded_faults = 0, padded_faults = 0, wrong_counts = 0;
		for (int devices = 1; devices <= 64; ++devices)
		{
			list_header* bare = build(devices, &end_alloc, &end_realloc);
			const int bare_walked = bare ? walk(bare) : -2;
			const bool full = devices == 16 || devices == 32 || devices == 64;
			if (full)
			{
				unpadded_faults += bare_walked == -1;
			}
			else
			{
				wrong_counts += bare_walked != devices;
			}
			if (bare) end_free(bare);
		}
		expect(unpadded_faults == 3, "unpadded, the walk faults for 16, 32 and 64 devices (count == capacity)");
		expect(wrong_counts == 0, "unpadded, every other count walks its entries without a fault");

		// The production hook over the page-end placement: point a module's imports at the placement, then install
		// the fix over them (as in the game, the fix's hook is the outer one and calls what was there before).
		const HMODULE module = LoadLibraryW(system_dinput_path().c_str());
		expect(module != nullptr, "the system dinput.dll loads");
		iat_hook::hook(module, "KERNEL32.dll", "LocalAlloc", 0, reinterpret_cast<void*>(&end_alloc));
		iat_hook::hook(module, "KERNEL32.dll", "LocalReAlloc", 0, reinterpret_cast<void*>(&end_realloc));
		const auto installed = dinput_heap_slack::install(module);
		expect(installed.alloc && installed.realloc, "the fix redirects the system dinput.dll's LocalAlloc and LocalReAlloc");
		const auto again = dinput_heap_slack::install(module);
		expect(again.alloc && again.realloc, "a second install finds its own hooks and keeps the chain");
		int padded_wrong = 0, outside = 0;
		for (int devices = 1; devices <= 64; ++devices)
		{
			list_header* padded = build(devices, &dinput_heap_slack::padded_local_alloc, &dinput_heap_slack::padded_local_realloc);
			const int walked = padded ? walk(padded) : -2;
			padded_faults += walked == -1;
			padded_wrong += walked != devices;
			placed p{};
			outside += !(padded && find(padded, p));
			if (padded) end_free(padded);
		}
		expect(outside == 0, "padded blocks still come from the placement under the hook");
		expect(padded_faults == 0, "padded, the walk never faults (1..64 devices)");
		expect(padded_wrong == 0, "padded, the walk counts every device");
		std::printf("walk: unpadded faults %d (16/32/64), padded faults %d of 64 counts\n", unpadded_faults, padded_faults);
		// The module stays loaded: its imports point into this image.
	}

	// 3. This PC's DirectInput 7.
	{
		struct parsed_child
		{
			bool parsed = false;
			unsigned long hr = 0, rva = 0;
			int count = -1, capacity = -1, limited = 0;
		};
		const auto parse = [](const child_result& r)
		{
			parsed_child p;
			p.parsed = std::sscanf(r.output.c_str(), "hr %lx count %d capacity %d shape-rva %lx limited %d", &p.hr, &p.count, &p.capacity, &p.rva, &p.limited) == 5;
			return p;
		};
		const child_result unfixed = run_child(L"--child-unfixed");
		const child_result fixed = run_child(L"--child-fixed");
		std::printf("system dinput.dll, page-end placement, unhooked: exit %08lx %s\n", static_cast<unsigned long>(unfixed.exit_code), unfixed.output.c_str());
		std::printf("system dinput.dll, page-end placement, hooked:   exit %08lx %s\n", static_cast<unsigned long>(fixed.exit_code), fixed.output.c_str());
		expect(fixed.exit_code == 0, "with the fix, DirectInputCreateEx succeeds under the page-end placement");
		const parsed_child natural = parse(fixed);
		if (natural.parsed && natural.rva && natural.count > 0)
		{
			const bool full = natural.count == natural.capacity;
			std::printf("this PC: %d HID devices in a list of %d - %s\n", natural.count, natural.capacity,
				full ? "full: the boot crash's condition" : "not full: the walk's extra read stays inside the list here");
			// The count can change between the two children (a device arriving or leaving): an unhooked fault is only
			// accepted at the walk's compare.
			unsigned long natural_fault = 0, natural_read = 0;
			const bool natural_faulted = std::sscanf(unfixed.output.c_str(), "fault rva %lx reading %lx", &natural_fault, &natural_read) == 2;
			if (unfixed.exit_code != 0)
			{
				std::printf("the unhooked child faulted: its list was full when it ran\n");
			}
			expect(full ? unfixed.exit_code == static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION) && natural_fault == natural.rva
					: unfixed.exit_code == 0 || (unfixed.exit_code == static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION) && natural_faulted && natural_fault == natural.rva),
				"unhooked, the page-end placement faults only at the walk's compare, and when this PC's list is full");

			// Stand in for PCs whose HID devices exactly fill the list (16, 32).
			int full_cases = 0;
			for (const int wanted : {16, 32})
			{
				if (natural.count < wanted)
				{
					continue;
				}
				long found = -1;
				for (long n = wanted; n <= 128 && found < 0; ++n)
				{
					const parsed_child p = parse(run_child(L"--child-fixed", n));
					if (!p.parsed || !p.limited || p.count > wanted)
					{
						break;
					}
					if (p.count == wanted)
					{
						found = n;
					}
				}
				if (found < 0)
				{
					std::printf("could not stand in for %d devices\n", wanted);
					continue;
				}
				++full_cases;
				const child_result bare = run_child(L"--child-unfixed", found);
				const child_result hooked = run_child(L"--child-fixed", found);
				const parsed_child h = parse(hooked);
				std::printf("%d devices (first %ld interfaces): unhooked exit %08lx %s; hooked exit %08lx %s\n", wanted, found,
					static_cast<unsigned long>(bare.exit_code), bare.output.c_str(), static_cast<unsigned long>(hooked.exit_code), hooked.output.c_str());
				expect(bare.exit_code == static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION), "a full list under the page-end placement faults unhooked (the boot crash)");
				unsigned long fault_rva = 0, fault_read = 0;
				const bool fault_seen = std::sscanf(bare.output.c_str(), "fault rva %lx reading %lx", &fault_rva, &fault_read) == 2;
				expect(fault_seen && fault_rva == natural.rva, "the unhooked fault is the walk's compare (the boot crash's instruction)");
				expect(hooked.exit_code == 0 && h.parsed && h.count == wanted && h.capacity == wanted, "a full list under the page-end placement is walked safely hooked");
			}
			expect(full_cases > 0, "at least one full-list case could be stood in for on this PC");
		}
		else
		{
			std::printf("this PC's dinput.dll walk doesn't have the read-before-count shape (or no list): the unhooked child is informational only\n");
		}
	}

	std::printf("%s: %d of %d checks\n", failures ? "FAIL" : "ok", checks - failures, checks);
	return failures ? 1 : 0;
}
